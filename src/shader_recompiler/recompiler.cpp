// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <ranges>
#include <unordered_set>
#include <vector>

#include "shader_recompiler/frontend/control_flow_graph.h"
#include "shader_recompiler/frontend/decode.h"
#include "shader_recompiler/frontend/structured_control_flow.h"
#include "shader_recompiler/frontend/translate/translate.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/ir/post_order.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/recompiler.h"

namespace Shader {

IR::BlockList GenerateBlocks(const IR::AbstractSyntaxList& syntax_list) {
    size_t num_syntax_blocks{};
    for (const auto& [_, type] : syntax_list) {
        if (type == IR::AbstractSyntaxNode::Type::Block) {
            ++num_syntax_blocks;
        }
    }
    IR::BlockList blocks{};
    blocks.reserve(num_syntax_blocks);
    for (const auto& [data, type] : syntax_list) {
        if (type == IR::AbstractSyntaxNode::Type::Block) {
            blocks.push_back(data.block);
        }
    }
    return blocks;
}

void EmitControlFlowGraph(IR::Program& program, Pools& pools, Gcn::CFG& cfg,
                          RuntimeInfo& runtime_info, const Profile& profile) {
    Gcn::Translator translator{program.info, runtime_info, profile};
    bool emit_prologue = true;
    for (auto& block : cfg) {
        const u32 start = block.begin_index;
        const u32 size = block.end_index - start + 1;
        auto* ir_block = pools.block_pool.Create(pools.inst_pool);
        ir_block->cfg_block = &block;
        block.ir_block = ir_block;
        translator.Translate(ir_block, block.begin,
                             std::span{program.ins_list}.subspan(start, size));
        if (emit_prologue) {
            translator.EmitPrologue(ir_block);
            emit_prologue = false;
        }
        program.blocks.push_back(ir_block);
    }
    ASSERT_MSG(!program.info.translation_failed, "Shader translation has failed");
    for (auto& block : cfg) {
        auto* ir_block = block.ir_block;
        if (block.branch_true) {
            auto* true_block = block.branch_true->ir_block;
            ir_block->AddBranch(true_block);
        }
        if (block.branch_false) {
            auto* false_block = block.branch_false->ir_block;
            ir_block->AddBranch(false_block);
        }
    }
    program.post_order_blocks = Shader::IR::PostOrder(program.blocks.front());
}

static bool IsPhiType(IR::Type type) {
    switch (type) {
    case IR::Type::U1:
    case IR::Type::U8:
    case IR::Type::U16:
    case IR::Type::U32:
    case IR::Type::U64:
    case IR::Type::F16:
    case IR::Type::F32:
    case IR::Type::F64:
    case IR::Type::U32x2:
    case IR::Type::U32x3:
    case IR::Type::U32x4:
    case IR::Type::F16x2:
    case IR::Type::F16x3:
    case IR::Type::F16x4:
    case IR::Type::F32x2:
    case IR::Type::F32x3:
    case IR::Type::F32x4:
    case IR::Type::F64x2:
    case IR::Type::F64x3:
    case IR::Type::F64x4:
        return true;
    default:
        return false;
    }
}

static bool IsCompositeExtract(IR::Inst* const inst) {
    switch (inst->GetOpcode()) {
    case IR::Opcode::CompositeExtractU32x2:
    case IR::Opcode::CompositeExtractU32x3:
    case IR::Opcode::CompositeExtractU32x4:
    case IR::Opcode::CompositeExtractF32x2:
    case IR::Opcode::CompositeExtractF32x3:
    case IR::Opcode::CompositeExtractF32x4:
        return true;
    default:
        return false;
    }
}

static bool IsValidCompositeExtractIndex(const IR::Value& value) {
    return value.IsImmediate() && value.Type() == IR::Type::U32;
}

static bool IsValueOfType(const IR::Value& value, IR::Type type) {
    if (value.IsImmediate()) {
        return value.Type() == type;
    }
    const IR::Inst* const inst = value.TryInst();
    return inst && inst->Type() == type;
}

static bool IsPhiFoldableOpcode(IR::Opcode opcode) {
    switch (opcode) {
    case IR::Opcode::CompositeExtractU32x2:
    case IR::Opcode::CompositeExtractU32x3:
    case IR::Opcode::CompositeExtractU32x4:
    case IR::Opcode::CompositeExtractF32x2:
    case IR::Opcode::CompositeExtractF32x3:
    case IR::Opcode::CompositeExtractF32x4:
    case IR::Opcode::FPAbs32:
    case IR::Opcode::FPAbs64:
    case IR::Opcode::FPAdd32:
    case IR::Opcode::FPAdd64:
    case IR::Opcode::FPSub32:
    case IR::Opcode::FPFma32:
    case IR::Opcode::FPFma64:
    case IR::Opcode::FPMax32:
    case IR::Opcode::FPMax64:
    case IR::Opcode::FPMin32:
    case IR::Opcode::FPMin64:
    case IR::Opcode::FPMul32:
    case IR::Opcode::FPMul64:
    case IR::Opcode::FPDiv32:
    case IR::Opcode::FPDiv64:
    case IR::Opcode::FPNeg32:
    case IR::Opcode::FPNeg64:
    case IR::Opcode::FPRecip32:
    case IR::Opcode::FPRecip64:
    case IR::Opcode::FPRecipSqrt32:
    case IR::Opcode::FPRecipSqrt64:
    case IR::Opcode::FPSqrt:
    case IR::Opcode::FPSin:
    case IR::Opcode::FPExp2:
    case IR::Opcode::FPPow:
    case IR::Opcode::FPLdexp:
    case IR::Opcode::FPCos:
    case IR::Opcode::FPLog2:
    case IR::Opcode::FPSaturate32:
    case IR::Opcode::FPSaturate64:
    case IR::Opcode::FPClamp32:
    case IR::Opcode::FPClamp64:
    case IR::Opcode::FPRoundEven32:
    case IR::Opcode::FPRoundEven64:
    case IR::Opcode::FPFloor32:
    case IR::Opcode::FPFloor64:
    case IR::Opcode::FPCeil32:
    case IR::Opcode::FPCeil64:
    case IR::Opcode::FPTrunc32:
    case IR::Opcode::FPTrunc64:
    case IR::Opcode::FPFract32:
    case IR::Opcode::FPFract64:
    case IR::Opcode::BitCastU16F16:
    case IR::Opcode::BitCastU32F32:
    case IR::Opcode::BitCastF16U16:
    case IR::Opcode::BitCastF32U32:
    case IR::Opcode::ConvertF16F32:
    case IR::Opcode::ConvertF32F16:
    case IR::Opcode::ConvertF32F64:
    case IR::Opcode::ConvertF64F32:
    case IR::Opcode::ConvertF32U16:
    case IR::Opcode::ConvertU16U32:
    case IR::Opcode::ConvertU32U16:
    case IR::Opcode::ConvertU64U32:
    case IR::Opcode::ConvertU8U32:
    case IR::Opcode::ConvertU32U8:
    case IR::Opcode::ConvertS32S8:
    case IR::Opcode::ConvertS32S16:
    case IR::Opcode::IAdd32:
    case IR::Opcode::IAdd64:
    case IR::Opcode::ISub32:
    case IR::Opcode::ISub64:
    case IR::Opcode::IMul32:
    case IR::Opcode::IMul64:
    case IR::Opcode::INeg32:
    case IR::Opcode::INeg64:
    case IR::Opcode::IAbs32:
    case IR::Opcode::ShiftLeftLogical32:
    case IR::Opcode::ShiftLeftLogical64:
    case IR::Opcode::ShiftRightLogical32:
    case IR::Opcode::ShiftRightLogical64:
    case IR::Opcode::ShiftRightArithmetic32:
    case IR::Opcode::ShiftRightArithmetic64:
    case IR::Opcode::BitwiseAnd32:
    case IR::Opcode::BitwiseAnd64:
    case IR::Opcode::BitwiseOr32:
    case IR::Opcode::BitwiseOr64:
    case IR::Opcode::BitwiseXor32:
    case IR::Opcode::BitFieldInsert:
    case IR::Opcode::BitFieldSExtract:
    case IR::Opcode::BitFieldUExtract:
    case IR::Opcode::BitReverse32:
    case IR::Opcode::BitCount32:
    case IR::Opcode::BitCount64:
    case IR::Opcode::BitwiseNot32:
    case IR::Opcode::FindSMsb32:
    case IR::Opcode::FindUMsb32:
    case IR::Opcode::FindUMsb64:
    case IR::Opcode::FindILsb32:
    case IR::Opcode::FindILsb64:
    case IR::Opcode::SMin32:
    case IR::Opcode::UMin32:
    case IR::Opcode::SMax32:
    case IR::Opcode::UMax32:
    case IR::Opcode::SLessThan32:
    case IR::Opcode::SLessThan64:
    case IR::Opcode::ULessThan32:
    case IR::Opcode::ULessThan64:
    case IR::Opcode::IEqual32:
    case IR::Opcode::IEqual64:
    case IR::Opcode::SLessThanEqual32:
    case IR::Opcode::SLessThanEqual64:
    case IR::Opcode::ULessThanEqual32:
    case IR::Opcode::ULessThanEqual64:
    case IR::Opcode::SGreaterThan32:
    case IR::Opcode::SGreaterThan64:
    case IR::Opcode::UGreaterThan32:
    case IR::Opcode::UGreaterThan64:
    case IR::Opcode::INotEqual32:
    case IR::Opcode::INotEqual64:
    case IR::Opcode::SGreaterThanEqual32:
    case IR::Opcode::SGreaterThanEqual64:
    case IR::Opcode::UGreaterThanEqual32:
    case IR::Opcode::UGreaterThanEqual64:
    case IR::Opcode::LogicalOr:
    case IR::Opcode::LogicalAnd:
    case IR::Opcode::LogicalXor:
    case IR::Opcode::LogicalNot:
        return true;
    default:
        return false;
    }
}

static bool IsValidPhiProducer(const IR::Inst* producer, IR::Opcode opcode) {
    if (!producer || producer->GetOpcode() != opcode || !IsPhiFoldableOpcode(opcode) ||
        IR::NumArgsOf(opcode) == 0 || producer->NumArgs() != IR::NumArgsOf(opcode) ||
        IR::TypeOf(opcode) == IR::Type::Void) {
        return false;
    }
    for (size_t arg_index = 0; arg_index < producer->NumArgs(); ++arg_index) {
        if (!IsValueOfType(producer->Arg(arg_index), IR::ArgTypeOf(opcode, arg_index))) {
            return false;
        }
    }
    return true;
}

static void AddPhiUsersToWorklist(IR::Inst& phi, std::vector<IR::Inst*>& worklist,
                                  std::unordered_set<IR::Inst*>& queued) {
    for (const auto& use : phi.Uses()) {
        IR::Inst* const user = use.user;
        if (user != &phi && user->GetOpcode() == IR::Opcode::Phi && queued.insert(user).second) {
            worklist.push_back(user);
        }
    }
}

static IR::Inst* FoldPhi(IR::Inst& phi, IR::Opcode opcode, IR::Type input_type,
                         std::vector<IR::Inst*>& worklist, std::unordered_set<IR::Inst*>& queued,
                         auto&&... args) {
    const IR::Type output_type = IR::TypeOf(opcode);
    if (opcode == IR::Opcode::Phi || !IsPhiType(input_type) || !IsPhiType(output_type) ||
        phi.Flags<IR::Type>() != output_type || phi.NumArgs() == 0) {
        return nullptr;
    }

    std::vector<IR::Inst*> producers;
    producers.reserve(phi.NumArgs());
    for (size_t arg_index = 0; arg_index < phi.NumArgs(); ++arg_index) {
        IR::Inst* const producer = phi.Arg(arg_index).TryInst();
        if (!IsValidPhiProducer(producer, opcode) || !IsValueOfType(producer->Arg(0), input_type)) {
            return nullptr;
        }
        producers.push_back(producer);
    }

    auto insert_point = IR::Block::InstructionList::s_iterator_to(phi);
    IR::Block* block = phi.GetParent();
    IR::Inst* const new_phi{&*block->PrependNewInst(insert_point, IR::Opcode::Phi)};
    new_phi->SetFlags(input_type);

    for (size_t arg_index = 0; arg_index < phi.NumArgs(); ++arg_index) {
        new_phi->AddPhiOperand(phi.PhiBlock(arg_index), producers[arg_index]->Arg(0));
    }

    auto it = std::ranges::find_if_not(block->Instructions(), IR::IsPhi);
    IR::Value const replacement{
        &*block->PrependNewInst(it, opcode, {IR::Value{new_phi}, IR::Value{args}...})};
    AddPhiUsersToWorklist(phi, worklist, queued);
    phi.ReplaceUsesWithAndRemove(replacement);
    ASSERT(!insert_point->HasUses());
    block->Instructions().erase(insert_point);
    return new_phi;
}

static IR::Inst* FoldPhiArgOpIntoPhi(IR::Inst& phi, std::vector<IR::Inst*>& worklist,
                                     std::unordered_set<IR::Inst*>& queued) {
    IR::Inst* const first_arg = phi.Arg(0).TryInst();
    if (!first_arg || !IsValidPhiProducer(first_arg, first_arg->GetOpcode())) {
        return nullptr;
    }
    const IR::Opcode opcode = first_arg->GetOpcode();
    if (opcode == IR::Opcode::Phi || IR::NumArgsOf(opcode) == 0) {
        return nullptr;
    }
    const IR::Type input_type = IR::ArgTypeOf(opcode, 0);

    if (IsCompositeExtract(first_arg)) {
        const IR::Value first_index = first_arg->Arg(1);
        if (!IsValidCompositeExtractIndex(first_index)) {
            return nullptr;
        }
        const u32 index = first_index.U32();
        for (size_t arg_index = 1; arg_index < phi.NumArgs(); ++arg_index) {
            const IR::Inst* const arg = phi.Arg(arg_index).TryInst();
            if (!arg || !IsValidPhiProducer(arg, opcode) ||
                !IsValidCompositeExtractIndex(arg->Arg(1)) || arg->Arg(1).U32() != index) {
                return nullptr;
            }
        }
        return FoldPhi(phi, opcode, input_type, worklist, queued, index);
    }
    if (first_arg->NumArgs() == 1) {
        return FoldPhi(phi, opcode, input_type, worklist, queued);
    }

    return nullptr;
}

static bool AllPhiArgsHaveSameOp(const IR::Inst& phi) {
    IR::Inst* const first_arg = phi.Arg(0).TryInst();
    if (!first_arg || !IsValidPhiProducer(first_arg, first_arg->GetOpcode())) {
        return false;
    }
    const IR::Opcode opcode = first_arg->GetOpcode();
    for (size_t arg_index = 1; arg_index < phi.NumArgs(); ++arg_index) {
        IR::Inst* const arg = phi.Arg(arg_index).TryInst();
        if (!IsValidPhiProducer(arg, opcode)) {
            return false;
        }
    }
    return true;
}

static IR::Inst* VisitPhiNode(IR::Inst& phi, std::vector<IR::Inst*>& worklist,
                              std::unordered_set<IR::Inst*>& queued) {
    if (phi.GetOpcode() != IR::Opcode::Phi || phi.NumArgs() == 0) {
        return nullptr;
    }

    if (AllPhiArgsHaveSameOp(phi)) {
        if (IR::Inst* inst = FoldPhiArgOpIntoPhi(phi, worklist, queued)) {
            return inst;
        }
    }

    IR::Block* block = phi.GetParent();
    for (IR::Inst& inst : block->Instructions()) {
        if (inst.GetOpcode() != IR::Opcode::Phi) {
            break;
        }
        if (&inst == &phi || inst.NumArgs() != phi.NumArgs()) {
            continue;
        }
        bool identical = true;
        for (size_t i = 0; i < inst.NumArgs(); ++i) {
            if (phi.Flags<IR::Type>() != inst.Flags<IR::Type>() ||
                phi.PhiBlock(i) != inst.PhiBlock(i) || phi.Arg(i) != inst.Arg(i)) {
                identical = false;
                break;
            }
        }
        if (identical) {
            AddPhiUsersToWorklist(phi, worklist, queued);
            phi.ReplaceUsesWithAndRemove(IR::Value{&inst});
            auto it = IR::Block::InstructionList::s_iterator_to(phi);
            ASSERT(!it->HasUses());
            block->Instructions().erase(it);
            return nullptr;
        }
    }

    return nullptr;
}

static void PhiSimplificationPass(IR::Program& program) {
    std::vector<IR::Inst*> worklist;
    std::unordered_set<IR::Inst*> queued;
    for (IR::Block* const block : program.blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::Phi) {
                break;
            }
            queued.insert(&inst);
            worklist.push_back(&inst);
        }
    }
    while (!worklist.empty()) {
        IR::Inst* const phi = worklist.back();
        worklist.pop_back();
        queued.erase(phi);
        if (phi->GetOpcode() != IR::Opcode::Phi) {
            continue;
        }
        if (auto* new_phi = VisitPhiNode(*phi, worklist, queued)) {
            if (queued.insert(new_phi).second) {
                worklist.push_back(new_phi);
            }
        }
    }
}

