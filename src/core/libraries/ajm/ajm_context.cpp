// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/thread.h"
#include "core/libraries/ajm/ajm.h"
#include "core/libraries/ajm/ajm_at9.h"
#include "core/libraries/ajm/ajm_context.h"
#include "core/libraries/ajm/ajm_error.h"
#include "core/libraries/ajm/ajm_instance.h"
#include "core/libraries/ajm/ajm_instance_statistics.h"
#include "core/libraries/ajm/ajm_mp3.h"
#include "core/libraries/error_codes.h"

#include <span>
#include <utility>

namespace Libraries::Ajm {

constexpr u32 ORBIS_AJM_WAIT_INFINITE = -1;
constexpr int INSTANCE_ID_MASK = 0x3FFF;

AjmContext::AjmContext() {
    worker_thread = std::jthread([this](std::stop_token stop) { this->WorkerThread(stop); });
}

bool AjmContext::IsRegistered(AjmCodecType type) const {
    return registered_codecs[std::to_underlying(type)];
}

s32 AjmContext::BatchCancel(const u32 batch_id) {
    std::shared_ptr<AjmBatch> batch{};
    {
        std::shared_lock guard(batches_mutex);
        const auto p_batch = batches.Get(batch_id);
        if (p_batch == nullptr) {
            return ORBIS_AJM_ERROR_INVALID_BATCH;
        }
        batch = *p_batch;
    }

    if (batch->processed) {
        return ORBIS_OK;
    }

    bool expected = false;
    batch->canceled.compare_exchange_strong(expected, true);
    return ORBIS_OK;
}

s32 AjmContext::ModuleRegister(AjmCodecType type) {
    if (std::to_underlying(type) >= NumAjmCodecs) {
        return ORBIS_AJM_ERROR_INVALID_PARAMETER;
    }
    if (IsRegistered(type)) {
        return ORBIS_AJM_ERROR_CODEC_ALREADY_REGISTERED;
    }
    registered_codecs[std::to_underlying(type)] = true;
    return ORBIS_OK;
}

void AjmContext::WorkerThread(std::stop_token stop) {
    Common::SetCurrentThreadName("shadPS4:AjmWorker");
    // AT9 decode latency directly gates sceAjmBatchWait, which the game
    // calls on its main thread. Without elevated priority the single
    // worker gets starved during shader compilation / resource loading
    // bursts, delaying batch completion and stalling the game main thread.
    Common::SetCurrentThreadPriority(Common::ThreadPriority::VeryHigh);
    while (!stop.stop_requested()) {
        auto batch = batch_queue.PopWait(stop);
        if (batch != nullptr && !batch->canceled) {
            bool expected = false;
            batch->processed.compare_exchange_strong(expected, true);
            ProcessBatch(batch->id, batch->jobs);
            batch->finished.release();
        }
    }
}

void AjmContext::ProcessBatch(u32 id, std::span<AjmJob> jobs) {
    // Perform operation requested by control flags.
    for (auto& job : jobs) {
        LOG_TRACE(Lib_Ajm, "Processing job {} for instance {}. flags = {:#x}", id, job.instance_id,
                  job.flags.raw);

        if (job.instance_id == AJM_INSTANCE_STATISTICS) {
            AjmInstanceStatistics::Getinstance().ExecuteJob(job);
        } else {
            std::shared_ptr<AjmInstance> instance;
            {
                std::shared_lock lock(instances_mutex);
                auto* p_instance = instances.Get(job.instance_id & INSTANCE_ID_MASK);
                ASSERT_MSG(p_instance != nullptr, "Attempting to execute job on null instance");
                instance = *p_instance;
            }

            instance->ExecuteJob(job);
        }

        // Log non-zero job results to surface silent AT9 decode issues
        // (PARTIAL_INPUT, NOT_ENOUGH_ROOM, CODEC_ERROR, etc.) that may cause
        // the game to retry instances or produce audio gaps.
        if (job.output.p_result != nullptr && job.output.p_result->result != 0) {
            LOG_WARNING(Lib_Ajm,
                        "Batch {} job for instance {} returned result = {:#x}, internal = {:#x}",
                        id, job.instance_id, job.output.p_result->result,
                        job.output.p_result->internal_result);
        }
    }
}

s32 AjmContext::BatchWait(const u32 batch_id, const u32 timeout, AjmBatchError* const batch_error) {
    std::shared_ptr<AjmBatch> batch{};
    s32 wait_result = ORBIS_OK;
    {
        std::shared_lock guard(batches_mutex);
        const auto p_batch = batches.Get(batch_id);
        if (p_batch == nullptr) {
            return ORBIS_AJM_ERROR_INVALID_BATCH;
        }
        batch = *p_batch;
    }

    // If a previous wait already consumed this batch, return the saved result.
    // Game code (e.g. nusc in S4U Live) uses timeout=0 polling on the same
    // batch_id after an earlier blocking wait succeeded; without this we'd
    // return INVALID_BATCH because the slot was destroyed, and the game would
    // think batch processing failed (stalling BGM progress polling).
    //
    // Guard rails: only trust the saved result if the batch was actually
    // processed. In the (theoretical) case that processed==false but
    // consumed==true (e.g. canceled worker raced with the save, or a prior
    // timeout=16 wait returned CANCELLED), fall through to the normal wait
    // path so the caller gets the semaphore-driven truth instead of stale
    // state. If processed AND canceled both hold, return the saved
    // CANCELLED result explicitly so the two code paths agree.
    if (batch->consumed.load(std::memory_order_acquire)) {
        if (!batch->processed.load(std::memory_order_acquire)) {
            LOG_WARNING(Lib_Ajm,
                        "sceAjmBatchWait batch {} consumed=true but processed=false, "
                        "falling through to normal wait",
                        batch_id);
        } else if (batch->canceled.load(std::memory_order_acquire) &&
                   batch->last_wait_result == ORBIS_OK) {
            // Worker finished (processed=true) but the batch was also
            // canceled; re-derive the cancelled result to stay consistent
            // with a fresh wait through the semaphore.
            return ORBIS_AJM_ERROR_CANCELLED;
        } else {
            return batch->last_wait_result;
        }
    }

    bool expected = false;
    if (!batch->waiting.compare_exchange_strong(expected, true)) {
        return ORBIS_AJM_ERROR_BUSY;
    }

    const auto wait_begin = std::chrono::high_resolution_clock::now();
    if (timeout == ORBIS_AJM_WAIT_INFINITE) {
        batch->finished.acquire();
    } else if (!batch->finished.try_acquire_for(std::chrono::milliseconds(timeout))) {
        batch->waiting = false;
        return ORBIS_AJM_ERROR_IN_PROGRESS;
    }
    const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::high_resolution_clock::now() - wait_begin)
                             .count();
    if (wait_ms > 20) {
        LOG_WARNING(Lib_Ajm, "sceAjmBatchWait blocked for {} ms on batch {}", wait_ms, batch_id);
    }

