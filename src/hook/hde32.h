/*
 * hde32 - Hacker Disassembler Engine 32-bit
 * Minimal x86-32 instruction length decoder for inline hook trampolines.
 *
 * Based on the public-domain hde32 by Vyacheslav Patkov.
 * Reimplemented as a single self-contained header for this project.
 *
 * Usage:
 *   unsigned int len = hde32_len(code_ptr);
 *   // len == 0 means unknown instruction
 */
#pragma once
#include <stdint.h>

/*
 * Opcode map flags (internal).
 * Each byte in the table encodes how to decode the rest of the instruction.
 */
#define C_NONE    0x00
#define C_MODRM   0x01   /* has ModR/M byte */
#define C_IMM8    0x02   /* has 1-byte immediate */
#define C_IMM16   0x04   /* has 2-byte immediate */
#define C_IMM32   0x08   /* has 4-byte immediate (or 2 with 0x66 prefix) */
#define C_REL8    0x10   /* has 1-byte relative offset */
#define C_REL32   0x20   /* has 4-byte relative offset */
#define C_GROUP   0x40   /* opcode extension in reg field of ModR/M */
#define C_ERROR   0x80   /* unknown/unsupported */

/* One-byte opcode table (256 entries) */
static const uint8_t hde32_table1[256] = {
/*       x0          x1          x2          x3          x4          x5          x6          x7  */
/*       x8          x9          xA          xB          xC          xD          xE          xF  */
/* 0x */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_IMM8,     C_IMM32,    C_NONE,     C_NONE,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_IMM8,     C_IMM32,    C_NONE,     C_NONE, /* 0F = escape */
/* 1x */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_IMM8,     C_IMM32,    C_NONE,     C_NONE,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_IMM8,     C_IMM32,    C_NONE,     C_NONE,
/* 2x */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_IMM8,     C_IMM32,    C_NONE,     C_NONE,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_IMM8,     C_IMM32,    C_NONE,     C_NONE,
/* 3x */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_IMM8,     C_IMM32,    C_NONE,     C_NONE,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_IMM8,     C_IMM32,    C_NONE,     C_NONE,
/* 4x */ C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,
         C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,
/* 5x */ C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,
         C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,
/* 6x */ C_NONE,     C_NONE,     C_MODRM,    C_MODRM|C_IMM8, C_NONE, C_NONE,    C_NONE,     C_NONE,
         C_IMM32,    C_MODRM|C_IMM32, C_IMM8, C_MODRM|C_IMM8, C_NONE, C_NONE,  C_NONE,     C_NONE,
/* 7x */ C_REL8,     C_REL8,     C_REL8,     C_REL8,     C_REL8,     C_REL8,     C_REL8,     C_REL8,
         C_REL8,     C_REL8,     C_REL8,     C_REL8,     C_REL8,     C_REL8,     C_REL8,     C_REL8,
/* 8x */ C_MODRM|C_IMM8, C_MODRM|C_IMM32, C_MODRM|C_IMM8, C_MODRM|C_IMM8,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
/* 9x */ C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,
         C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,
/* Ax */ C_IMM32,    C_IMM32,    C_IMM32,    C_IMM32,    C_NONE,     C_NONE,     C_NONE,     C_NONE,
         C_IMM8,     C_IMM32,    C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,
/* Bx */ C_IMM8,     C_IMM8,     C_IMM8,     C_IMM8,     C_IMM8,     C_IMM8,     C_IMM8,     C_IMM8,
         C_IMM32,    C_IMM32,    C_IMM32,    C_IMM32,    C_IMM32,    C_IMM32,    C_IMM32,    C_IMM32,
/* Cx */ C_MODRM|C_IMM8, C_MODRM|C_IMM8, C_IMM16,  C_NONE,     C_MODRM,    C_MODRM,    C_MODRM|C_IMM8, C_MODRM|C_IMM32,
         C_IMM16|C_IMM8, C_NONE, C_IMM16, C_NONE,     C_NONE,     C_IMM8,     C_NONE,     C_NONE,
/* Dx */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_IMM8,     C_IMM8,     C_NONE,     C_NONE,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
/* Ex */ C_REL8,     C_REL8,     C_REL8,     C_REL8,     C_IMM8,     C_IMM8,     C_IMM8,     C_IMM8,
         C_REL32,    C_REL32,    C_IMM32|C_IMM16, C_REL8, C_NONE,   C_NONE,     C_NONE,     C_NONE,
/* Fx */ C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_MODRM|C_GROUP, C_MODRM|C_GROUP,
         C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_MODRM|C_GROUP, C_MODRM|C_GROUP,
};

