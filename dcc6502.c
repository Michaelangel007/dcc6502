/**********************************************************************************
 * dcc6502.c -> Main module of:                                                   *
 * Disassembler and Cycle Counter for the 6502 microprocessor                     *
 *                                                                                *
 * This code is offered under the MIT License (MIT)                               *
 *                                                                                *
 * Copyright (c) 1998-2014 Tennessee Carmel-Veilleux <veilleux@tentech.ca>        *
 * Copyright (c) 2017      Michael Pohoreski <michaelangel007@sharedcraft.com>    *
 *                                                                                *
 * Permission is hereby granted, free of charge, to any person obtaining a copy   *
 * of this software and associated documentation files (the "Software"), to deal  *
 * in the Software without restriction, including without limitation the rights   *
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell      *
 * copies of the Software, and to permit persons to whom the Software is          *
 * furnished to do so, subject to the following conditions:                       *
 *                                                                                *
 * The above copyright notice and this permission notice shall be included in all *
 * copies or substantial portions of the Software.                                *
 *                                                                                *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR     *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,       *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE    *
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER         *
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,  *
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE  *
 * SOFTWARE.                                                                      *
 **********************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>

#define AUTHOR "Michael Pohoreski <michaelangel007@sharedcraft.com>"
#define GIT_LOCATION "https://github.com/Michaelangel007/dcc6502"
#define FORK_LOCATION "https://github.com/tcarmelveilleux/dcc6502"
#define VERSION_INFO "v2.1"
#define NUMBER_OPCODES 256

/* Exceptions for cycle counting */
#define CYCLE_PAGE      (1 << 0) // Cross page boundary, +1 cycle
#define CYCLE_BRANCH    (1 << 1) // Branch taken, +1 cycle
#define _65C02          (1 << 2) // 65C02 only instruction
#define BAD             (1 << 3) // Illegal 6502 instruction
#define CYCLE_MASK      (CYCLE_PAGE | CYCLE_BRANCH)

/* The 6502's 13 addressing modes */
typedef enum {
    IMMED = 0, /* Immediate */
    ABSOL,     /* Absolute */
    ZEROP,     /* Zero Page */
    IMPLI,     /* Implied */
    INDIA,     /* Indirect Absolute */
    ABSIX,     /* Absolute indexed with X */
    ABSIY,     /* Absolute indexed with Y */
    ZEPIX,     /* Zero page indexed with X */
    ZEPIY,     /* Zero page indexed with Y */
    INDIN,     /* Indexed indirect (with X) */
    ININD,     /* Indirect indexed (with Y) */
    RELAT,     /* Relative */
    ACCUM      /* Accumulator */
} addressing_mode_e;

/** Some compilers don't have EOK in errno.h */
#ifndef EOK
#define EOK 0
#endif

typedef struct opcode_s {
    uint8_t           number;            /* Number of the opcode */
    const char       *mnemonic;          /* Index in the name table */
    addressing_mode_e addressing;        /* Addressing mode */
    unsigned int      cycles;            /* Number of cycles */
    unsigned int      cycles_exceptions; /* Mask of cycle-counting exceptions */
} opcode_t;

typedef struct options_s {
    char         *filename;       /* Input filename */
    int           nes_mode;       /* 1 if NES commenting and warnings are enabled */
    int           cycle_counting; /* 1 if we want cycle counting */
    int           hex_output;     /* 1 if hex dump output is desired at beginning of line */
    int           apple2_output;  /* 1 if Apple 2/Atari disassembly output stype */
    unsigned long max_num_bytes;
    uint16_t      org;            /* Origin of addresses */
} options_t;

