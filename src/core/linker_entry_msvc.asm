; SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
; SPDX-License-Identifier: GPL-2.0-or-later

.code

PUBLIC RunMainEntryWindows
RunMainEntryWindows PROC
    mov rdi, rcx
    mov rsi, rdx
    and rsp, 0FFFFFFFFFFFFFFF0h
    sub rsp, 8
    push qword ptr [rcx+08h]
    push qword ptr [rcx+00h]
    mov rax, [rcx+110h]
    jmp rax
RunMainEntryWindows ENDP

END
