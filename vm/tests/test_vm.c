#define _POSIX_C_SOURCE 200809L

#include "vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int assemble_text(const char *source, uint16_t *code, size_t capacity,
                         size_t *length, const char *diagnostic) {
    char path[] = "/tmp/vm-assembler-test-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *source_file = fdopen(fd, "w");
    assert(source_file);
    assert(fputs(source, source_file) >= 0);
    assert(fclose(source_file) == 0);

    FILE *capture = tmpfile();
    assert(capture);
    fflush(stderr);
    int saved_stderr = dup(STDERR_FILENO);
    assert(saved_stderr >= 0);
    assert(dup2(fileno(capture), STDERR_FILENO) >= 0);
    int result = vm_assemble_file(path, code, capacity, length);
    fflush(stderr);
    assert(dup2(saved_stderr, STDERR_FILENO) >= 0);
    close(saved_stderr);
    unlink(path);

    if (diagnostic) {
        char output[512] = {0};
        rewind(capture);
        assert(fgets(output, sizeof(output), capture));
        assert(strstr(output, diagnostic));
    }
    fclose(capture);
    return result;
}

static void test_assembler_success_and_execution(void) {
    const char *source =
        "start:\n"
        "  movi r0, 4\n"
        "  movi r1, 5\n"
        "  add r2, r0, r1\n"
        "  movi r3, 0\n"
        "  jz r3, done\n"
        "  movi r0, 99\n"
        "done:\n"
        "  mov r0, r2\n"
        "  jr lr\n";
    uint16_t code[16] = {0};
    size_t length = 0;
    assert(assemble_text(source, code, 16, &length, NULL) == 0);
    assert(length == 8);

    uint8_t memory[64] = {0};
    vm_env_t env;
    vm_env_init(&env, memory, sizeof(memory), 0, (int32_t)length, 0);
    assert(vm_run_env(&env, code, length, 32) == 9);
    assert(env.halted);
}

static void test_assembler_errors_are_atomic(void) {
    const char *cases[] = {
        "movi r0, 1\nnot_an_op r0\n",
        "movi r16, 1\n",
        "movi r0, nowhere\n",
        "same:\n same:\n",
        "movi r0, 256\n",
        "movi r0, 999999999999999999999\n",
        "jz r0, distant\n"
    };
    const char *diagnostics[] = {
        "line 2: unknown instruction 'not_an_op'",
        "line 1: unknown register 'r16'",
        "line 1: unknown label 'nowhere'",
        "line 2: duplicate label 'same'",
        "line 1: movi immediate out of range",
        "line 1: integer out of range",
        "line 1: jz target out of range"
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint16_t code[256];
        for (size_t j = 0; j < sizeof(code) / sizeof(code[0]); j++)
            code[j] = 0xA55A;
        size_t length = 123;
        char long_source[2048];
        if (i == 6) {
            size_t used = (size_t)snprintf(long_source, sizeof(long_source),
                                            "%s", cases[i]);
            for (int j = 0; j < 128; j++)
                used += (size_t)snprintf(long_source + used,
                                         sizeof(long_source) - used,
                                         "movi r1, 0\n");
            snprintf(long_source + used, sizeof(long_source) - used, "distant:\n");
        } else {
            snprintf(long_source, sizeof(long_source), "%s", cases[i]);
        }
        assert(assemble_text(long_source, code, 256, &length,
                             diagnostics[i]) == -1);
        assert(length == 123);
        for (size_t j = 0; j < sizeof(code) / sizeof(code[0]); j++)
            assert(code[j] == 0xA55A);
    }

    uint16_t code[2] = {0xA55A, 0xA55A};
    size_t length = 77;
    assert(assemble_text("movi r0, 1\nmovi r1, 2\n", code, 1, &length,
                         "line 2: program exceeds max_code") == -1);
    assert(code[0] == 0xA55A && code[1] == 0xA55A && length == 77);

    char *long_source = malloc(32768);
    assert(long_source);
    size_t used = (size_t)snprintf(long_source, 32768, "jmp distant\n");
    for (int i = 0; i < 2048; i++) {
        int written = snprintf(long_source + used, 32768 - used, "movi r1, 0\n");
        assert(written > 0 && (size_t)written < 32768 - used);
        used += (size_t)written;
    }
    snprintf(long_source + used, 32768 - used, "distant:\n");
    uint16_t large_code[4096];
    for (size_t i = 0; i < 4096; i++) large_code[i] = 0xA55A;
    length = 11;
    assert(assemble_text(long_source, large_code, 4096, &length,
                         "line 1: jmp target out of range") == -1);
    assert(length == 11 && large_code[0] == 0xA55A &&
           large_code[4095] == 0xA55A);
    free(long_source);
}

