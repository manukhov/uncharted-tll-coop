; Isolated ABI fixture. No retail instructions or addresses are executed.
EXTERN p99_wrist_native_body:PROC
PUBLIC p99_wrist_native_entry_probe
PUBLIC p99_wrist_invoke_abi_probe
.const
ALIGN 16
wrist_xmm_pattern QWORD 0123456789ABCDEFh, 0FEDCBA9876543210h
.code

; Native(status,context). Exercise all four incoming home slots and expose
; the exact entry RSP to the C++ fixture, without changing the two arguments.
p99_wrist_native_entry_probe PROC FRAME
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    mov [rsp+30h], rcx
    mov [rsp+38h], rdx
    mov QWORD PTR [rsp+40h], 12345678h
    mov QWORD PTR [rsp+48h], 76543210h
    lea r8, [rsp+28h]
    call p99_wrist_native_body
    add rsp, 28h
    ret
p99_wrist_native_entry_probe ENDP

; RCX=void thunk(). Return0 only if stack and every ABI nonvolatile GPR/XMM
; survive. Own saves are fully unwind-described, including the sentinel RBP.
p99_wrist_invoke_abi_probe PROC FRAME
    push rbp
    .pushreg rbp
    push rbx
    .pushreg rbx
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp, 0E8h
    .allocstack 0E8h
    movaps [rsp+30h], xmm6
    .savexmm128 xmm6, 30h
    movaps [rsp+40h], xmm7
    .savexmm128 xmm7, 40h
    movaps [rsp+50h], xmm8
    .savexmm128 xmm8, 50h
    movaps [rsp+60h], xmm9
    .savexmm128 xmm9, 60h
    movaps [rsp+70h], xmm10
    .savexmm128 xmm10, 70h
    movaps [rsp+80h], xmm11
    .savexmm128 xmm11, 80h
    movaps [rsp+90h], xmm12
    .savexmm128 xmm12, 90h
    movaps [rsp+0A0h], xmm13
    .savexmm128 xmm13, 0A0h
    movaps [rsp+0B0h], xmm14
    .savexmm128 xmm14, 0B0h
    movaps [rsp+0C0h], xmm15
    .savexmm128 xmm15, 0C0h
    .endprolog
    mov [rsp+20h], rcx
    mov [rsp+28h], rsp
    mov ebp, 11223301h
    mov ebx, 11223302h
    mov esi, 11223303h
    mov edi, 11223304h
    mov r12d, 11223305h
    mov r13d, 11223306h
    mov r14d, 11223307h
    mov r15d, 11223308h
    movaps xmm6, XMMWORD PTR [wrist_xmm_pattern]
    movaps xmm7, XMMWORD PTR [wrist_xmm_pattern]
    movaps xmm8, XMMWORD PTR [wrist_xmm_pattern]
    movaps xmm9, XMMWORD PTR [wrist_xmm_pattern]
    movaps xmm10, XMMWORD PTR [wrist_xmm_pattern]
    movaps xmm11, XMMWORD PTR [wrist_xmm_pattern]
    movaps xmm12, XMMWORD PTR [wrist_xmm_pattern]
    movaps xmm13, XMMWORD PTR [wrist_xmm_pattern]
    movaps xmm14, XMMWORD PTR [wrist_xmm_pattern]
    movaps xmm15, XMMWORD PTR [wrist_xmm_pattern]
    call QWORD PTR [rsp+20h]
    cmp rsp, [rsp+28h]
    jne wrist_abi_bad
    cmp rbp, 11223301h
    jne wrist_abi_bad
    cmp rbx, 11223302h
    jne wrist_abi_bad
    cmp rsi, 11223303h
    jne wrist_abi_bad
    cmp rdi, 11223304h
    jne wrist_abi_bad
    cmp r12, 11223305h
    jne wrist_abi_bad
    cmp r13, 11223306h
    jne wrist_abi_bad
    cmp r14, 11223307h
    jne wrist_abi_bad
    cmp r15, 11223308h
    jne wrist_abi_bad
    IRP xreg,<xmm6,xmm7,xmm8,xmm9,xmm10,xmm11,xmm12,xmm13,xmm14,xmm15>
        movaps xmm0, XMMWORD PTR [wrist_xmm_pattern]
        pcmpeqb xmm0, xreg
        pmovmskb eax, xmm0
        cmp eax, 0FFFFh
        jne wrist_abi_bad
    ENDM
    xor eax, eax
    jmp wrist_abi_restore
wrist_abi_bad:
    mov eax, 1
wrist_abi_restore:
    movaps xmm6, [rsp+30h]
    movaps xmm7, [rsp+40h]
    movaps xmm8, [rsp+50h]
    movaps xmm9, [rsp+60h]
    movaps xmm10, [rsp+70h]
    movaps xmm11, [rsp+80h]
    movaps xmm12, [rsp+90h]
    movaps xmm13, [rsp+0A0h]
    movaps xmm14, [rsp+0B0h]
    movaps xmm15, [rsp+0C0h]
    add rsp, 0E8h
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret
p99_wrist_invoke_abi_probe ENDP
END