IR::Program TranslateProgram(const std::span<const u32>& code, Pools& pools, Info& info,
                             RuntimeInfo& runtime_info, const Profile& profile) {
    // Ensure first instruction is expected.
    constexpr u32 token_mov_vcchi = 0xBEEB03FF;
    if (code[0] != token_mov_vcchi) {
        LOG_WARNING(Render_Recompiler, "First instruction is not s_mov_b32 vcc_hi, #imm");
    }

    Gcn::GcnCodeSlice slice(code.data(), code.data() + code.size());
    Gcn::GcnDecodeContext decoder;

    // Decode and save instructions
    IR::Program program{info};
    program.ins_list.reserve(code.size());
    while (!slice.atEnd()) {
        program.ins_list.emplace_back(decoder.decodeInstruction(slice));
    }

    // Clear any previous pooled data.
    pools.ReleaseContents();

    // Create control flow graph
    Common::ObjectPool<Gcn::Block> gcn_block_pool{64};
    Gcn::CFG cfg{gcn_block_pool, program.ins_list};
    EmitControlFlowGraph(program, pools, cfg, runtime_info, profile);

    // On NVIDIA GPUs HW interpolation of clip distance values seems broken, and we need to emulate
    // it with expensive discard in PS.
    Shader::InjectClipDistanceAttributes(program, runtime_info);

    // Run optimization passes on unstructured graph
    if (!profile.support_float64) {
        Shader::Optimization::LowerFp64ToFp32(program);
    }
    Shader::Optimization::SsaRewritePass(program);
    Shader::Optimization::ConstantPropagationPass(program.post_order_blocks);
    if (info.l_stage == LogicalStage::TessellationControl) {
        Shader::Optimization::TessellationPreprocess(program, runtime_info);
        Shader::Optimization::HullShaderTransform(program, runtime_info);
    } else if (info.l_stage == LogicalStage::TessellationEval) {
        Shader::Optimization::TessellationPreprocess(program, runtime_info);
        Shader::Optimization::DomainShaderTransform(program, runtime_info);
    }
    Shader::Optimization::RingAccessElimination(program, runtime_info);
    Shader::Optimization::ReadLaneEliminationPass(program);
    Shader::IR::DumpProgram(program, info);
    auto resources = Shader::Optimization::ResourceDiscoverPass(program, profile);
    Shader::Optimization::FlattenExtendedUserdataPass(program);
    Shader::IR::DumpProgram(program, info);
    Shader::Optimization::ResourcePatchingPass(program.info, resources, profile);
    Shader::Optimization::LowerBufferFormatToRaw(program);
    Shader::Optimization::SharedMemorySimplifyPass(program, profile);
    Shader::Optimization::SharedMemoryToStoragePass(program, runtime_info, profile);
    Shader::Optimization::LowerUserClipPlanes(program, runtime_info);

    // Prepare for structurization by clearing flow graph and lowering phis
    for (auto* ir_block : program.blocks) {
        ir_block->imm_predecessors.clear();
        ir_block->imm_successors.clear();
        ir_block->ssa_state.Reset();
    }
    Shader::Optimization::LowerPhisToRegsPass(program);

    // Structurize control flow graph and create program.
    program.syntax_list = Shader::Gcn::BuildASL(pools, cfg, info);
    program.blocks = GenerateBlocks(program.syntax_list);
    program.post_order_blocks = Shader::IR::PostOrder(program.syntax_list.front().data.block);

    // Run optimization passes on structured graph
    Shader::Optimization::SsaRepairPass(program);
    Shader::Optimization::SsaRewritePass(program);
    PhiSimplificationPass(program);
    Shader::Optimization::ConstantPropagationPass(program.post_order_blocks);
    Shader::Optimization::DeadCodeEliminationPass(program);
    Shader::Optimization::SharedMemoryBarrierPass(program, runtime_info, profile);
    Shader::Optimization::CollectShaderInfoPass(program, profile);
    // Shader::IR::DumpProgram(program, info);

    return program;
}

} // namespace Shader
