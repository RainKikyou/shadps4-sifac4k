; SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
; SPDX-License-Identifier: GPL-2.0-or-later

.code

EXTERN _sceFiberForceQuit:PROC

PUBLIC _sceFiberSetJmp
_sceFiberSetJmp PROC
    mov [rsp+08h], rax
    mov [rsp+10h], r11
    mov r11, rcx
    mov rax, [rsp+08h]
    mov [r11+00h], rax
    mov rax, [rsp+10h]
    mov [r11+48h], rax
    mov [r11+08h], rcx
    mov rdx, [rsp]
    mov [r11+10h], rdx
    mov [r11+18h], rbx
    mov [r11+20h], rsp
    mov [r11+28h], rbp
    mov [r11+30h], r8
    mov [r11+38h], r9
    mov [r11+40h], r10
    mov [r11+50h], r12
    mov [r11+58h], r13
    mov [r11+60h], r14
    mov [r11+68h], r15
    fnstcw word ptr [r11+70h]
    stmxcsr dword ptr [r11+74h]
    xor eax, eax
    ret
_sceFiberSetJmp ENDP

PUBLIC _sceFiberLongJmp
_sceFiberLongJmp PROC
    mov r10, rcx
    sub rsp, 08h
    stmxcsr dword ptr [rsp]
    mov eax, dword ptr [r10+74h]
    and eax, 0FFFFFFC0h
    mov edx, dword ptr [rsp]
    and edx, 3Fh
    xor eax, edx
    mov dword ptr [rsp], eax
    ldmxcsr dword ptr [rsp]
    mov rax, [r10+00h]
    mov rdx, [r10+10h]
    mov rbx, [r10+18h]
    mov rsp, [r10+20h]
    mov rbp, [r10+28h]
    mov r8, [r10+30h]
    mov r9, [r10+38h]
    mov r11, [r10+48h]
    mov r12, [r10+50h]
    mov r13, [r10+58h]
    mov r14, [r10+60h]
    mov r15, [r10+68h]
    fldcw word ptr [r10+70h]
    mov rcx, [r10+08h]
    mov r10, [r10+40h]
    mov [rsp], rdx
    mov eax, 1
    ret
_sceFiberLongJmp ENDP

PUBLIC _sceFiberSwitchEntry
_sceFiberSwitchEntry PROC
    mov r11, rcx
    mov rsp, [r11+18h]
    xor ebp, ebp
    mov r10, [r11+20h]
    test r10, r10
    jz switch_entry_clear_regs
    mov dword ptr [r10], 2
switch_entry_clear_regs:
    test edx, edx
    jz switch_entry_skip_fpu
    ldmxcsr dword ptr [r11+2Ch]
    fldcw word ptr [r11+28h]
switch_entry_skip_fpu:
    mov rcx, [r11+08h]
    mov rdx, [r11+10h]
    mov rax, [r11+00h]
    xor ebx, ebx
    xor esi, esi
    xor edi, edi
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    pxor mm0, mm0
    pxor mm1, mm1
    pxor mm2, mm2
    pxor mm3, mm3
    pxor mm4, mm4
    pxor mm5, mm5
    pxor mm6, mm6
    pxor mm7, mm7
    emms
    vzeroall
    sub rsp, 20h
    call rax
    mov ecx, 1
    call _sceFiberForceQuit
    ret
_sceFiberSwitchEntry ENDP

END
