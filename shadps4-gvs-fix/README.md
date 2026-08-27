# GUNDAM VERSUS (CUSA08379) - shadPS4 gvs-fix branch

This branch is based on the latest shadPS4 mainline (54ec7d0, v0.18.1).
Goal: let GUNDAM VERSUS enter real gameplay on latest mainline (no screen freeze / no controller dead).

## Root cause (from shad_log(1) vs shad_log(2))

| item | shad_log(1) v0.14.1 (works) | shad_log(2) v0.18.1 mainline (broken) |
|---|---|---|
| directMemoryAccess | false | true |
| readbacksMode | 0 | 2 |
| fault_manager non-GPU-cached accesses | 0 | 24 |
| vkValidationCore | false | true |

- Mainline source defaults already match v0.14.1 (DMA off, readbacks Disabled).
- The regression comes from the per-game config custom_configs/CUSA08379.json, which set
  direct_memory_access_enabled=true and readbacks_mode=2, causing 24 GPU page-fault
  recoveries on the DMA path (screen freeze / hang).

## Fix content

1. shadps4-gvs-fix/CUSA08379.json - merges shad_log(1)'s working config:
   direct_memory_access_enabled=false, readbacks_mode=0,
   readback_linear_images_enabled=true, vkvalidation_core_enabled=false.
   Drop it into the runtime user/custom_configs/CUSA08379.json to take effect.

2. Source-level MSVC-compat patches (no effect on clang-cl CI builds):
   - src/common/types.h: PS4_SYSV_ABI becomes empty under MSVC
   - src/common/assert.cpp: Crash() uses __debugbreak() under MSVC
   - src/shader_recompiler/runtime_info.h: defaulted <=> uses auto (MSVC C7634)

## Usage

- GitHub Actions (build.yml) compiles shadPS4.exe automatically for this branch.
- Copy shadps4-gvs-fix/CUSA08379.json into the emulator's user/custom_configs/ then launch the game.
