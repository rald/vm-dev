#ifndef VM_H
#define VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
    OP_JR    = 0x0, /* pc = code + (uint32_t)r[a]           */
    OP_MOVI  = 0x1, /* r[a] = (int8_t)i8                    */
    OP_MOV   = 0x2, /* r[a] = r[b]                          */
    OP_ADD   = 0x3, /* r[a] = r[b] + r[c]                   */
    OP_SUB   = 0x4, /* r[a] = r[b] - r[c]                   */
    OP_AND   = 0x5, /* r[a] = r[b] & r[c]                   */
    OP_OR    = 0x6, /* r[a] = r[b] | r[c]                   */
    OP_XOR   = 0x7, /* r[a] = r[b] ^ r[c]                   */
    OP_SHL   = 0x8, /* r[a] = (uint32_t)r[a] << (i8 & 31)   */
    OP_SHR   = 0x9, /* r[a] = (uint32_t)r[a] >> (i8 & 31)   */
    OP_CMPLT = 0xA, /* r[a] = (r[b] < r[c]) ? 1 : 0         */
    OP_JMP   = 0xB, /* pc += sext12(ins)                    */
    OP_JZ    = 0xC, /* if (r[a] == 0) pc += (int8_t)i8      */
    OP_LD32  = 0xD, /* r[a] = mem32[r[b] + c*4]             */
    OP_ST32  = 0xE, /* mem32[r[b] + c*4] = r[a]             */
    OP_CALL  = 0xF  /* r[14] = next_pc_idx; pc += sext12(ins) */
};

/* Memory Map Constants */
#define VM_SCREEN_W      128
#define VM_SCREEN_H      128
#define VM_BACK_FB_ADDR  0x0000u /* 8192 bytes: VM draws here (128x128 4bpp) */
#define VM_FRONT_FB_ADDR 0x2000u /* 8192 bytes: SDL2 reads here (128x128 4bpp) */
#define VM_FB_ADDR       VM_BACK_FB_ADDR
#define VM_FB_SIZE       8192u   /* 128 * 128 / 2 (4bpp packed: low nibble = even x, high nibble = odd x) */
#define VM_INPUT_ADDR    0x4000u /* 32-bit bitmask of pressed buttons */
#define VM_SYS_ADDR      0x4004u /* 32-bit system register (bit 0 = swap back->front & yield frame) */
#define VM_RAM_ADDR      0x4010u /* General purpose RAM start */
#define VM_FONT_ADDR     0x4400u /* 8x8 DOS BIOS Font (128 ASCII chars * 8 bytes = 1024 bytes) */
#define VM_MEM_SIZE      0x6000u /* 24 KB total address space */

/* Button Bitmasks */
#define BTN_UP           (1u << 0)
#define BTN_DOWN         (1u << 1)
#define BTN_LEFT         (1u << 2)
#define BTN_RIGHT        (1u << 3)
#define BTN_X            (1u << 4)
#define BTN_O            (1u << 5)
#define BTN_L            (1u << 6)
#define BTN_R            (1u << 7)
#define BTN_SELECT       (1u << 8)
#define BTN_START        (1u << 9)

typedef struct {
    int32_t r[16];
    int32_t pc;
    uint8_t *mem;
    size_t mem_size;
    bool halted;
    bool frame_ready;
} vm_env_t;

static inline uint16_t enc_rrr(uint8_t op, uint8_t a, uint8_t b, uint8_t c) {
    return (uint16_t)(((op & 0xF) << 12) | ((a & 0xF) << 8) |
                      ((b & 0xF) << 4) | (c & 0xF));
}

static inline uint16_t enc_ri8(uint8_t op, uint8_t a, uint8_t imm8) {
    return (uint16_t)(((op & 0xF) << 12) | ((a & 0xF) << 8) | imm8);
}

static inline uint16_t enc_i12(uint8_t op, int16_t imm12) {
    return (uint16_t)(((op & 0xF) << 12) | ((uint16_t)imm12 & 0x0FFFu));
}

static inline int32_t sext12(uint16_t w) {
    uint16_t v = w & 0x0FFFu;
    return (v & 0x0800u) ? (int32_t)v - 0x1000 : (int32_t)v;
}

void vm_env_init(vm_env_t *env, uint8_t *mem, size_t mem_size,
                 int32_t init_pc, int32_t init_lr, int32_t init_sp);

int32_t vm_run_env(vm_env_t *env, const uint16_t *code, size_t code_len,
                   size_t max_steps);

int32_t vm_run(const uint16_t *code, size_t code_len,
               uint8_t *mem, size_t mem_size,
               int32_t init_pc, int32_t init_lr, int32_t init_sp);

/*
 * Assemble a source file into out_code. On failure, returns -1 and leaves both
 * out_code and out_len unchanged; all three pointer arguments must be non-NULL.
 */
int vm_assemble_file(const char *path, uint16_t *out_code, size_t max_code, size_t *out_len);

#endif /* VM_H */
