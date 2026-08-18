; =====================================================================
; Arkanoid for Pantasya Virtual Machine (No Gaps & Clean Boundaries)
; Controls: Left Arrow (Move Left), Right Arrow (Move Right)
; =====================================================================

.ORG 0100h

init:
    ; Initialize game variables using labels
    MOV R0, 36h         ; Initial paddle X position (54)
    STR paddle_x, R0
    MOV R0, 40h         ; Initial ball X position (64)
    STR ball_x, R0
    MOV R0, 3ch         ; Initial ball Y position (60)
    STR ball_y, R0
    MOV R0, 1h          ; Ball velocity X (+1)
    STR ball_vx, R0
    MOV R0, 1h          ; Ball velocity Y (+1)
    STR ball_vy, R0

main_loop:
    ; Clear framebuffer memory completely (8000h to 0C000h)[cite: 3]
    ; Increment by 1h to prevent pixel smudging trails
    MOV R1, 8000h
    MOV R2, 0000h
clear_loop:
    STR R1, R2
    ADD R1, 1h          
    CMP R1, 0c000h
    JNZ clear_loop

    ; Read keyboard input from MMIO address fffch[cite: 3]
    LDR R1, fffch
    CMP R1, 25h         ; Left arrow keycode (37 decimal -> 25h)
    JNZ check_right
    
    ; --- LEFT BOUNDARY & MOVEMENT ---
    LDR R2, paddle_x
    CMP R2, 0h          ; Check if already at left edge (0h)
    JNZ check_left_overflow
    JMP update_ball

check_left_overflow:
    CMP R2, 2h          ; If paddle_x < 2h, clamp to 0h to avoid underflow gap
    JNZ move_left
    MOV R2, 0h
    STR paddle_x, R2
    JMP update_ball

move_left:
    SUB R2, 2h          ; Smooth movement step
    STR paddle_x, R2
    JMP update_ball

check_right:
    CMP R1, 27h         ; Right arrow keycode (39 decimal -> 27h)
    JNZ update_ball
    
    ; --- RIGHT BOUNDARY & MOVEMENT ---
    LDR R2, paddle_x
    CMP R2, 6ch         ; Right wall boundary check (128 - 20 width = 108 / 6Ch)
    JNZ check_right_overflow
    JMP update_ball

check_right_overflow:
    CMP R2, 6ah         ; If paddle_x > 6ah, clamp to 6ch max limit
    JNZ move_right
    MOV R2, 6ch
    STR paddle_x, R2
    JMP update_ball

move_right:
    ADD R2, 2h          ; Smooth movement step
    STR paddle_x, R2

update_ball:
    ; Update ball position using label variables
    LDR R1, ball_x
    LDR R2, ball_vx
    ADD R1, R2
    STR ball_x, R1

    LDR R3, ball_y
    LDR R4, ball_vy
    ADD R3, R4
    STR ball_y, R3

    ; --- WALL COLLISIONS ---
    ; Left wall collision (ball_x <= 0h)
    CMP R1, 0h
    JNZ check_right_wall
    MOV R2, 1h          ; Bounce right (+1)
    STR ball_vx, R2

check_right_wall:
    ; Right wall collision (ball_x >= 7eh, since ball width is 2)
    CMP R1, 7eh
    JNZ check_top_wall
    MOV R2, 0ffffh      ; Bounce left (-1 in 16-bit)
    STR ball_vx, R2

check_top_wall:
    ; Top wall collision (ball_y <= 1h)
    CMP R3, 1h
    JNZ check_paddle
    MOV R4, 1h          ; Bounce down (+1)
    STR ball_vy, R4
    JMP draw_paddle

check_paddle:
    ; Paddle Y position is 76h (118)
    CMP R3, 76h
    JNZ check_bottom

    ; Precise range check: paddle_x <= ball_x <= paddle_x + 20 (14h)
    LDR R1, ball_x
    LDR R5, paddle_x
    MOV R7, R1
    SUB R7, R5          ; R7 = ball_x - paddle_x

    ; Check if ball is to the left of the paddle (negative result / high bit set)
    MOV R8, R7
    AND R8, 8000h
    CMP R8, 8000h
    JNZ check_paddle_right_bound
    JMP check_bottom

check_paddle_right_bound:
    ; Check if ball is to the right of the paddle width (20 / 14h)
    CMP R7, 15h         ; If R7 >= 21, miss
    JNZ bounce_paddle
    JMP check_bottom

bounce_paddle:
    MOV R4, 0ffffh      ; Bounce ball UP (-1)
    STR ball_vy, R4
    JMP draw_paddle

check_bottom:
    ; Check if ball reaches bottom row (7bh) -> Reset Ball
    CMP R3, 7bh
    JNZ draw_paddle
    MOV R1, 40h         ; Reset X to center (64)
    STR ball_x, R1
    MOV R1, 3ch         ; Reset Y to middle (60)
    STR ball_y, R1
    MOV R1, 1h          ; Reset velocity X (+1)
    STR ball_vx, R1
    MOV R1, 1h          ; Reset velocity Y (+1)
    STR ball_vy, R1

draw_paddle:
    ; Draw Paddle at Y = 76h (118), width = 14h (20 pixels)
    LDR R1, paddle_x
    MOV R3, 76h         ; Y coordinate
    SHL R3, 7h          ; Multiply Y by 128 (screen width)
    MOV R4, 8000h       ; Framebuffer start address[cite: 3]
    ADD R4, R3
    ADD R4, R1
    MOV R5, 7h          ; Palette color index 7
    MOV R6, 14h         ; Width counter (20 pixels)
paddle_loop:
    STR R4, R5
    ADD R4, 1h
    SUB R6, 1h
    CMP R6, 0h
    JNZ paddle_loop

draw_ball:
    ; Draw Ball at ball_x, ball_y using palette color index 4 (2x1 pixels)
    LDR R1, ball_x
    LDR R3, ball_y
    MOV R4, R3
    SHL R4, 7h
    MOV R5, 8000h       ; Framebuffer start address[cite: 3]
    ADD R5, R4
    ADD R5, R1
    MOV R6, 4h          ; Palette color index 4
    STR R5, R6
    ADD R5, 1h
    STR R5, R6

    ; Synchronize with the 60fps display refresh cycle[cite: 3]
    VWAIT
    JMP main_loop

; =====================================================================
; Variable Declarations in RAM (Using Labels)
; =====================================================================
.ORG 7000h
paddle_x:  .DW 0h
ball_x:    .DW 0h
ball_y:    .DW 0h
ball_vx:   .DW 0h
ball_vy:   .DW 0h