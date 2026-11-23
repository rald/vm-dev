#include "vm.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static int32_t uint32_to_int32(uint32_t value) {
    if (value <= INT32_MAX) return (int32_t)value;
    return -1 - (int32_t)(UINT32_MAX - value);
}

static int32_t sign_extend8(uint8_t value) {
    return (value & 0x80u) ? (int32_t)value - 0x100 : (int32_t)value;
}

static int32_t load32_le(const uint8_t *mem, size_t addr) {
    uint32_t value = (uint32_t)mem[addr] |
                     ((uint32_t)mem[addr + 1] << 8) |
                     ((uint32_t)mem[addr + 2] << 16) |
                     ((uint32_t)mem[addr + 3] << 24);
    return uint32_to_int32(value);
}

static void store32_le(uint8_t *mem, size_t addr, int32_t value) {
    uint32_t bits = (uint32_t)value;
    mem[addr] = (uint8_t)bits;
    mem[addr + 1] = (uint8_t)(bits >> 8);
    mem[addr + 2] = (uint8_t)(bits >> 16);
    mem[addr + 3] = (uint8_t)(bits >> 24);
}

void vm_env_init(vm_env_t *env, uint8_t *mem, size_t mem_size,
                 int32_t init_pc, int32_t init_lr, int32_t init_sp) {
    if (!env) return;
    memset(env, 0, sizeof(*env));
    env->mem = mem;
    env->mem_size = mem ? mem_size : 0;
    env->pc = init_pc;
    env->r[14] = init_lr;
    env->r[15] = init_sp;
}

static void vm_halt(vm_env_t *env, size_t pc) {
    env->pc = pc <= INT32_MAX ? (int32_t)pc : INT32_MAX;
    env->halted = true;
}

static bool memory_range_valid(size_t mem_size, int32_t base, uint8_t word_offset,
                               size_t *address) {
    int64_t effective = (int64_t)base + (int64_t)word_offset * 4;
    if (effective < 0 || (uint64_t)effective > mem_size) return false;
    size_t addr = (size_t)effective;
    if (mem_size - addr < sizeof(uint32_t)) return false;
    *address = addr;
    return true;
}

