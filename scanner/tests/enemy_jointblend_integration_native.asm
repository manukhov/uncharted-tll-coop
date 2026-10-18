; Owned fixture ONLY, never linked into the runtime/core. These frames model
; the two independently checked native stack distances; no game code is copied.
EXTERN enemy_fixture_jointblend_body:PROC
EXTERN enemy_fixture_process_body:PROC
EXTERN __chkstk:PROC
PUBLIC enemy_fixture_jointblend
PUBLIC enemy_fixture_process
.code
enemy_fixture_jointblend PROC FRAME
    sub rsp, 5F8h
    .allocstack 5F8h
    .endprolog
    lea rdx, [rsp+110h]
    call enemy_fixture_jointblend_body
    add rsp, 5F8h
    ret
enemy_fixture_jointblend ENDP
enemy_fixture_process PROC FRAME
    ; Keep the helper's home area BELOW Segment, not inside its fields.
    mov rax, 1648h
    call __chkstk
    sub rsp, rax
    .allocstack 1648h
    .endprolog
    mov r9, rcx
    mov rcx, r8
    lea rdx, [rsp+20h]
    mov r8, r9
    call enemy_fixture_process_body
    add rsp, 1648h
    ret
enemy_fixture_process ENDP
END
