; Isolated Async-shaped stack. No retail code or addresses are executed.
EXTERN p99_wrist_route_async_body:PROC
PUBLIC p99_wrist_route_async_fixture
.code
p99_wrist_route_async_fixture PROC FRAME
    sub rsp, 5F8h
    .allocstack 5F8h
    .endprolog
    lea rdx, [rsp+110h]
    lea r8, [rsp+160h]
    lea r9, [rsp+5F8h]
    call p99_wrist_route_async_body
    add rsp, 5F8h
    ret
p99_wrist_route_async_fixture ENDP
END
