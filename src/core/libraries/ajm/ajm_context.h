// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/bounded_threadsafe_queue.h"
#include "common/slot_array.h"
#include "common/types.h"
#include "core/libraries/ajm/ajm.h"
#include "core/libraries/ajm/ajm_batch.h"
#include "core/libraries/ajm/ajm_instance.h"

#include <array>
#include <deque>
#include <memory>
#include <shared_mutex>
#include <span>
#include <thread>
#include <utility>

namespace Libraries::Ajm {

class AjmContext {
public:
    AjmContext();

    s32 InstanceCreate(AjmCodecType codec_type, AjmInstanceFlags flags, u32* out_instance_id);
    s32 InstanceDestroy(u32 instance_id);

    s32 BatchCancel(const u32 batch_id);
    s32 ModuleRegister(AjmCodecType type);
    s32 BatchWait(const u32 batch_id, const u32 timeout, AjmBatchError* const p_batch_error);
    s32 BatchStartBuffer(u8* p_batch, u32 batch_size, const int priority,
                         AjmBatchError* p_batch_error, u32* p_batch_id);

    void WorkerThread(std::stop_token stop);
    void ProcessBatch(u32 id, std::span<AjmJob> jobs);

private:
    static constexpr u32 MaxInstances = 0x2fff;
    static constexpr u32 MaxBatches = 0x0400;
    static constexpr u32 NumAjmCodecs = std::to_underlying(AjmCodecType::Max);

    // Number of most-recently-consumed batches we keep alive so repeated
    // timeout=0 polling on the same batch_id (observed from nusc in S4U
    // Live) returns the cached result instead of INVALID_BATCH. If we kept
    // consumed batches forever MaxBatches=1024 would be exhausted after
    // ~1024 StartBuffer calls and every subsequent Create would fail with
    // ERROR_OUT_OF_MEMORY, causing 97% of decode jobs to never be enqueued
    // (observed in the af3b10c8 run: 41338 StartBuffer vs 1023 WorkerPop).
    static constexpr u32 ConsumedBatchRetainWindow = 64;

    // If we are above this many consumed batches, trim the tail on the
    // next StartBuffer call so we stay under MaxBatches and still leave
    // headroom for in-flight unconsumed batches.
    static constexpr u32 ConsumedBatchTrimThreshold = MaxBatches - 128;

    [[nodiscard]] bool IsRegistered(AjmCodecType type) const;

    std::array<bool, NumAjmCodecs> registered_codecs{};

    std::shared_mutex instances_mutex;
    Common::SlotArray<u32, std::shared_ptr<AjmInstance>, MaxInstances, 1> instances;

    std::shared_mutex batches_mutex;
    Common::SlotArray<u32, std::shared_ptr<AjmBatch>, MaxBatches, 1> batches;

    // Order = insertion order (oldest first, newest last). Trimmed on
    // StartBuffer when above ConsumedBatchTrimThreshold.
    std::deque<u32> consumed_batch_ids;

    std::jthread worker_thread{};
    Common::MPSCQueue<std::shared_ptr<AjmBatch>> batch_queue;
};

} // namespace Libraries::Ajm
