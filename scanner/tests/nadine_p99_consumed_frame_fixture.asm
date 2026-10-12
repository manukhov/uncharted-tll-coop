; Native-shape fixtures only. No retail code is relocated or called.
EXTERN p99_consumed_fixture_process_cpp:PROC
EXTERN p99_consumed_fixture_post_cpp:PROC
EXTERN p99_consumed_fixture_tail_cpp:PROC
PUBLIC p99_consumed_fixture_outer
PUBLIC p99_consumed_fixture_perform
PUBLIC p99_consumed_fixture_post_relay
.code

p99_consumed_fixture_outer PROC FRAME
    sub rsp, 28h
    .allocstack 28h
    mov [rsp+30h], rbx
    .savereg rbx, 30h
    mov rbx, [rcx+8]
    mov [rsp+38h], rsi
    .savereg rsi, 38h
    mov rsi, rcx
    .endprolog
    call p99_consumed_fixture_perform
    test DWORD PTR [rsi+10h], 2
    jnz fixture_outer_done
    mov rcx, rsi
    call p99_consumed_fixture_post_relay
    test DWORD PTR [rsi+10h], 100h
    jz fixture_outer_done
    mov rcx, rsi
    call p99_consumed_fixture_post_relay
fixture_outer_done:
    mov rax, 123456789ABCDEF0h
    test DWORD PTR [rsi+10h], 20h
    jnz fixture_outer_tail
    mov rsi, [rsp+38h]
    mov rbx, [rsp+30h]
    add rsp, 28h
    ret
fixture_outer_tail:
    mov rcx, rsi
    mov rsi, [rsp+38h]
    mov rbx, [rsp+30h]
    add rsp, 28h
    jmp p99_consumed_fixture_tail_cpp
p99_consumed_fixture_outer ENDP

p99_consumed_fixture_perform PROC FRAME
    mov rax, rsp
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
    sub rsp, 190h
    .allocstack 190h
    mov QWORD PTR [rax-98h], -2
    mov [rax+10h], rbx
    .savereg rbx, 1C8h
    mov [rax+18h], rsi
    .savereg rsi, 1D0h
    vmovaps XMMWORD PTR [rax-38h], xmm6
    .savexmm128 xmm6, 180h
    .endprolog
    mov rdi, rcx
    test DWORD PTR [rdi+10h], 1
    jnz fixture_perform_done
    mov QWORD PTR [rsp+20h], 55h
    mov QWORD PTR [rsp+28h], 66h
    mov QWORD PTR [rsp+30h], 77h
    xor r9d, r9d
    lea r8, [rsp+140h]
    mov edx, 7
    mov rcx, rdi
    mov rax, [rdi+18h]
    test rax, rax
    jz fixture_process_default
    call rax
    jmp fixture_perform_done
fixture_process_default:
    call fixture_process_relay
fixture_perform_done:
    vmovaps xmm6, XMMWORD PTR [rsp+180h]
    mov rbx, [rsp+1C8h]
    mov rsi, [rsp+1D0h]
    add rsp, 190h
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    ret
p99_consumed_fixture_perform ENDP

; Ordinary leaf tail relays: no stack change and no synthetic unwind frames.
fixture_process_relay PROC
    jmp p99_consumed_fixture_process_cpp
fixture_process_relay ENDP
p99_consumed_fixture_post_relay PROC
    mov rax, [rcx+20h]
    test rax, rax
    jz fixture_post_default
    jmp rax
fixture_post_default:
    jmp p99_consumed_fixture_post_cpp
p99_consumed_fixture_post_relay ENDP
END