int32_t vm_run_env(vm_env_t *env, const uint16_t *code, size_t code_len,
                   size_t max_steps) {
    if (!env) return 0;
    if (env->halted) return env->r[0];
    if (!code || !env->mem || code_len > INT32_MAX || env->pc < 0 ||
        (size_t)env->pc >= code_len) {
        vm_halt(env, env->pc < 0 ? 0 : (size_t)env->pc);
        return env->r[0];
    }

    size_t pc = (size_t)env->pc;
    size_t steps = 0;
    env->frame_ready = false;

    while (pc < code_len) {
        if (max_steps && steps >= max_steps) {
            env->pc = (int32_t)pc;
            return env->r[0];
        }
        steps++;

        uint16_t ins = code[pc++];
        uint8_t op = (uint8_t)(ins >> 12);
        uint8_t a = (uint8_t)((ins >> 8) & 0x0F);
        uint8_t b = (uint8_t)((ins >> 4) & 0x0F);
        uint8_t c = (uint8_t)(ins & 0x0F);
        uint8_t imm8 = (uint8_t)ins;
        int64_t target;
        size_t addr;

        switch (op) {
        case OP_JR:
            if (env->r[a] < 0 || (uint64_t)env->r[a] > code_len) {
                vm_halt(env, pc);
                return env->r[0];
            }
            pc = (size_t)env->r[a];
            break;
        case OP_MOVI:
            env->r[a] = sign_extend8(imm8);
            break;
        case OP_MOV:
            env->r[a] = env->r[b];
            break;
        case OP_ADD:
            env->r[a] = uint32_to_int32((uint32_t)env->r[b] +
                                         (uint32_t)env->r[c]);
            break;
        case OP_SUB:
            env->r[a] = uint32_to_int32((uint32_t)env->r[b] -
                                         (uint32_t)env->r[c]);
            break;
        case OP_AND:
            env->r[a] = env->r[b] & env->r[c];
            break;
        case OP_OR:
            env->r[a] = env->r[b] | env->r[c];
            break;
        case OP_XOR:
            env->r[a] = env->r[b] ^ env->r[c];
            break;
        case OP_SHL:
            env->r[a] = uint32_to_int32((uint32_t)env->r[a] << (imm8 & 31));
            break;
        case OP_SHR:
            env->r[a] = uint32_to_int32((uint32_t)env->r[a] >> (imm8 & 31));
            break;
        case OP_CMPLT:
            env->r[a] = env->r[b] < env->r[c] ? 1 : 0;
            break;
        case OP_JMP:
            target = (int64_t)pc + sext12(ins);
            if (target < 0 || (uint64_t)target > code_len) {
                vm_halt(env, pc);
                return env->r[0];
            }
            pc = (size_t)target;
            break;
        case OP_JZ:
            if (env->r[a] == 0) {
                target = (int64_t)pc + sign_extend8(imm8);
                if (target < 0 || (uint64_t)target > code_len) {
                    vm_halt(env, pc);
                    return env->r[0];
                }
                pc = (size_t)target;
            }
            break;
        case OP_LD32:
            if (!memory_range_valid(env->mem_size, env->r[b], c, &addr)) {
                vm_halt(env, pc);
                return env->r[0];
            }
            env->r[a] = load32_le(env->mem, addr);
            break;
        case OP_ST32:
            if (!memory_range_valid(env->mem_size, env->r[b], c, &addr)) {
                vm_halt(env, pc);
                return env->r[0];
            }
            store32_le(env->mem, addr, env->r[a]);
            if (addr == VM_SYS_ADDR && (env->r[a] & 1)) {
                if (VM_FRONT_FB_ADDR > env->mem_size ||
                    VM_FB_SIZE > env->mem_size - VM_FRONT_FB_ADDR ||
                    VM_FB_SIZE > env->mem_size - VM_BACK_FB_ADDR) {
                    vm_halt(env, pc);
                    return env->r[0];
                }
                memcpy(env->mem + VM_FRONT_FB_ADDR, env->mem + VM_BACK_FB_ADDR,
                       VM_FB_SIZE);
                env->frame_ready = true;
                env->pc = (int32_t)pc;
                return env->r[0];
            }
            break;
        case OP_CALL:
            target = (int64_t)pc + sext12(ins);
            if (target < 0 || (uint64_t)target > code_len) {
                vm_halt(env, pc);
                return env->r[0];
            }
            env->r[14] = (int32_t)pc;
            pc = (size_t)target;
            break;
        default:
            vm_halt(env, pc);
            return env->r[0];
        }
    }

    vm_halt(env, pc);
    return env->r[0];
}

int32_t vm_run(const uint16_t *code, size_t code_len,
               uint8_t *mem, size_t mem_size,
               int32_t init_pc, int32_t init_lr, int32_t init_sp) {
    vm_env_t env;
    vm_env_init(&env, mem, mem_size, init_pc, init_lr, init_sp);
    return vm_run_env(&env, code, code_len, 0);
}

typedef struct {
    char *name;
    size_t addr;
} asm_label_t;

static int ascii_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static void asm_error(size_t line, const char *format, ...) {
    if (line) fprintf(stderr, "Assembler error line %zu: ", line);
    else fputs("Assembler error: ", stderr);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

static int parse_reg(const char *token) {
    if (!token) return -1;
    if ((token[0] == 'r' || token[0] == 'R') &&
        isdigit((unsigned char)token[1])) {
        char *end = NULL;
        errno = 0;
        long value = strtol(token + 1, &end, 10);
        if (!errno && end && *end == '\0' && value >= 0 && value <= 15)
            return (int)value;
    }
    if (ascii_strcasecmp(token, "lr") == 0) return 14;
    if (ascii_strcasecmp(token, "sp") == 0) return 15;
    return -1;
}

static char *trim(char *text) {
    while (*text && isspace((unsigned char)*text)) text++;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) *--end = '\0';
    return text;
}

static void strip_comment(char *line) {
    for (char *p = line; *p; p++) {
        if (*p == ';' || *p == '#' || (*p == '/' && p[1] == '/')) {
            *p = '\0';
            return;
        }
    }
}

static const asm_label_t *find_label(const asm_label_t *labels, size_t count,
                                     const char *name) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(labels[i].name, name) == 0) return &labels[i];
    }
    return NULL;
}

static bool valid_label_name(const char *name) {
    if (!name || !(isalpha((unsigned char)name[0]) || name[0] == '_' ||
                   name[0] == '.' || name[0] == '$'))
        return false;
    for (const char *p = name + 1; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '.' ||
              *p == '$'))
            return false;
    }
    return true;
}