/* Two-byte opcode table (0F xx, 256 entries) */
static const uint8_t hde32_table2[256] = {
/*       x0          x1          x2          x3          x4          x5          x6          x7  */
/*       x8          x9          xA          xB          xC          xD          xE          xF  */
/* 0x */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_ERROR,    C_NONE,     C_NONE,     C_NONE,
         C_NONE,     C_NONE,     C_ERROR,    C_NONE,     C_ERROR,    C_MODRM,    C_NONE,     C_ERROR,
/* 1x */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
/* 2x */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_ERROR,    C_ERROR,    C_ERROR,    C_ERROR,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
/* 3x */ C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_ERROR,    C_NONE,
         C_MODRM,    C_ERROR,    C_MODRM,    C_ERROR,    C_ERROR,    C_ERROR,    C_ERROR,    C_ERROR,
/* 4x */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
/* 5x */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
/* 6x */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
/* 7x */ C_MODRM|C_IMM8, C_MODRM|C_IMM8, C_MODRM|C_IMM8, C_MODRM|C_IMM8,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
         C_MODRM,    C_MODRM,    C_ERROR,    C_ERROR,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
/* 8x */ C_REL32,    C_REL32,    C_REL32,    C_REL32,    C_REL32,    C_REL32,    C_REL32,    C_REL32,
         C_REL32,    C_REL32,    C_REL32,    C_REL32,    C_REL32,    C_REL32,    C_REL32,    C_REL32,
/* 9x */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
/* Ax */ C_NONE,     C_NONE,     C_NONE,     C_MODRM,    C_MODRM|C_IMM8, C_MODRM, C_ERROR,  C_ERROR,
         C_NONE,     C_NONE,     C_NONE,     C_MODRM,    C_MODRM|C_IMM8, C_MODRM, C_MODRM,  C_MODRM,
/* Bx */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
         C_MODRM,    C_ERROR,    C_MODRM|C_IMM8, C_MODRM, C_MODRM,  C_MODRM,    C_MODRM,    C_MODRM,
/* Cx */ C_MODRM,    C_MODRM,    C_MODRM|C_IMM8, C_MODRM, C_MODRM|C_IMM8, C_MODRM|C_IMM8, C_MODRM|C_IMM8, C_MODRM|C_GROUP,
         C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,     C_NONE,
/* Dx */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
/* Ex */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
/* Fx */ C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,
         C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_MODRM,    C_ERROR,
};

/*
 * Decode ModR/M + SIB + displacement.
 * Returns number of bytes consumed (1 for ModR/M, optionally +1 SIB, +disp).
 */
inline unsigned int hde32_modrm_len(const uint8_t* p)
{
    uint8_t modrm = p[0];
    uint8_t mod = (modrm >> 6) & 3;
    uint8_t rm  = modrm & 7;
    unsigned int len = 1; /* ModR/M byte itself */

    if (mod == 3)
        return len; /* register-register, no displacement */

    if (mod == 0 && rm == 5)
        return len + 4; /* disp32 only */

    if (rm == 4) {
        len += 1; /* SIB byte */
        uint8_t sib_base = p[1] & 7;
        if (mod == 0 && sib_base == 5)
            return len + 4; /* SIB + disp32 */
    }

    if (mod == 1) len += 1; /* disp8 */
    if (mod == 2) len += 4; /* disp32 */

    return len;
}