    // Determine the final status before marking consumed.
    if (batch->canceled) {
        wait_result = ORBIS_AJM_ERROR_CANCELLED;
    } else {
        wait_result = ORBIS_OK;
    }

    // Mark the batch as consumed so subsequent polls return the same result
    // instead of INVALID_BATCH. The slot is recycled later by StartBuffer
    // when the retain window is full.
    {
        std::unique_lock guard(batches_mutex);
        batch->consumed.store(true, std::memory_order_release);
        batch->last_wait_result = wait_result;
        consumed_batch_ids.push_back(batch_id);
    }

    return wait_result;
}

int AjmContext::BatchStartBuffer(u8* p_batch, u32 batch_size, const int priority,
                                 AjmBatchError* batch_error, u32* out_batch_id) {
    if ((batch_size & 7) != 0) {
        LOG_ERROR(Lib_Ajm, "ORBIS_AJM_ERROR_MALFORMED_BATCH");
        return ORBIS_AJM_ERROR_MALFORMED_BATCH;
    }

    const auto batch_info = AjmBatch::FromBatchBuffer({p_batch, batch_size});

    // Before allocating a fresh slot, free consumed batches outside the
    // recent retain window. Otherwise we hit MaxBatches=1024 slots after
    // ~1024 StartBuffer calls and every later batch creation silently
    // fails with ERROR_OUT_OF_MEMORY, which effectively drops 97% of the
    // decode jobs while keeping S4U Live only barely running on the first
    // 1023 queued jobs (af3b10c8 observed 41338 StartBuffer vs 1023
    // WorkerPop because of this).
    u32 trimmed = 0;
    {
        std::unique_lock guard(batches_mutex);
        while (consumed_batch_ids.size() > ConsumedBatchTrimThreshold &&
               consumed_batch_ids.size() > ConsumedBatchRetainWindow) {
            const auto old_id = consumed_batch_ids.front();
            consumed_batch_ids.pop_front();
            auto* p_old_batch = batches.Get(old_id);
            if (p_old_batch != nullptr && p_old_batch->get() != nullptr &&
                (*p_old_batch)->consumed.load(std::memory_order_acquire)) {
                batches.Destroy(old_id);
                ++trimmed;
            }
        }
    }

    std::optional<u32> batch_id;
    {
        std::unique_lock guard(batches_mutex);
        batch_id = batches.Create(batch_info);
    }
    if (!batch_id.has_value()) {
        LOG_ERROR(Lib_Ajm,
                  "ORBIS_AJM_ERROR_OUT_OF_MEMORY at StartBuffer (MaxBatches={}, consumed_deque={})",
                  MaxBatches, consumed_batch_ids.size());
        return ORBIS_AJM_ERROR_OUT_OF_MEMORY;
    }
    *out_batch_id = batch_id.value();
    batch_info->id = *out_batch_id;

    if (!batch_info->jobs.empty()) {
        batch_queue.EmplaceWait(batch_info);
    } else {
        // Empty batches are not submitted to the processor and are marked as finished
        batch_info->finished.release();
    }

    return ORBIS_OK;
}

