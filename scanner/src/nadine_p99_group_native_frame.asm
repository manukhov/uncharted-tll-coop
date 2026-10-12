; No moved game instructions. This is an ordinary Windows x64 function with
; unwind metadata, not a leaf relay. RCX=job, RDX=native, R8=return-slot storage.
; Shadow space [rsp..rsp+1F] belongs to native; our saved pointer is outside it.
PUBLIC p99_group_invoke_native
PUBLIC p99_consumed_invoke_native
.code
p99_consumed_invoke_native LABEL NEAR
p99_group_invoke_native PROC FRAME
    sub rsp, 38h
    .allocstack 38h
    .endprolog
    mov [rsp+28h], r8
    lea rax, [rsp-8]
    mov [r8], rax
    call rdx
    mov r8, [rsp+28h]
    mov QWORD PTR [r8], 0
    add rsp, 38h
    ret
p99_group_invoke_native ENDP
END
