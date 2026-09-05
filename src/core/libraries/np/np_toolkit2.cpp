// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <cstring>

#include "common/logging/log.h"
#include "common/types.h"
#include "core/libraries/libs.h"
#include "core/libraries/system/userservice.h"

namespace Libraries::Np::NpTk2 {
namespace {

// Offline toolkit2 (libSceNpToolkit2) support for games that gate their boot sequence on
// Authenticate / UserProfile round-trips (e.g. Unity NpToolkit2 titles such as
// Kamen Rider Climax Fighters, CUSA09666). Without this HLE the imported C++ symbols would
// be resolved to aerolib stubs that always return zero, leaving the wrapper's async state
// machine (Response::isLocked and friends) broken, which manifests as an endless loading
// screen.
//
// NOTE: the guest-side layouts below model only the fields this HLE reads/writes. They are
// intentionally self-consistent: everything we store here is later read back through the
// same HLE functions. If a specific title reads payload bytes directly, capture the real
// layouts from the bundled libSceNpToolkit2.prx and extend the structs below.

struct ResponseBaseGuest {
    u64 vtable{};       // +0x00
    u32 locked{};       // +0x08
    s32 return_code{};  // +0x0C
    u64 server_error{}; // +0x10
    u32 state{};        // +0x18
    u32 padding{};      // +0x1C
};
static_assert(sizeof(ResponseBaseGuest) == 0x20);

// Payload of Response<T> sits right after the base class.
constexpr std::size_t kResponsePayloadOffset = 0x20;

struct Toolkit2State {
    bool initialized = false;
    s32 user_id = 1000; // SCE_USER_SERVICE_USER_ID_SYSTEM
};
Toolkit2State g_state;

s32 GetBootUserId() {
    int user_id = 0;
    if (Libraries::UserService::sceUserServiceGetInitialUser(&user_id) != 0 || user_id == 0) {
        user_id = 1000;
    }
    return user_id;
}

ResponseBaseGuest* Base(void* self) {
    return static_cast<ResponseBaseGuest*>(self);
}

void InitResponse(void* self, bool locked) {
    std::memset(self, 0, sizeof(ResponseBaseGuest));
    Base(self)->locked = locked ? 1u : 0u;
    Base(self)->return_code = 0;
}

void CompleteResponse(void* self) {
    ResponseBaseGuest* base = Base(self);
    base->locked = 0;
    base->return_code = 0;
    base->state = 0x02; // done
}

void ZeroPayload(void* self, std::size_t size) {
    if (self != nullptr) {
        std::memset(static_cast<u8*>(self) + kResponsePayloadOffset, 0, size);
    }
}

void ZeroObject(void* object, std::size_t size) {
    if (object != nullptr) {
        std::memset(object, 0, size);
    }
}

// ---- Core ----

s32 PS4_SYSV_ABI ToolkitCoreInit(const void* /*init_params*/, void* response) {
    g_state.initialized = true;
    g_state.user_id = GetBootUserId();
    LOG_INFO(Lib_NpManager, "NpTk2: Core::init (offline), boot user = {}", g_state.user_id);
    CompleteResponse(response);
    return 0;
}

// ---- Auth ----

s32 PS4_SYSV_ABI ToolkitAuthGetAuthCode(const void* /*request*/, void* response) {
    LOG_INFO(Lib_NpManager, "NpTk2: Auth::getAuthCode (offline, fake token)");
    ZeroPayload(response, 0x20);
    CompleteResponse(response);
    return 0;
}

s32 PS4_SYSV_ABI ToolkitAuthGetIdToken(const void* /*request*/, void* response) {
    LOG_INFO(Lib_NpManager, "NpTk2: Auth::getIdToken (offline, fake token)");
    ZeroPayload(response, 0x20);
    CompleteResponse(response);
    return 0;
}

// ---- UserProfile ----

s32 PS4_SYSV_ABI ToolkitProfileGetNpProfiles(const void* /*request*/, void* response) {
    LOG_INFO(Lib_NpManager, "NpTk2: UserProfile::getNpProfiles (offline, no profiles)");
    // Empty profile list: the wrapper gets a successful call with zero profiles. If a title
    // needs a concrete profile payload, extend NpProfilesGuest here (data/size/capacity).
    ZeroPayload(response, sizeof(u64) * 3);
    CompleteResponse(response);
    return 0;
}

// ---- ResponseBase / Response<T> state machine ----

s32 PS4_SYSV_ABI ToolkitResponseGetReturnCode(void* self) {
    return Base(self)->return_code;
}

s32 PS4_SYSV_ABI ToolkitResponseIsLocked(void* self) {
    return Base(self)->locked;
}

void PS4_SYSV_ABI ToolkitResponseSetLocked(void* self, bool locked) {
    Base(self)->locked = locked ? 1u : 0u;
}

void PS4_SYSV_ABI ToolkitResponseSetReturnCode(void* self, s32 return_code) {
    Base(self)->return_code = return_code;
}

s32 PS4_SYSV_ABI ToolkitResponseGetState(void* self) {
    return Base(self)->state;
}

void PS4_SYSV_ABI ToolkitResponseSetServerError(void* self, void* server_error) {
    Base(self)->server_error = reinterpret_cast<u64>(server_error);
}

u64 PS4_SYSV_ABI ToolkitResponseGetServerError(void* self) {
    return Base(self)->server_error;
}

void PS4_SYSV_ABI ToolkitResponseResetValues(void* self) {
    ResponseBaseGuest* base = Base(self);
    base->return_code = 0;
    base->server_error = 0;
    base->state = 0;
}

void PS4_SYSV_ABI ToolkitResponseBaseC1(void* self) {
    InitResponse(self, true);
}

void PS4_SYSV_ABI ToolkitResponseBaseC2(void* self) {
    InitResponse(self, true);
}

void PS4_SYSV_ABI ToolkitResponseBaseD1(void* /*self*/) {}

void PS4_SYSV_ABI ToolkitResponseBaseD2(void* /*self*/) {}

void PS4_SYSV_ABI ToolkitResponseIdTokenC1(void* self) {
    InitResponse(self, true);
}

void PS4_SYSV_ABI ToolkitResponseIdTokenD1(void* /*self*/) {}

void PS4_SYSV_ABI ToolkitResponseAuthCodeC1(void* self) {
    InitResponse(self, true);
}

void PS4_SYSV_ABI ToolkitResponseAuthCodeC2(void* self) {
    InitResponse(self, true);
}

void PS4_SYSV_ABI ToolkitResponseAuthCodeD2(void* /*self*/) {}

void PS4_SYSV_ABI ToolkitResponseProfilesC1(void* self) {
    InitResponse(self, true);
}

void PS4_SYSV_ABI ToolkitResponseProfilesC2(void* self) {
    InitResponse(self, true);
}

u64 PS4_SYSV_ABI ToolkitResponseGet(void* self) {
    return reinterpret_cast<u64>(static_cast<u8*>(self) + kResponsePayloadOffset);
}

// ---- Request / payload constructors ----

void PS4_SYSV_ABI ToolkitRequestGetIdTokenC1(void* self) {
    ZeroObject(self, 0x28);
}

void PS4_SYSV_ABI ToolkitRequestGetAuthCodeC1(void* self) {
    ZeroObject(self, 0x28);
}

void PS4_SYSV_ABI ToolkitRequestGetNpProfilesC1(void* self) {
    ZeroObject(self, 0x28);
}

void PS4_SYSV_ABI ToolkitRequestGetNpProfilesC2(void* self) {
    ZeroObject(self, 0x28);
}

void PS4_SYSV_ABI ToolkitRequestGetNpProfilesD1(void* /*self*/) {}

void PS4_SYSV_ABI ToolkitAuthCodeC1(void* self) {
    ZeroObject(self, 0x20);
}

void PS4_SYSV_ABI ToolkitAuthCodeC2(void* self) {
    ZeroObject(self, 0x20);
}

void PS4_SYSV_ABI ToolkitAuthCodeReset(void* self) {
    ZeroObject(self, 0x20);
}

void PS4_SYSV_ABI ToolkitAuthCodeD2(void* /*self*/) {}

void PS4_SYSV_ABI ToolkitIdTokenC1(void* self) {
    ZeroObject(self, 0x20);
}

void PS4_SYSV_ABI ToolkitIdTokenC2(void* self) {
    ZeroObject(self, 0x20);
}

void PS4_SYSV_ABI ToolkitIdTokenDeepCopy(void* self) {
    ZeroObject(self, 0x20);
}

void PS4_SYSV_ABI ToolkitNpProfileC1(void* self) {
    ZeroObject(self, 0x80);
}

void PS4_SYSV_ABI ToolkitNpProfileC2(void* self) {
    ZeroObject(self, 0x80);
}

void PS4_SYSV_ABI ToolkitNpProfileD1(void* /*self*/) {}

void PS4_SYSV_ABI ToolkitNpProfileD2(void* /*self*/) {}

void PS4_SYSV_ABI ToolkitNpProfilesC2(void* self) {
    ZeroObject(self, 0x18);
}

void PS4_SYSV_ABI ToolkitNpProfilesD1(void* /*self*/) {}

} // namespace

void RegisterLib(Core::Loader::SymbolsResolver* sym) {
    // Core
    LIB_FUNCTION("LLdnqVnnNBQ", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitCoreInit);
    // Auth
    LIB_FUNCTION("XIbXIDTEUpo", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitAuthGetAuthCode);
    LIB_FUNCTION("t-tNMj1vbVw", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitAuthGetIdToken);
    // UserProfile
    LIB_FUNCTION("YlpnKm99h3Q", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitProfileGetNpProfiles);
    // ResponseBase state machine
    LIB_FUNCTION("5-UHpg8s00I", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitResponseGetReturnCode);
    LIB_FUNCTION("mIinAn8TvdY", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitResponseIsLocked);
    LIB_FUNCTION("S83yx-vretc", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitResponseSetLocked);
    LIB_FUNCTION("xZWYmRsx-SU", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitResponseSetReturnCode);
    LIB_FUNCTION("ZWEcg5Lqdfk", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitResponseGetState);
    LIB_FUNCTION("ZWNK+VuChkg", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitResponseSetServerError);
    LIB_FUNCTION("kxzokJHZ2nY", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitResponseGetServerError);
    LIB_FUNCTION("kK+c8MOkymg", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitResponseResetValues);
    LIB_FUNCTION("Di4s6zsQIH4", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitResponseBaseC1);
    LIB_FUNCTION("By+M3Ko+Nb8", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitResponseBaseC2);
    LIB_FUNCTION("GBHytuHJ+Ic", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitResponseBaseD1);
    LIB_FUNCTION("RoHCyY7B6z0", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitResponseBaseD2);
    // Response<T> ctors / dtors / getters
    LIB_FUNCTION("2ho2X0o+LGc", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitResponseIdTokenC1);
    LIB_FUNCTION("EEvugJCBmb8", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitResponseIdTokenD1);
    LIB_FUNCTION("2eQ9LLJMx5Q", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitResponseAuthCodeC1);
    LIB_FUNCTION("8HrKWYwzjJ0", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitResponseAuthCodeC2);
    LIB_FUNCTION("HqqUHMbskgo", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitResponseAuthCodeD2);
    LIB_FUNCTION("Kv8XXD7X7XQ", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitResponseProfilesC1);
    LIB_FUNCTION("3+43NKBK2S4", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitResponseProfilesC2);
    LIB_FUNCTION("fh+OX5Gf-BA", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitResponseGet);
    LIB_FUNCTION("oBhiTEQfdoM", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitResponseGet);
    LIB_FUNCTION("q-uIM1VNvwg", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitResponseGet);
    LIB_FUNCTION("dPN4cmA3Tok", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitResponseGet);
    // Request / payload constructors
    LIB_FUNCTION("MkSYr+wZN-A", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitRequestGetIdTokenC1);
    LIB_FUNCTION("8qtytWLvK3c", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitRequestGetAuthCodeC1);
    LIB_FUNCTION("31Bh5lfPPXs", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitRequestGetNpProfilesC1);
    LIB_FUNCTION("-sDxqsfDDFE", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitRequestGetNpProfilesC2);
    LIB_FUNCTION("6esKXS17kNM", "libSceNpToolkit2", 1, "libSceNpToolkit2",
                 ToolkitRequestGetNpProfilesD1);
    LIB_FUNCTION("fGoDnKx0jSI", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitAuthCodeC1);
    LIB_FUNCTION("Brnt8dE5A9I", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitAuthCodeC2);
    LIB_FUNCTION("H2S3lRfnOrc", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitAuthCodeReset);
    LIB_FUNCTION("8wWauon64VQ", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitAuthCodeD2);
    LIB_FUNCTION("PiYbgSiNYTU", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitIdTokenC1);
    LIB_FUNCTION("61yrarhAKxA", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitIdTokenC2);
    LIB_FUNCTION("G5VoD9ffZ3M", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitIdTokenDeepCopy);
    LIB_FUNCTION("HY0-+bRqLho", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitNpProfileC1);
    LIB_FUNCTION("2LLpgZUyZTw", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitNpProfileC2);
    LIB_FUNCTION("+LVSE3Fjofw", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitNpProfileD1);
    LIB_FUNCTION("32sr9JSPqrQ", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitNpProfileD2);
    LIB_FUNCTION("FzqYPd7E5Ho", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitNpProfilesC2);
    LIB_FUNCTION("N8ff+sy7SEU", "libSceNpToolkit2", 1, "libSceNpToolkit2", ToolkitNpProfilesD1);
}

} // namespace Libraries::Np::NpTk2