/* Opcode table */
static opcode_t g_opcode_table[NUMBER_OPCODES] = {
    {0x00, "BRK", IMPLI, 7, 0                        }, /* 00 BRK */
    {0x01, "ORA", INDIN, 6, 0                        }, /* 01 ORA */
    {0x02, "???", 0    , 0, BAD                      }, /* 02     illegal 6502 */
    {0x03, "???", 0    , 0, BAD                      }, /* 03     illegal 6502 */
    {0x04, "???", 0    , 0, BAD                      }, /* 04     illegal 6502 */
    {0x05, "ORA", ZEROP, 3, 0                        }, /* 05 ORA */
    {0x06, "ASL", ZEROP, 5, 0                        }, /* 06 ASL */
    {0x07, "???", 0    , 0, BAD                      }, /* 07     illegal 6502 */
    {0x08, "PHP", IMPLI, 3, 0                        }, /* 08 PHP */
    {0x09, "ORA", IMMED, 2, 0                        }, /* 09 ORA */
    {0x0A, "ASL", ACCUM, 2, 0                        }, /* 0A ASL */
    {0x0B, "???", 0    , 0, BAD                      }, /* 0B     illegal 6502 */
    {0x0C, "???", 0    , 0, BAD                      }, /* 0C     illegal 6502 */
    {0x0D, "ORA", ABSOL, 4, 0                        }, /* 0D ORA */
    {0x0E, "ASL", ABSOL, 6, 0                        }, /* 0E ASL */
    {0x0F, "???", 0    , 0, BAD                      }, /* 0F     illegal 6502 */
    {0x10, "BPL", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* 10 BPL */
    {0x11, "ORA", ININD, 5, CYCLE_PAGE               }, /* 11 ORA */
    {0x12, "???", 0    , 0, BAD                      }, /* 12     illegal 6502 */
    {0x13, "???", 0    , 0, BAD                      }, /* 13     illegal 6502 */
    {0x14, "???", 0    , 0, BAD                      }, /* 14     illegal 6502 */
    {0x15, "ORA", ZEPIX, 4, 0                        }, /* 15 ORA */
    {0x16, "ASL", ZEPIX, 6, 0                        }, /* 16 ASL */
    {0x17, "???", 0    , 0, BAD                      }, /* 17     illegal 6502 */
    {0x18, "CLC", IMPLI, 2, 0                        }, /* 18 CLC */
    {0x19, "ORA", ABSIY, 4, CYCLE_PAGE               }, /* 19 ORA */
    {0x1A, "???", 0    , 0, BAD                      }, /* 1A     illegal 6502 */
    {0x1B, "???", 0    , 0, BAD                      }, /* 1B     illegal 6502 */
    {0x1C, "???", 0    , 0, BAD                      }, /* 1C     illegal 6502 */
    {0x1D, "ORA", ABSIX, 4, CYCLE_PAGE               }, /* 1D ORA */
    {0x1E, "ASL", ABSIX, 7, 0                        }, /* 1E ASL */
    {0x1F, "???", 0    , 0, BAD                      }, /* 1F     illegal 6502 */
    {0x20, "JSR", ABSOL, 6, 0                        }, /* 20 JSR */
    {0x21, "AND", INDIN, 6, 0                        }, /* 21 AND */
    {0x22, "???", 0    , 0, BAD                      }, /* 22     illegal 6502 */
    {0x23, "???", 0    , 0, BAD                      }, /* 23     illegal 6502 */
    {0x24, "BIT", ZEROP, 3, 0                        }, /* 24 BIT */
    {0x25, "AND", ZEROP, 3, 0                        }, /* 25 AND */
    {0x26, "ROL", ZEROP, 5, 0                        }, /* 26 ROL */
    {0x27, "???", 0    , 0, BAD                      }, /* 27     illegal 6502 */
    {0x28, "PLP", IMPLI, 4, 0                        }, /* 28 PLP */
    {0x29, "AND", IMMED, 2, 0                        }, /* 29 AND */
    {0x2A, "ROL", ACCUM, 2, 0                        }, /* 2A ROL */
    {0x2B, "???", 0    , 0, BAD                      }, /* 2B     illegal 6502 */
    {0x2C, "BIT", ABSOL, 4, 0                        }, /* 2C BIT */
    {0x2D, "AND", ABSOL, 4, 0                        }, /* 2D AND */
    {0x2E, "ROL", ABSOL, 6, 0                        }, /* 2E ROL */
    {0x2F, "???", 0    , 0, BAD                      }, /* 2F     illegal 6502 */
    {0x30, "BMI", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* 30 BMI */
    {0x31, "AND", ININD, 5, CYCLE_PAGE               }, /* 31 AND */
    {0x32, "???", 0    , 0, BAD                      }, /* 32     illegal 6502 */
    {0x33, "???", 0    , 0, BAD                      }, /* 33     illegal 6502 */
    {0x34, "???", 0    , 0, BAD                      }, /* 34     illegal 6502 */
    {0x35, "AND", ZEPIX, 4, 0                        }, /* 35 AND */
    {0x36, "ROL", ZEPIX, 6, 0                        }, /* 36 ROL */
    {0x37, "???", 0    , 0, BAD                      }, /* 37     illegal 6502 */
    {0x38, "SEC", IMPLI, 2, 0                        }, /* 38 SEC */
    {0x39, "AND", ABSIY, 4, CYCLE_PAGE               }, /* 39 AND */
    {0x3A, "???", 0    , 0, BAD                      }, /* 3A     illegal 6502 */
    {0x3B, "???", 0    , 0, BAD                      }, /* 3B     illegal 6502 */
    {0x3C, "???", 0    , 0, BAD                      }, /* 3C     illegal 6502 */
    {0x3D, "AND", ABSIX, 4, CYCLE_PAGE               }, /* 3D AND */
    {0x3E, "ROL", ABSIX, 7, 0                        }, /* 3E ROL */
    {0x3F, "???", 0    , 0, BAD                      }, /* 3F     illegal 6502 */
    {0x40, "RTI", IMPLI, 6, 0                        }, /* 40 RTI */
    {0x41, "EOR", INDIN, 6, 1                        }, /* 41 EOR */
    {0x42, "???", 0    , 0, BAD                      }, /* 42     illegal 6502 */
    {0x43, "???", 0    , 0, BAD                      }, /* 43     illegal 6502 */
    {0x44, "???", 0    , 0, BAD                      }, /* 44     illegal 6502 */
    {0x45, "EOR", ZEROP, 3, 0                        }, /* 45 EOR */
    {0x46, "LSR", ZEROP, 5, 0                        }, /* 46 LSR */
    {0x47, "???", 0    , 0, BAD                      }, /* 47     illegal 6502 */
    {0x48, "PHA", IMPLI, 3, 0                        }, /* 48 PHA */
    {0x49, "EOR", IMMED, 2, 0                        }, /* 49 EOR */
    {0x4A, "LSR", ACCUM, 2, 0                        }, /* 4A LSR */
    {0x4B, "???", 0    , 0, BAD                      }, /* 4B     illegal 6502 */
    {0x4C, "JMP", ABSOL, 3, 0                        }, /* 4C JMP */
    {0x4D, "EOR", ABSOL, 4, 0                        }, /* 4D EOR */
    {0x4E, "LSR", ABSOL, 6, 0                        }, /* 4E LSR */
    {0x4F, "???", 0    , 0, BAD                      }, /* 4F     illegal 6502 */
    {0x50, "BVC", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* 50 BVC */
    {0x51, "EOR", ININD, 5, CYCLE_PAGE               }, /* 51 EOR */
    {0x52, "???", 0    , 0, BAD                      }, /* 52     illegal 6502 */
    {0x53, "???", 0    , 0, BAD                      }, /* 53     illegal 6502 */
    {0x54, "???", 0    , 0, BAD                      }, /* 54     illegal 6502 */
    {0x55, "EOR", ZEPIX, 4, 0                        }, /* 55 EOR */
    {0x56, "LSR", ZEPIX, 6, 0                        }, /* 56 LSR */
    {0x57, "???", 0    , 0, BAD                      }, /* 57     illegal 6502 */
    {0x58, "CLI", IMPLI, 2, 0                        }, /* 58 CLI */
    {0x59, "EOR", ABSIY, 4, CYCLE_PAGE               }, /* 59 EOR */
    {0x5A, "???", 0    , 0, BAD                      }, /* 5A     illegal 6502 */
    {0x5B, "???", 0    , 0, BAD                      }, /* 5B     illegal 6502 */
    {0x5C, "???", 0    , 0, BAD                      }, /* 5C     illegal 6502 */
    {0x5D, "EOR", ABSIX, 4, CYCLE_PAGE               }, /* 5D EOR */
    {0x5E, "LSR", ABSIX, 7, 0                        }, /* 5E LSR */
    {0x5F, "???", 0    , 0, BAD                      }, /* 5F     illegal 6502 */
    {0x60, "RTS", IMPLI, 6, 0                        }, /* 60 RTS */
    {0x61, "ADC", INDIN, 6, 0                        }, /* 61 ADC */
    {0x62, "???", 0    , 0, BAD                      }, /* 62     illegal 6502 */
    {0x63, "???", 0    , 0, BAD                      }, /* 63     illegal 6502 */
    {0x64, "???", 0    , 0, BAD                      }, /* 64     illegal 6502 */
    {0x65, "ADC", ZEROP, 3, 0                        }, /* 65 ADC */
    {0x66, "ROR", ZEROP, 5, 0                        }, /* 66 ROR */
    {0x67, "???", 0    , 0, BAD                      }, /* 67     illegal 6502 */
    {0x68, "PLA", IMPLI, 4, 0                        }, /* 68 PLA */
    {0x69, "ADC", IMMED, 2, 0                        }, /* 69 ADC */
    {0x6A, "ROR", ACCUM, 2, 0                        }, /* 6A ROR */
    {0x6B, "???", 0    , 0, BAD                      }, /* 6B     illegal 6502 */
    {0x6C, "JMP", INDIA, 5, 0                        }, /* 6C JMP */
    {0x6D, "ADC", ABSOL, 4, 0                        }, /* 6D ADC */
    {0x6E, "ROR", ABSOL, 6, 0                        }, /* 6E ROR */
    {0x6F, "???", 0    , 0, BAD                      }, /* 6F     illegal 6502 */
    {0x70, "BVS", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* 70 BVS */
    {0x71, "ADC", ININD, 5, CYCLE_PAGE               }, /* 71 ADC */
    {0x72, "???", 0    , 0, BAD                      }, /* 72     illegal 6502 */
    {0x73, "???", 0    , 0, BAD                      }, /* 73     illegal 6502 */
    {0x74, "???", 0    , 0, BAD                      }, /* 74     illegal 6502 */
    {0x75, "ADC", ZEPIX, 4, 0                        }, /* 75 ADC */
    {0x76, "ROR", ZEPIX, 6, 0                        }, /* 76 ROR */
    {0x77, "???", 0    , 0, BAD                      }, /* 77     illegal 6502 */
    {0x78, "SEI", IMPLI, 2, 0                        }, /* 78 SEI */
    {0x79, "ADC", ABSIY, 4, CYCLE_PAGE               }, /* 79 ADC */
    {0x7A, "???", 0    , 0, BAD                      }, /* 7A     illegal 6502 */
    {0x7B, "???", 0    , 0, BAD                      }, /* 7B     illegal 6502 */
    {0x7C, "???", 0    , 0, BAD                      }, /* 7C     illegal 6502 */
    {0x7D, "ADC", ABSIX, 4, CYCLE_PAGE               }, /* 7D ADC */
    {0x7E, "ROR", ABSIX, 7, 0                        }, /* 7E ROR */
    {0x7F, "???", 0    , 0, BAD                      }, /* 7F     illegal 6502 */
    {0x80, "???", 0    , 0, BAD                      }, /* 80     illegal 6502 */
    {0x81, "STA", INDIN, 6, 0                        }, /* 81 STA */
    {0x82, "???", 0    , 0, BAD                      }, /* 82     illegal 6502 */
    {0x83, "???", 0    , 0, BAD                      }, /* 83     illegal 6502 */
    {0x84, "STY", ZEROP, 3, 0                        }, /* 84 STY */
    {0x85, "STA", ZEROP, 3, 0                        }, /* 85 STA */
    {0x86, "STX", ZEROP, 3, 0                        }, /* 86 STX */
    {0x87, "???", 0    , 0, BAD                      }, /* 87     illegal 6502 */
    {0x88, "DEY", IMPLI, 2, 0                        }, /* 88 DEY */
    {0x89, "???", 0    , 0, BAD                      }, /* 89     illegal 6502 */
    {0x8A, "TXA", IMPLI, 2, 0                        }, /* 8A TXA */
    {0x8B, "???", 0    , 0, BAD                      }, /* 8B     illegal 6502 */
    {0x8C, "STY", ABSOL, 4, 0                        }, /* 9C STY */
    {0x8D, "STA", ABSOL, 4, 0                        }, /* 8D STA */
    {0x8E, "STX", ABSOL, 4, 0                        }, /* 8E STX */
    {0x8F, "???", 0    , 0, BAD                      }, /* 8F     illegal 6502 */
    {0x90, "BCC", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* 90 BCC */
    {0x91, "STA", ININD, 5, CYCLE_PAGE               }, /* 91 STA */
    {0x92, "???", 0    , 0, BAD                      }, /* 92     illegal 6502 */
    {0x93, "???", 0    , 0, BAD                      }, /* 93     illegal 6502 */
    {0x94, "STY", ZEPIX, 4, 0                        }, /* 94 STY */
    {0x95, "STA", ZEPIX, 4, 0                        }, /* 95 STA */
    {0x96, "STX", ZEPIY, 4, 0                        }, /* 96 STX */
    {0x97, "???", 0    , 0, BAD                      }, /* 97     illegal 6502 */
    {0x98, "TYA", IMPLI, 2, 0                        }, /* 98 TYA */
    {0x99, "STA", ABSIY, 4, CYCLE_PAGE               }, /* 99 STA */
    {0x9A, "TXS", IMPLI, 2, 0                        }, /* 9A TXS */
    {0x9B, "???", 0    , 0, BAD                      }, /* 9B     illegal 6502 */
    {0x9C, "???", 0    , 0, BAD                      }, /* 9C     illegal 6502 */
    {0x9D, "STA", ABSIX, 4, CYCLE_PAGE               }, /* 9D STA */
    {0x9E, "???", 0    , 0, BAD                      }, /* 9E     illegal 6502 */
    {0x9F, "???", 0    , 0, BAD                      }, /* 9F     illegal 6502 */
    {0xA0, "LDY", IMMED, 2, 0                        }, /* A0 LDY */
    {0xA1, "LDA", INDIN, 6, 0                        }, /* A1 LDA */
    {0xA2, "LDX", IMMED, 2, 0                        }, /* A2 LDX */
    {0xA3, "???", 0    , 0, BAD                      }, /* A3     illegal 6502 */
    {0xA4, "LDY", ZEROP, 3, 0                        }, /* A4 LDY */
    {0xA5, "LDA", ZEROP, 3, 0                        }, /* A5 LDA */
    {0xA6, "LDX", ZEROP, 3, 0                        }, /* A6 LDX */
    {0xA7, "???", 0    , 0, BAD                      }, /* A7     illegal 6502 */
    {0xA8, "TAY", IMPLI, 2, 0                        }, /* A8 TAY */
    {0xA9, "LDA", IMMED, 2, 0                        }, /* A9 LDA */
    {0xAA, "TAX", IMPLI, 2, 0                        }, /* AA TAX */
    {0xAB, "???", 0    , 0, BAD                      }, /* AB     illegal 6502 */
    {0xAC, "LDY", ABSOL, 4, 0                        }, /* AC LDY */
    {0xAD, "LDA", ABSOL, 4, 0                        }, /* AD LDA */
    {0xAE, "LDX", ABSOL, 4, 0                        }, /* AE LDX */
    {0xAF, "???", 0    , 0, BAD                      }, /* AF     illegal 6502 */
    {0xB0, "BCS", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* B0 BCS */
    {0xB1, "LDA", ININD, 5, CYCLE_PAGE               }, /* B1 LDA */
    {0xB2, "???", 0    , 0, BAD                      }, /* B2     illegal 6502 */
    {0xB3, "???", 0    , 0, BAD                      }, /* B3     illegal 6502 */
    {0xB4, "LDY", ZEPIX, 4, 0                        }, /* B4 LDY */
    {0xB5, "LDA", ZEPIX, 4, 0                        }, /* B5 LDA */
    {0xB6, "LDX", ZEPIY, 4, 0                        }, /* B6 LDX */
    {0xB7, "???", 0    , 0, BAD                      }, /* B7     illegal 6502 */
    {0xB8, "CLV", IMPLI, 2, 0                        }, /* B8 CLV */
    {0xB9, "LDA", ABSIY, 4, CYCLE_PAGE               }, /* B9 LDA */
    {0xBA, "TSX", IMPLI, 2, 0                        }, /* BA TSX */
    {0xBB, "???", 0    , 0, BAD                      }, /* BB     illegal 6502 */
    {0xBC, "LDY", ABSIX, 4, CYCLE_PAGE               }, /* BC LDY */
    {0xBD, "LDA", ABSIX, 4, CYCLE_PAGE               }, /* BD LDA */
    {0xBE, "LDX", ABSIY, 4, CYCLE_PAGE               }, /* BE LDX */
    {0xBF, "???", 0    , 0, BAD                      }, /* BF     illegal 6502 */
    {0xC0, "CPY", IMMED, 2, 0                        }, /* C0 CPY */
    {0xC1, "CMP", INDIN, 6, 0                        }, /* C1 CMP */
    {0xC2, "???", 0    , 0, BAD                      }, /* C2     illegal 6502 */
    {0xC3, "???", 0    , 0, BAD                      }, /* C3     illegal 6502 */
    {0xC4, "CPY", ZEROP, 3, 0                        }, /* C4 CPY */
    {0xC5, "CMP", ZEROP, 3, 0                        }, /* C5 CMP */
    {0xC6, "DEC", ZEROP, 5, 0                        }, /* C6 DEC */
    {0xC7, "???", 0    , 0, BAD                      }, /* C7     illegal 6502 */
    {0xC8, "INY", IMPLI, 2, 0                        }, /* C8 INY */
    {0xC9, "CMP", IMMED, 2, 0                        }, /* C9 CMP */
    {0xCA, "DEX", IMPLI, 2, 0                        }, /* CA DEX */
    {0xCB, "???", 0    , 0, BAD                      }, /* CB     illegal 6502 */
    {0xCC, "CPY", ABSOL, 4, 0                        }, /* CC CPY */
    {0xCD, "CMP", ABSOL, 4, 0                        }, /* CD CMP */
    {0xCE, "DEC", ABSOL, 6, 0                        }, /* CE DEC */
    {0xCF, "???", 0    , 0, BAD                      }, /* CF     illegal 6502 */
    {0xD0, "BNE", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* D0 BNE */
    {0xD1, "CMP", ININD, 5, CYCLE_PAGE               }, /* D1 CMP */
    {0xD2, "???", 0    , 0, BAD                      }, /* D2     illegal 6502 */
    {0xD3, "???", 0    , 0, BAD                      }, /* D3     illegal 6502 */
    {0x04, "???", 0    , 0, BAD                      }, /* D4     illegal 6502 */
    {0xD5, "CMP", ZEPIX, 4, 0                        }, /* D5 CMP */
    {0xD6, "DEC", ZEPIX, 6, 0                        }, /* D6 DEC */
    {0xD7, "???", 0    , 0, BAD                      }, /* D7     illegal 6502 */
    {0xD8, "CLD", IMPLI, 2, 0                        }, /* D8 CLD */
    {0xD9, "CMP", ABSIY, 4, CYCLE_PAGE               }, /* D9 CMP */
    {0xDA, "???", 0    , 0, BAD                      }, /* DA     illegal 6502 */
    {0xDB, "???", 0    , 0, BAD                      }, /* DB     illegal 6502 */
    {0xDC, "???", 0    , 0, BAD                      }, /* DC     illegal 6502 */
    {0xDD, "CMP", ABSIX, 4, CYCLE_PAGE               }, /* DD CMP */
    {0xDE, "DEC", ABSIX, 7, 0                        }, /* DE DEC */
    {0xDF, "???", 0    , 0, BAD                      }, /* DF     illegal 6502 */
    {0xE0, "CPX", IMMED, 2, 0                        }, /* E0 CPX */
    {0xE1, "SBC", INDIN, 6, 0                        }, /* E1 SBC */
    {0xE2, "???", 0    , 0, BAD                      }, /* E2     illegal 6502 */
    {0xE3, "???", 0    , 0, BAD                      }, /* E3     illegal 6502 */
    {0xE4, "CPX", ZEROP, 3, 0                        }, /* E4 CPX */
    {0xE5, "SBC", ZEROP, 3, 0                        }, /* E5 SBC */
    {0xE6, "INC", ZEROP, 5, 0                        }, /* E6 INC */
    {0xE7, "???", 0    , 0, BAD                      }, /* E7     illegal 6502 */
    {0xE8, "INX", IMPLI, 2, 0                        }, /* E8 INX */
    {0xE9, "SBC", IMMED, 2, 0                        }, /* E9 SBC */
    {0xEA, "NOP", IMPLI, 2, 0                        }, /* EA NOP */
    {0xEB, "???", 0    , 0, BAD                      }, /* EB     illegal 6502 */
    {0xEC, "CPX", ABSOL, 4, 0                        }, /* EC CPX */
    {0xED, "SBC", ABSOL, 4, 0                        }, /* ED SBC */
    {0xEE, "INC", ABSOL, 6, 0                        }, /* EE INC */
    {0xEF, "???", 0    , 0, BAD                      }, /* EF     illegal 6502 */
    {0xF0, "BEQ", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* F0 BEQ */
    {0xF1, "SBC", ININD, 5, CYCLE_PAGE               }, /* F1 SBC */
    {0xF2, "???", 0    , 0, BAD                      }, /* F2     illegal 6502 */
    {0xF3, "???", 0    , 0, BAD                      }, /* F3     illegal 6502 */
    {0xF4, "???", 0    , 0, BAD                      }, /* F4     illegal 6502 */
    {0xF5, "SBC", ZEPIX, 4, 0                        }, /* F5 SBC */
    {0xF6, "INC", ZEPIX, 6, 0                        }, /* F6 INC */
    {0xF7, "???", 0    , 0, BAD                      }, /* F7     illegal 6502 */
    {0xF8, "SED", IMPLI, 2, 0                        }, /* F8 SED */
    {0xF9, "SBC", ABSIY, 4, CYCLE_PAGE               }, /* F9 SBC */
    {0xFA, "???", 0    , 0, BAD                      }, /* FA     illegal 6502 */
    {0xFB, "???", 0    , 0, BAD                      }, /* FB     illegal 6502 */
    {0xFC, "???", 0    , 0, BAD                      }, /* FC     illegal 6502 */
    {0xFD, "SBC", ABSIX, 4, CYCLE_PAGE               }, /* FD SBC */
    {0xFE, "INC", ABSIX, 7, 0                        }, /* FE INC */
    {0xFF, "???", 0    , 0, BAD                      }  /* FF     illegal 6502 */
};

#define DUMP_FORMAT (options->hex_output ? "%-16s%-16s;" : "%-8s%-16s;")

/* This function emits a comment header with information about the file
   being disassembled */
static void emit_header(options_t *options, int fsize) {
    char mnemonic[256];
    sprintf( mnemonic, "ORG $%04X", options->org);

    /*                        */ fprintf(stdout, "; Source generated by DCC6502 version %s\n", VERSION_INFO);
    /*                        */ fprintf(stdout, "; For more info about DCC6502, see %s\n", GIT_LOCATION);
    /*                        */ fprintf(stdout, "; FILENAME: %s, File Size: $%04X (%d)\n", options->filename, fsize, fsize);
    if (options->hex_output)     fprintf(stdout, ";     -> Hex output enabled\n");
    if (options->cycle_counting) fprintf(stdout, ";     -> Cycle counting enabled\n");
    if (options->nes_mode)       fprintf(stdout, ";     -> NES mode enabled\n");
    if (options->apple2_output)  fprintf(stdout, ";     -> Apple II output enabled\n");
    /*                        */ fprintf(stdout, ";---------------------------------------------------------------------------\n");
    /*                        */ fprintf(stdout, DUMP_FORMAT, "", mnemonic);
    /*                        */ fprintf(stdout, "\n" );
}

/* This function appends cycle counting to the comment block. See following
 * for methods used:
 * "Nick Bensema's Guide to Cycle Counting on the Atari 2600"
 * http://www.alienbill.com/2600/cookbook/cycles/nickb.txt
 */
static char *append_cycle(char *input, uint8_t entry, uint16_t pc, uint16_t new_pc) {
    char tmpstr[256];
    int  cycles       = g_opcode_table[entry].cycles;
    int  exceptions   = g_opcode_table[entry].cycles_exceptions & CYCLE_MASK;
    int  crosses_page = ((pc & 0xff00u) != (new_pc & 0xff00u)) ? 1 : 0;

    // On some exceptional conditions, instruction will take an extra cycle, or even two
    if (exceptions != 0) {
        if ((exceptions & CYCLE_BRANCH) && (exceptions & CYCLE_PAGE)) {
            /* Branch case: check for page crossing, since it can be determined
             * statically from the relative offset and current PC.
             */
            if (crosses_page) {
                /* Crosses page, always at least 1 extra cycle, two times */
                sprintf(tmpstr, " Cycles: %d/%d", cycles + 1, cycles + 2);
            } else {
                /* Does not cross page, maybe one extra cycle if branch taken */
                sprintf(tmpstr, " Cycles: %d/%d", cycles, cycles + 1);
            }
        } else {
            /* One exception: two times, can't tell in advance whether page crossing occurs */
            sprintf(tmpstr, " Cycles: %d/%d", cycles, cycles + 1);
        }
    } else {
        /* No exceptions, no extra time */
        sprintf(tmpstr, " Cycles: %d", cycles);
    }

    strcat(input, tmpstr);
    return (input + strlen(input));
}

static void add_nes_str(char *instr, char *instr2) {
    strcat(instr, " [NES] ");
    strcat(instr, instr2);
}

/* This function put NES-specific info in the comment block */
static void append_nes(char *input, uint16_t arg) {
    switch(arg) {
        case 0x2000: add_nes_str(input, "PPU setup #1"); break;
        case 0x2001: add_nes_str(input, "PPU setup #2"); break;
        case 0x2002: add_nes_str(input, "PPU status"); break;
        case 0x2003: add_nes_str(input, "SPR-RAM address select"); break;
        case 0x2004: add_nes_str(input, "SPR-RAM data"); break;
        case 0x2005: add_nes_str(input, "PPU scroll"); break;
        case 0x2006: add_nes_str(input, "VRAM address select"); break;
        case 0x2007: add_nes_str(input, "VRAM data"); break;
        case 0x4000: add_nes_str(input, "Audio -> Square 1"); break;
        case 0x4001: add_nes_str(input, "Audio -> Square 1"); break;
        case 0x4002: add_nes_str(input, "Audio -> Square 1"); break;
        case 0x4003: add_nes_str(input, "Audio -> Square 1"); break;
        case 0x4004: add_nes_str(input, "Audio -> Square 2"); break;
        case 0x4005: add_nes_str(input, "Audio -> Square 2"); break;
        case 0x4006: add_nes_str(input, "Audio -> Square 2"); break;
        case 0x4007: add_nes_str(input, "Audio -> Square 2"); break;
        case 0x4008: add_nes_str(input, "Audio -> Triangle"); break;
        case 0x4009: add_nes_str(input, "Audio -> Triangle"); break;
        case 0x400a: add_nes_str(input, "Audio -> Triangle"); break;
        case 0x400b: add_nes_str(input, "Audio -> Triangle"); break;
        case 0x400c: add_nes_str(input, "Audio -> Noise control reg"); break;
        case 0x400e: add_nes_str(input, "Audio -> Noise Frequency reg #1"); break;
        case 0x400f: add_nes_str(input, "Audio -> Noise Frequency reg #2"); break;
        case 0x4010: add_nes_str(input, "Audio -> DPCM control"); break;
        case 0x4011: add_nes_str(input, "Audio -> DPCM D/A data"); break;
        case 0x4012: add_nes_str(input, "Audio -> DPCM address"); break;
        case 0x4013: add_nes_str(input, "Audio -> DPCM data length"); break;
        case 0x4014: add_nes_str(input, "Sprite DMA trigger"); break;
        case 0x4015: add_nes_str(input, "IRQ status / Sound enable"); break;
        case 0x4016: add_nes_str(input, "Joypad & I/O port for port #1"); break;
        case 0x4017: add_nes_str(input, "Joypad & I/O port for port #2"); break;
    }
}

/* Helper macros for disassemble() function */
#define HIGH_PART(val) (((val) >> 8) & 0xFFu)
#define LOW_PART(val) ((val) & 0xFFu)

#define LOAD_BYTE() byte_operand = buffer[*pc]                                     ; *pc += 1;
#define LOAD_WORD() word_operand = buffer[*pc] | (((uint16_t)buffer[*pc + 1]) << 8); *pc += 2;

#define HEXDUMP_APPLE_0() if (options->apple2_output) { sprintf(hex_dump, "%04X:"              , current_addr                                                        ); } else
#define HEXDUMP_APPLE_1() if (options->apple2_output) { sprintf(hex_dump, "%04X:%02X        "  , current_addr, opcode                                                 ); } else
#define HEXDUMP_APPLE_2() if (options->apple2_output) { sprintf(hex_dump, "%04X:%02X %02X    " , current_addr, opcode, byte_operand                                   ); } else
#define HEXDUMP_APPLE_3() if (options->apple2_output) { sprintf(hex_dump, "%04X:%02X %02X %02X", current_addr, opcode, LOW_PART(word_operand), HIGH_PART(word_operand)); } else

#define HEXDUMP_NES_1() sprintf(hex_dump, "$%04X> %02X:"         , current_addr, opcode);
#define HEXDUMP_NES_2() sprintf(hex_dump, "$%04X> %02X %02X:"    , current_addr, opcode, byte_operand);
#define HEXDUMP_NES_3() sprintf(hex_dump, "$%04X> %02X %02X%02X:", current_addr, opcode, LOW_PART(word_operand), HIGH_PART(word_operand));

/* This function disassembles the opcode at the PC and outputs it in *output */
static void disassemble(char *output, uint8_t *buffer, options_t *options, uint16_t *pc) {
    char        opcode_repr[256], hex_dump[256];
    int         len = 0;
    uint8_t     byte_operand;
    uint16_t    word_operand = 0;
    uint16_t    current_addr = *pc;
    uint8_t     opcode = buffer[current_addr];
    int         found  = !(g_opcode_table[opcode].cycles_exceptions & BAD);
    const char *mnemonic = g_opcode_table[opcode].mnemonic; // Opcode found in table: disassemble properly according to addressing mode

    opcode_repr[0] = '\0';
    hex_dump[0] = '\0';

    // Set hex dump to default single address format. Will be overwritten
    // by more complex output in case of hex dump mode enabled
    HEXDUMP_APPLE_0()
    sprintf(hex_dump, "$%04X", current_addr);

    *pc += 1; // Instructions are always at least 1 byte

    // For opcode not found, terminate early
    if (!found) {
        sprintf(opcode_repr, ".byte $%02X", opcode);
        if (options->hex_output) {
            HEXDUMP_APPLE_1()
            sprintf(hex_dump, "$%04X> %02X:", current_addr, opcode);
        }
        len = sprintf(output, DUMP_FORMAT, hex_dump, opcode_repr);
        sprintf( &output[len], "%s", " INVALID OPCODE !!!" );
        return;
    }

    switch (g_opcode_table[opcode].addressing) {
        case IMMED:
            /* Get immediate value operand */
            LOAD_BYTE()

            sprintf(opcode_repr, "%s #$%02X", mnemonic, byte_operand);
            if (options->hex_output) {
                HEXDUMP_APPLE_2()
                sprintf(hex_dump, "$%04X> %02X %02X:", current_addr, opcode, byte_operand);
            }

            break;
        case ABSOL:
            /* Get absolute address operand */
            LOAD_WORD()

            sprintf(opcode_repr, "%s $%02X%02X", mnemonic, HIGH_PART(word_operand), LOW_PART(word_operand));
            if (options->hex_output) {
                HEXDUMP_APPLE_3()
                sprintf(hex_dump, "$%04X> %02X %02X%02X:", current_addr, opcode, LOW_PART(word_operand), HIGH_PART(word_operand));
            }

            break;
        case ZEROP:
            /* Get zero page address */
            LOAD_BYTE()

            sprintf(opcode_repr, "%s $%02X", mnemonic, byte_operand);
            if (options->hex_output) {
                HEXDUMP_APPLE_2()
                sprintf(hex_dump, "$%04X> %02X %02X:", current_addr, opcode, byte_operand);
            }

            break;
        case IMPLI:
            sprintf(opcode_repr, "%s", mnemonic);
            if (options->hex_output) {
                HEXDUMP_APPLE_1()
                sprintf(hex_dump, "$%04X> %02X:", current_addr, opcode);
            }

            break;
        case INDIA:
            /* Get indirection address */
            LOAD_WORD()

            sprintf(opcode_repr, "%s ($%02X%02X)", mnemonic, HIGH_PART(word_operand), LOW_PART(word_operand));
            if (options->hex_output) {
                HEXDUMP_APPLE_3()
                sprintf(hex_dump, "$%04X> %02X %02X%02X:", current_addr, opcode, LOW_PART(word_operand), HIGH_PART(word_operand));
            }

            break;
        case ABSIX:
            /* Get base address */
            LOAD_WORD()

            sprintf(opcode_repr, "%s $%02X%02X,X", mnemonic, HIGH_PART(word_operand), LOW_PART(word_operand));
            if (options->hex_output) {
                HEXDUMP_APPLE_3()
                sprintf(hex_dump, "$%04X> %02X %02X%02X:", current_addr, opcode, LOW_PART(word_operand), HIGH_PART(word_operand));
            }

            break;
        case ABSIY:
            /* Get baser address */
            LOAD_WORD()

            sprintf(opcode_repr, "%s $%02X%02X,Y", mnemonic, HIGH_PART(word_operand), LOW_PART(word_operand));
            if (options->hex_output) {
                HEXDUMP_APPLE_3()
                sprintf(hex_dump, "$%04X> %02X %02X%02X:", current_addr, opcode, LOW_PART(word_operand), HIGH_PART(word_operand));
            }

            break;
        case ZEPIX:
            /* Get zero-page base address */
            LOAD_BYTE()

            sprintf(opcode_repr, "%s $%02X,X", mnemonic, byte_operand);
            if (options->hex_output) {
                HEXDUMP_APPLE_2()
                sprintf(hex_dump, "$%04X> %02X %02X:", current_addr, opcode, byte_operand);
            }

            break;
        case ZEPIY:
            /* Get zero-page base address */
            LOAD_BYTE()

            sprintf(opcode_repr, "%s $%02X,Y", mnemonic, byte_operand);
            if (options->hex_output) {
                HEXDUMP_APPLE_2()
                sprintf(hex_dump, "$%04X> %02X %02X:", current_addr, opcode, byte_operand);
            }

            break;
        case INDIN:
            /* Get zero-page base address */
            LOAD_BYTE()

            sprintf(opcode_repr, "%s ($%02X,X)", mnemonic, byte_operand);
            if (options->hex_output) {
                HEXDUMP_APPLE_2()
                sprintf(hex_dump, "$%04X> %02X %02X:", current_addr, opcode, byte_operand);
            }

            break;
        case ININD:
            /* Get zero-page base address */
            LOAD_BYTE()

            sprintf(opcode_repr, "%s ($%02X),Y", mnemonic, byte_operand);
            if (options->hex_output) {
                HEXDUMP_APPLE_2()
                sprintf(hex_dump, "$%04X> %02X %02X:", current_addr, opcode, byte_operand);
            }

            break;
        case RELAT:
            /* Get relative modifier */
            LOAD_BYTE()

            // Compute displacement from first byte after full instruction.
            word_operand = current_addr + 2;
            if (byte_operand > 0x7Fu) {
                word_operand -= ((~byte_operand & 0x7Fu) + 1);
            } else {
                word_operand += byte_operand & 0x7Fu;
            }

            sprintf(opcode_repr, "%s $%04X", mnemonic, word_operand);
            if (options->hex_output) {
                HEXDUMP_APPLE_2()
                sprintf(hex_dump, "$%04X> %02X %02X:", current_addr, opcode, byte_operand);
            }

            break;
        case ACCUM:
            sprintf(opcode_repr, "%s A", mnemonic);
            if (options->hex_output) {
                HEXDUMP_APPLE_1()
                sprintf(hex_dump, "$%04X> %02X:", current_addr, opcode);
            }

            break;
        default:
            // Will not happen since each entry in opcode_table has address mode set
            break;
    }

    // Emit disassembly line content, prior to annotation comments
    len = sprintf(output, DUMP_FORMAT, hex_dump, opcode_repr);
    output += len;

    /* Add cycle count if necessary */
    if (options->cycle_counting) {
        output = append_cycle(output, opcode, *pc + 1, word_operand);
    }

    /* Add NES port info if necessary */
    switch (g_opcode_table[opcode].addressing) {
        case ABSOL:
        case ABSIX:
        case ABSIY:
            if (options->nes_mode) {
                append_nes(output, word_operand);
            }
            break;
        default:
            /* Other addressing modes: not enough info to add NES register annotation */
            break;
    }
}

static void version(void) {
    fprintf(stderr,
"DCC6502 %s (C)1998-2014 Tennessee Carmel-Veilleux <veilleux@tentech.ca>\n"
"Enhancements by %s\n"
"This software is licensed under the MIT license. See the LICENSE file.\n"
"See source on GitHub: %s\n"
"Forked from version: %s\n"
        , VERSION_INFO
        , AUTHOR
        , GIT_LOCATION
        , FORK_LOCATION
    );
}

static void usage(void) {
    fprintf(stderr,
"\n"
"Usage: dcc6502 [options] FILENAME\n"
"  -?           : Show this help message\n"
"  -a           : Apple II/Atari style output\n"
"  -apple\n"
"  -c           : Enable cycle counting annotations\n"
"  -d           : Enable hex dump within disassembly\n"
"  -h           : Show this help message\n"
"  -m NUM_BYTES : Only disassemble the first NUM_BYTES bytes\n"
"  -n           : Enable NES register annotations\n"
"  -o ORIGIN    : Set the origin (base address of disassembly) [default: 0x8000]\n"
"  -v           : Get only version information\n"
"\n"
    );
}

static int str_arg_to_ulong(char *str, unsigned long *value) {
    uint32_t tmp = 0;
    char *endptr;

    errno = EOK;
    tmp = strtoul(str, &endptr, 0);
    /* In case of conversion error, return error indication */
    if ((EOK != errno) || (*endptr != '\0')) {
        return 0;
    } else {
        *value = tmp;
        return 1;
    }
}

static void usage_and_exit(int exit_code, const char *message) {
    version();
    usage();
    if (NULL != message) {
        fprintf(stderr, "%s\n", message);
    }
    exit(exit_code);
}

static void parse_args(int argc, char *argv[], options_t *options) {
    int arg_idx = 1;
    int arg_len;
    unsigned long tmp_value;

    options->apple2_output  = 0;
    options->cycle_counting = 0;
    options->hex_output     = 0;
    options->nes_mode       = 0;
    options->org            = 0x8000;
    options->max_num_bytes  = 65536;

    while (arg_idx < argc) {
        /* First non-dash-starting argument is assumed to be filename */
        if (argv[arg_idx][0] != '-') {
            break;
        }

        /* Got a switch, process it */
        switch (argv[arg_idx][1]) {
            case '?':
            case 'h':
                usage_and_exit(0, NULL);
                break;
            case 'a':
                /* Optional long form */
                arg_len = strlen(&argv[arg_idx][1]);
                if ((arg_len > 1) && (strcmp(&argv[arg_idx][1], "apple") != 0))
                    goto unknown;
                options->apple2_output = 1;
                break;
            case 'c':
                options->cycle_counting = 1;
                break;
            case 'd':
                options->hex_output = 1;
                break;
            case 'm':
                if ((arg_idx == (argc - 1)) || (argv[arg_idx + 1][0] == '-')) {
                    usage_and_exit(1, "Missing argument to -m switch");
                }

                /* Get argument and parse it */
                arg_idx++;
                if (!str_arg_to_ulong(argv[arg_idx], &tmp_value)) {
                    usage_and_exit(1, "Invalid argument to -m switch");
                }
                options->max_num_bytes = tmp_value;
                break;
            case 'o':
                if ((arg_idx == (argc - 1)) || (argv[arg_idx + 1][0] == '-')) {
                    usage_and_exit(1, "Missing argument to -o switch");
                }

                /* Get argument and parse it */
                arg_idx++;
                if (!str_arg_to_ulong(argv[arg_idx], &tmp_value)) {
                    usage_and_exit(1, "Invalid argument to -o switch");
                }
                options->org = (uint16_t)(tmp_value & 0xFFFFu);
                break;
            case 'n':
                options->nes_mode = 1;
                break;
            case 'v':
                version();
                exit(0);
                break;
            default:
unknown:
                version();
                usage();
                fprintf(stderr, "Unrecognized switch: %s\n", argv[arg_idx]);
                exit(1);
        }
        arg_idx++;
    }

    /* Make sure we have a filename left to take after we stopped parsing switches */
    if (arg_idx >= argc) {
        usage_and_exit(1, "Missing filename from command line");
    }

    options->filename = argv[arg_idx];
}

int main(int argc, char *argv[]) {
    int       byte_count = 0;
    char      tmpstr[512];
    uint8_t  *buffer;     /* Memory buffer */
    FILE     *input_file; /* Input file */
    uint16_t  pc;         /* Program counter */
    options_t options;    /* Command-line options parsing results */

    parse_args(argc, argv, &options);

    buffer = calloc(1, 65536);
    if (NULL == buffer) {
        usage_and_exit(3, "Could not allocate disassembly memory buffer.");
    }

    /* Read file into memory buffer */
    input_file = fopen(options.filename, "rb");

    if (NULL == input_file) {
        version();
        fprintf(stderr, "File not found or invalid filename : %s\n", options.filename);
        exit(2);
    }

    byte_count = 0;
    while(!feof(input_file) && ((options.org + byte_count) <= 0xFFFFu) && (byte_count < options.max_num_bytes)) {
        fread(&buffer[options.org + byte_count], 1, 1, input_file);
        byte_count++;
    }

    fclose(input_file);

    /* Disassemble contents of buffer */
    emit_header(&options, byte_count);
    pc = options.org;
    while((pc <= 0xFFFFu) && ((pc - options.org) < byte_count)) {
        disassemble(tmpstr, buffer, &options, &pc);
        fprintf(stdout, "%s\n", tmpstr);
    }

    free(buffer);

    return 0;
}
