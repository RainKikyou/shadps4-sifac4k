// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Core::Loader {
class SymbolsResolver;
}

namespace Libraries::Np::NpTk2 {

// Diagnostic switch: true disables the offline toolkit2 HLE (falls back to aerolib stubs).
// Controlled by KAMEN_NPTK2_OFF / KAMEN_NPTK2_ON env vars, or by the presence of the
// file <user dir>/npTk2Off (works when the game is launched from the Qt GUI).
bool IsNpTk2HleDisabled();

void RegisterLib(Core::Loader::SymbolsResolver* sym);

} // namespace Libraries::Np::NpTk2