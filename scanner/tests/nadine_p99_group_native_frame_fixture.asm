; Isolated fixture only: no tll bytes or entrypoint are executed. Reproduce the
; retail total stack displacement and both Process argument3 offsets exactly.
EXTERN p99_native_frame_fixture_body:PROC
PUBLIC p99_native_frame_fixture
.code
p99_native_frame_fixture PROC FRAME
    sub rsp, 5F8h
    .allocstack 5F8h
    .endprolog
    lea rdx, [rsp+110h]
    lea r8, [rsp+160h]
    lea r9, [rsp+5F8h]
    call p99_native_frame_fixture_body
    add rsp, 5F8h
    ret
p99_native_frame_fixture ENDP
END
