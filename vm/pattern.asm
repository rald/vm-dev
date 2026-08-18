; =====================================================================
; Pantasya VM - Direct Gradient Test Pattern
; =====================================================================

.ORG 0x0100

MAIN:
    MOV r0, 0          ; r0 = Y counter (0 to 127)

Y_LOOP:
    MOV r1, 0          ; r1 = X counter (0 to 127)

X_LOOP:
    ; Calculate Framebuffer address: 0x8000 + (Y * 128) + X[cite: 3]
    MOV r2, r0
    SHL r2, 7          ; Y * 128
    ADD r2, r1         ; + X
    MOV r3, 0x8000     ; Framebuffer base[cite: 3]
    ADD r2, r3

    ; Generate a changing color value based on X and Y
    MOV r3, r0
    ADD r3, r1         ; Color = X + Y
    MOV r4, 15
    AND r3, r4         ; Clamp to 0-15 color palette index

    STR r2, r3         ; Write color to screen buffer[cite: 3]

    ; Increment X
    MOV r4, 1
    ADD r1, r4
    MOV r4, 128
    CMP r1, r4
    JNZ X_LOOP

    ; Increment Y
    MOV r4, 1
    ADD r0, r4
    MOV r4, 128
    CMP r0, r4
    JNZ Y_LOOP

    VWAIT              ; Refresh screen frame
    JMP MAIN