static void test_vm_bounds_steps_and_frame_swap(void) {
    uint16_t invalid_load[] = {enc_rrr(OP_LD32, 0, 1, 0)};
    uint8_t small_memory[8] = {0};
    vm_env_t env;
    vm_env_init(&env, small_memory, sizeof(small_memory), 0, 0, 0);
    env.r[1] = -1;
    vm_run_env(&env, invalid_load, 1, 0);
    assert(env.halted && env.pc == 1);

    uint16_t loop[] = {enc_i12(OP_JMP, -1)};
    vm_env_init(&env, small_memory, sizeof(small_memory), 0, 0, 0);
    vm_run_env(&env, loop, 1, 2);
    assert(!env.halted && env.pc == 0);

    uint8_t memory[VM_MEM_SIZE] = {0};
    memory[VM_BACK_FB_ADDR] = 0x5A;
    uint16_t frame_code[] = {
        enc_ri8(OP_MOVI, 1, 0x40),
        enc_ri8(OP_SHL, 1, 8),
        enc_ri8(OP_MOVI, 2, 4),
        enc_rrr(OP_ADD, 1, 1, 2),
        enc_ri8(OP_MOVI, 0, 1),
        enc_rrr(OP_ST32, 0, 1, 0)
    };
    vm_env_init(&env, memory, sizeof(memory), 0, 0, 0);
    vm_run_env(&env, frame_code, sizeof(frame_code) / sizeof(frame_code[0]), 16);
    assert(env.frame_ready && !env.halted);
    assert(memory[VM_FRONT_FB_ADDR] == 0x5A);
}

static void test_api_argument_guards(void) {
    uint16_t code[2] = {0xA55A, 0xA55A};
    size_t length = 42;
    assert(vm_assemble_file(NULL, code, 2, &length) == -1);
    assert(vm_assemble_file("asm/arkanoid.asm", NULL, 2, &length) == -1);
    assert(vm_assemble_file("asm/arkanoid.asm", code, 2, NULL) == -1);
    assert(code[0] == 0xA55A && code[1] == 0xA55A && length == 42);

    vm_env_init(NULL, NULL, 1, 0, 0, 0);
    assert(vm_run_env(NULL, NULL, 0, 0) == 0);
    vm_env_t env;
    vm_env_init(&env, NULL, sizeof(code), 0, 0, 0);
    vm_run_env(&env, code, 2, 0);
    assert(env.halted && env.mem_size == 0);
}

int main(void) {
    test_assembler_success_and_execution();
    test_assembler_errors_are_atomic();
    test_vm_bounds_steps_and_frame_swap();
    test_api_argument_guards();

    uint16_t arkanoid[4096];
    size_t arkanoid_len = 0;
    assert(vm_assemble_file("asm/arkanoid.asm", arkanoid, 4096,
                            &arkanoid_len) == 0);
    assert(arkanoid_len > 0);
    uint8_t arkanoid_memory[VM_MEM_SIZE] = {0};
    vm_env_t arkanoid_env;
    vm_env_init(&arkanoid_env, arkanoid_memory, sizeof(arkanoid_memory), 0,
                (int32_t)arkanoid_len, VM_MEM_SIZE);
    for (int frame = 0; frame < 20 && !arkanoid_env.frame_ready; frame++) {
        vm_run_env(&arkanoid_env, arkanoid, arkanoid_len, 250000);
        assert(!arkanoid_env.halted);
    }
    assert(arkanoid_env.frame_ready);
    puts("VM tests passed");
    return 0;
}
