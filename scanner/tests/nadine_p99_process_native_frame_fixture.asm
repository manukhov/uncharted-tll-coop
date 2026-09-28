; Test-only Process-shaped stack. No retail bytes or addresses are executed.
EXTERN __chkstk:PROC
EXTERN p99_process_fixture_body:PROC
PUBLIC p99_process_frame_fixture
PUBLIC p99_process_frame_abi_probe
.const
ALIGN 16
process_xmm_pattern QWORD 0123456789ABCDEFh, 0FEDCBA9876543210h
.code

; Receive original7. Body additionally receives actual entry return-slot,
; synthetic native status and Segment addresses as args8/9/10.
p99_process_frame_fixture PROC FRAME
    mov eax, 16A8h
    call __chkstk
    sub rsp, rax
    .allocstack 16A8h
    .endprolog
    mov [rsp+16B0h], rcx
    mov [rsp+16B8h], rdx
    mov [rsp+16C0h], r8
    mov [rsp+16C8h], r9
    mov rax, [rsp+16D0h]
    mov [rsp+20h], rax
    mov rax, [rsp+16D8h]
    mov [rsp+28h], rax
    mov rax, [rsp+16E0h]
    mov [rsp+30h], rax
    lea rax, [rsp+16A8h]
    mov [rsp+38h], rax
    lea rax, [rsp+158h]
    mov [rsp+40h], rax
    lea rax, [rsp+80h]
    mov [rsp+48h], rax
    call p99_process_fixture_body
    add rsp, 16A8h
    ret
p99_process_frame_fixture ENDP

; RCX=void thunk();0 means every nonvolatile GPR/XMM and RSP survived.
p99_process_frame_abi_probe PROC FRAME
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
    IRP xreg,<xmm6,xmm7,xmm8,xmm9,xmm10,xmm11,xmm12,xmm13,xmm14,xmm15>
        movaps xreg, XMMWORD PTR [process_xmm_pattern]
    ENDM
    call QWORD PTR [rsp+20h]
    cmp rsp, [rsp+28h]
    jne process_abi_bad
    cmp rbp, 11223301h
    jne process_abi_bad
    cmp rbx, 11223302h
    jne process_abi_bad
    cmp rsi, 11223303h
    jne process_abi_bad
    cmp rdi, 11223304h
    jne process_abi_bad
    cmp r12, 11223305h
    jne process_abi_bad
    cmp r13, 11223306h
    jne process_abi_bad
    cmp r14, 11223307h
    jne process_abi_bad
    cmp r15, 11223308h
    jne process_abi_bad
    IRP xreg,<xmm6,xmm7,xmm8,xmm9,xmm10,xmm11,xmm12,xmm13,xmm14,xmm15>
        movaps xmm0, XMMWORD PTR [process_xmm_pattern]
        pcmpeqb xmm0, xreg
        pmovmskb eax, xmm0
        cmp eax, 0FFFFh
        jne process_abi_bad
    ENDM
    xor eax, eax
    jmp process_abi_restore
process_abi_bad:
    mov eax, 1
process_abi_restore:
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
p99_process_frame_abi_probe ENDP
END