static bool split_label(char *line, char **instruction, char **label) {
    char *colon = strchr(line, ':');
    *label = NULL;
    *instruction = trim(line);
    if (!colon) return true;
    *colon = '\0';
    *label = trim(line);
    *instruction = trim(colon + 1);
    return valid_label_name(*label);
}

static bool read_source(FILE *file, char **source, size_t *source_len) {
    size_t capacity = 4096;
    size_t length = 0;
    char *buffer = malloc(capacity);
    if (!buffer) return false;
    for (;;) {
        if (length == capacity) {
            if (capacity > SIZE_MAX / 2) {
                free(buffer);
                return false;
            }
            size_t new_capacity = capacity * 2;
            char *grown = realloc(buffer, new_capacity);
            if (!grown) {
                free(buffer);
                return false;
            }
            buffer = grown;
            capacity = new_capacity;
        }
        size_t count = fread(buffer + length, 1, capacity - length, file);
        length += count;
        if (count == 0) {
            if (ferror(file)) {
                free(buffer);
                return false;
            }
            break;
        }
    }
    if (length == SIZE_MAX) {
        free(buffer);
        return false;
    }
    char *terminated = realloc(buffer, length + 1);
    if (!terminated) {
        free(buffer);
        return false;
    }
    terminated[length] = '\0';
    *source = terminated;
    *source_len = length;
    return true;
}

static char *next_line(const char *source, size_t source_len, size_t *offset,
                       size_t *line_number) {
    if (*offset >= source_len) return NULL;
    size_t start = *offset;
    size_t end = start;
    while (end < source_len && source[end] != '\n') end++;
    size_t length = end - start;
    if (end < source_len) end++;
    *offset = end;
    (*line_number)++;
    char *line = malloc(length + 1);
    if (!line) return NULL;
    memcpy(line, source + start, length);
    line[length] = '\0';
    return line;
}

static bool add_label(asm_label_t **labels, size_t *count, size_t *capacity,
                      const char *name, size_t addr, size_t line) {
    if (find_label(*labels, *count, name)) {
        asm_error(line, "duplicate label '%s'", name);
        return false;
    }
    if (*count == *capacity) {
        size_t new_capacity = *capacity ? *capacity * 2 : 32;
        if (new_capacity < *capacity ||
            new_capacity > SIZE_MAX / sizeof(**labels)) {
            asm_error(line, "too many labels");
            return false;
        }
        asm_label_t *grown = realloc(*labels, new_capacity * sizeof(**labels));
        if (!grown) {
            asm_error(line, "out of memory while collecting labels");
            return false;
        }
        *labels = grown;
        *capacity = new_capacity;
    }
    char *copy = malloc(strlen(name) + 1);
    if (!copy) {
        asm_error(line, "out of memory while collecting labels");
        return false;
    }
    strcpy(copy, name);
    (*labels)[*count].name = copy;
    (*labels)[*count].addr = addr;
    (*count)++;
    return true;
}

static bool parse_value(const char *token, const asm_label_t *labels,
                        size_t label_count, int64_t *value, size_t line) {
    const asm_label_t *label = find_label(labels, label_count, token);
    if (label) {
        *value = (int64_t)label->addr;
        return true;
    }
    errno = 0;
    char *end = NULL;
    long long parsed = strtoll(token, &end, 0);
    if (errno == ERANGE) {
        asm_error(line, "integer out of range '%s'", token);
        return false;
    }
    if (end && end != token && *end == '\0') {
        *value = (int64_t)parsed;
        return true;
    }
    if (isalpha((unsigned char)token[0]) || token[0] == '_' ||
        token[0] == '.' || token[0] == '$') {
        asm_error(line, "unknown label '%s'", token);
    } else {
        asm_error(line, "invalid integer '%s'", token);
    }
    return false;
}

static bool require_reg(const char *token, int *reg, size_t line) {
    *reg = parse_reg(token);
    if (*reg >= 0) return true;
    asm_error(line, "unknown register '%s'", token);
    return false;
}

static bool require_count(const char *op, int actual, int expected, size_t line) {
    if (actual == expected) return true;
    asm_error(line, "instruction '%s' expects %d operand%s, got %d", op,
              expected - 1, expected == 2 ? "" : "s", actual - 1);
    return false;
}

