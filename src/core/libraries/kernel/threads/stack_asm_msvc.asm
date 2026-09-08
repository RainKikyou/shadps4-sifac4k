.code

PUBLIC _runOnAnotherStack
_runOnAnotherStack PROC
    push r12
    push r13
    push r14
    push r15
    mov r14, gs:[08h]
    mov r15, gs:[10h]
    xor rax, rax
    mov gs:[08h], rax
    mov gs:[10h], rax
    mov r12, rsp
    mov r13, rbp
    and r8, 0FFFFFFFFFFFFFFF0h
    mov rsp, r8
    mov rbp, r8
    sub rsp, 20h
    call rdx
    mov rsp, r12
    mov rbp, r13
    mov gs:[08h], r14
    mov gs:[10h], r15
    pop r15
    pop r14
    pop r13
    pop r12
    ret
_runOnAnotherStack ENDP

END
