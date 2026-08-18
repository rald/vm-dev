; =====================================================================
; Arkanoid for Pantasya Virtual Machine (Hex Register Naming & LCG RNG)
; Controls: Left Arrow (Move Left), Right Arrow (Move Right)
; =====================================================================

.ORG 0100h

init:
    ; --- 1. SRAND: Seed RNG using system timestamp from MMIO fff0h ---
    LDR R1, fff0h
    STR seed, R1

    ; --- 2. RAND for Ball Velocity X (ball_vx: +1 or -1) ---
    ; LCG Formula: seed = (seed * 33 + 13849) mod 65536
    LDR R1, seed
    MOV R2, R1
    SHL R1, 5h          ; R1 = seed * 32
    ADD R1, R2          ; R1 = seed * 33
    ADD R1, 35B9h       ; R1 = seed * 33 + 13849 (constant C)
    STR seed, R1        ; Save updated seed

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
    LDR R1, seed
    MOV R2, R1
    SHL R1, 5h          ; R1 = seed * 32
    ADD R1, R2          ; R1 = seed * 33
    ADD R1, 35B9h       ; R1 = seed * 33 + 13849
    STR seed, R1        ; Save updated seed

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
    JNZ check_left_val
    JMP update_ball

check_left_val:
    CMP R2, 1h          ; If paddle_x is 1h, clamp to 0h
    JNZ check_left_2h
    MOV R2, 0h
    STR paddle_x, R2
    JMP update_ball

check_left_2h:
    CMP R2, 2h          ; If paddle_x is 2h, clamp to 0h
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
    JNZ check_right_val
    JMP update_ball

check_right_val:
    CMP R2, 6ah         ; If paddle_x is 6ah, clamp to 6ch max limit
    JNZ check_right_6bh
    MOV R2, 6ch
    STR paddle_x, R2
    JMP update_ball

check_right_6bh:
    CMP R2, 6bh         ; If paddle_x is 6bh, clamp to 6ch max limit
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
    MOV RA, R1
    SUB RA, R5          ; RA = ball_x - paddle_x

    ; 1. Check if ball is to the left of the paddle (RA < 0)
    MOV RB, RA
    AND RB, 8000h
    CMP RB, 8000h
    JNZ check_paddle_right_bound
    JMP check_bottom

check_paddle_right_bound:
    ; 2. Check if ball is to the right of the paddle width (RA > 20 / 14h)
    MOV RB, RA
    SUB RB, 15h         ; RB = RA - 21 (15h = 21 decimal)
    MOV R7, RB          ; Use R7 instead of reserved RC (PC)
    AND R7, 8000h       ; Extract sign bit of (RA - 21)
    CMP R7, 8000h
    JNZ check_bottom    ; If sign bit is 0 (positive/zero), RA >= 21 -> Miss!
    JMP bounce_paddle   ; If sign bit is 1 (negative), RA < 21 -> Hit!

bounce_paddle:
    ; Increment score variable on paddle hit (+1)
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

draw_score:
    ; --- Extract Decimal Digits (Hundreds, Tens, and Units) from score ---
    LDR R1, score       ; R1 = score
    
    ; 1. Extract Hundreds
    MOV R2, 0h          ; R2 = hundreds counter
hundreds_div_loop:
    MOV R3, R1
    SUB R3, 64h         ; 64h = 100 decimal
    MOV R4, R3
    AND R4, 8000h       ; Check sign bit
    CMP R4, 8000h
    JNZ hundreds_sub
    JMP hundreds_div_done
hundreds_sub:
    MOV R1, R3          ; remainder (< 100)
    ADD R2, 1h          ; hundreds++
    JMP hundreds_div_loop
hundreds_div_done:
    ; R2 = hundreds digit, R1 = remainder (< 100)

    ; 2. Extract Tens and Units from remainder
    MOV R3, 0h          ; R3 = tens counter
tens_div_loop:
    MOV R4, R1
    SUB R4, 0Ah         ; 0Ah = 10 decimal
    MOV R5, R4
    AND R5, 8000h       ; Check sign bit
    CMP R5, 8000h
    JNZ tens_sub
    JMP tens_div_done
tens_sub:
    MOV R1, R4          ; remainder (< 10) -> units
    ADD R3, 1h          ; tens++
    JMP tens_div_loop
tens_div_done:
    ; Now: R2 = hundreds digit, R3 = tens digit, R1 = units digit

    MOV R5, 0fh         ; Color index 15 (White)

    ; --- Render Hundreds Digit at X = 2h, Y = 4h ---
    MOV RA, R2
    SHL RA, 4h          ; digit * 16 bytes
    MOV RB, font_digits
    ADD RA, RB          ; RA = font pointer for hundreds digit
    
    MOV R6, 0h          ; row = 0
hundreds_row_loop:
    MOV R7, 4h          ; Y = 4
    ADD R7, R6          ; Y + row
    SHL R7, 7h          ; * 128 width
    MOV R8, 2h          ; X = 2
    ADD R7, R8
    ADD R7, 8000h       ; FB address

    MOV R9, R6
    SHL R9, 1h          ; row * 2 bytes
    MOV R4, RA
    ADD R4, R9
    LDR RB, R4          ; RB = 16-bit row bitmap

    MOV R0, 8h          ; col = 8 pixels
hundreds_col_loop:
    MOV R4, RB
    AND R4, 0080h       ; Test MSB
    CMP R4, 0080h
    JNZ hundreds_skip_px
    STR R7, R5