/*
 * Main entry: returns the length of the x86-32 instruction at `code`.
 * Returns 0 on error (unknown/unsupported instruction).
 */
inline unsigned int hde32_len(const void* code)
{
    const uint8_t* p = (const uint8_t*)code;
    unsigned int len = 0;
    int has_operand_prefix = 0;

    /* --- Skip legacy prefixes --- */
    for (;;) {
        switch (*p) {
        case 0xF0: /* LOCK */
        case 0xF2: /* REPNE */
        case 0xF3: /* REP */
        case 0x26: case 0x2E: case 0x36: case 0x3E: /* segment overrides */
        case 0x64: case 0x65: /* FS / GS */
            p++; len++; continue;
        case 0x66: /* operand-size prefix */
            has_operand_prefix = 1;
            p++; len++; continue;
        case 0x67: /* address-size prefix */
            p++; len++; continue;
        }
        break;
    }

    /* --- Two-byte escape (0F xx) --- */
    if (*p == 0x0F) {
        p++; len++; /* skip 0F */
        uint8_t op2 = *p;
        p++; len++; /* skip second opcode byte */

        /* Three-byte escape: 0F 38 xx and 0F 3A xx */
        if (op2 == 0x38) {
            /* 0F 38 xx: all have ModR/M, no immediate */
            p++; len++; /* third opcode byte */
            len += hde32_modrm_len(p);
            return len;
        }
        if (op2 == 0x3A) {
            /* 0F 3A xx: all have ModR/M + imm8 */
            p++; len++; /* third opcode byte */
            len += hde32_modrm_len(p);
            len += 1; /* imm8 */
            return len;
        }

        uint8_t flags = hde32_table2[op2];
        if (flags & C_ERROR)
            return 0;

        if (flags & C_MODRM)
            len += hde32_modrm_len(p);

        if (flags & C_IMM8)  len += 1;
        if (flags & C_IMM16) len += 2;
        if (flags & C_IMM32) len += (has_operand_prefix ? 2 : 4);
        if (flags & C_REL8)  len += 1;
        if (flags & C_REL32) len += 4;

        /* Special: 0F BA /4..7 (BT/BTS/BTR/BTC r/m, imm8) has imm8 */
        if (op2 == 0xBA) {
            /* The IMM8 flag is already set via C_MODRM|C_IMM8 in the table */
        }

        return len;
    }

    /* --- One-byte opcode --- */
    uint8_t op1 = *p;
    p++; len++; /* skip opcode */

    uint8_t flags = hde32_table1[op1];

    if (flags & C_ERROR)
        return 0;

    if (flags & C_MODRM) {
        unsigned int mrm = hde32_modrm_len(p);
        len += mrm;
        /* For group opcodes, check reg field for special immediate handling */
        if (flags & C_GROUP) {
            uint8_t reg = (p[0] >> 3) & 7;
            /* F6/F7: reg==0/1 means TEST r/m, imm — extra immediate */
            if (op1 == 0xF6) {
                if (reg == 0 || reg == 1) len += 1; /* imm8 */
            } else if (op1 == 0xF7) {
                if (reg == 0 || reg == 1) len += (has_operand_prefix ? 2 : 4); /* imm16/32 */
            }
        }
    }

    if (flags & C_IMM8)  len += 1;
    if (flags & C_IMM16) len += 2;
    if (flags & C_IMM32) len += (has_operand_prefix ? 2 : 4);
    if (flags & C_REL8)  len += 1;
    if (flags & C_REL32) len += 4;

    return len;
}

#undef C_NONE
#undef C_MODRM
#undef C_IMM8
#undef C_IMM16
#undef C_IMM32
#undef C_REL8
#undef C_REL32
#undef C_GROUP
#undef C_ERROR
