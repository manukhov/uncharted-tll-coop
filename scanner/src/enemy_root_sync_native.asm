option casemap:none
EXTERN TllEnemyRootPostblend:PROC
PUBLIC TllEnemyRootPostblendBridge
.code
; Exact Perform141A929A1 has RSI=job. Preserve the original return PC/stack;
; C++ checks that PC BEFORE treating this volatile argument as a job pointer.
; Other Pirate+408 callers are forwarded once without inspecting RSI.
TllEnemyRootPostblendBridge PROC
    mov rdx, rsi
    jmp TllEnemyRootPostblend
TllEnemyRootPostblendBridge ENDP
END