static bool encode_instruction(char **tokens, int token_count,
                               const asm_label_t *labels, size_t label_count,
                               size_t pc, size_t line, uint16_t *instruction) {
    const char *op = tokens[0];
    int a, b, c;
    int64_t value;
    int32_t offset;

#define MATCH(name) (ascii_strcasecmp(op, name) == 0)
#define COUNT(n) do { if (!require_count(op, token_count, (n) + 1, line)) return false; } while (0)
#define REG(index, result) do { if (!require_reg(tokens[index], &(result), line)) return false; } while (0)

    if (MATCH("jr")) {
        COUNT(1); REG(1, a);
        *instruction = enc_rrr(OP_JR, (uint8_t)a, 0, 0);
    } else if (MATCH("movi") || MATCH("shl") || MATCH("shr")) {
        COUNT(2); REG(1, a);
        if (!parse_value(tokens[2], labels, label_count, &value, line)) return false;
        if (MATCH("movi")) {
            if (value < INT8_MIN || value > UINT8_MAX) {
                asm_error(line, "movi immediate out of range (%lld; expected -128..255)",
                          (long long)value);
                return false;
            }
            *instruction = enc_ri8(OP_MOVI, (uint8_t)a, (uint8_t)value);
        } else {
            if (value < 0 || value > 31) {
                asm_error(line, "%s shift out of range (%lld; expected 0..31)",
                          op, (long long)value);
                return false;
            }
            *instruction = enc_ri8(MATCH("shl") ? OP_SHL : OP_SHR,
                                   (uint8_t)a, (uint8_t)value);
        }
    } else if (MATCH("mov")) {
        COUNT(2); REG(1, a); REG(2, b);
        *instruction = enc_rrr(OP_MOV, (uint8_t)a, (uint8_t)b, 0);
    } else if (MATCH("add") || MATCH("sub") || MATCH("and") ||
               MATCH("or") || MATCH("xor") || MATCH("cmplt")) {
        COUNT(3); REG(1, a); REG(2, b); REG(3, c);
        uint8_t opcode = MATCH("add") ? OP_ADD :
                         MATCH("sub") ? OP_SUB :
                         MATCH("and") ? OP_AND :
                         MATCH("or") ? OP_OR :
                         MATCH("xor") ? OP_XOR : OP_CMPLT;
        *instruction = enc_rrr(opcode, (uint8_t)a, (uint8_t)b, (uint8_t)c);
    } else if (MATCH("jmp") || MATCH("call")) {
        COUNT(1);
        if (!parse_value(tokens[1], labels, label_count, &value, line)) return false;
        if (value < INT32_MIN || value > INT32_MAX) {
            asm_error(line, "%s target out of range", op);
            return false;
        }
        int64_t branch_offset = value - ((int64_t)pc + 1);
        if (branch_offset < -2048 || branch_offset > 2047) {
            asm_error(line, "%s target out of range (offset %lld; expected -2048..2047)",
                      op, (long long)branch_offset);
            return false;
        }
        offset = (int32_t)branch_offset;
        *instruction = enc_i12(MATCH("jmp") ? OP_JMP : OP_CALL,
                               (int16_t)offset);
    } else if (MATCH("jz")) {
        COUNT(2); REG(1, a);
        if (!parse_value(tokens[2], labels, label_count, &value, line)) return false;
        if (value < INT32_MIN || value > INT32_MAX) {
            asm_error(line, "jz target out of range");
            return false;
        }
        int64_t branch_offset = value - ((int64_t)pc + 1);
        if (branch_offset < -128 || branch_offset > 127) {
            asm_error(line, "jz target out of range (offset %lld; expected -128..127)",
                      (long long)branch_offset);
            return false;
        }
        *instruction = enc_ri8(OP_JZ, (uint8_t)a, (uint8_t)(int8_t)branch_offset);
    } else if (MATCH("ld32") || MATCH("st32")) {
        COUNT(3); REG(1, a); REG(2, b);
        if (!parse_value(tokens[3], labels, label_count, &value, line)) return false;
        if (value < 0 || value > 15) {
            asm_error(line, "%s word offset out of range (%lld; expected 0..15)",
                      op, (long long)value);
            return false;
        }
        *instruction = enc_rrr(MATCH("ld32") ? OP_LD32 : OP_ST32,
                               (uint8_t)a, (uint8_t)b, (uint8_t)value);
    } else {
        asm_error(line, "unknown instruction '%s'", op);
        return false;
    }
#undef REG
#undef COUNT
#undef MATCH
    return true;
}