s32 AjmContext::InstanceCreate(AjmCodecType codec_type, AjmInstanceFlags flags, u32* out_instance) {
    if (codec_type >= AjmCodecType::Max) {
        return ORBIS_AJM_ERROR_INVALID_PARAMETER;
    }
    if (flags.version == 0) {
        return ORBIS_AJM_ERROR_WRONG_REVISION_FLAG;
    }
    if (!IsRegistered(codec_type)) {
        return ORBIS_AJM_ERROR_CODEC_NOT_REGISTERED;
    }
    std::optional<u32> opt_index;
    {
        // Construct the instance (including AT9 decoder init) outside the
        // lock to minimize exclusive lock hold time. During the S4U Live
        // AT9 create-destroy livelock, the worker's ProcessBatch takes a
        // shared lock on instances_mutex; a long exclusive hold here
        // blocks every batch and stalls sceAjmBatchWait on the game thread.
        auto instance = std::make_unique<AjmInstance>(codec_type, flags);
        std::unique_lock lock(instances_mutex);
        opt_index = instances.Create(std::move(instance));
    }
    if (!opt_index.has_value()) {
        return ORBIS_AJM_ERROR_OUT_OF_RESOURCES;
    }
    *out_instance = opt_index.value() | (static_cast<u32>(codec_type) << 14);

    LOG_INFO(Lib_Ajm, "instance = {}", *out_instance);
    return ORBIS_OK;
}

s32 AjmContext::InstanceDestroy(u32 instance_id) {
    std::unique_lock lock(instances_mutex);
    if (!instances.Destroy(instance_id & INSTANCE_ID_MASK)) {
        return ORBIS_AJM_ERROR_INVALID_INSTANCE;
    }
    return ORBIS_OK;
}

} // namespace Libraries::Ajm