hundreds_skip_px:
    ADD R7, 1h
    SHL RB, 1h
    SUB R0, 1h
    CMP R0, 0h
    JNZ hundreds_col_loop

    ADD R6, 1h
    CMP R6, 8h
    JNZ hundreds_row_loop


    ; --- Render Tens Digit at X = Ah (10), Y = 4h ---
    MOV RA, R3
    SHL RA, 4h          ; digit * 16 bytes
    MOV RB, font_digits
    ADD RA, RB          ; font pointer for tens digit

    MOV R6, 0h          ; row = 0
tens_row_loop:
    MOV R7, 4h          ; Y = 4
    ADD R7, R6          ; Y + row
    SHL R7, 7h          ; * 128 width
    MOV R8, 0Ah         ; X = 10 (Ah)
    ADD R7, R8
    ADD R7, 8000h       ; FB address

    MOV R9, R6
    SHL R9, 1h          ; row * 2 bytes
    MOV R4, RA
    ADD R4, R9
    LDR RB, R4          ; RB = 16-bit row bitmap

    MOV R0, 8h          ; col = 8 pixels
tens_col_loop:
    MOV R4, RB
    AND R4, 0080h       ; Test MSB
    CMP R4, 0080h
    JNZ tens_skip_px
    STR R7, R5
tens_skip_px:
    ADD R7, 1h
    SHL RB, 1h
    SUB R0, 1h
    CMP R0, 0h
    JNZ tens_col_loop

    ADD R6, 1h
    CMP R6, 8h
    JNZ tens_row_loop


    ; --- Render Units Digit at X = 12h (18), Y = 4h ---
    MOV RA, R1          ; R1 holds units digit remainder
    SHL RA, 4h          ; digit * 16 bytes
    MOV RB, font_digits
    ADD RA, RB          ; font pointer for units digit

    MOV R6, 0h          ; row = 0
units_row_loop:
    MOV R7, 4h          ; Y = 4
    ADD R7, R6          ; Y + row
    SHL R7, 7h          ; * 128 width
    MOV R8, 12h         ; X = 18 (12h)
    ADD R7, R8
    ADD R7, 8000h       ; FB address

    MOV R9, R6
    SHL R9, 1h          ; row * 2 bytes
    MOV R4, RA
    ADD R4, R9
    LDR RB, R4          ; RB = 16-bit row bitmap

    MOV R0, 8h          ; col = 8 pixels
units_col_loop:
    MOV R4, RB
    AND R4, 0080h       ; Test MSB
    CMP R4, 0080h
    JNZ units_skip_px
    STR R7, R5
units_skip_px:
    ADD R7, 1h
    SHL RB, 1h
    SUB R0, 1h
    CMP R0, 0h
    JNZ units_col_loop

    ADD R6, 1h
    CMP R6, 8h
    JNZ units_row_loop


    ; Check if game over condition was met (ball_y == 7fh)
    LDR R3, ball_y
    CMP R3, 7fh
    JNZ continue_game
    JMP game_over       ; Halt execution after rendering the final frame

continue_game:
    ; Synchronize with the 60fps display refresh cycle
    VWAIT
    JMP main_loop

game_over:
    ; Permanently halt execution and stop VM
    HALT

; =====================================================================
; Variable Declarations in RAM & Polished Font Bitmaps (0 to 9)
; =====================================================================
.ORG 7000h
paddle_x:  .DW 0h
ball_x:    .DW 0h
ball_y:    .DW 0h
ball_vx:   .DW 0h
ball_vy:   .DW 0h
score:     .DW 0h
seed:      .DW 0h

.ORG 7200h
font_digits:
; Digit 0 (Clean box with smooth corners)
.DW 0078h, 00CCh, 00DCh, 00FCh, 00ECh, 00CCh, 0078h, 0000h
; Digit 1 (Centered vertical stem with base)
.DW 0030h, 00F0h, 0030h, 0030h, 0030h, 0030h, 00FCh, 0000h
; Digit 2 (Curved top with solid base)
.DW 0078h, 00CCh, 000Ch, 0038h, 0060h, 00CCh, 00FCh, 0000h
; Digit 3 (Symmetric dual-arc curve)
.DW 0078h, 00CCh, 000Ch, 0038h, 000Ch, 00CCh, 0078h, 0000h
; Digit 4 (Clean geometric crossbar)
.DW 001Ch, 003Ch, 006Ch, 00CCh, 00FEh, 000Ch, 000Ch, 0000h
; Digit 5 (Top bar with curved bowl)
.DW 00FCh, 00C0h, 00F8h, 000Ch, 000Ch, 00CCh, 0078h, 0000h
; Digit 6 (Rounded lower bowl with upper curve)
.DW 0038h, 0060h, 00C0h, 00F8h, 00CCh, 00CCh, 0078h, 0000h
; Digit 7 (Sharp top bar with diagonal stem)
.DW 00FCh, 00CCh, 000Ch, 0018h, 0030h, 0060h, 0060h, 0000h
; Digit 8 (Symmetric double-loop)
.DW 0078h, 00CCh, 00CCh, 0078h, 00CCh, 00CCh, 0078h, 0000h
; Digit 9 (Rounded upper bowl with tail)
.DW 0078h, 00CCh, 00CCh, 007Ch, 000Ch, 0018h, 0070h, 0000h
