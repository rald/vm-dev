; =====================================================================
; Arkanoid for Pantasya Virtual Machine (Randomized Ball VX & VY)
; Controls: Left Arrow (Move Left), Right Arrow (Move Right)
; Restart on Game Over: Press Enter
; =====================================================================

.ORG 0100h

init:
    ; --- 1. SRAND: Seed RNG using system timestamp from MMIO fff0h ---
    LDR R1, fff0h
    STR rng_seed, R1

    ; --- 2. RAND for Ball Velocity X (ball_vx: +1 or -1) ---
    ; LCG Formula: seed = (seed * 33 + 13849) mod 65536
    LDR R1, rng_seed
    MOV R2, R1
    SHL R1, 5h          ; R1 = seed * 32
    ADD R1, R2          ; R1 = seed * 33
    ADD R1, 35B9h       ; R1 = seed * 33 + 13849 (constant C)
    STR rng_seed, R1    ; Save updated seed

    MOV R2, R1
    AND R2, 1h          ; Check bit 0 for direction
    CMP R2, 0h
    JNZ set_vx_neg
    MOV R0, 1h          ; ball_vx = +1 (Right)
    JMP store_vx
set_vx_neg:
    MOV R0, 0ffffh      ; ball_vx = -1 (Left, 0FFFFh in 16-bit)
store_vx:
    STR ball_vx, R0

    ; --- 3. RAND for Ball Velocity Y (ball_vy: +1 or -1) ---
    LDR R1, rng_seed
    MOV R2, R1
    SHL R1, 5h          ; R1 = seed * 32
    ADD R1, R2          ; R1 = seed * 33
    ADD R1, 35B9h       ; R1 = seed * 33 + 13849
    STR rng_seed, R1    ; Save updated seed

    MOV R2, R1
    AND R2, 1h          ; Check bit 0 for direction
    CMP R2, 0h
    JNZ set_vy_neg
    MOV R0, 1h          ; ball_vy = +1 (Down)
    JMP store_vy
set_vy_neg:
    MOV R0, 0ffffh      ; ball_vy = -1 (Up)
store_vy:
    STR ball_vy, R0

    ; Initialize other game variables
    MOV R0, 36h         ; Initial paddle X position (54)
    STR paddle_x, R0
    MOV R0, 40h         ; Initial ball X position (64)
    STR ball_x, R0
    MOV R0, 3ch         ; Initial ball Y position (60)
    STR ball_y, R0
    MOV R0, 0h          ; Initial score
    STR score, R0

main_loop:
    ; Clear framebuffer memory completely (8000h to 0C000h)
    MOV R1, 8000h
    MOV R2, 0000h
clear_loop:
    STR R1, R2
    ADD R1, 1h          
    CMP R1, 0c000h
    JNZ clear_loop

    ; Read keyboard input from MMIO address fffch
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

    ; 1. Check if ball is to the left of the paddle (R7 < 0)
    MOV R8, R7
    AND R8, 8000h
    CMP R8, 8000h
    JNZ check_paddle_right_bound
    JMP check_bottom

check_paddle_right_bound:
    ; 2. Check if ball is to the right of the paddle width (R7 > 20 / 14h)
    MOV R8, R7
    SUB R8, 15h         ; R8 = R7 - 21 (15h = 21 decimal)
    MOV R9, R8
    AND R9, 8000h       ; Extract sign bit of (R7 - 21)
    CMP R9, 8000h
    JNZ check_bottom    ; If sign bit is 0 (positive/zero), R7 >= 21 -> Miss!
    JMP bounce_paddle   ; If sign bit is 1 (negative), R7 < 21 -> Hit!

bounce_paddle:
    ; Increment score variable on paddle hit
    LDR R6, score
    ADD R6, 1h
    STR score, R6

    MOV R4, 0ffffh      ; Bounce ball UP (-1)
    STR ball_vy, R4
    JMP draw_paddle

check_bottom:
    ; Check if ball reaches bottom of screen (7fh)
    CMP R3, 7fh
    JNZ draw_paddle
    ; Fall through to draw_paddle and draw_ball to render the final frame

draw_paddle:
    ; Draw Paddle at Y = 76h (118), width = 14h (20 pixels)
    LDR R1, paddle_x
    MOV R3, 76h         ; Y coordinate
    SHL R3, 7h          ; Multiply Y by 128 (screen width)
    MOV R4, 8000h       ; Framebuffer start address
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
    MOV R5, 8000h       ; Framebuffer start address
    ADD R5, R4
    ADD R5, R1
    MOV R6, 4h          ; Palette color index 4
    STR R5, R6
    ADD R5, 1h
    STR R5, R6

    ; Check if game over condition was met (ball_y == 7fh)
    LDR R3, ball_y
    CMP R3, 7fh
    JNZ continue_game
    JMP game_over       ; Freeze and enter Game Over loop after rendering final frame

continue_game:
    ; Synchronize with the 60fps display refresh cycle
    VWAIT
    JMP main_loop

game_over:
    ; Freeze game updates and listen for restart key (Enter key code 0Dh / 13 decimal)
    LDR R1, fffch
    CMP R1, 0dh
    JNZ skip_restart
    JMP init            ; Restart game if Enter is pressed
skip_restart:

    VWAIT
    JMP game_over

; =====================================================================
; Variable Declarations in RAM (Using Labels)
; =====================================================================
.ORG 7000h
paddle_x:  .DW 0h
ball_x:    .DW 0h
ball_y:    .DW 0h
ball_vx:   .DW 0h
ball_vy:   .DW 0h
score:     .DW 0h
rng_seed:  .DW 0h