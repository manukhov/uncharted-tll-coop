; Normal Windows x64 function with unwind metadata. No moved game code.
; Input9: original RCX/RDX/R8/R9 and stack a5/a6/a7, Native, slot pointer.
PUBLIC p99_process_invoke_native
.code
p99_process_invoke_native PROC FRAME
    sub rsp, 58h
    .allocstack 58h
    .endprolog
    ; EntrySP+28/30/38 => currentSP+80/88/90. Outgoing stack arguments are
    ; currentSP+20/28/30, outside the32-byte home area owned by Native.
    mov rax, [rsp+80h]
    mov [rsp+20h], rax
    mov rax, [rsp+88h]
    mov [rsp+28h], rax
    mov rax, [rsp+90h]
    mov [rsp+30h], rax
    mov r11, [rsp+98h]
    mov r10, [rsp+0A0h]
    mov [rsp+40h], r10
    lea rax, [rsp-8]
    mov [r10], rax
    call r11
    ; Preserve the native uint32 EAX result. Native may clobber all home slots.
    mov r10, [rsp+40h]
    mov QWORD PTR [r10], 0
    add rsp, 58h
    ret
p99_process_invoke_native ENDP
END
