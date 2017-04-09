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
#define VERSION_INFO "v2.4"
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
    unsigned long start_offset;   /*=0     starting offset to read from binary file */
    unsigned long max_num_bytes;  /*=65536 maximum number of bytes to read from binary file */
    int           user_length;    /* 1 if user requested custom (file) length */
    int           omit_opcodes;   /* 1 if address and opcodes should be skipped (left blank) == clean assembly style */
    uint16_t      org;            /* Origin of addresses */
} options_t;

/* Opcode table */
static opcode_t g_opcode_table[NUMBER_OPCODES] = {
    {"BRK", IMPLI, 7, 0                        }, /* 00 BRK */
    {"ORA", INDIN, 6, 0                        }, /* 01 ORA */
    {"???", 0    , 0, BAD                      }, /* 02     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 03     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 04     illegal 6502 */
    {"ORA", ZEROP, 3, 0                        }, /* 05 ORA */
    {"ASL", ZEROP, 5, 0                        }, /* 06 ASL */
    {"???", 0    , 0, BAD                      }, /* 07     illegal 6502 */
    {"PHP", IMPLI, 3, 0                        }, /* 08 PHP */
    {"ORA", IMMED, 2, 0                        }, /* 09 ORA */
    {"ASL", ACCUM, 2, 0                        }, /* 0A ASL */
    {"???", 0    , 0, BAD                      }, /* 0B     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 0C     illegal 6502 */
    {"ORA", ABSOL, 4, 0                        }, /* 0D ORA */
    {"ASL", ABSOL, 6, 0                        }, /* 0E ASL */
    {"???", 0    , 0, BAD                      }, /* 0F     illegal 6502 */
    {"BPL", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* 10 BPL */
    {"ORA", ININD, 5, CYCLE_PAGE               }, /* 11 ORA */
    {"???", 0    , 0, BAD                      }, /* 12     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 13     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 14     illegal 6502 */
    {"ORA", ZEPIX, 4, 0                        }, /* 15 ORA */
    {"ASL", ZEPIX, 6, 0                        }, /* 16 ASL */
    {"???", 0    , 0, BAD                      }, /* 17     illegal 6502 */
    {"CLC", IMPLI, 2, 0                        }, /* 18 CLC */
    {"ORA", ABSIY, 4, CYCLE_PAGE               }, /* 19 ORA */
    {"???", 0    , 0, BAD                      }, /* 1A     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 1B     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 1C     illegal 6502 */
    {"ORA", ABSIX, 4, CYCLE_PAGE               }, /* 1D ORA */
    {"ASL", ABSIX, 7, 0                        }, /* 1E ASL */
    {"???", 0    , 0, BAD                      }, /* 1F     illegal 6502 */
    {"JSR", ABSOL, 6, 0                        }, /* 20 JSR */
    {"AND", INDIN, 6, 0                        }, /* 21 AND */
    {"???", 0    , 0, BAD                      }, /* 22     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 23     illegal 6502 */
    {"BIT", ZEROP, 3, 0                        }, /* 24 BIT */
    {"AND", ZEROP, 3, 0                        }, /* 25 AND */
    {"ROL", ZEROP, 5, 0                        }, /* 26 ROL */
    {"???", 0    , 0, BAD                      }, /* 27     illegal 6502 */
    {"PLP", IMPLI, 4, 0                        }, /* 28 PLP */
    {"AND", IMMED, 2, 0                        }, /* 29 AND */
    {"ROL", ACCUM, 2, 0                        }, /* 2A ROL */
    {"???", 0    , 0, BAD                      }, /* 2B     illegal 6502 */
    {"BIT", ABSOL, 4, 0                        }, /* 2C BIT */
    {"AND", ABSOL, 4, 0                        }, /* 2D AND */
    {"ROL", ABSOL, 6, 0                        }, /* 2E ROL */
    {"???", 0    , 0, BAD                      }, /* 2F     illegal 6502 */
    {"BMI", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* 30 BMI */
    {"AND", ININD, 5, CYCLE_PAGE               }, /* 31 AND */
    {"???", 0    , 0, BAD                      }, /* 32     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 33     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 34     illegal 6502 */
    {"AND", ZEPIX, 4, 0                        }, /* 35 AND */
    {"ROL", ZEPIX, 6, 0                        }, /* 36 ROL */
    {"???", 0    , 0, BAD                      }, /* 37     illegal 6502 */
    {"SEC", IMPLI, 2, 0                        }, /* 38 SEC */
    {"AND", ABSIY, 4, CYCLE_PAGE               }, /* 39 AND */
    {"???", 0    , 0, BAD                      }, /* 3A     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 3B     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 3C     illegal 6502 */
    {"AND", ABSIX, 4, CYCLE_PAGE               }, /* 3D AND */
    {"ROL", ABSIX, 7, 0                        }, /* 3E ROL */
    {"???", 0    , 0, BAD                      }, /* 3F     illegal 6502 */
    {"RTI", IMPLI, 6, 0                        }, /* 40 RTI */
    {"EOR", INDIN, 6, 1                        }, /* 41 EOR */
    {"???", 0    , 0, BAD                      }, /* 42     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 43     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 44     illegal 6502 */
    {"EOR", ZEROP, 3, 0                        }, /* 45 EOR */
    {"LSR", ZEROP, 5, 0                        }, /* 46 LSR */
    {"???", 0    , 0, BAD                      }, /* 47     illegal 6502 */
    {"PHA", IMPLI, 3, 0                        }, /* 48 PHA */
    {"EOR", IMMED, 2, 0                        }, /* 49 EOR */
    {"LSR", ACCUM, 2, 0                        }, /* 4A LSR */
    {"???", 0    , 0, BAD                      }, /* 4B     illegal 6502 */
    {"JMP", ABSOL, 3, 0                        }, /* 4C JMP */
    {"EOR", ABSOL, 4, 0                        }, /* 4D EOR */
    {"LSR", ABSOL, 6, 0                        }, /* 4E LSR */
    {"???", 0    , 0, BAD                      }, /* 4F     illegal 6502 */
    {"BVC", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* 50 BVC */
    {"EOR", ININD, 5, CYCLE_PAGE               }, /* 51 EOR */
    {"???", 0    , 0, BAD                      }, /* 52     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 53     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 54     illegal 6502 */
    {"EOR", ZEPIX, 4, 0                        }, /* 55 EOR */
    {"LSR", ZEPIX, 6, 0                        }, /* 56 LSR */
    {"???", 0    , 0, BAD                      }, /* 57     illegal 6502 */
    {"CLI", IMPLI, 2, 0                        }, /* 58 CLI */
    {"EOR", ABSIY, 4, CYCLE_PAGE               }, /* 59 EOR */
    {"???", 0    , 0, BAD                      }, /* 5A     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 5B     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 5C     illegal 6502 */
    {"EOR", ABSIX, 4, CYCLE_PAGE               }, /* 5D EOR */
    {"LSR", ABSIX, 7, 0                        }, /* 5E LSR */
    {"???", 0    , 0, BAD                      }, /* 5F     illegal 6502 */
    {"RTS", IMPLI, 6, 0                        }, /* 60 RTS */
    {"ADC", INDIN, 6, 0                        }, /* 61 ADC */
    {"???", 0    , 0, BAD                      }, /* 62     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 63     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 64     illegal 6502 */
    {"ADC", ZEROP, 3, 0                        }, /* 65 ADC */
    {"ROR", ZEROP, 5, 0                        }, /* 66 ROR */
    {"???", 0    , 0, BAD                      }, /* 67     illegal 6502 */
    {"PLA", IMPLI, 4, 0                        }, /* 68 PLA */
    {"ADC", IMMED, 2, 0                        }, /* 69 ADC */
    {"ROR", ACCUM, 2, 0                        }, /* 6A ROR */
    {"???", 0    , 0, BAD                      }, /* 6B     illegal 6502 */
    {"JMP", INDIA, 5, 0                        }, /* 6C JMP */
    {"ADC", ABSOL, 4, 0                        }, /* 6D ADC */
    {"ROR", ABSOL, 6, 0                        }, /* 6E ROR */
    {"???", 0    , 0, BAD                      }, /* 6F     illegal 6502 */
    {"BVS", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* 70 BVS */
    {"ADC", ININD, 5, CYCLE_PAGE               }, /* 71 ADC */
    {"???", 0    , 0, BAD                      }, /* 72     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 73     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 74     illegal 6502 */
    {"ADC", ZEPIX, 4, 0                        }, /* 75 ADC */
    {"ROR", ZEPIX, 6, 0                        }, /* 76 ROR */
    {"???", 0    , 0, BAD                      }, /* 77     illegal 6502 */
    {"SEI", IMPLI, 2, 0                        }, /* 78 SEI */
    {"ADC", ABSIY, 4, CYCLE_PAGE               }, /* 79 ADC */
    {"???", 0    , 0, BAD                      }, /* 7A     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 7B     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 7C     illegal 6502 */
    {"ADC", ABSIX, 4, CYCLE_PAGE               }, /* 7D ADC */
    {"ROR", ABSIX, 7, 0                        }, /* 7E ROR */
    {"???", 0    , 0, BAD                      }, /* 7F     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 80     illegal 6502 */
    {"STA", INDIN, 6, 0                        }, /* 81 STA */
    {"???", 0    , 0, BAD                      }, /* 82     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 83     illegal 6502 */
    {"STY", ZEROP, 3, 0                        }, /* 84 STY */
    {"STA", ZEROP, 3, 0                        }, /* 85 STA */
    {"STX", ZEROP, 3, 0                        }, /* 86 STX */
    {"???", 0    , 0, BAD                      }, /* 87     illegal 6502 */
    {"DEY", IMPLI, 2, 0                        }, /* 88 DEY */
    {"???", 0    , 0, BAD                      }, /* 89     illegal 6502 */
    {"TXA", IMPLI, 2, 0                        }, /* 8A TXA */
    {"???", 0    , 0, BAD                      }, /* 8B     illegal 6502 */
    {"STY", ABSOL, 4, 0                        }, /* 9C STY */
    {"STA", ABSOL, 4, 0                        }, /* 8D STA */
    {"STX", ABSOL, 4, 0                        }, /* 8E STX */
    {"???", 0    , 0, BAD                      }, /* 8F     illegal 6502 */
    {"BCC", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* 90 BCC */
    {"STA", ININD, 5, CYCLE_PAGE               }, /* 91 STA */
    {"???", 0    , 0, BAD                      }, /* 92     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 93     illegal 6502 */
    {"STY", ZEPIX, 4, 0                        }, /* 94 STY */
    {"STA", ZEPIX, 4, 0                        }, /* 95 STA */
    {"STX", ZEPIY, 4, 0                        }, /* 96 STX */
    {"???", 0    , 0, BAD                      }, /* 97     illegal 6502 */
    {"TYA", IMPLI, 2, 0                        }, /* 98 TYA */
    {"STA", ABSIY, 4, CYCLE_PAGE               }, /* 99 STA */
    {"TXS", IMPLI, 2, 0                        }, /* 9A TXS */
    {"???", 0    , 0, BAD                      }, /* 9B     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 9C     illegal 6502 */
    {"STA", ABSIX, 4, CYCLE_PAGE               }, /* 9D STA */
    {"???", 0    , 0, BAD                      }, /* 9E     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* 9F     illegal 6502 */
    {"LDY", IMMED, 2, 0                        }, /* A0 LDY */
    {"LDA", INDIN, 6, 0                        }, /* A1 LDA */
    {"LDX", IMMED, 2, 0                        }, /* A2 LDX */
    {"???", 0    , 0, BAD                      }, /* A3     illegal 6502 */
    {"LDY", ZEROP, 3, 0                        }, /* A4 LDY */
    {"LDA", ZEROP, 3, 0                        }, /* A5 LDA */
    {"LDX", ZEROP, 3, 0                        }, /* A6 LDX */
    {"???", 0    , 0, BAD                      }, /* A7     illegal 6502 */
    {"TAY", IMPLI, 2, 0                        }, /* A8 TAY */
    {"LDA", IMMED, 2, 0                        }, /* A9 LDA */
    {"TAX", IMPLI, 2, 0                        }, /* AA TAX */
    {"???", 0    , 0, BAD                      }, /* AB     illegal 6502 */
    {"LDY", ABSOL, 4, 0                        }, /* AC LDY */
    {"LDA", ABSOL, 4, 0                        }, /* AD LDA */
    {"LDX", ABSOL, 4, 0                        }, /* AE LDX */
    {"???", 0    , 0, BAD                      }, /* AF     illegal 6502 */
    {"BCS", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* B0 BCS */
    {"LDA", ININD, 5, CYCLE_PAGE               }, /* B1 LDA */
    {"???", 0    , 0, BAD                      }, /* B2     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* B3     illegal 6502 */
    {"LDY", ZEPIX, 4, 0                        }, /* B4 LDY */
    {"LDA", ZEPIX, 4, 0                        }, /* B5 LDA */
    {"LDX", ZEPIY, 4, 0                        }, /* B6 LDX */
    {"???", 0    , 0, BAD                      }, /* B7     illegal 6502 */
    {"CLV", IMPLI, 2, 0                        }, /* B8 CLV */
    {"LDA", ABSIY, 4, CYCLE_PAGE               }, /* B9 LDA */
    {"TSX", IMPLI, 2, 0                        }, /* BA TSX */
    {"???", 0    , 0, BAD                      }, /* BB     illegal 6502 */
    {"LDY", ABSIX, 4, CYCLE_PAGE               }, /* BC LDY */
    {"LDA", ABSIX, 4, CYCLE_PAGE               }, /* BD LDA */
    {"LDX", ABSIY, 4, CYCLE_PAGE               }, /* BE LDX */
    {"???", 0    , 0, BAD                      }, /* BF     illegal 6502 */
    {"CPY", IMMED, 2, 0                        }, /* C0 CPY */
    {"CMP", INDIN, 6, 0                        }, /* C1 CMP */
    {"???", 0    , 0, BAD                      }, /* C2     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* C3     illegal 6502 */
    {"CPY", ZEROP, 3, 0                        }, /* C4 CPY */
    {"CMP", ZEROP, 3, 0                        }, /* C5 CMP */
    {"DEC", ZEROP, 5, 0                        }, /* C6 DEC */
    {"???", 0    , 0, BAD                      }, /* C7     illegal 6502 */
    {"INY", IMPLI, 2, 0                        }, /* C8 INY */
    {"CMP", IMMED, 2, 0                        }, /* C9 CMP */
    {"DEX", IMPLI, 2, 0                        }, /* CA DEX */
    {"???", 0    , 0, BAD                      }, /* CB     illegal 6502 */
    {"CPY", ABSOL, 4, 0                        }, /* CC CPY */
    {"CMP", ABSOL, 4, 0                        }, /* CD CMP */
    {"DEC", ABSOL, 6, 0                        }, /* CE DEC */
    {"???", 0    , 0, BAD                      }, /* CF     illegal 6502 */
    {"BNE", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* D0 BNE */
    {"CMP", ININD, 5, CYCLE_PAGE               }, /* D1 CMP */
    {"???", 0    , 0, BAD                      }, /* D2     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* D3     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* D4     illegal 6502 */
    {"CMP", ZEPIX, 4, 0                        }, /* D5 CMP */
    {"DEC", ZEPIX, 6, 0                        }, /* D6 DEC */
    {"???", 0    , 0, BAD                      }, /* D7     illegal 6502 */
    {"CLD", IMPLI, 2, 0                        }, /* D8 CLD */
    {"CMP", ABSIY, 4, CYCLE_PAGE               }, /* D9 CMP */
    {"???", 0    , 0, BAD                      }, /* DA     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* DB     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* DC     illegal 6502 */
    {"CMP", ABSIX, 4, CYCLE_PAGE               }, /* DD CMP */
    {"DEC", ABSIX, 7, 0                        }, /* DE DEC */
    {"???", 0    , 0, BAD                      }, /* DF     illegal 6502 */
    {"CPX", IMMED, 2, 0                        }, /* E0 CPX */
    {"SBC", INDIN, 6, 0                        }, /* E1 SBC */
    {"???", 0    , 0, BAD                      }, /* E2     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* E3     illegal 6502 */
    {"CPX", ZEROP, 3, 0                        }, /* E4 CPX */
    {"SBC", ZEROP, 3, 0                        }, /* E5 SBC */
    {"INC", ZEROP, 5, 0                        }, /* E6 INC */
    {"???", 0    , 0, BAD                      }, /* E7     illegal 6502 */
    {"INX", IMPLI, 2, 0                        }, /* E8 INX */
    {"SBC", IMMED, 2, 0                        }, /* E9 SBC */
    {"NOP", IMPLI, 2, 0                        }, /* EA NOP */
    {"???", 0    , 0, BAD                      }, /* EB     illegal 6502 */
    {"CPX", ABSOL, 4, 0                        }, /* EC CPX */
    {"SBC", ABSOL, 4, 0                        }, /* ED SBC */
    {"INC", ABSOL, 6, 0                        }, /* EE INC */
    {"???", 0    , 0, BAD                      }, /* EF     illegal 6502 */
    {"BEQ", RELAT, 2, CYCLE_PAGE | CYCLE_BRANCH}, /* F0 BEQ */
    {"SBC", ININD, 5, CYCLE_PAGE               }, /* F1 SBC */
    {"???", 0    , 0, BAD                      }, /* F2     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* F3     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* F4     illegal 6502 */
    {"SBC", ZEPIX, 4, 0                        }, /* F5 SBC */
    {"INC", ZEPIX, 6, 0                        }, /* F6 INC */
    {"???", 0    , 0, BAD                      }, /* F7     illegal 6502 */
    {"SED", IMPLI, 2, 0                        }, /* F8 SED */
    {"SBC", ABSIY, 4, CYCLE_PAGE               }, /* F9 SBC */
    {"???", 0    , 0, BAD                      }, /* FA     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* FB     illegal 6502 */
    {"???", 0    , 0, BAD                      }, /* FC     illegal 6502 */
    {"SBC", ABSIX, 4, CYCLE_PAGE               }, /* FD SBC */
    {"INC", ABSIX, 7, 0                        }, /* FE INC */
    {"???", 0    , 0, BAD                      }  /* FF     illegal 6502 */
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

        if (options->omit_opcodes)
            hex_dump[0] = '\0';
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
    if (options->omit_opcodes)
        hex_dump[0] = '\0';

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
"  -b NUM_BYTES : Skip this many bytes of the input file [default: 0x0]\n"
"  -c           : Enable cycle counting annotations\n"
"  -d           : Enable hex dump within disassembly\n"
"  -h           : Show this help message\n"
"  -m NUM_BYTES : Only disassemble the first NUM_BYTES bytes\n"
"  -n           : Enable NES register annotations\n"
"  -o ORIGIN    : Set the origin (base address of disassembly) [default: 0x8000]\n"
"  -s           : Assembly style output only (omit address and opcodes) [default OFF]\n"
"  -v           : Get only version information\n"
"\n"
"Examples:\n"
"\n"
"\tdcc6502       -o 0xF800 f800.rom\n"
"\n"
"\tdcc6502 -a -d -o 0xF800 f800.rom\n"
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
    options->omit_opcodes   = 0;
    options->org            = 0x8000;
    options->max_num_bytes  = 65536; // Default to entire file
    options->start_offset   = 0; // Default to first byte
    options->user_length    = 0; // False=read default 64K, True=user provided num bytes to read

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
            case 'b':
                if ((arg_idx == (argc - 1)) || (argv[arg_idx + 1][0] == '-')) {
                    usage_and_exit(1, "Missing argument to -b switch");
                }

                /* Get argument and parse it */
                arg_idx++;
                if (!str_arg_to_ulong(argv[arg_idx], &tmp_value)) {
                    usage_and_exit(1, "Invalid argument to -b switch");
                }
                options->start_offset = tmp_value;
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
                if (tmp_value > 0x10000)
                    tmp_value = 0x10000;
                options->max_num_bytes = tmp_value;
                options->user_length   = 1;
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
            case 's':
                options->omit_opcodes = 1;
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
    char      tmpstr[512];
    uint8_t  *buffer;     /* Memory buffer */
    FILE     *input_file; /* Input file */
    uint16_t  pc;         /* Program counter */
    size_t    end;
    options_t options;    /* Command-line options parsing results */

    parse_args(argc, argv, &options);

    buffer = calloc(1, 65536 + 4); // fix array out-of-bounds buffer overflow
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

    fseek( input_file, 0, SEEK_END );
    size_t size = ftell( input_file );
    fseek( input_file, 0, SEEK_SET );
    fseek( input_file, (long int) options.start_offset, SEEK_CUR );

    if (size > 0x10000) {
        size = 0x10000;
        fprintf(stderr, ";WARNING: File size > $10000 (65,536) bytes.\n");
        fprintf(stderr, ";         Clamping to $%05X.\n", (uint32_t) size);
    }

    if (!options.user_length) {
        options.max_num_bytes = size;
    }

    if ((options.start_offset + options.max_num_bytes) > size) {
        options.max_num_bytes = size - options.start_offset;

        fprintf(stderr, ";INFORMATION: Starting offset + disassembly length > file size!\n");
        fprintf(stderr, ";             Clamping disassembly length to $%05X.\n", (uint32_t) options.max_num_bytes);
    }

    // If file offset > file length nothing to do
    if (options.start_offset > size) {
        fprintf(stderr, ";INFORMATION: Starting position > file size.\n");
        fprintf(stderr, ";             Skipping file since nothing to do.\n");
        options.max_num_bytes = 0;
        goto done_file;
    }

    // If user offset + user length > (0xFFFF+1) then clamp
    if ((options.org + options.max_num_bytes) > 0x10000) {
        options.max_num_bytes = 0x10000 - options.org;
        fprintf(stderr, ";WARNING: Start + Length > $FFFF (65,535) bytes.\n");
        fprintf(stderr, ";         Clamping to $%05X.\n", (uint32_t) options.max_num_bytes );
    }

    fread(&buffer[ options.org ], options.max_num_bytes, 1, input_file);

done_file:
    fclose(input_file);

    /* Disassemble contents of buffer */
    pc  = options.org;
    end = options.org + options.max_num_bytes;
    emit_header(&options, size);

    while (pc < end) {
        disassemble(tmpstr, buffer, &options, &pc);
        fprintf(stdout, "%s\n", tmpstr);
    }

    free(buffer);

    return 0;
}