int vm_assemble_file(const char *path, uint16_t *out_code, size_t max_code,
                    size_t *out_len) {
    if (!path || !out_code || !out_len) {
        asm_error(0, "path, output buffer, and output length are required");
        return -1;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        asm_error(0, "cannot open '%s': %s", path, strerror(errno));
        return -1;
    }
    char *source = NULL;
    size_t source_len = 0;
    if (!read_source(file, &source, &source_len)) {
        asm_error(0, "failed to read '%s'", path);
        fclose(file);
        return -1;
    }
    if (fclose(file) != 0) {
        asm_error(0, "failed to close '%s'", path);
        free(source);
        return -1;
    }
    if (memchr(source, '\0', source_len)) {
        asm_error(0, "source contains a NUL byte");
        free(source);
        return -1;
    }

    asm_label_t *labels = NULL;
    size_t label_count = 0, label_capacity = 0;
    size_t instruction_count = 0, offset = 0, line_number = 0;
    bool ok = true;
    while (offset < source_len) {
        char *line = next_line(source, source_len, &offset, &line_number);
        if (!line) {
            asm_error(line_number + 1, "out of memory while reading source");
            ok = false;
            break;
        }
        strip_comment(line);
        char *instruction = NULL, *label = NULL;
        if (!split_label(line, &instruction, &label)) {
            asm_error(line_number, "invalid label '%s'", label ? label : "");
            ok = false;
        } else if (label && !add_label(&labels, &label_count, &label_capacity,
                                       label, instruction_count, line_number)) {
            ok = false;
        }
        if (ok && *instruction) {
            if (instruction_count >= max_code) {
                asm_error(line_number, "program exceeds max_code (%zu)", max_code);
                ok = false;
            } else if (instruction_count >= INT32_MAX) {
                asm_error(line_number, "program exceeds VM addressable code range");
                ok = false;
            } else {
                instruction_count++;
            }
        }
        free(line);
        if (!ok) break;
    }

    if (ok && max_code > SIZE_MAX / sizeof(uint16_t)) {
        asm_error(0, "max_code is too large");
        ok = false;
    }
    uint16_t *encoded = NULL;
    if (ok && max_code) {
        encoded = malloc(max_code * sizeof(*encoded));
        if (!encoded) {
            asm_error(0, "out of memory for output buffer");
            ok = false;
        }
    }

    offset = 0;
    line_number = 0;
    size_t pc = 0;
    while (ok && offset < source_len) {
        char *line = next_line(source, source_len, &offset, &line_number);
        if (!line) {
            asm_error(line_number + 1, "out of memory while reading source");
            ok = false;
            break;
        }
        strip_comment(line);
        char *instruction = NULL, *label = NULL;
        if (!split_label(line, &instruction, &label)) {
            asm_error(line_number, "invalid label '%s'", label ? label : "");
            ok = false;
            free(line);
            break;
        }
        if (!*instruction) {
            free(line);
            continue;
        }
        char *tokens[6] = {0};
        int token_count = 0;
        char *token = strtok(instruction, " ,\t\r\n");
        while (token && token_count < 6) {
            tokens[token_count++] = token;
            token = strtok(NULL, " ,\t\r\n");
        }
        if (token_count > 5) {
            asm_error(line_number, "too many operands");
            ok = false;
        } else if (token_count == 0) {
            asm_error(line_number, "missing instruction");
            ok = false;
        } else {
            ok = encode_instruction(tokens, token_count, labels, label_count,
                                    pc, line_number, &encoded[pc]);
        }
        if (ok) pc++;
        free(line);
    }

    if (ok && pc != instruction_count) {
        asm_error(0, "internal assembler pass mismatch");
        ok = false;
    }
    if (ok) {
        if (instruction_count)
            memcpy(out_code, encoded, instruction_count * sizeof(*out_code));
        *out_len = instruction_count;
    }

    for (size_t i = 0; i < label_count; i++) free(labels[i].name);
    free(labels);
    free(encoded);
    free(source);
    return ok ? 0 : -1;
}
