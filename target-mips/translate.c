/*
 *  MIPS32 emulation for qemu: main translation routines.
 * 
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"

//#define MIPS_DEBUG_DISAS
//#define MIPS_DEBUG_SIGN_EXTENSIONS
//#define MIPS_SINGLE_STEP

#ifdef USE_DIRECT_JUMP
#define TBPARAM(x)
#else
#define TBPARAM(x) (long)(x)
#endif

enum {
#define DEF(s, n, copy_size) INDEX_op_ ## s,
#include "opc.h"
#undef DEF
    NB_OPS,
};

static uint16_t *gen_opc_ptr;
static uint32_t *gen_opparam_ptr;

#include "gen-op.h"

/* MIPS major opcodes */
#define MASK_OP_MAJOR(op)  (op & (0x3F << 26))

enum {
    /* indirect opcode tables */
    OPC_SPECIAL  = (0x00 << 26),
    OPC_REGIMM   = (0x01 << 26),
    OPC_CP0      = (0x10 << 26),
    OPC_CP1      = (0x11 << 26),
    OPC_CP2      = (0x12 << 26),
    OPC_CP3      = (0x13 << 26),
    OPC_SPECIAL2 = (0x1C << 26),
    OPC_SPECIAL3 = (0x1F << 26),
    /* arithmetic with immediate */
    OPC_ADDI     = (0x08 << 26),
    OPC_ADDIU    = (0x09 << 26),
    OPC_SLTI     = (0x0A << 26),
    OPC_SLTIU    = (0x0B << 26),
    OPC_ANDI     = (0x0C << 26),
    OPC_ORI      = (0x0D << 26),
    OPC_XORI     = (0x0E << 26),
    OPC_LUI      = (0x0F << 26),
    OPC_DADDI    = (0x18 << 26),
    OPC_DADDIU   = (0x19 << 26),
    /* Jump and branches */
    OPC_J        = (0x02 << 26),
    OPC_JAL      = (0x03 << 26),
    OPC_BEQ      = (0x04 << 26),  /* Unconditional if rs = rt = 0 (B) */
    OPC_BEQL     = (0x14 << 26),
    OPC_BNE      = (0x05 << 26),
    OPC_BNEL     = (0x15 << 26),
    OPC_BLEZ     = (0x06 << 26),
    OPC_BLEZL    = (0x16 << 26),
    OPC_BGTZ     = (0x07 << 26),
    OPC_BGTZL    = (0x17 << 26),
    OPC_JALX     = (0x1D << 26),  /* MIPS 16 only */
    /* Load and stores */
    OPC_LDL      = (0x1A << 26),
    OPC_LDR      = (0x1B << 26),
    OPC_LB       = (0x20 << 26),
    OPC_LH       = (0x21 << 26),
    OPC_LWL      = (0x22 << 26),
    OPC_LW       = (0x23 << 26),
    OPC_LBU      = (0x24 << 26),
    OPC_LHU      = (0x25 << 26),
    OPC_LWR      = (0x26 << 26),
    OPC_LWU      = (0x27 << 26),
    OPC_SB       = (0x28 << 26),
    OPC_SH       = (0x29 << 26),
    OPC_SWL      = (0x2A << 26),
    OPC_SW       = (0x2B << 26),
    OPC_SDL      = (0x2C << 26),
    OPC_SDR      = (0x2D << 26),
    OPC_SWR      = (0x2E << 26),
    OPC_LL       = (0x30 << 26),
    OPC_LLD      = (0x34 << 26),
    OPC_LD       = (0x37 << 26),
    OPC_SC       = (0x38 << 26),
    OPC_SCD      = (0x3C << 26),
    OPC_SD       = (0x3F << 26),
    /* Floating point load/store */
    OPC_LWC1     = (0x31 << 26),
    OPC_LWC2     = (0x32 << 26),
    OPC_LDC1     = (0x35 << 26),
    OPC_LDC2     = (0x36 << 26),
    OPC_SWC1     = (0x39 << 26),
    OPC_SWC2     = (0x3A << 26),
    OPC_SDC1     = (0x3D << 26),
    OPC_SDC2     = (0x3E << 26),
    /* MDMX ASE specific */
    OPC_MDMX     = (0x1E << 26),
    /* Cache and prefetch */
    OPC_CACHE    = (0x2F << 26),
    OPC_PREF     = (0x33 << 26),
    /* Reserved major opcode */
    OPC_MAJOR3B_RESERVED = (0x3B << 26),
};

/* MIPS special opcodes */
#define MASK_SPECIAL(op)   MASK_OP_MAJOR(op) | (op & 0x3F)

enum {
    /* Shifts */
    OPC_SLL      = 0x00 | OPC_SPECIAL,
    /* NOP is SLL r0, r0, 0   */
    /* SSNOP is SLL r0, r0, 1 */
    /* EHB is SLL r0, r0, 3 */
    OPC_SRL      = 0x02 | OPC_SPECIAL, /* also ROTR */
    OPC_SRA      = 0x03 | OPC_SPECIAL,
    OPC_SLLV     = 0x04 | OPC_SPECIAL,
    OPC_SRLV     = 0x06 | OPC_SPECIAL,
    OPC_SRAV     = 0x07 | OPC_SPECIAL,
    OPC_DSLLV    = 0x14 | OPC_SPECIAL,
    OPC_DSRLV    = 0x16 | OPC_SPECIAL, /* also DROTRV */
    OPC_DSRAV    = 0x17 | OPC_SPECIAL,
    OPC_DSLL     = 0x38 | OPC_SPECIAL,
    OPC_DSRL     = 0x3A | OPC_SPECIAL, /* also DROTR */
    OPC_DSRA     = 0x3B | OPC_SPECIAL,
    OPC_DSLL32   = 0x3C | OPC_SPECIAL,
    OPC_DSRL32   = 0x3E | OPC_SPECIAL, /* also DROTR32 */
    OPC_DSRA32   = 0x3F | OPC_SPECIAL,
    /* Multiplication / division */
    OPC_MULT     = 0x18 | OPC_SPECIAL,
    OPC_MULTU    = 0x19 | OPC_SPECIAL,
    OPC_DIV      = 0x1A | OPC_SPECIAL,
    OPC_DIVU     = 0x1B | OPC_SPECIAL,
    OPC_DMULT    = 0x1C | OPC_SPECIAL,
    OPC_DMULTU   = 0x1D | OPC_SPECIAL,
    OPC_DDIV     = 0x1E | OPC_SPECIAL,
    OPC_DDIVU    = 0x1F | OPC_SPECIAL,
    /* 2 registers arithmetic / logic */
    OPC_ADD      = 0x20 | OPC_SPECIAL,
    OPC_ADDU     = 0x21 | OPC_SPECIAL,
    OPC_SUB      = 0x22 | OPC_SPECIAL,
    OPC_SUBU     = 0x23 | OPC_SPECIAL,
    OPC_AND      = 0x24 | OPC_SPECIAL,
    OPC_OR       = 0x25 | OPC_SPECIAL,
    OPC_XOR      = 0x26 | OPC_SPECIAL,
    OPC_NOR      = 0x27 | OPC_SPECIAL,
    OPC_SLT      = 0x2A | OPC_SPECIAL,
    OPC_SLTU     = 0x2B | OPC_SPECIAL,
    OPC_DADD     = 0x2C | OPC_SPECIAL,
    OPC_DADDU    = 0x2D | OPC_SPECIAL,
    OPC_DSUB     = 0x2E | OPC_SPECIAL,
    OPC_DSUBU    = 0x2F | OPC_SPECIAL,
    /* Jumps */
    OPC_JR       = 0x08 | OPC_SPECIAL, /* Also JR.HB */
    OPC_JALR     = 0x09 | OPC_SPECIAL, /* Also JALR.HB */
    /* Traps */
    OPC_TGE      = 0x30 | OPC_SPECIAL,
    OPC_TGEU     = 0x31 | OPC_SPECIAL,
    OPC_TLT      = 0x32 | OPC_SPECIAL,
    OPC_TLTU     = 0x33 | OPC_SPECIAL,
    OPC_TEQ      = 0x34 | OPC_SPECIAL,
    OPC_TNE      = 0x36 | OPC_SPECIAL,
    /* HI / LO registers load & stores */
    OPC_MFHI     = 0x10 | OPC_SPECIAL,
    OPC_MTHI     = 0x11 | OPC_SPECIAL,
    OPC_MFLO     = 0x12 | OPC_SPECIAL,
    OPC_MTLO     = 0x13 | OPC_SPECIAL,
    /* Conditional moves */
    OPC_MOVZ     = 0x0A | OPC_SPECIAL,
    OPC_MOVN     = 0x0B | OPC_SPECIAL,

    OPC_MOVCI    = 0x01 | OPC_SPECIAL,

    /* Special */
    OPC_PMON     = 0x05 | OPC_SPECIAL, /* inofficial */
    OPC_SYSCALL  = 0x0C | OPC_SPECIAL,
    OPC_BREAK    = 0x0D | OPC_SPECIAL,
    OPC_SPIM     = 0x0E | OPC_SPECIAL, /* inofficial */
    OPC_SYNC     = 0x0F | OPC_SPECIAL,

    OPC_SPECIAL15_RESERVED = 0x15 | OPC_SPECIAL,
    OPC_SPECIAL28_RESERVED = 0x28 | OPC_SPECIAL,
    OPC_SPECIAL29_RESERVED = 0x29 | OPC_SPECIAL,
    OPC_SPECIAL35_RESERVED = 0x35 | OPC_SPECIAL,
    OPC_SPECIAL37_RESERVED = 0x37 | OPC_SPECIAL,
    OPC_SPECIAL39_RESERVED = 0x39 | OPC_SPECIAL,
    OPC_SPECIAL3D_RESERVED = 0x3D | OPC_SPECIAL,
};

/* REGIMM (rt field) opcodes */
#define MASK_REGIMM(op)    MASK_OP_MAJOR(op) | (op & (0x1F << 16))

enum {
    OPC_BLTZ     = (0x00 << 16) | OPC_REGIMM,
    OPC_BLTZL    = (0x02 << 16) | OPC_REGIMM,
    OPC_BGEZ     = (0x01 << 16) | OPC_REGIMM,
    OPC_BGEZL    = (0x03 << 16) | OPC_REGIMM,
    OPC_BLTZAL   = (0x10 << 16) | OPC_REGIMM,
    OPC_BLTZALL  = (0x12 << 16) | OPC_REGIMM,
    OPC_BGEZAL   = (0x11 << 16) | OPC_REGIMM,
    OPC_BGEZALL  = (0x13 << 16) | OPC_REGIMM,
    OPC_TGEI     = (0x08 << 16) | OPC_REGIMM,
    OPC_TGEIU    = (0x09 << 16) | OPC_REGIMM,
    OPC_TLTI     = (0x0A << 16) | OPC_REGIMM,
    OPC_TLTIU    = (0x0B << 16) | OPC_REGIMM,
    OPC_TEQI     = (0x0C << 16) | OPC_REGIMM,
    OPC_TNEI     = (0x0E << 16) | OPC_REGIMM,
    OPC_SYNCI    = (0x1F << 16) | OPC_REGIMM,
};

/* Special2 opcodes */
#define MASK_SPECIAL2(op)  MASK_OP_MAJOR(op) | (op & 0x3F)

enum {
    /* Multiply & xxx operations */
    OPC_MADD     = 0x00 | OPC_SPECIAL2,
    OPC_MADDU    = 0x01 | OPC_SPECIAL2,
    OPC_MUL      = 0x02 | OPC_SPECIAL2,
    OPC_MSUB     = 0x04 | OPC_SPECIAL2,
    OPC_MSUBU    = 0x05 | OPC_SPECIAL2,
    /* Misc */
    OPC_CLZ      = 0x20 | OPC_SPECIAL2,
    OPC_CLO      = 0x21 | OPC_SPECIAL2,
    OPC_DCLZ     = 0x24 | OPC_SPECIAL2,
    OPC_DCLO     = 0x25 | OPC_SPECIAL2,
    /* Special */
    OPC_SDBBP    = 0x3F | OPC_SPECIAL2,
};

/* Special3 opcodes */
#define MASK_SPECIAL3(op)  MASK_OP_MAJOR(op) | (op & 0x3F)

enum {
    OPC_EXT      = 0x00 | OPC_SPECIAL3,
    OPC_DEXTM    = 0x01 | OPC_SPECIAL3,
    OPC_DEXTU    = 0x02 | OPC_SPECIAL3,
    OPC_DEXT     = 0x03 | OPC_SPECIAL3,
    OPC_INS      = 0x04 | OPC_SPECIAL3,
    OPC_DINSM    = 0x05 | OPC_SPECIAL3,
    OPC_DINSU    = 0x06 | OPC_SPECIAL3,
    OPC_DINS     = 0x07 | OPC_SPECIAL3,
    OPC_BSHFL    = 0x20 | OPC_SPECIAL3,
    OPC_DBSHFL   = 0x24 | OPC_SPECIAL3,
    OPC_RDHWR    = 0x3B | OPC_SPECIAL3,
};

/* BSHFL opcodes */
#define MASK_BSHFL(op)     MASK_SPECIAL3(op) | (op & (0x1F << 6))

enum {
    OPC_WSBH     = (0x02 << 6) | OPC_BSHFL,
    OPC_SEB      = (0x10 << 6) | OPC_BSHFL,
    OPC_SEH      = (0x18 << 6) | OPC_BSHFL,
};

/* DBSHFL opcodes */
#define MASK_DBSHFL(op)    MASK_SPECIAL3(op) | (op & (0x1F << 6))

enum {
    OPC_DSBH     = (0x02 << 6) | OPC_DBSHFL,
    OPC_DSHD     = (0x05 << 6) | OPC_DBSHFL,
};

/* Coprocessor 0 (rs field) */
#define MASK_CP0(op)       MASK_OP_MAJOR(op) | (op & (0x1F << 21))

enum {
    OPC_MFC0     = (0x00 << 21) | OPC_CP0,
    OPC_DMFC0    = (0x01 << 21) | OPC_CP0,
    OPC_MTC0     = (0x04 << 21) | OPC_CP0,
    OPC_DMTC0    = (0x05 << 21) | OPC_CP0,
    OPC_RDPGPR   = (0x0A << 21) | OPC_CP0,
    OPC_MFMC0    = (0x0B << 21) | OPC_CP0,
    OPC_WRPGPR   = (0x0E << 21) | OPC_CP0,
    OPC_C0       = (0x10 << 21) | OPC_CP0,
    OPC_C0_FIRST = (0x10 << 21) | OPC_CP0,
    OPC_C0_LAST  = (0x1F << 21) | OPC_CP0,
};

/* MFMC0 opcodes */
#define MASK_MFMC0(op)     MASK_CP0(op) | (op & 0xFFFF)

enum {
    OPC_DI       = (0 << 5) | (0x0C << 11) | OPC_MFMC0,
    OPC_EI       = (1 << 5) | (0x0C << 11) | OPC_MFMC0,
};

/* Coprocessor 0 (with rs == C0) */
#define MASK_C0(op)        MASK_CP0(op) | (op & 0x3F)

enum {
    OPC_TLBR     = 0x01 | OPC_C0,
    OPC_TLBWI    = 0x02 | OPC_C0,
    OPC_TLBWR    = 0x06 | OPC_C0,
    OPC_TLBP     = 0x08 | OPC_C0,
    OPC_RFE      = 0x10 | OPC_C0,
    OPC_ERET     = 0x18 | OPC_C0,
    OPC_DERET    = 0x1F | OPC_C0,
    OPC_WAIT     = 0x20 | OPC_C0,
};

/* Coprocessor 1 (rs field) */
#define MASK_CP1(op)       MASK_OP_MAJOR(op) | (op & (0x1F << 21))

enum {
    OPC_MFC1     = (0x00 << 21) | OPC_CP1,
    OPC_DMFC1    = (0x01 << 21) | OPC_CP1,
    OPC_CFC1     = (0x02 << 21) | OPC_CP1,
    OPC_MFHC1    = (0x03 << 21) | OPC_CP1,
    OPC_MTC1     = (0x04 << 21) | OPC_CP1,
    OPC_DMTC1    = (0x05 << 21) | OPC_CP1,
    OPC_CTC1     = (0x06 << 21) | OPC_CP1,
    OPC_MTHC1    = (0x07 << 21) | OPC_CP1,
    OPC_BC1      = (0x08 << 21) | OPC_CP1, /* bc */
    OPC_BC1ANY2  = (0x09 << 21) | OPC_CP1,
    OPC_BC1ANY4  = (0x0A << 21) | OPC_CP1,
    OPC_S_FMT    = (0x10 << 21) | OPC_CP1, /* 16: fmt=single fp */
    OPC_D_FMT    = (0x11 << 21) | OPC_CP1, /* 17: fmt=double fp */
    OPC_E_FMT    = (0x12 << 21) | OPC_CP1, /* 18: fmt=extended fp */
    OPC_Q_FMT    = (0x13 << 21) | OPC_CP1, /* 19: fmt=quad fp */
    OPC_W_FMT    = (0x14 << 21) | OPC_CP1, /* 20: fmt=32bit fixed */
    OPC_L_FMT    = (0x15 << 21) | OPC_CP1, /* 21: fmt=64bit fixed */
    OPC_PS_FMT   = (0x16 << 21) | OPC_CP1, /* 22: fmt=paired single fp */
};

#define MASK_CP1_FUNC(op)       MASK_CP1(op) | (op & 0x3F)
#define MASK_BC1(op)            MASK_CP1(op) | (op & (0x3 << 16))

enum {
    OPC_BC1F     = (0x00 << 16) | OPC_BC1,
    OPC_BC1T     = (0x01 << 16) | OPC_BC1,
    OPC_BC1FL    = (0x02 << 16) | OPC_BC1,
    OPC_BC1TL    = (0x03 << 16) | OPC_BC1,
};

enum {
    OPC_BC1FANY2     = (0x00 << 16) | OPC_BC1ANY2,
    OPC_BC1TANY2     = (0x01 << 16) | OPC_BC1ANY2,
};

enum {
    OPC_BC1FANY4     = (0x00 << 16) | OPC_BC1ANY4,
    OPC_BC1TANY4     = (0x01 << 16) | OPC_BC1ANY4,
};

#define MASK_CP2(op)       MASK_OP_MAJOR(op) | (op & (0x1F << 21))

enum {
    OPC_MFC2    = (0x00 << 21) | OPC_CP2,
    OPC_DMFC2   = (0x01 << 21) | OPC_CP2,
    OPC_CFC2    = (0x02 << 21) | OPC_CP2,
    OPC_MFHC2   = (0x03 << 21) | OPC_CP2,
    OPC_MTC2    = (0x04 << 21) | OPC_CP2,
    OPC_DMTC2   = (0x05 << 21) | OPC_CP2,
    OPC_CTC2    = (0x06 << 21) | OPC_CP2,
    OPC_MTHC2   = (0x07 << 21) | OPC_CP2,
    OPC_BC2     = (0x08 << 21) | OPC_CP2,
};

#define MASK_CP3(op)       MASK_OP_MAJOR(op) | (op & 0x3F)

enum {
    OPC_LWXC1   = 0x00 | OPC_CP3,
    OPC_LDXC1   = 0x01 | OPC_CP3,
    OPC_LUXC1   = 0x05 | OPC_CP3,
    OPC_SWXC1   = 0x08 | OPC_CP3,
    OPC_SDXC1   = 0x09 | OPC_CP3,
    OPC_SUXC1   = 0x0D | OPC_CP3,
    OPC_PREFX   = 0x0F | OPC_CP3,
    OPC_ALNV_PS = 0x1E | OPC_CP3,
    OPC_MADD_S  = 0x20 | OPC_CP3,
    OPC_MADD_D  = 0x21 | OPC_CP3,
    OPC_MADD_PS = 0x26 | OPC_CP3,
    OPC_MSUB_S  = 0x28 | OPC_CP3,
    OPC_MSUB_D  = 0x29 | OPC_CP3,
    OPC_MSUB_PS = 0x2E | OPC_CP3,
    OPC_NMADD_S = 0x30 | OPC_CP3,
    OPC_NMADD_D = 0x31 | OPC_CP3,
    OPC_NMADD_PS= 0x36 | OPC_CP3,
    OPC_NMSUB_S = 0x38 | OPC_CP3,
    OPC_NMSUB_D = 0x39 | OPC_CP3,
    OPC_NMSUB_PS= 0x3E | OPC_CP3,
};


const unsigned char *regnames[] =
    { "r0", "at", "v0", "v1", "a0", "a1", "a2", "a3",
      "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
      "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
      "t8", "t9", "k0", "k1", "gp", "sp", "s8", "ra", };

/* Warning: no function for r0 register (hard wired to zero) */
#define GEN32(func, NAME)                        \
static GenOpFunc *NAME ## _table [32] = {        \
NULL,       NAME ## 1, NAME ## 2, NAME ## 3,     \
NAME ## 4,  NAME ## 5, NAME ## 6, NAME ## 7,     \
NAME ## 8,  NAME ## 9, NAME ## 10, NAME ## 11,   \
NAME ## 12, NAME ## 13, NAME ## 14, NAME ## 15,  \
NAME ## 16, NAME ## 17, NAME ## 18, NAME ## 19,  \
NAME ## 20, NAME ## 21, NAME ## 22, NAME ## 23,  \
NAME ## 24, NAME ## 25, NAME ## 26, NAME ## 27,  \
NAME ## 28, NAME ## 29, NAME ## 30, NAME ## 31,  \
};                                               \
static inline void func(int n)                   \
{                                                \
    NAME ## _table[n]();                         \
}

/* General purpose registers moves */
GEN32(gen_op_load_gpr_T0, gen_op_load_gpr_T0_gpr);
GEN32(gen_op_load_gpr_T1, gen_op_load_gpr_T1_gpr);
GEN32(gen_op_load_gpr_T2, gen_op_load_gpr_T2_gpr);

GEN32(gen_op_store_T0_gpr, gen_op_store_T0_gpr_gpr);
GEN32(gen_op_store_T1_gpr, gen_op_store_T1_gpr_gpr);

static const char *fregnames[] =
    { "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",
      "f8",  "f9",  "f10", "f11", "f12", "f13", "f14", "f15",
      "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
      "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31", };

#define FGEN32(func, NAME)                       \
static GenOpFunc *NAME ## _table [32] = {        \
NAME ## 0,  NAME ## 1,  NAME ## 2,  NAME ## 3,   \
NAME ## 4,  NAME ## 5,  NAME ## 6,  NAME ## 7,   \
NAME ## 8,  NAME ## 9,  NAME ## 10, NAME ## 11,  \
NAME ## 12, NAME ## 13, NAME ## 14, NAME ## 15,  \
NAME ## 16, NAME ## 17, NAME ## 18, NAME ## 19,  \
NAME ## 20, NAME ## 21, NAME ## 22, NAME ## 23,  \
NAME ## 24, NAME ## 25, NAME ## 26, NAME ## 27,  \
NAME ## 28, NAME ## 29, NAME ## 30, NAME ## 31,  \
};                                               \
static inline void func(int n)                   \
{                                                \
    NAME ## _table[n]();                         \
}

FGEN32(gen_op_load_fpr_WT0,  gen_op_load_fpr_WT0_fpr);
FGEN32(gen_op_store_fpr_WT0, gen_op_store_fpr_WT0_fpr);

FGEN32(gen_op_load_fpr_WT1,  gen_op_load_fpr_WT1_fpr);
FGEN32(gen_op_store_fpr_WT1, gen_op_store_fpr_WT1_fpr);

FGEN32(gen_op_load_fpr_WT2,  gen_op_load_fpr_WT2_fpr);
FGEN32(gen_op_store_fpr_WT2, gen_op_store_fpr_WT2_fpr);

FGEN32(gen_op_load_fpr_DT0,  gen_op_load_fpr_DT0_fpr);
FGEN32(gen_op_store_fpr_DT0, gen_op_store_fpr_DT0_fpr);

FGEN32(gen_op_load_fpr_DT1,  gen_op_load_fpr_DT1_fpr);
FGEN32(gen_op_store_fpr_DT1, gen_op_store_fpr_DT1_fpr);

FGEN32(gen_op_load_fpr_DT2,  gen_op_load_fpr_DT2_fpr);
FGEN32(gen_op_store_fpr_DT2, gen_op_store_fpr_DT2_fpr);

FGEN32(gen_op_load_fpr_WTH0,  gen_op_load_fpr_WTH0_fpr);
FGEN32(gen_op_store_fpr_WTH0, gen_op_store_fpr_WTH0_fpr);

FGEN32(gen_op_load_fpr_WTH1,  gen_op_load_fpr_WTH1_fpr);
FGEN32(gen_op_store_fpr_WTH1, gen_op_store_fpr_WTH1_fpr);

FGEN32(gen_op_load_fpr_WTH2,  gen_op_load_fpr_WTH2_fpr);
FGEN32(gen_op_store_fpr_WTH2, gen_op_store_fpr_WTH2_fpr);

#define FOP_CONDS(type, fmt)                                            \
static GenOpFunc1 * gen_op_cmp ## type ## _ ## fmt ## _table[16] = {    \
    gen_op_cmp ## type ## _ ## fmt ## _f,                               \
    gen_op_cmp ## type ## _ ## fmt ## _un,                              \
    gen_op_cmp ## type ## _ ## fmt ## _eq,                              \
    gen_op_cmp ## type ## _ ## fmt ## _ueq,                             \
    gen_op_cmp ## type ## _ ## fmt ## _olt,                             \
    gen_op_cmp ## type ## _ ## fmt ## _ult,                             \
    gen_op_cmp ## type ## _ ## fmt ## _ole,                             \
    gen_op_cmp ## type ## _ ## fmt ## _ule,                             \
    gen_op_cmp ## type ## _ ## fmt ## _sf,                              \
    gen_op_cmp ## type ## _ ## fmt ## _ngle,                            \
    gen_op_cmp ## type ## _ ## fmt ## _seq,                             \
    gen_op_cmp ## type ## _ ## fmt ## _ngl,                             \
    gen_op_cmp ## type ## _ ## fmt ## _lt,                              \
    gen_op_cmp ## type ## _ ## fmt ## _nge,                             \
    gen_op_cmp ## type ## _ ## fmt ## _le,                              \
    gen_op_cmp ## type ## _ ## fmt ## _ngt,                             \
};                                                                      \
static inline void gen_cmp ## type ## _ ## fmt(int n, long cc)          \
{                                                                       \
    gen_op_cmp ## type ## _ ## fmt ## _table[n](cc);                    \
}

FOP_CONDS(, d)
FOP_CONDS(abs, d)
FOP_CONDS(, s)
FOP_CONDS(abs, s)
FOP_CONDS(, ps)
FOP_CONDS(abs, ps)

typedef struct DisasContext {
    struct TranslationBlock *tb;
    target_ulong pc, saved_pc;
    uint32_t opcode;
    uint32_t fp_status;
    /* Routine used to access memory */
    int mem_idx;
    uint32_t hflags, saved_hflags;
    int bstate;
    target_ulong btarget;
} DisasContext;

enum {
    BS_NONE     = 0, /* We go out of the TB without reaching a branch or an
                      * exception condition
                      */
    BS_STOP     = 1, /* We want to stop translation for any reason */
    BS_BRANCH   = 2, /* We reached a branch condition     */
    BS_EXCP     = 3, /* We reached an exception condition */
};

#ifdef MIPS_DEBUG_DISAS
#define MIPS_DEBUG(fmt, args...)                                              \
do {                                                                          \
    if (loglevel & CPU_LOG_TB_IN_ASM) {                                       \
        fprintf(logfile, TARGET_FMT_lx ": %08x " fmt "\n",                    \
                ctx->pc, ctx->opcode , ##args);                               \
    }                                                                         \
} while (0)
#else
#define MIPS_DEBUG(fmt, args...) do { } while(0)
#endif

#define MIPS_INVAL(op)                                                        \
do {                                                                          \
    MIPS_DEBUG("Invalid %s %03x %03x %03x", op, ctx->opcode >> 26,            \
               ctx->opcode & 0x3F, ((ctx->opcode >> 16) & 0x1F));             \
} while (0)

#define GEN_LOAD_REG_TN(Tn, Rn)                                               \
do {                                                                          \
    if (Rn == 0) {                                                            \
        glue(gen_op_reset_, Tn)();                                            \
    } else {                                                                  \
        glue(gen_op_load_gpr_, Tn)(Rn);                                       \
    }                                                                         \
} while (0)

#ifdef TARGET_MIPS64
#define GEN_LOAD_IMM_TN(Tn, Imm)                                              \
do {                                                                          \
    if (Imm == 0) {                                                           \
        glue(gen_op_reset_, Tn)();                                            \
    } else if ((int32_t)Imm == Imm) {                                         \
        glue(gen_op_set_, Tn)(Imm);                                           \
    } else {                                                                  \
        glue(gen_op_set64_, Tn)(((uint64_t)Imm) >> 32, (uint32_t)Imm);        \
    }                                                                         \
} while (0)
#else
#define GEN_LOAD_IMM_TN(Tn, Imm)                                              \
do {                                                                          \
    if (Imm == 0) {                                                           \
        glue(gen_op_reset_, Tn)();                                            \
    } else {                                                                  \
        glue(gen_op_set_, Tn)(Imm);                                           \
    }                                                                         \
} while (0)
#endif

#define GEN_STORE_TN_REG(Rn, Tn)                                              \
do {                                                                          \
    if (Rn != 0) {                                                            \
        glue(glue(gen_op_store_, Tn),_gpr)(Rn);                               \
    }                                                                         \
} while (0)

#define GEN_LOAD_FREG_FTN(FTn, Fn)                                            \
do {                                                                          \
    glue(gen_op_load_fpr_, FTn)(Fn);                                          \
} while (0)

#define GEN_STORE_FTN_FREG(Fn, FTn)                                           \
do {                                                                          \
    glue(gen_op_store_fpr_, FTn)(Fn);                                         \
} while (0)

static inline void gen_save_pc(target_ulong pc)
{
#ifdef TARGET_MIPS64
    if (pc == (int32_t)pc) {
        gen_op_save_pc(pc);
    } else {
        gen_op_save_pc64(pc >> 32, (uint32_t)pc);
    }
#else
    gen_op_save_pc(pc);
#endif
}

static inline void gen_save_btarget(target_ulong btarget)
{
#ifdef TARGET_MIPS64
    if (btarget == (int32_t)btarget) {
        gen_op_save_btarget(btarget);
    } else {
        gen_op_save_btarget64(btarget >> 32, (uint32_t)btarget);
    }
#else
    gen_op_save_btarget(btarget);
#endif
}

static inline void save_cpu_state (DisasContext *ctx, int do_save_pc)
{
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
            fprintf(logfile, "hflags %08x saved %08x\n",
                    ctx->hflags, ctx->saved_hflags);
    }
#endif
    if (do_save_pc && ctx->pc != ctx->saved_pc) {
        gen_save_pc(ctx->pc);
        ctx->saved_pc = ctx->pc;
    }
    if (ctx->hflags != ctx->saved_hflags) {
        gen_op_save_state(ctx->hflags);
        ctx->saved_hflags = ctx->hflags;
        switch (ctx->hflags & MIPS_HFLAG_BMASK) {
        case MIPS_HFLAG_BR:
            gen_op_save_breg_target();
            break;
        case MIPS_HFLAG_BC:
            gen_op_save_bcond();
            /* fall through */
        case MIPS_HFLAG_BL:
            /* bcond was already saved by the BL insn */
            /* fall through */
        case MIPS_HFLAG_B:
            gen_save_btarget(ctx->btarget);
            break;
        }
    }
}

static inline void restore_cpu_state (CPUState *env, DisasContext *ctx)
{
    ctx->saved_hflags = ctx->hflags;
    switch (ctx->hflags & MIPS_HFLAG_BMASK) {
    case MIPS_HFLAG_BR:
        gen_op_restore_breg_target();
        break;
    case MIPS_HFLAG_B:
        ctx->btarget = env->btarget;
        break;
    case MIPS_HFLAG_BC:
    case MIPS_HFLAG_BL:
        ctx->btarget = env->btarget;
        gen_op_restore_bcond();
        break;
    }
}

static inline void generate_exception_err (DisasContext *ctx, int excp, int err)
{
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM)
            fprintf(logfile, "%s: raise exception %d\n", __func__, excp);
#endif
    save_cpu_state(ctx, 1);
    if (err == 0)
        gen_op_raise_exception(excp);
    else
        gen_op_raise_exception_err(excp, err);
    ctx->bstate = BS_EXCP;
}

static inline void generate_exception (DisasContext *ctx, int excp)
{
    generate_exception_err (ctx, excp, 0);
}

static inline void check_cp1_enabled(DisasContext *ctx)
{
    if (!(ctx->hflags & MIPS_HFLAG_FPU))
        generate_exception_err(ctx, EXCP_CpU, 1);
}

static inline void check_cp1_64bitmode(DisasContext *ctx)
{
    if (!(ctx->hflags & MIPS_HFLAG_F64))
        generate_exception(ctx, EXCP_RI);
}

/*
 * Verify if floating point register is valid; an operation is not defined
 * if bit 0 of any register specification is set and the FR bit in the
 * Status register equals zero, since the register numbers specify an
 * even-odd pair of adjacent coprocessor general registers. When the FR bit
 * in the Status register equals one, both even and odd register numbers
 * are valid. This limitation exists only for 64 bit wide (d,l,ps) registers.
 *
 * Multiple 64 bit wide registers can be checked by calling
 * gen_op_cp1_registers(freg1 | freg2 | ... | fregN);
 */
void check_cp1_registers(DisasContext *ctx, int regs)
{
    if (!(ctx->hflags & MIPS_HFLAG_F64) && (regs & 1))
        generate_exception(ctx, EXCP_RI);
}

/* This code generates a "reserved instruction" exception if the
   CPU is not a MIPS R2 (or higher) CPU. */
static inline void check_mips_r2(CPUState *env, DisasContext *ctx)
{
    if ((env->CP0_Config0 & (0x7 << CP0C0_AR)) < (1 << CP0C0_AR))
        generate_exception(ctx, EXCP_RI);
}

#if defined(CONFIG_USER_ONLY)
#define op_ldst(name)        gen_op_##name##_raw()
#define OP_LD_TABLE(width)
#define OP_ST_TABLE(width)
#else
#define op_ldst(name)        (*gen_op_##name[ctx->mem_idx])()
#define OP_LD_TABLE(width)                                                    \
static GenOpFunc *gen_op_l##width[] = {                                       \
    &gen_op_l##width##_user,                                                  \
    &gen_op_l##width##_kernel,                                                \
}
#define OP_ST_TABLE(width)                                                    \
static GenOpFunc *gen_op_s##width[] = {                                       \
    &gen_op_s##width##_user,                                                  \
    &gen_op_s##width##_kernel,                                                \
}
#endif

#ifdef TARGET_MIPS64
OP_LD_TABLE(d);
OP_LD_TABLE(dl);
OP_LD_TABLE(dr);
OP_ST_TABLE(d);
OP_ST_TABLE(dl);
OP_ST_TABLE(dr);
OP_LD_TABLE(ld);
OP_ST_TABLE(cd);
OP_LD_TABLE(wu);
#endif
OP_LD_TABLE(w);
OP_LD_TABLE(wl);
OP_LD_TABLE(wr);
OP_ST_TABLE(w);
OP_ST_TABLE(wl);
OP_ST_TABLE(wr);
OP_LD_TABLE(h);
OP_LD_TABLE(hu);
OP_ST_TABLE(h);
OP_LD_TABLE(b);
OP_LD_TABLE(bu);
OP_ST_TABLE(b);
OP_LD_TABLE(l);
OP_ST_TABLE(c);
OP_LD_TABLE(wc1);
OP_ST_TABLE(wc1);
OP_LD_TABLE(dc1);
OP_ST_TABLE(dc1);
OP_LD_TABLE(uxc1);
OP_ST_TABLE(uxc1);

/* Load and store */
static void gen_ldst (DisasContext *ctx, uint32_t opc, int rt,
                      int base, int16_t offset)
{
    const char *opn = "ldst";

    if (base == 0) {
        GEN_LOAD_IMM_TN(T0, offset);
    } else if (offset == 0) {
        gen_op_load_gpr_T0(base);
    } else {
        gen_op_load_gpr_T0(base);
        gen_op_set_T1(offset);
        gen_op_addr_add();
    }
    /* Don't do NOP if destination is zero: we must perform the actual
     * memory access
     */
    switch (opc) {
#ifdef TARGET_MIPS64
    case OPC_LWU:
        op_ldst(lwu);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lwu";
        break;
    case OPC_LD:
        op_ldst(ld);
        GEN_STORE_TN_REG(rt, T0);
        opn = "ld";
        break;
    case OPC_LLD:
        op_ldst(lld);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lld";
        break;
    case OPC_SD:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sd);
        opn = "sd";
        break;
    case OPC_SCD:
        save_cpu_state(ctx, 1);
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(scd);
        GEN_STORE_TN_REG(rt, T0);
        opn = "scd";
        break;
    case OPC_LDL:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(ldl);
        GEN_STORE_TN_REG(rt, T0);
        opn = "ldl";
        break;
    case OPC_SDL:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sdl);
        opn = "sdl";
        break;
    case OPC_LDR:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(ldr);
        GEN_STORE_TN_REG(rt, T0);
        opn = "ldr";
        break;
    case OPC_SDR:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sdr);
        opn = "sdr";
        break;
#endif
    case OPC_LW:
        op_ldst(lw);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lw";
        break;
    case OPC_SW:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sw);
        opn = "sw";
        break;
    case OPC_LH:
        op_ldst(lh);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lh";
        break;
    case OPC_SH:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sh);
        opn = "sh";
        break;
    case OPC_LHU:
        op_ldst(lhu);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lhu";
        break;
    case OPC_LB:
        op_ldst(lb);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lb";
        break;
    case OPC_SB:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sb);
        opn = "sb";
        break;
    case OPC_LBU:
        op_ldst(lbu);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lbu";
        break;
    case OPC_LWL:
	GEN_LOAD_REG_TN(T1, rt);
        op_ldst(lwl);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lwl";
        break;
    case OPC_SWL:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(swl);
        opn = "swr";
        break;
    case OPC_LWR:
	GEN_LOAD_REG_TN(T1, rt);
        op_ldst(lwr);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lwr";
        break;
    case OPC_SWR:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(swr);
        opn = "swr";
        break;
    case OPC_LL:
        op_ldst(ll);
        GEN_STORE_TN_REG(rt, T0);
        opn = "ll";
        break;
    case OPC_SC:
        save_cpu_state(ctx, 1);
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sc);
        GEN_STORE_TN_REG(rt, T0);
        opn = "sc";
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s, %d(%s)", opn, regnames[rt], offset, regnames[base]);
}

/* Load and store */
static void gen_flt_ldst (DisasContext *ctx, uint32_t opc, int ft,
                      int base, int16_t offset)
{
    const char *opn = "flt_ldst";

    if (base == 0) {
        GEN_LOAD_IMM_TN(T0, offset);
    } else if (offset == 0) {
        gen_op_load_gpr_T0(base);
    } else {
        gen_op_load_gpr_T0(base);
        gen_op_set_T1(offset);
        gen_op_addr_add();
    }
    /* Don't do NOP if destination is zero: we must perform the actual
     * memory access
     */
    switch (opc) {
    case OPC_LWC1:
        op_ldst(lwc1);
        GEN_STORE_FTN_FREG(ft, WT0);
        opn = "lwc1";
        break;
    case OPC_SWC1:
        GEN_LOAD_FREG_FTN(WT0, ft);
        op_ldst(swc1);
        opn = "swc1";
        break;
    case OPC_LDC1:
        op_ldst(ldc1);
        GEN_STORE_FTN_FREG(ft, DT0);
        opn = "ldc1";
        break;
    case OPC_SDC1:
        GEN_LOAD_FREG_FTN(DT0, ft);
        op_ldst(sdc1);
        opn = "sdc1";
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s, %d(%s)", opn, fregnames[ft], offset, regnames[base]);
}

/* Arithmetic with immediate operand */
static void gen_arith_imm (DisasContext *ctx, uint32_t opc, int rt,
                           int rs, int16_t imm)
{
    target_ulong uimm;
    const char *opn = "imm arith";

    if (rt == 0 && opc != OPC_ADDI && opc != OPC_DADDI) {
        /* if no destination, treat it as a NOP 
         * For addi, we must generate the overflow exception when needed.
         */
        MIPS_DEBUG("NOP");
        return;
    }
    uimm = (uint16_t)imm;
    switch (opc) {
    case OPC_ADDI:
    case OPC_ADDIU:
#ifdef TARGET_MIPS64
    case OPC_DADDI:
    case OPC_DADDIU:
#endif
    case OPC_SLTI:
    case OPC_SLTIU:
        uimm = (target_long)imm; /* Sign extend to 32/64 bits */
        /* Fall through. */
    case OPC_ANDI:
    case OPC_ORI:
    case OPC_XORI:
        GEN_LOAD_REG_TN(T0, rs);
        GEN_LOAD_IMM_TN(T1, uimm);
        break;
    case OPC_LUI:
        GEN_LOAD_IMM_TN(T0, imm << 16);
        break;
    case OPC_SLL:
    case OPC_SRA:
    case OPC_SRL:
#ifdef TARGET_MIPS64
    case OPC_DSLL:
    case OPC_DSRA:
    case OPC_DSRL:
    case OPC_DSLL32:
    case OPC_DSRA32:
    case OPC_DSRL32:
#endif
        uimm &= 0x1f;
        GEN_LOAD_REG_TN(T0, rs);
        GEN_LOAD_IMM_TN(T1, uimm);
        break;
    }
    switch (opc) {
    case OPC_ADDI:
        save_cpu_state(ctx, 1);
        gen_op_addo();
        opn = "addi";
        break;
    case OPC_ADDIU:
        gen_op_add();
        opn = "addiu";
        break;
#ifdef TARGET_MIPS64
    case OPC_DADDI:
        save_cpu_state(ctx, 1);
        gen_op_daddo();
        opn = "daddi";
        break;
    case OPC_DADDIU:
        gen_op_dadd();
        opn = "daddiu";
        break;
#endif
    case OPC_SLTI:
        gen_op_lt();
        opn = "slti";
        break;
    case OPC_SLTIU:
        gen_op_ltu();
        opn = "sltiu";
        break;
    case OPC_ANDI:
        gen_op_and();
        opn = "andi";
        break;
    case OPC_ORI:
        gen_op_or();
        opn = "ori";
        break;
    case OPC_XORI:
        gen_op_xor();
        opn = "xori";
        break;
    case OPC_LUI:
        opn = "lui";
        break;
    case OPC_SLL:
        gen_op_sll();
        opn = "sll";
        break;
    case OPC_SRA:
        gen_op_sra();
        opn = "sra";
        break;
    case OPC_SRL:
        switch ((ctx->opcode >> 21) & 0x1f) {
        case 0:
            gen_op_srl();
            opn = "srl";
            break;
        case 1:
            gen_op_rotr();
            opn = "rotr";
            break;
        default:
            MIPS_INVAL("invalid srl flag");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
#ifdef TARGET_MIPS64
    case OPC_DSLL:
        gen_op_dsll();
        opn = "dsll";
        break;
    case OPC_DSRA:
        gen_op_dsra();
        opn = "dsra";
        break;
    case OPC_DSRL:
        switch ((ctx->opcode >> 21) & 0x1f) {
        case 0:
            gen_op_dsrl();
            opn = "dsrl";
            break;
        case 1:
            gen_op_drotr();
            opn = "drotr";
            break;
        default:
            MIPS_INVAL("invalid dsrl flag");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_DSLL32:
        gen_op_dsll32();
        opn = "dsll32";
        break;
    case OPC_DSRA32:
        gen_op_dsra32();
        opn = "dsra32";
        break;
    case OPC_DSRL32:
        switch ((ctx->opcode >> 21) & 0x1f) {
        case 0:
            gen_op_dsrl32();
            opn = "dsrl32";
            break;
        case 1:
            gen_op_drotr32();
            opn = "drotr32";
            break;
        default:
            MIPS_INVAL("invalid dsrl32 flag");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
#endif
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        return;
    }
    GEN_STORE_TN_REG(rt, T0);
    MIPS_DEBUG("%s %s, %s, " TARGET_FMT_lx, opn, regnames[rt], regnames[rs], uimm);
}

/* Arithmetic */
static void gen_arith (DisasContext *ctx, uint32_t opc,
                       int rd, int rs, int rt)
{
    const char *opn = "arith";

    if (rd == 0 && opc != OPC_ADD && opc != OPC_SUB
       && opc != OPC_DADD && opc != OPC_DSUB) {
        /* if no destination, treat it as a NOP 
         * For add & sub, we must generate the overflow exception when needed.
         */
        MIPS_DEBUG("NOP");
        return;
    }
    GEN_LOAD_REG_TN(T0, rs);
    GEN_LOAD_REG_TN(T1, rt);
    switch (opc) {
    case OPC_ADD:
        save_cpu_state(ctx, 1);
        gen_op_addo();
        opn = "add";
        break;
    case OPC_ADDU:
        gen_op_add();
        opn = "addu";
        break;
    case OPC_SUB:
        save_cpu_state(ctx, 1);
        gen_op_subo();
        opn = "sub";
        break;
    case OPC_SUBU:
        gen_op_sub();
        opn = "subu";
        break;
#ifdef TARGET_MIPS64
    case OPC_DADD:
        save_cpu_state(ctx, 1);
        gen_op_daddo();
        opn = "dadd";
        break;
    case OPC_DADDU:
        gen_op_dadd();
        opn = "daddu";
        break;
    case OPC_DSUB:
        save_cpu_state(ctx, 1);
        gen_op_dsubo();
        opn = "dsub";
        break;
    case OPC_DSUBU:
        gen_op_dsub();
        opn = "dsubu";
        break;
#endif
    case OPC_SLT:
        gen_op_lt();
        opn = "slt";
        break;
    case OPC_SLTU:
        gen_op_ltu();
        opn = "sltu";
        break;
    case OPC_AND:
        gen_op_and();
        opn = "and";
        break;
    case OPC_NOR:
        gen_op_nor();
        opn = "nor";
        break;
    case OPC_OR:
        gen_op_or();
        opn = "or";
        break;
    case OPC_XOR:
        gen_op_xor();
        opn = "xor";
        break;
    case OPC_MUL:
        gen_op_mul();
        opn = "mul";
        break;
    case OPC_MOVN:
        gen_op_movn(rd);
        opn = "movn";
        goto print;
    case OPC_MOVZ:
        gen_op_movz(rd);
        opn = "movz";
        goto print;
    case OPC_SLLV:
        gen_op_sllv();
        opn = "sllv";
        break;
    case OPC_SRAV:
        gen_op_srav();
        opn = "srav";
        break;
    case OPC_SRLV:
        switch ((ctx->opcode >> 6) & 0x1f) {
        case 0:
            gen_op_srlv();
            opn = "srlv";
            break;
        case 1:
            gen_op_rotrv();
            opn = "rotrv";
            break;
        default:
            MIPS_INVAL("invalid srlv flag");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
#ifdef TARGET_MIPS64
    case OPC_DSLLV:
        gen_op_dsllv();
        opn = "dsllv";
        break;
    case OPC_DSRAV:
        gen_op_dsrav();
        opn = "dsrav";
        break;
    case OPC_DSRLV:
        switch ((ctx->opcode >> 6) & 0x1f) {
        case 0:
            gen_op_dsrlv();
            opn = "dsrlv";
            break;
        case 1:
            gen_op_drotrv();
            opn = "drotrv";
            break;
        default:
            MIPS_INVAL("invalid dsrlv flag");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
#endif
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        return;
    }
    GEN_STORE_TN_REG(rd, T0);
 print:
    MIPS_DEBUG("%s %s, %s, %s", opn, regnames[rd], regnames[rs], regnames[rt]);
}

/* Arithmetic on HI/LO registers */
static void gen_HILO (DisasContext *ctx, uint32_t opc, int reg)
{
    const char *opn = "hilo";

    if (reg == 0 && (opc == OPC_MFHI || opc == OPC_MFLO)) {
        /* Treat as a NOP */
        MIPS_DEBUG("NOP");
        return;
    }
    switch (opc) {
    case OPC_MFHI:
        gen_op_load_HI();
        GEN_STORE_TN_REG(reg, T0);
        opn = "mfhi";
        break;
    case OPC_MFLO:
        gen_op_load_LO();
        GEN_STORE_TN_REG(reg, T0);
        opn = "mflo";
        break;
    case OPC_MTHI:
        GEN_LOAD_REG_TN(T0, reg);
        gen_op_store_HI();
        opn = "mthi";
        break;
    case OPC_MTLO:
        GEN_LOAD_REG_TN(T0, reg);
        gen_op_store_LO();
        opn = "mtlo";
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s", opn, regnames[reg]);
}

static void gen_muldiv (DisasContext *ctx, uint32_t opc,
                        int rs, int rt)
{
    const char *opn = "mul/div";

    GEN_LOAD_REG_TN(T0, rs);
    GEN_LOAD_REG_TN(T1, rt);
    switch (opc) {
    case OPC_DIV:
        gen_op_div();
        opn = "div";
        break;
    case OPC_DIVU:
        gen_op_divu();
        opn = "divu";
        break;
    case OPC_MULT:
        gen_op_mult();
        opn = "mult";
        break;
    case OPC_MULTU:
        gen_op_multu();
        opn = "multu";
        break;
#ifdef TARGET_MIPS64
    case OPC_DDIV:
        gen_op_ddiv();
        opn = "ddiv";
        break;
    case OPC_DDIVU:
        gen_op_ddivu();
        opn = "ddivu";
        break;
    case OPC_DMULT:
        gen_op_dmult();
        opn = "dmult";
        break;
    case OPC_DMULTU:
        gen_op_dmultu();
        opn = "dmultu";
        break;
#endif
    case OPC_MADD:
        gen_op_madd();
        opn = "madd";
        break;
    case OPC_MADDU:
        gen_op_maddu();
        opn = "maddu";
        break;
    case OPC_MSUB:
        gen_op_msub();
        opn = "msub";
        break;
    case OPC_MSUBU:
        gen_op_msubu();
        opn = "msubu";
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s %s", opn, regnames[rs], regnames[rt]);
}

static void gen_cl (DisasContext *ctx, uint32_t opc,
                    int rd, int rs)
{
    const char *opn = "CLx";
    if (rd == 0) {
        /* Treat as a NOP */
        MIPS_DEBUG("NOP");
        return;
    }
    GEN_LOAD_REG_TN(T0, rs);
    switch (opc) {
    case OPC_CLO:
        gen_op_clo();
        opn = "clo";
        break;
    case OPC_CLZ:
        gen_op_clz();
        opn = "clz";
        break;
#ifdef TARGET_MIPS64
    case OPC_DCLO:
        gen_op_dclo();
        opn = "dclo";
        break;
    case OPC_DCLZ:
        gen_op_dclz();
        opn = "dclz";
        break;
#endif
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        return;
    }
    gen_op_store_T0_gpr(rd);
    MIPS_DEBUG("%s %s, %s", opn, regnames[rd], regnames[rs]);
}

/* Traps */
static void gen_trap (DisasContext *ctx, uint32_t opc,
                      int rs, int rt, int16_t imm)
{
    int cond;

    cond = 0;
    /* Load needed operands */
    switch (opc) {
    case OPC_TEQ:
    case OPC_TGE:
    case OPC_TGEU:
    case OPC_TLT:
    case OPC_TLTU:
    case OPC_TNE:
        /* Compare two registers */
        if (rs != rt) {
            GEN_LOAD_REG_TN(T0, rs);
            GEN_LOAD_REG_TN(T1, rt);
            cond = 1;
        }
        break;
    case OPC_TEQI:
    case OPC_TGEI:
    case OPC_TGEIU:
    case OPC_TLTI:
    case OPC_TLTIU:
    case OPC_TNEI:
        /* Compare register to immediate */
        if (rs != 0 || imm != 0) {
            GEN_LOAD_REG_TN(T0, rs);
            GEN_LOAD_IMM_TN(T1, (int32_t)imm);
            cond = 1;
        }
        break;
    }
    if (cond == 0) {
        switch (opc) {
        case OPC_TEQ:   /* rs == rs */
        case OPC_TEQI:  /* r0 == 0  */
        case OPC_TGE:   /* rs >= rs */
        case OPC_TGEI:  /* r0 >= 0  */
        case OPC_TGEU:  /* rs >= rs unsigned */
        case OPC_TGEIU: /* r0 >= 0  unsigned */
            /* Always trap */
            gen_op_set_T0(1);
            break;
        case OPC_TLT:   /* rs < rs           */
        case OPC_TLTI:  /* r0 < 0            */
        case OPC_TLTU:  /* rs < rs unsigned  */
        case OPC_TLTIU: /* r0 < 0  unsigned  */
        case OPC_TNE:   /* rs != rs          */
        case OPC_TNEI:  /* r0 != 0           */
            /* Never trap: treat as NOP */
            return;
        default:
            MIPS_INVAL("trap");
            generate_exception(ctx, EXCP_RI);
            return;
        }
    } else {
        switch (opc) {
        case OPC_TEQ:
        case OPC_TEQI:
            gen_op_eq();
            break;
        case OPC_TGE:
        case OPC_TGEI:
            gen_op_ge();
            break;
        case OPC_TGEU:
        case OPC_TGEIU:
            gen_op_geu();
            break;
        case OPC_TLT:
        case OPC_TLTI:
            gen_op_lt();
            break;
        case OPC_TLTU:
        case OPC_TLTIU:
            gen_op_ltu();
            break;
        case OPC_TNE:
        case OPC_TNEI:
            gen_op_ne();
            break;
        default:
            MIPS_INVAL("trap");
            generate_exception(ctx, EXCP_RI);
            return;
        }
    }
    save_cpu_state(ctx, 1);
    gen_op_trap();
    ctx->bstate = BS_STOP;
}

static inline void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    TranslationBlock *tb;
    tb = ctx->tb;
    if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)) {
        if (n == 0)
            gen_op_goto_tb0(TBPARAM(tb));
        else
            gen_op_goto_tb1(TBPARAM(tb));
        gen_save_pc(dest);
        gen_op_set_T0((long)tb + n);
    } else {
        gen_save_pc(dest);
        gen_op_reset_T0();
    }
    gen_op_exit_tb();
}

/* Branches (before delay slot) */
static void gen_compute_branch (DisasContext *ctx, uint32_t opc,
                                int rs, int rt, int32_t offset)
{
    target_ulong btarget = -1;
    int blink = 0;
    int bcond = 0;

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
#ifdef MIPS_DEBUG_DISAS
        if (loglevel & CPU_LOG_TB_IN_ASM) {
            fprintf(logfile,
                    "Branch in delay slot at PC 0x" TARGET_FMT_lx "\n",
                    ctx->pc);
	}
#endif
        generate_exception(ctx, EXCP_RI);
        return;
    }

    /* Load needed operands */
    switch (opc) {
    case OPC_BEQ:
    case OPC_BEQL:
    case OPC_BNE:
    case OPC_BNEL:
        /* Compare two registers */
        if (rs != rt) {
            GEN_LOAD_REG_TN(T0, rs);
            GEN_LOAD_REG_TN(T1, rt);
            bcond = 1;
        }
        btarget = ctx->pc + 4 + offset;
        break;
    case OPC_BGEZ:
    case OPC_BGEZAL:
    case OPC_BGEZALL:
    case OPC_BGEZL:
    case OPC_BGTZ:
    case OPC_BGTZL:
    case OPC_BLEZ:
    case OPC_BLEZL:
    case OPC_BLTZ:
    case OPC_BLTZAL:
    case OPC_BLTZALL:
    case OPC_BLTZL:
        /* Compare to zero */
        if (rs != 0) {
            gen_op_load_gpr_T0(rs);
            bcond = 1;
        }
        btarget = ctx->pc + 4 + offset;
        break;
    case OPC_J:
    case OPC_JAL:
        /* Jump to immediate */
        btarget = ((ctx->pc + 4) & (int32_t)0xF0000000) | (uint32_t)offset;
        break;
    case OPC_JR:
    case OPC_JALR:
        /* Jump to register */
        if (offset != 0 && offset != 16) {
            /* Hint = 0 is JR/JALR, hint 16 is JR.HB/JALR.HB, the
               others are reserved. */
            MIPS_INVAL("jump hint");
            generate_exception(ctx, EXCP_RI);
            return;
        }
        GEN_LOAD_REG_TN(T2, rs);
        break;
    default:
        MIPS_INVAL("branch/jump");
        generate_exception(ctx, EXCP_RI);
        return;
    }
    if (bcond == 0) {
        /* No condition to be computed */
        switch (opc) {
        case OPC_BEQ:     /* rx == rx        */
        case OPC_BEQL:    /* rx == rx likely */
        case OPC_BGEZ:    /* 0 >= 0          */
        case OPC_BGEZL:   /* 0 >= 0 likely   */
        case OPC_BLEZ:    /* 0 <= 0          */
        case OPC_BLEZL:   /* 0 <= 0 likely   */
            /* Always take */
            ctx->hflags |= MIPS_HFLAG_B;
            MIPS_DEBUG("balways");
            break;
        case OPC_BGEZAL:  /* 0 >= 0          */
        case OPC_BGEZALL: /* 0 >= 0 likely   */
            /* Always take and link */
            blink = 31;
            ctx->hflags |= MIPS_HFLAG_B;
            MIPS_DEBUG("balways and link");
            break;
        case OPC_BNE:     /* rx != rx        */
        case OPC_BGTZ:    /* 0 > 0           */
        case OPC_BLTZ:    /* 0 < 0           */
            /* Treated as NOP */
            MIPS_DEBUG("bnever (NOP)");
            return;
        case OPC_BLTZAL:  /* 0 < 0           */
            GEN_LOAD_IMM_TN(T0, ctx->pc + 8);
            gen_op_store_T0_gpr(31);
            MIPS_DEBUG("bnever and link");
            return;
        case OPC_BLTZALL: /* 0 < 0 likely */
            GEN_LOAD_IMM_TN(T0, ctx->pc + 8);
            gen_op_store_T0_gpr(31);
            /* Skip the instruction in the delay slot */
            MIPS_DEBUG("bnever, link and skip");
            ctx->pc += 4;
            return;
        case OPC_BNEL:    /* rx != rx likely */
        case OPC_BGTZL:   /* 0 > 0 likely */
        case OPC_BLTZL:   /* 0 < 0 likely */
            /* Skip the instruction in the delay slot */
            MIPS_DEBUG("bnever and skip");
            ctx->pc += 4;
            return;
        case OPC_J:
            ctx->hflags |= MIPS_HFLAG_B;
            MIPS_DEBUG("j " TARGET_FMT_lx, btarget);
            break;
        case OPC_JAL:
            blink = 31;
            ctx->hflags |= MIPS_HFLAG_B;
            MIPS_DEBUG("jal " TARGET_FMT_lx, btarget);
            break;
        case OPC_JR:
            ctx->hflags |= MIPS_HFLAG_BR;
            MIPS_DEBUG("jr %s", regnames[rs]);
            break;
        case OPC_JALR:
            blink = rt;
            ctx->hflags |= MIPS_HFLAG_BR;
            MIPS_DEBUG("jalr %s, %s", regnames[rt], regnames[rs]);
            break;
        default:
            MIPS_INVAL("branch/jump");
            generate_exception(ctx, EXCP_RI);
            return;
        }
    } else {
        switch (opc) {
        case OPC_BEQ:
            gen_op_eq();
            MIPS_DEBUG("beq %s, %s, " TARGET_FMT_lx,
                       regnames[rs], regnames[rt], btarget);
            goto not_likely;
        case OPC_BEQL:
            gen_op_eq();
            MIPS_DEBUG("beql %s, %s, " TARGET_FMT_lx,
                       regnames[rs], regnames[rt], btarget);
            goto likely;
        case OPC_BNE:
            gen_op_ne();
            MIPS_DEBUG("bne %s, %s, " TARGET_FMT_lx,
                       regnames[rs], regnames[rt], btarget);
            goto not_likely;
        case OPC_BNEL:
            gen_op_ne();
            MIPS_DEBUG("bnel %s, %s, " TARGET_FMT_lx,
                       regnames[rs], regnames[rt], btarget);
            goto likely;
        case OPC_BGEZ:
            gen_op_gez();
            MIPS_DEBUG("bgez %s, " TARGET_FMT_lx, regnames[rs], btarget);
            goto not_likely;
        case OPC_BGEZL:
            gen_op_gez();
            MIPS_DEBUG("bgezl %s, " TARGET_FMT_lx, regnames[rs], btarget);
            goto likely;
        case OPC_BGEZAL:
            gen_op_gez();
            MIPS_DEBUG("bgezal %s, " TARGET_FMT_lx, regnames[rs], btarget);
            blink = 31;
            goto not_likely;
        case OPC_BGEZALL:
            gen_op_gez();
            blink = 31;
            MIPS_DEBUG("bgezall %s, " TARGET_FMT_lx, regnames[rs], btarget);
            goto likely;
        case OPC_BGTZ:
            gen_op_gtz();
            MIPS_DEBUG("bgtz %s, " TARGET_FMT_lx, regnames[rs], btarget);
            goto not_likely;
        case OPC_BGTZL:
            gen_op_gtz();
            MIPS_DEBUG("bgtzl %s, " TARGET_FMT_lx, regnames[rs], btarget);
            goto likely;
        case OPC_BLEZ:
            gen_op_lez();
            MIPS_DEBUG("blez %s, " TARGET_FMT_lx, regnames[rs], btarget);
            goto not_likely;
        case OPC_BLEZL:
            gen_op_lez();
            MIPS_DEBUG("blezl %s, " TARGET_FMT_lx, regnames[rs], btarget);
            goto likely;
        case OPC_BLTZ:
            gen_op_ltz();
            MIPS_DEBUG("bltz %s, " TARGET_FMT_lx, regnames[rs], btarget);
            goto not_likely;
        case OPC_BLTZL:
            gen_op_ltz();
            MIPS_DEBUG("bltzl %s, " TARGET_FMT_lx, regnames[rs], btarget);
            goto likely;
        case OPC_BLTZAL:
            gen_op_ltz();
            blink = 31;
            MIPS_DEBUG("bltzal %s, " TARGET_FMT_lx, regnames[rs], btarget);
        not_likely:
            ctx->hflags |= MIPS_HFLAG_BC;
            gen_op_set_bcond();
            break;
        case OPC_BLTZALL:
            gen_op_ltz();
            blink = 31;
            MIPS_DEBUG("bltzall %s, " TARGET_FMT_lx, regnames[rs], btarget);
        likely:
            ctx->hflags |= MIPS_HFLAG_BL;
            gen_op_set_bcond();
            gen_op_save_bcond();
            break;
        default:
            MIPS_INVAL("conditional branch/jump");
            generate_exception(ctx, EXCP_RI);
            return;
        }
    }
    MIPS_DEBUG("enter ds: link %d cond %02x target " TARGET_FMT_lx,
               blink, ctx->hflags, btarget);

    ctx->btarget = btarget;
    if (blink > 0) {
        GEN_LOAD_IMM_TN(T0, ctx->pc + 8);
        gen_op_store_T0_gpr(blink);
    }
}

/* special3 bitfield operations */
static void gen_bitops (DisasContext *ctx, uint32_t opc, int rt,
                       int rs, int lsb, int msb)
{
    GEN_LOAD_REG_TN(T1, rs);
    switch (opc) {
    case OPC_EXT:
        if (lsb + msb > 31)
            goto fail;
        gen_op_ext(lsb, msb + 1);
        break;
    case OPC_DEXTM:
        if (lsb + msb > 63)
            goto fail;
        gen_op_ext(lsb, msb + 1 + 32);
        break;
    case OPC_DEXTU:
        if (lsb + msb > 63)
            goto fail;
        gen_op_ext(lsb + 32, msb + 1);
        break;
    case OPC_DEXT:
        gen_op_ext(lsb, msb + 1);
        break;
    case OPC_INS:
        if (lsb > msb)
            goto fail;
        GEN_LOAD_REG_TN(T0, rt);
        gen_op_ins(lsb, msb - lsb + 1);
        break;
    case OPC_DINSM:
        if (lsb > msb)
            goto fail;
        GEN_LOAD_REG_TN(T0, rt);
        gen_op_ins(lsb, msb - lsb + 1 + 32);
        break;
    case OPC_DINSU:
        if (lsb > msb)
            goto fail;
        GEN_LOAD_REG_TN(T0, rt);
        gen_op_ins(lsb + 32, msb - lsb + 1);
        break;
    case OPC_DINS:
        if (lsb > msb)
            goto fail;
        GEN_LOAD_REG_TN(T0, rt);
        gen_op_ins(lsb, msb - lsb + 1);
        break;
    default:
fail:
        MIPS_INVAL("bitops");
        generate_exception(ctx, EXCP_RI);
        return;
    }
    GEN_STORE_TN_REG(rt, T0);
}

/* CP0 (MMU and control) */
static void gen_mfc0 (CPUState *env, DisasContext *ctx, int reg, int sel)
{
    const char *rn = "invalid";

    switch (reg) {
    case 0:
        switch (sel) {
        case 0:
            gen_op_mfc0_index();
            rn = "Index";
            break;
        case 1:
//            gen_op_mfc0_mvpcontrol(); /* MT ASE */
            rn = "MVPControl";
//            break;
        case 2:
//            gen_op_mfc0_mvpconf0(); /* MT ASE */
            rn = "MVPConf0";
//            break;
        case 3:
//            gen_op_mfc0_mvpconf1(); /* MT ASE */
            rn = "MVPConf1";
//            break;
        default:
            goto die;
        }
        break;
    case 1:
        switch (sel) {
        case 0:
            gen_op_mfc0_random();
            rn = "Random";
            break;
        case 1:
//            gen_op_mfc0_vpecontrol(); /* MT ASE */
            rn = "VPEControl";
//            break;
        case 2:
//            gen_op_mfc0_vpeconf0(); /* MT ASE */
            rn = "VPEConf0";
//            break;
        case 3:
//            gen_op_mfc0_vpeconf1(); /* MT ASE */
            rn = "VPEConf1";
//            break;
        case 4:
//            gen_op_mfc0_YQMask(); /* MT ASE */
            rn = "YQMask";
//            break;
        case 5:
//            gen_op_mfc0_vpeschedule(); /* MT ASE */
            rn = "VPESchedule";
//            break;
        case 6:
//            gen_op_mfc0_vpeschefback(); /* MT ASE */
            rn = "VPEScheFBack";
//            break;
        case 7:
//            gen_op_mfc0_vpeopt(); /* MT ASE */
            rn = "VPEOpt";
//            break;
        default:
            goto die;
        }
        break;
    case 2:
        switch (sel) {
        case 0:
            gen_op_mfc0_entrylo0();
            rn = "EntryLo0";
            break;
        case 1:
//            gen_op_mfc0_tcstatus(); /* MT ASE */
            rn = "TCStatus";
//            break;
        case 2:
//            gen_op_mfc0_tcbind(); /* MT ASE */
            rn = "TCBind";
//            break;
        case 3:
//            gen_op_mfc0_tcrestart(); /* MT ASE */
            rn = "TCRestart";
//            break;
        case 4:
//            gen_op_mfc0_tchalt(); /* MT ASE */
            rn = "TCHalt";
//            break;
        case 5:
//            gen_op_mfc0_tccontext(); /* MT ASE */
            rn = "TCContext";
//            break;
        case 6:
//            gen_op_mfc0_tcschedule(); /* MT ASE */
            rn = "TCSchedule";
//            break;
        case 7:
//            gen_op_mfc0_tcschefback(); /* MT ASE */
            rn = "TCScheFBack";
//            break;
        default:
            goto die;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            gen_op_mfc0_entrylo1();
            rn = "EntryLo1";
            break;
        default:
            goto die;
        }
        break;
    case 4:
        switch (sel) {
        case 0:
            gen_op_mfc0_context();
            rn = "Context";
            break;
        case 1:
//            gen_op_mfc0_contextconfig(); /* SmartMIPS ASE */
            rn = "ContextConfig";
//            break;
        default:
            goto die;
        }
        break;
    case 5:
        switch (sel) {
        case 0:
            gen_op_mfc0_pagemask();
            rn = "PageMask";
            break;
        case 1:
            check_mips_r2(env, ctx);
            gen_op_mfc0_pagegrain();
            rn = "PageGrain";
            break;
        default:
            goto die;
        }
        break;
    case 6:
        switch (sel) {
        case 0:
            gen_op_mfc0_wired();
            rn = "Wired";
            break;
        case 1:
//            gen_op_mfc0_srsconf0(); /* shadow registers */
            rn = "SRSConf0";
//            break;
        case 2:
//            gen_op_mfc0_srsconf1(); /* shadow registers */
            rn = "SRSConf1";
//            break;
        case 3:
//            gen_op_mfc0_srsconf2(); /* shadow registers */
            rn = "SRSConf2";
//            break;
        case 4:
//            gen_op_mfc0_srsconf3(); /* shadow registers */
            rn = "SRSConf3";
//            break;
        case 5:
//            gen_op_mfc0_srsconf4(); /* shadow registers */
            rn = "SRSConf4";
//            break;
        default:
            goto die;
        }
        break;
    case 7:
        switch (sel) {
        case 0:
            check_mips_r2(env, ctx);
            gen_op_mfc0_hwrena();
            rn = "HWREna";
            break;
        default:
            goto die;
        }
        break;
    case 8:
        switch (sel) {
        case 0:
            gen_op_mfc0_badvaddr();
            rn = "BadVaddr";
            break;
        default:
            goto die;
       }
        break;
    case 9:
        switch (sel) {
        case 0:
            gen_op_mfc0_count();
            rn = "Count";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        break;
    case 10:
        switch (sel) {
        case 0:
            gen_op_mfc0_entryhi();
            rn = "EntryHi";
            break;
        default:
            goto die;
        }
        break;
    case 11:
        switch (sel) {
        case 0:
            gen_op_mfc0_compare();
            rn = "Compare";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        break;
    case 12:
        switch (sel) {
        case 0:
            gen_op_mfc0_status();
            rn = "Status";
            break;
        case 1:
            check_mips_r2(env, ctx);
            gen_op_mfc0_intctl();
            rn = "IntCtl";
            break;
        case 2:
            check_mips_r2(env, ctx);
            gen_op_mfc0_srsctl();
            rn = "SRSCtl";
            break;
        case 3:
            check_mips_r2(env, ctx);
            gen_op_mfc0_srsmap();
            rn = "SRSMap";
            break;
        default:
            goto die;
       }
        break;
    case 13:
        switch (sel) {
        case 0:
            gen_op_mfc0_cause();
            rn = "Cause";
            break;
        default:
            goto die;
       }
        break;
    case 14:
        switch (sel) {
        case 0:
            gen_op_mfc0_epc();
            rn = "EPC";
            break;
        default:
            goto die;
        }
        break;
    case 15:
        switch (sel) {
        case 0:
            gen_op_mfc0_prid();
            rn = "PRid";
            break;
        case 1:
            check_mips_r2(env, ctx);
            gen_op_mfc0_ebase();
            rn = "EBase";
            break;
        default:
            goto die;
       }
        break;
    case 16:
        switch (sel) {
        case 0:
            gen_op_mfc0_config0();
            rn = "Config";
            break;
        case 1:
            gen_op_mfc0_config1();
            rn = "Config1";
            break;
        case 2:
            gen_op_mfc0_config2();
            rn = "Config2";
            break;
        case 3:
            gen_op_mfc0_config3();
            rn = "Config3";
            break;
        /* 4,5 are reserved */
        /* 6,7 are implementation dependent */
        case 6:
            gen_op_mfc0_config6();
            rn = "Config6";
            break;
        case 7:
            gen_op_mfc0_config7();
            rn = "Config7";
            break;
        default:
            goto die;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            gen_op_mfc0_lladdr();
            rn = "LLAddr";
            break;
        default:
            goto die;
        }
        break;
    case 18:
        switch (sel) {
        case 0 ... 7:
            gen_op_mfc0_watchlo(sel);
            rn = "WatchLo";
            break;
        default:
            goto die;
        }
        break;
    case 19:
        switch (sel) {
        case 0 ...7:
            gen_op_mfc0_watchhi(sel);
            rn = "WatchHi";
            break;
        default:
            goto die;
        }
        break;
    case 20:
        switch (sel) {
        case 0:
#ifdef TARGET_MIPS64
            gen_op_mfc0_xcontext();
            rn = "XContext";
            break;
#endif
        default:
            goto die;
        }
        break;
    case 21:
       /* Officially reserved, but sel 0 is used for R1x000 framemask */
        switch (sel) {
        case 0:
            gen_op_mfc0_framemask();
            rn = "Framemask";
            break;
        default:
            goto die;
        }
        break;
    case 22:
        /* ignored */
        rn = "'Diagnostic"; /* implementation dependent */
        break;
    case 23:
        switch (sel) {
        case 0:
            gen_op_mfc0_debug(); /* EJTAG support */
            rn = "Debug";
            break;
        case 1:
//            gen_op_mfc0_tracecontrol(); /* PDtrace support */
            rn = "TraceControl";
//            break;
        case 2:
//            gen_op_mfc0_tracecontrol2(); /* PDtrace support */
            rn = "TraceControl2";
//            break;
        case 3:
//            gen_op_mfc0_usertracedata(); /* PDtrace support */
            rn = "UserTraceData";
//            break;
        case 4:
//            gen_op_mfc0_debug(); /* PDtrace support */
            rn = "TraceBPC";
//            break;
        default:
            goto die;
        }
        break;
    case 24:
        switch (sel) {
        case 0:
            gen_op_mfc0_depc(); /* EJTAG support */
            rn = "DEPC";
            break;
        default:
            goto die;
        }
        break;
    case 25:
        switch (sel) {
        case 0:
            gen_op_mfc0_performance0();
            rn = "Performance0";
            break;
        case 1:
//            gen_op_mfc0_performance1();
            rn = "Performance1";
//            break;
        case 2:
//            gen_op_mfc0_performance2();
            rn = "Performance2";
//            break;
        case 3:
//            gen_op_mfc0_performance3();
            rn = "Performance3";
//            break;
        case 4:
//            gen_op_mfc0_performance4();
            rn = "Performance4";
//            break;
        case 5:
//            gen_op_mfc0_performance5();
            rn = "Performance5";
//            break;
        case 6:
//            gen_op_mfc0_performance6();
            rn = "Performance6";
//            break;
        case 7:
//            gen_op_mfc0_performance7();
            rn = "Performance7";
//            break;
        default:
            goto die;
        }
        break;
    case 26:
       rn = "ECC";
       break;
    case 27:
        switch (sel) {
        /* ignored */
        case 0 ... 3:
            rn = "CacheErr";
            break;
        default:
            goto die;
        }
        break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_op_mfc0_taglo();
            rn = "TagLo";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_op_mfc0_datalo();
            rn = "DataLo";
            break;
        default:
            goto die;
        }
        break;
    case 29:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_op_mfc0_taghi();
            rn = "TagHi";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_op_mfc0_datahi();
            rn = "DataHi";
            break;
        default:
            goto die;
        }
        break;
    case 30:
        switch (sel) {
        case 0:
            gen_op_mfc0_errorepc();
            rn = "ErrorEPC";
            break;
        default:
            goto die;
        }
        break;
    case 31:
        switch (sel) {
        case 0:
            gen_op_mfc0_desave(); /* EJTAG support */
            rn = "DESAVE";
            break;
        default:
            goto die;
        }
        break;
    default:
       goto die;
    }
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "mfc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    return;

die:
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "mfc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    generate_exception(ctx, EXCP_RI);
}

static void gen_mtc0 (CPUState *env, DisasContext *ctx, int reg, int sel)
{
    const char *rn = "invalid";

    switch (reg) {
    case 0:
        switch (sel) {
        case 0:
           gen_op_mtc0_index();
            rn = "Index";
            break;
        case 1:
//            gen_op_mtc0_mvpcontrol(); /* MT ASE */
            rn = "MVPControl";
//            break;
        case 2:
//            gen_op_mtc0_mvpconf0(); /* MT ASE */
            rn = "MVPConf0";
//            break;
        case 3:
//            gen_op_mtc0_mvpconf1(); /* MT ASE */
            rn = "MVPConf1";
//            break;
        default:
            goto die;
        }
        break;
    case 1:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "Random";
            break;
        case 1:
//            gen_op_mtc0_vpecontrol(); /* MT ASE */
            rn = "VPEControl";
//            break;
        case 2:
//            gen_op_mtc0_vpeconf0(); /* MT ASE */
            rn = "VPEConf0";
//            break;
        case 3:
//            gen_op_mtc0_vpeconf1(); /* MT ASE */
            rn = "VPEConf1";
//            break;
        case 4:
//            gen_op_mtc0_YQMask(); /* MT ASE */
            rn = "YQMask";
//            break;
        case 5:
//            gen_op_mtc0_vpeschedule(); /* MT ASE */
            rn = "VPESchedule";
//            break;
        case 6:
//            gen_op_mtc0_vpeschefback(); /* MT ASE */
            rn = "VPEScheFBack";
//            break;
        case 7:
//            gen_op_mtc0_vpeopt(); /* MT ASE */
            rn = "VPEOpt";
//            break;
        default:
            goto die;
        }
        break;
    case 2:
        switch (sel) {
        case 0:
            gen_op_mtc0_entrylo0();
            rn = "EntryLo0";
            break;
        case 1:
//            gen_op_mtc0_tcstatus(); /* MT ASE */
            rn = "TCStatus";
//            break;
        case 2:
//            gen_op_mtc0_tcbind(); /* MT ASE */
            rn = "TCBind";
//            break;
        case 3:
//            gen_op_mtc0_tcrestart(); /* MT ASE */
            rn = "TCRestart";
//            break;
        case 4:
//            gen_op_mtc0_tchalt(); /* MT ASE */
            rn = "TCHalt";
//            break;
        case 5:
//            gen_op_mtc0_tccontext(); /* MT ASE */
            rn = "TCContext";
//            break;
        case 6:
//            gen_op_mtc0_tcschedule(); /* MT ASE */
            rn = "TCSchedule";
//            break;
        case 7:
//            gen_op_mtc0_tcschefback(); /* MT ASE */
            rn = "TCScheFBack";
//            break;
        default:
            goto die;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            gen_op_mtc0_entrylo1();
            rn = "EntryLo1";
            break;
        default:
            goto die;
        }
        break;
    case 4:
        switch (sel) {
        case 0:
            gen_op_mtc0_context();
            rn = "Context";
            break;
        case 1:
//            gen_op_mtc0_contextconfig(); /* SmartMIPS ASE */
            rn = "ContextConfig";
//            break;
        default:
            goto die;
        }
        break;
    case 5:
        switch (sel) {
        case 0:
            gen_op_mtc0_pagemask();
            rn = "PageMask";
            break;
        case 1:
            check_mips_r2(env, ctx);
            gen_op_mtc0_pagegrain();
            rn = "PageGrain";
            break;
        default:
            goto die;
        }
        break;
    case 6:
        switch (sel) {
        case 0:
            gen_op_mtc0_wired();
            rn = "Wired";
            break;
        case 1:
//            gen_op_mtc0_srsconf0(); /* shadow registers */
            rn = "SRSConf0";
//            break;
        case 2:
//            gen_op_mtc0_srsconf1(); /* shadow registers */
            rn = "SRSConf1";
//            break;
        case 3:
//            gen_op_mtc0_srsconf2(); /* shadow registers */
            rn = "SRSConf2";
//            break;
        case 4:
//            gen_op_mtc0_srsconf3(); /* shadow registers */
            rn = "SRSConf3";
//            break;
        case 5:
//            gen_op_mtc0_srsconf4(); /* shadow registers */
            rn = "SRSConf4";
//            break;
        default:
            goto die;
        }
        break;
    case 7:
        switch (sel) {
        case 0:
            check_mips_r2(env, ctx);
            gen_op_mtc0_hwrena();
            rn = "HWREna";
            break;
        default:
            goto die;
        }
        break;
    case 8:
        /* ignored */
        rn = "BadVaddr";
        break;
    case 9:
        switch (sel) {
        case 0:
            gen_op_mtc0_count();
            rn = "Count";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 10:
        switch (sel) {
        case 0:
            gen_op_mtc0_entryhi();
            rn = "EntryHi";
            break;
        default:
            goto die;
        }
        break;
    case 11:
        switch (sel) {
        case 0:
            gen_op_mtc0_compare();
            rn = "Compare";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 12:
        switch (sel) {
        case 0:
            gen_op_mtc0_status();
            /* BS_STOP isn't good enough here, hflags may have changed. */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Status";
            break;
        case 1:
            check_mips_r2(env, ctx);
            gen_op_mtc0_intctl();
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "IntCtl";
            break;
        case 2:
            check_mips_r2(env, ctx);
            gen_op_mtc0_srsctl();
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "SRSCtl";
            break;
        case 3:
            check_mips_r2(env, ctx);
            gen_op_mtc0_srsmap();
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "SRSMap";
            break;
        default:
            goto die;
        }
        break;
    case 13:
        switch (sel) {
        case 0:
            gen_op_mtc0_cause();
            rn = "Cause";
            break;
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 14:
        switch (sel) {
        case 0:
            gen_op_mtc0_epc();
            rn = "EPC";
            break;
        default:
            goto die;
        }
        break;
    case 15:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "PRid";
            break;
        case 1:
            check_mips_r2(env, ctx);
            gen_op_mtc0_ebase();
            rn = "EBase";
            break;
        default:
            goto die;
        }
        break;
    case 16:
        switch (sel) {
        case 0:
            gen_op_mtc0_config0();
            rn = "Config";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 1:
            /* ignored, read only */
            rn = "Config1";
            break;
        case 2:
            gen_op_mtc0_config2();
            rn = "Config2";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 3:
            /* ignored, read only */
            rn = "Config3";
            break;
        /* 4,5 are reserved */
        /* 6,7 are implementation dependent */
        case 6:
            /* ignored */
            rn = "Config6";
            break;
        case 7:
            /* ignored */
            rn = "Config7";
            break;
        default:
            rn = "Invalid config selector";
            goto die;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "LLAddr";
            break;
        default:
            goto die;
        }
        break;
    case 18:
        switch (sel) {
        case 0 ... 7:
            gen_op_mtc0_watchlo(sel);
            rn = "WatchLo";
            break;
        default:
            goto die;
        }
        break;
    case 19:
        switch (sel) {
        case 0 ... 7:
            gen_op_mtc0_watchhi(sel);
            rn = "WatchHi";
            break;
        default:
            goto die;
        }
        break;
    case 20:
        switch (sel) {
        case 0:
#ifdef TARGET_MIPS64
            gen_op_mtc0_xcontext();
            rn = "XContext";
            break;
#endif
        default:
            goto die;
        }
        break;
    case 21:
       /* Officially reserved, but sel 0 is used for R1x000 framemask */
        switch (sel) {
        case 0:
            gen_op_mtc0_framemask();
            rn = "Framemask";
            break;
        default:
            goto die;
        }
        break;
    case 22:
        /* ignored */
        rn = "Diagnostic"; /* implementation dependent */
        break;
    case 23:
        switch (sel) {
        case 0:
            gen_op_mtc0_debug(); /* EJTAG support */
            /* BS_STOP isn't good enough here, hflags may have changed. */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Debug";
            break;
        case 1:
//            gen_op_mtc0_tracecontrol(); /* PDtrace support */
            rn = "TraceControl";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
//            break;
        case 2:
//            gen_op_mtc0_tracecontrol2(); /* PDtrace support */
            rn = "TraceControl2";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
//            break;
        case 3:
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
//            gen_op_mtc0_usertracedata(); /* PDtrace support */
            rn = "UserTraceData";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
//            break;
        case 4:
//            gen_op_mtc0_debug(); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "TraceBPC";
//            break;
        default:
            goto die;
        }
        break;
    case 24:
        switch (sel) {
        case 0:
            gen_op_mtc0_depc(); /* EJTAG support */
            rn = "DEPC";
            break;
        default:
            goto die;
        }
        break;
    case 25:
        switch (sel) {
        case 0:
            gen_op_mtc0_performance0();
            rn = "Performance0";
            break;
        case 1:
//            gen_op_mtc0_performance1();
            rn = "Performance1";
//            break;
        case 2:
//            gen_op_mtc0_performance2();
            rn = "Performance2";
//            break;
        case 3:
//            gen_op_mtc0_performance3();
            rn = "Performance3";
//            break;
        case 4:
//            gen_op_mtc0_performance4();
            rn = "Performance4";
//            break;
        case 5:
//            gen_op_mtc0_performance5();
            rn = "Performance5";
//            break;
        case 6:
//            gen_op_mtc0_performance6();
            rn = "Performance6";
//            break;
        case 7:
//            gen_op_mtc0_performance7();
            rn = "Performance7";
//            break;
        default:
            goto die;
        }
       break;
    case 26:
        /* ignored */
        rn = "ECC";
        break;
    case 27:
        switch (sel) {
        case 0 ... 3:
            /* ignored */
            rn = "CacheErr";
            break;
        default:
            goto die;
        }
       break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_op_mtc0_taglo();
            rn = "TagLo";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_op_mtc0_datalo();
            rn = "DataLo";
            break;
        default:
            goto die;
        }
        break;
    case 29:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_op_mtc0_taghi();
            rn = "TagHi";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_op_mtc0_datahi();
            rn = "DataHi";
            break;
        default:
            rn = "invalid sel";
            goto die;
        }
       break;
    case 30:
        switch (sel) {
        case 0:
            gen_op_mtc0_errorepc();
            rn = "ErrorEPC";
            break;
        default:
            goto die;
        }
        break;
    case 31:
        switch (sel) {
        case 0:
            gen_op_mtc0_desave(); /* EJTAG support */
            rn = "DESAVE";
            break;
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    default:
       goto die;
    }
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "mtc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    return;

die:
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "mtc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    generate_exception(ctx, EXCP_RI);
}

#ifdef TARGET_MIPS64
static void gen_dmfc0 (CPUState *env, DisasContext *ctx, int reg, int sel)
{
    const char *rn = "invalid";

    switch (reg) {
    case 0:
        switch (sel) {
        case 0:
            gen_op_mfc0_index();
            rn = "Index";
            break;
        case 1:
//            gen_op_dmfc0_mvpcontrol(); /* MT ASE */
            rn = "MVPControl";
//            break;
        case 2:
//            gen_op_dmfc0_mvpconf0(); /* MT ASE */
            rn = "MVPConf0";
//            break;
        case 3:
//            gen_op_dmfc0_mvpconf1(); /* MT ASE */
            rn = "MVPConf1";
//            break;
        default:
            goto die;
        }
        break;
    case 1:
        switch (sel) {
        case 0:
            gen_op_mfc0_random();
            rn = "Random";
            break;
        case 1:
//            gen_op_dmfc0_vpecontrol(); /* MT ASE */
            rn = "VPEControl";
//            break;
        case 2:
//            gen_op_dmfc0_vpeconf0(); /* MT ASE */
            rn = "VPEConf0";
//            break;
        case 3:
//            gen_op_dmfc0_vpeconf1(); /* MT ASE */
            rn = "VPEConf1";
//            break;
        case 4:
//            gen_op_dmfc0_YQMask(); /* MT ASE */
            rn = "YQMask";
//            break;
        case 5:
//            gen_op_dmfc0_vpeschedule(); /* MT ASE */
            rn = "VPESchedule";
//            break;
        case 6:
//            gen_op_dmfc0_vpeschefback(); /* MT ASE */
            rn = "VPEScheFBack";
//            break;
        case 7:
//            gen_op_dmfc0_vpeopt(); /* MT ASE */
            rn = "VPEOpt";
//            break;
        default:
            goto die;
        }
        break;
    case 2:
        switch (sel) {
        case 0:
            gen_op_dmfc0_entrylo0();
            rn = "EntryLo0";
            break;
        case 1:
//            gen_op_dmfc0_tcstatus(); /* MT ASE */
            rn = "TCStatus";
//            break;
        case 2:
//            gen_op_dmfc0_tcbind(); /* MT ASE */
            rn = "TCBind";
//            break;
        case 3:
//            gen_op_dmfc0_tcrestart(); /* MT ASE */
            rn = "TCRestart";
//            break;
        case 4:
//            gen_op_dmfc0_tchalt(); /* MT ASE */
            rn = "TCHalt";
//            break;
        case 5:
//            gen_op_dmfc0_tccontext(); /* MT ASE */
            rn = "TCContext";
//            break;
        case 6:
//            gen_op_dmfc0_tcschedule(); /* MT ASE */
            rn = "TCSchedule";
//            break;
        case 7:
//            gen_op_dmfc0_tcschefback(); /* MT ASE */
            rn = "TCScheFBack";
//            break;
        default:
            goto die;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            gen_op_dmfc0_entrylo1();
            rn = "EntryLo1";
            break;
        default:
            goto die;
        }
        break;
    case 4:
        switch (sel) {
        case 0:
            gen_op_dmfc0_context();
            rn = "Context";
            break;
        case 1:
//            gen_op_dmfc0_contextconfig(); /* SmartMIPS ASE */
            rn = "ContextConfig";
//            break;
        default:
            goto die;
        }
        break;
    case 5:
        switch (sel) {
        case 0:
            gen_op_mfc0_pagemask();
            rn = "PageMask";
            break;
        case 1:
            check_mips_r2(env, ctx);
            gen_op_mfc0_pagegrain();
            rn = "PageGrain";
            break;
        default:
            goto die;
        }
        break;
    case 6:
        switch (sel) {
        case 0:
            gen_op_mfc0_wired();
            rn = "Wired";
            break;
        case 1:
//            gen_op_dmfc0_srsconf0(); /* shadow registers */
            rn = "SRSConf0";
//            break;
        case 2:
//            gen_op_dmfc0_srsconf1(); /* shadow registers */
            rn = "SRSConf1";
//            break;
        case 3:
//            gen_op_dmfc0_srsconf2(); /* shadow registers */
            rn = "SRSConf2";
//            break;
        case 4:
//            gen_op_dmfc0_srsconf3(); /* shadow registers */
            rn = "SRSConf3";
//            break;
        case 5:
//            gen_op_dmfc0_srsconf4(); /* shadow registers */
            rn = "SRSConf4";
//            break;
        default:
            goto die;
        }
        break;
    case 7:
        switch (sel) {
        case 0:
            check_mips_r2(env, ctx);
            gen_op_mfc0_hwrena();
            rn = "HWREna";
            break;
        default:
            goto die;
        }
        break;
    case 8:
        switch (sel) {
        case 0:
            gen_op_dmfc0_badvaddr();
            rn = "BadVaddr";
            break;
        default:
            goto die;
        }
        break;
    case 9:
        switch (sel) {
        case 0:
            gen_op_mfc0_count();
            rn = "Count";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        break;
    case 10:
        switch (sel) {
        case 0:
            gen_op_dmfc0_entryhi();
            rn = "EntryHi";
            break;
        default:
            goto die;
        }
        break;
    case 11:
        switch (sel) {
        case 0:
            gen_op_mfc0_compare();
            rn = "Compare";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        break;
    case 12:
        switch (sel) {
        case 0:
            gen_op_mfc0_status();
            rn = "Status";
            break;
        case 1:
            check_mips_r2(env, ctx);
            gen_op_mfc0_intctl();
            rn = "IntCtl";
            break;
        case 2:
            check_mips_r2(env, ctx);
            gen_op_mfc0_srsctl();
            rn = "SRSCtl";
            break;
        case 3:
            check_mips_r2(env, ctx);
            gen_op_mfc0_srsmap(); /* shadow registers */
            rn = "SRSMap";
            break;
        default:
            goto die;
        }
        break;
    case 13:
        switch (sel) {
        case 0:
            gen_op_mfc0_cause();
            rn = "Cause";
            break;
        default:
            goto die;
        }
        break;
    case 14:
        switch (sel) {
        case 0:
            gen_op_dmfc0_epc();
            rn = "EPC";
            break;
        default:
            goto die;
        }
        break;
    case 15:
        switch (sel) {
        case 0:
            gen_op_mfc0_prid();
            rn = "PRid";
            break;
        case 1:
            check_mips_r2(env, ctx);
            gen_op_mfc0_ebase();
            rn = "EBase";
            break;
        default:
            goto die;
        }
        break;
    case 16:
        switch (sel) {
        case 0:
            gen_op_mfc0_config0();
            rn = "Config";
            break;
        case 1:
            gen_op_mfc0_config1();
            rn = "Config1";
            break;
        case 2:
            gen_op_mfc0_config2();
            rn = "Config2";
            break;
        case 3:
            gen_op_mfc0_config3();
            rn = "Config3";
            break;
       /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            gen_op_dmfc0_lladdr();
            rn = "LLAddr";
            break;
        default:
            goto die;
        }
        break;
    case 18:
        switch (sel) {
        case 0 ... 7:
            gen_op_dmfc0_watchlo(sel);
            rn = "WatchLo";
            break;
        default:
            goto die;
        }
        break;
    case 19:
        switch (sel) {
        case 0 ... 7:
            gen_op_mfc0_watchhi(sel);
            rn = "WatchHi";
            break;
        default:
            goto die;
        }
        break;
    case 20:
        switch (sel) {
        case 0:
#ifdef TARGET_MIPS64
            gen_op_dmfc0_xcontext();
            rn = "XContext";
            break;
#endif
        default:
            goto die;
        }
        break;
    case 21:
       /* Officially reserved, but sel 0 is used for R1x000 framemask */
        switch (sel) {
        case 0:
            gen_op_mfc0_framemask();
            rn = "Framemask";
            break;
        default:
            goto die;
        }
        break;
    case 22:
        /* ignored */
        rn = "'Diagnostic"; /* implementation dependent */
        break;
    case 23:
        switch (sel) {
        case 0:
            gen_op_mfc0_debug(); /* EJTAG support */
            rn = "Debug";
            break;
        case 1:
//            gen_op_dmfc0_tracecontrol(); /* PDtrace support */
            rn = "TraceControl";
//            break;
        case 2:
//            gen_op_dmfc0_tracecontrol2(); /* PDtrace support */
            rn = "TraceControl2";
//            break;
        case 3:
//            gen_op_dmfc0_usertracedata(); /* PDtrace support */
            rn = "UserTraceData";
//            break;
        case 4:
//            gen_op_dmfc0_debug(); /* PDtrace support */
            rn = "TraceBPC";
//            break;
        default:
            goto die;
        }
        break;
    case 24:
        switch (sel) {
        case 0:
            gen_op_dmfc0_depc(); /* EJTAG support */
            rn = "DEPC";
            break;
        default:
            goto die;
        }
        break;
    case 25:
        switch (sel) {
        case 0:
            gen_op_mfc0_performance0();
            rn = "Performance0";
            break;
        case 1:
//            gen_op_dmfc0_performance1();
            rn = "Performance1";
//            break;
        case 2:
//            gen_op_dmfc0_performance2();
            rn = "Performance2";
//            break;
        case 3:
//            gen_op_dmfc0_performance3();
            rn = "Performance3";
//            break;
        case 4:
//            gen_op_dmfc0_performance4();
            rn = "Performance4";
//            break;
        case 5:
//            gen_op_dmfc0_performance5();
            rn = "Performance5";
//            break;
        case 6:
//            gen_op_dmfc0_performance6();
            rn = "Performance6";
//            break;
        case 7:
//            gen_op_dmfc0_performance7();
            rn = "Performance7";
//            break;
        default:
            goto die;
        }
        break;
    case 26:
       rn = "ECC";
       break;
    case 27:
        switch (sel) {
        /* ignored */
        case 0 ... 3:
            rn = "CacheErr";
            break;
        default:
            goto die;
        }
        break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_op_mfc0_taglo();
            rn = "TagLo";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_op_mfc0_datalo();
            rn = "DataLo";
            break;
        default:
            goto die;
        }
        break;
    case 29:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_op_mfc0_taghi();
            rn = "TagHi";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_op_mfc0_datahi();
            rn = "DataHi";
            break;
        default:
            goto die;
        }
        break;
    case 30:
        switch (sel) {
        case 0:
            gen_op_dmfc0_errorepc();
            rn = "ErrorEPC";
            break;
        default:
            goto die;
        }
        break;
    case 31:
        switch (sel) {
        case 0:
            gen_op_mfc0_desave(); /* EJTAG support */
            rn = "DESAVE";
            break;
        default:
            goto die;
        }
        break;
    default:
        goto die;
    }
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "dmfc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    return;

die:
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "dmfc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    generate_exception(ctx, EXCP_RI);
}

static void gen_dmtc0 (CPUState *env, DisasContext *ctx, int reg, int sel)
{
    const char *rn = "invalid";

    switch (reg) {
    case 0:
        switch (sel) {
        case 0:
            gen_op_mtc0_index();
            rn = "Index";
            break;
        case 1:
//            gen_op_mtc0_mvpcontrol(); /* MT ASE */
            rn = "MVPControl";
//            break;
        case 2:
//            gen_op_mtc0_mvpconf0(); /* MT ASE */
            rn = "MVPConf0";
//            break;
        case 3:
//            gen_op_mtc0_mvpconf1(); /* MT ASE */
            rn = "MVPConf1";
//            break;
        default:
            goto die;
        }
        break;
    case 1:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "Random";
            break;
        case 1:
//            gen_op_mtc0_vpecontrol(); /* MT ASE */
            rn = "VPEControl";
//            break;
        case 2:
//            gen_op_mtc0_vpeconf0(); /* MT ASE */
            rn = "VPEConf0";
//            break;
        case 3:
//            gen_op_mtc0_vpeconf1(); /* MT ASE */
            rn = "VPEConf1";
//            break;
        case 4:
//            gen_op_mtc0_YQMask(); /* MT ASE */
            rn = "YQMask";
//            break;
        case 5:
//            gen_op_mtc0_vpeschedule(); /* MT ASE */
            rn = "VPESchedule";
//            break;
        case 6:
//            gen_op_mtc0_vpeschefback(); /* MT ASE */
            rn = "VPEScheFBack";
//            break;
        case 7:
//            gen_op_mtc0_vpeopt(); /* MT ASE */
            rn = "VPEOpt";
//            break;
        default:
            goto die;
        }
        break;
    case 2:
        switch (sel) {
        case 0:
            gen_op_mtc0_entrylo0();
            rn = "EntryLo0";
            break;
        case 1:
//            gen_op_mtc0_tcstatus(); /* MT ASE */
            rn = "TCStatus";
//            break;
        case 2:
//            gen_op_mtc0_tcbind(); /* MT ASE */
            rn = "TCBind";
//            break;
        case 3:
//            gen_op_mtc0_tcrestart(); /* MT ASE */
            rn = "TCRestart";
//            break;
        case 4:
//            gen_op_mtc0_tchalt(); /* MT ASE */
            rn = "TCHalt";
//            break;
        case 5:
//            gen_op_mtc0_tccontext(); /* MT ASE */
            rn = "TCContext";
//            break;
        case 6:
//            gen_op_mtc0_tcschedule(); /* MT ASE */
            rn = "TCSchedule";
//            break;
        case 7:
//            gen_op_mtc0_tcschefback(); /* MT ASE */
            rn = "TCScheFBack";
//            break;
        default:
            goto die;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            gen_op_mtc0_entrylo1();
            rn = "EntryLo1";
            break;
        default:
            goto die;
        }
        break;
    case 4:
        switch (sel) {
        case 0:
            gen_op_mtc0_context();
            rn = "Context";
            break;
        case 1:
//           gen_op_mtc0_contextconfig(); /* SmartMIPS ASE */
            rn = "ContextConfig";
//           break;
        default:
            goto die;
        }
        break;
    case 5:
        switch (sel) {
        case 0:
            gen_op_mtc0_pagemask();
            rn = "PageMask";
            break;
        case 1:
            check_mips_r2(env, ctx);
            gen_op_mtc0_pagegrain();
            rn = "PageGrain";
            break;
        default:
            goto die;
        }
        break;
    case 6:
        switch (sel) {
        case 0:
            gen_op_mtc0_wired();
            rn = "Wired";
            break;
        case 1:
//            gen_op_mtc0_srsconf0(); /* shadow registers */
            rn = "SRSConf0";
//            break;
        case 2:
//            gen_op_mtc0_srsconf1(); /* shadow registers */
            rn = "SRSConf1";
//            break;
        case 3:
//            gen_op_mtc0_srsconf2(); /* shadow registers */
            rn = "SRSConf2";
//            break;
        case 4:
//            gen_op_mtc0_srsconf3(); /* shadow registers */
            rn = "SRSConf3";
//            break;
        case 5:
//            gen_op_mtc0_srsconf4(); /* shadow registers */
            rn = "SRSConf4";
//            break;
        default:
            goto die;
        }
        break;
    case 7:
        switch (sel) {
        case 0:
            check_mips_r2(env, ctx);
            gen_op_mtc0_hwrena();
            rn = "HWREna";
            break;
        default:
            goto die;
        }
        break;
    case 8:
        /* ignored */
        rn = "BadVaddr";
        break;
    case 9:
        switch (sel) {
        case 0:
            gen_op_mtc0_count();
            rn = "Count";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 10:
        switch (sel) {
        case 0:
            gen_op_mtc0_entryhi();
            rn = "EntryHi";
            break;
        default:
            goto die;
        }
        break;
    case 11:
        switch (sel) {
        case 0:
            gen_op_mtc0_compare();
            rn = "Compare";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 12:
        switch (sel) {
        case 0:
            gen_op_mtc0_status();
            /* BS_STOP isn't good enough here, hflags may have changed. */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Status";
            break;
        case 1:
            check_mips_r2(env, ctx);
            gen_op_mtc0_intctl();
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "IntCtl";
            break;
        case 2:
            check_mips_r2(env, ctx);
            gen_op_mtc0_srsctl();
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "SRSCtl";
            break;
        case 3:
            check_mips_r2(env, ctx);
            gen_op_mtc0_srsmap();
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "SRSMap";
            break;
        default:
            goto die;
        }
        break;
    case 13:
        switch (sel) {
        case 0:
            gen_op_mtc0_cause();
            rn = "Cause";
            break;
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 14:
        switch (sel) {
        case 0:
            gen_op_mtc0_epc();
            rn = "EPC";
            break;
        default:
            goto die;
        }
        break;
    case 15:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "PRid";
            break;
        case 1:
            check_mips_r2(env, ctx);
            gen_op_mtc0_ebase();
            rn = "EBase";
            break;
        default:
            goto die;
        }
        break;
    case 16:
        switch (sel) {
        case 0:
            gen_op_mtc0_config0();
            rn = "Config";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 1:
            /* ignored */
            rn = "Config1";
            break;
        case 2:
            gen_op_mtc0_config2();
            rn = "Config2";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 3:
            /* ignored */
            rn = "Config3";
            break;
        /* 6,7 are implementation dependent */
        default:
            rn = "Invalid config selector";
            goto die;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "LLAddr";
            break;
        default:
            goto die;
        }
        break;
    case 18:
        switch (sel) {
        case 0 ... 7:
            gen_op_mtc0_watchlo(sel);
            rn = "WatchLo";
            break;
        default:
            goto die;
        }
        break;
    case 19:
        switch (sel) {
        case 0 ... 7:
            gen_op_mtc0_watchhi(sel);
            rn = "WatchHi";
            break;
        default:
            goto die;
        }
        break;
    case 20:
        switch (sel) {
        case 0:
#ifdef TARGET_MIPS64
            gen_op_mtc0_xcontext();
            rn = "XContext";
            break;
#endif
        default:
            goto die;
        }
        break;
    case 21:
       /* Officially reserved, but sel 0 is used for R1x000 framemask */
        switch (sel) {
        case 0:
            gen_op_mtc0_framemask();
            rn = "Framemask";
            break;
        default:
            goto die;
        }
        break;
    case 22:
        /* ignored */
        rn = "Diagnostic"; /* implementation dependent */
        break;
    case 23:
        switch (sel) {
        case 0:
            gen_op_mtc0_debug(); /* EJTAG support */
            /* BS_STOP isn't good enough here, hflags may have changed. */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Debug";
            break;
        case 1:
//            gen_op_mtc0_tracecontrol(); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "TraceControl";
//            break;
        case 2:
//            gen_op_mtc0_tracecontrol2(); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "TraceControl2";
//            break;
        case 3:
//            gen_op_mtc0_usertracedata(); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "UserTraceData";
//            break;
        case 4:
//            gen_op_mtc0_debug(); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "TraceBPC";
//            break;
        default:
            goto die;
        }
        break;
    case 24:
        switch (sel) {
        case 0:
            gen_op_mtc0_depc(); /* EJTAG support */
            rn = "DEPC";
            break;
        default:
            goto die;
        }
        break;
    case 25:
        switch (sel) {
        case 0:
            gen_op_mtc0_performance0();
            rn = "Performance0";
            break;
        case 1:
//            gen_op_mtc0_performance1();
            rn = "Performance1";
//            break;
        case 2:
//            gen_op_mtc0_performance2();
            rn = "Performance2";
//            break;
        case 3:
//            gen_op_mtc0_performance3();
            rn = "Performance3";
//            break;
        case 4:
//            gen_op_mtc0_performance4();
            rn = "Performance4";
//            break;
        case 5:
//            gen_op_mtc0_performance5();
            rn = "Performance5";
//            break;
        case 6:
//            gen_op_mtc0_performance6();
            rn = "Performance6";
//            break;
        case 7:
//            gen_op_mtc0_performance7();
            rn = "Performance7";
//            break;
        default:
            goto die;
        }
        break;
    case 26:
        /* ignored */
        rn = "ECC";
        break;
    case 27:
        switch (sel) {
        case 0 ... 3:
            /* ignored */
            rn = "CacheErr";
            break;
        default:
            goto die;
        }
        break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_op_mtc0_taglo();
            rn = "TagLo";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_op_mtc0_datalo();
            rn = "DataLo";
            break;
        default:
            goto die;
        }
        break;
    case 29:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_op_mtc0_taghi();
            rn = "TagHi";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_op_mtc0_datahi();
            rn = "DataHi";
            break;
        default:
            rn = "invalid sel";
            goto die;
        }
        break;
    case 30:
        switch (sel) {
        case 0:
            gen_op_mtc0_errorepc();
            rn = "ErrorEPC";
            break;
        default:
            goto die;
        }
        break;
    case 31:
        switch (sel) {
        case 0:
            gen_op_mtc0_desave(); /* EJTAG support */
            rn = "DESAVE";
            break;
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    default:
        goto die;
    }
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "dmtc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    return;

die:
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "dmtc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    generate_exception(ctx, EXCP_RI);
}
#endif /* TARGET_MIPS64 */

static void gen_cp0 (CPUState *env, DisasContext *ctx, uint32_t opc, int rt, int rd)
{
    const char *opn = "ldst";

    switch (opc) {
    case OPC_MFC0:
        if (rt == 0) {
            /* Treat as NOP */
            return;
        }
        gen_mfc0(env, ctx, rd, ctx->opcode & 0x7);
        gen_op_store_T0_gpr(rt);
        opn = "mfc0";
        break;
    case OPC_MTC0:
        GEN_LOAD_REG_TN(T0, rt);
        gen_mtc0(env, ctx, rd, ctx->opcode & 0x7);
        opn = "mtc0";
        break;
#ifdef TARGET_MIPS64
    case OPC_DMFC0:
        if (rt == 0) {
            /* Treat as NOP */
            return;
        }
        gen_dmfc0(env, ctx, rd, ctx->opcode & 0x7);
        gen_op_store_T0_gpr(rt);
        opn = "dmfc0";
        break;
    case OPC_DMTC0:
        GEN_LOAD_REG_TN(T0, rt);
        gen_dmtc0(env,ctx, rd, ctx->opcode & 0x7);
        opn = "dmtc0";
        break;
#endif
    case OPC_TLBWI:
        opn = "tlbwi";
        if (!env->do_tlbwi)
            goto die;
        gen_op_tlbwi();
        break;
    case OPC_TLBWR:
        opn = "tlbwr";
        if (!env->do_tlbwr)
            goto die;
        gen_op_tlbwr();
        break;
    case OPC_TLBP:
        opn = "tlbp";
        if (!env->do_tlbp)
            goto die;
        gen_op_tlbp();
        break;
    case OPC_TLBR:
        opn = "tlbr";
        if (!env->do_tlbr)
            goto die;
        gen_op_tlbr();
        break;
    case OPC_ERET:
        opn = "eret";
        gen_op_eret();
        ctx->bstate = BS_EXCP;
        break;
    case OPC_DERET:
        opn = "deret";
        if (!(ctx->hflags & MIPS_HFLAG_DM)) {
            MIPS_INVAL(opn);
            generate_exception(ctx, EXCP_RI);
        } else {
            gen_op_deret();
            ctx->bstate = BS_EXCP;
        }
        break;
    case OPC_WAIT:
        opn = "wait";
        /* If we get an exception, we want to restart at next instruction */
        ctx->pc += 4;
        save_cpu_state(ctx, 1);
        ctx->pc -= 4;
        gen_op_wait();
        ctx->bstate = BS_EXCP;
        break;
    default:
 die:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s %d", opn, regnames[rt], rd);
}

/* CP1 Branches (before delay slot) */
static void gen_compute_branch1 (DisasContext *ctx, uint32_t op,
                                 int32_t cc, int32_t offset)
{
    target_ulong btarget;
    const char *opn = "cp1 cond branch";

    btarget = ctx->pc + 4 + offset;

    switch (op) {
    case OPC_BC1F:
        gen_op_bc1f(cc);
        opn = "bc1f";
        goto not_likely;
    case OPC_BC1FL:
        gen_op_bc1f(cc);
        opn = "bc1fl";
        goto likely;
    case OPC_BC1T:
        gen_op_bc1t(cc);
        opn = "bc1t";
        goto not_likely;
    case OPC_BC1TL:
        gen_op_bc1t(cc);
        opn = "bc1tl";
    likely:
        ctx->hflags |= MIPS_HFLAG_BL;
        gen_op_set_bcond();
        gen_op_save_bcond();
        break;
    case OPC_BC1FANY2:
        gen_op_bc1any2f(cc);
        opn = "bc1any2f";
        goto not_likely;
    case OPC_BC1TANY2:
        gen_op_bc1any2t(cc);
        opn = "bc1any2t";
        goto not_likely;
    case OPC_BC1FANY4:
        gen_op_bc1any4f(cc);
        opn = "bc1any4f";
        goto not_likely;
    case OPC_BC1TANY4:
        gen_op_bc1any4t(cc);
        opn = "bc1any4t";
    not_likely:
        ctx->hflags |= MIPS_HFLAG_BC;
        gen_op_set_bcond();
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception (ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s: cond %02x target " TARGET_FMT_lx, opn,
               ctx->hflags, btarget);
    ctx->btarget = btarget;
}

/* Coprocessor 1 (FPU) */

#define FOP(func, fmt) (((fmt) << 21) | (func))

static void gen_cp1 (DisasContext *ctx, uint32_t opc, int rt, int fs)
{
    const char *opn = "cp1 move";

    switch (opc) {
    case OPC_MFC1:
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_mfc1();
        GEN_STORE_TN_REG(rt, T0);
        opn = "mfc1";
        break;
    case OPC_MTC1:
        GEN_LOAD_REG_TN(T0, rt);
        gen_op_mtc1();
        GEN_STORE_FTN_FREG(fs, WT0);
        opn = "mtc1";
        break;
    case OPC_CFC1:
        GEN_LOAD_IMM_TN(T1, fs);
        gen_op_cfc1();
        GEN_STORE_TN_REG(rt, T0);
        opn = "cfc1";
        break;
    case OPC_CTC1:
        GEN_LOAD_IMM_TN(T1, fs);
        GEN_LOAD_REG_TN(T0, rt);
        gen_op_ctc1();
        opn = "ctc1";
        break;
    case OPC_DMFC1:
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_dmfc1();
        GEN_STORE_TN_REG(rt, T0);
        opn = "dmfc1";
        break;
    case OPC_DMTC1:
        GEN_LOAD_REG_TN(T0, rt);
        gen_op_dmtc1();
        GEN_STORE_FTN_FREG(fs, DT0);
        opn = "dmtc1";
        break;
    case OPC_MFHC1:
        GEN_LOAD_FREG_FTN(WTH0, fs);
        gen_op_mfhc1();
        GEN_STORE_TN_REG(rt, T0);
        opn = "mfhc1";
        break;
    case OPC_MTHC1:
        GEN_LOAD_REG_TN(T0, rt);
        gen_op_mthc1();
        GEN_STORE_FTN_FREG(fs, WTH0);
        opn = "mthc1";
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception (ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s %s", opn, regnames[rt], fregnames[fs]);
}

static void gen_movci (DisasContext *ctx, int rd, int rs, int cc, int tf)
{
    uint32_t ccbit;

    GEN_LOAD_REG_TN(T0, rd);
    GEN_LOAD_REG_TN(T1, rs);
    if (cc) {
        ccbit = 1 << (24 + cc);
    } else
        ccbit = 1 << 23;
    if (!tf)
        gen_op_movf(ccbit);
    else
        gen_op_movt(ccbit);
    GEN_STORE_TN_REG(rd, T0);
}

#define GEN_MOVCF(fmt)                                                \
static void glue(gen_movcf_, fmt) (DisasContext *ctx, int cc, int tf) \
{                                                                     \
    uint32_t ccbit;                                                   \
                                                                      \
    if (cc) {                                                         \
        ccbit = 1 << (24 + cc);                                       \
    } else                                                            \
        ccbit = 1 << 23;                                              \
    if (!tf)                                                          \
        glue(gen_op_float_movf_, fmt)(ccbit);                         \
    else                                                              \
        glue(gen_op_float_movt_, fmt)(ccbit);                         \
}
GEN_MOVCF(d);
GEN_MOVCF(s);
GEN_MOVCF(ps);
#undef GEN_MOVCF

static void gen_farith (DisasContext *ctx, uint32_t op1,
                        int ft, int fs, int fd, int cc)
{
    const char *opn = "farith";
    const char *condnames[] = {
            "c.f",
            "c.un",
            "c.eq",
            "c.ueq",
            "c.olt",
            "c.ult",
            "c.ole",
            "c.ule",
            "c.sf",
            "c.ngle",
            "c.seq",
            "c.ngl",
            "c.lt",
            "c.nge",
            "c.le",
            "c.ngt",
    };
    const char *condnames_abs[] = {
            "cabs.f",
            "cabs.un",
            "cabs.eq",
            "cabs.ueq",
            "cabs.olt",
            "cabs.ult",
            "cabs.ole",
            "cabs.ule",
            "cabs.sf",
            "cabs.ngle",
            "cabs.seq",
            "cabs.ngl",
            "cabs.lt",
            "cabs.nge",
            "cabs.le",
            "cabs.ngt",
    };
    enum { BINOP, CMPOP, OTHEROP } optype = OTHEROP;
    uint32_t func = ctx->opcode & 0x3f;

    switch (ctx->opcode & FOP(0x3f, 0x1f)) {
    case FOP(0, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        gen_op_float_add_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "add.s";
        optype = BINOP;
        break;
    case FOP(1, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        gen_op_float_sub_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "sub.s";
        optype = BINOP;
        break;
    case FOP(2, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        gen_op_float_mul_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "mul.s";
        optype = BINOP;
        break;
    case FOP(3, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        gen_op_float_div_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "div.s";
        optype = BINOP;
        break;
    case FOP(4, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_sqrt_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "sqrt.s";
        break;
    case FOP(5, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_abs_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "abs.s";
        break;
    case FOP(6, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_mov_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "mov.s";
        break;
    case FOP(7, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_chs_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "neg.s";
        break;
    case FOP(8, 16):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_roundl_s();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "round.l.s";
        break;
    case FOP(9, 16):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_truncl_s();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "trunc.l.s";
        break;
    case FOP(10, 16):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_ceill_s();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "ceil.l.s";
        break;
    case FOP(11, 16):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_floorl_s();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "floor.l.s";
        break;
    case FOP(12, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_roundw_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "round.w.s";
        break;
    case FOP(13, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_truncw_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "trunc.w.s";
        break;
    case FOP(14, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_ceilw_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "ceil.w.s";
        break;
    case FOP(15, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_floorw_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "floor.w.s";
        break;
    case FOP(17, 16):
        GEN_LOAD_REG_TN(T0, ft);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT2, fd);
        gen_movcf_s(ctx, (ft >> 2) & 0x7, ft & 0x1);
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "movcf.s";
        break;
    case FOP(18, 16):
        GEN_LOAD_REG_TN(T0, ft);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT2, fd);
        gen_op_float_movz_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "movz.s";
        break;
    case FOP(19, 16):
        GEN_LOAD_REG_TN(T0, ft);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT2, fd);
        gen_op_float_movn_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "movn.s";
        break;
    case FOP(21, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_recip_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "recip.s";
        break;
    case FOP(22, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_rsqrt_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "rsqrt.s";
        break;
    case FOP(28, 16):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT2, fd);
        gen_op_float_recip2_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "recip2.s";
        break;
    case FOP(29, 16):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_recip1_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "recip1.s";
        break;
    case FOP(30, 16):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_rsqrt1_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "rsqrt1.s";
        break;
    case FOP(31, 16):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT2, fd);
        gen_op_float_rsqrt2_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "rsqrt2.s";
        break;
    case FOP(33, 16):
        check_cp1_registers(ctx, fd);
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_cvtd_s();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "cvt.d.s";
        break;
    case FOP(36, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_cvtw_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "cvt.w.s";
        break;
    case FOP(37, 16):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_cvtl_s();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "cvt.l.s";
        break;
    case FOP(38, 16):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT1, fs);
        GEN_LOAD_FREG_FTN(WT0, ft);
        gen_op_float_cvtps_s();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "cvt.ps.s";
        break;
    case FOP(48, 16):
    case FOP(49, 16):
    case FOP(50, 16):
    case FOP(51, 16):
    case FOP(52, 16):
    case FOP(53, 16):
    case FOP(54, 16):
    case FOP(55, 16):
    case FOP(56, 16):
    case FOP(57, 16):
    case FOP(58, 16):
    case FOP(59, 16):
    case FOP(60, 16):
    case FOP(61, 16):
    case FOP(62, 16):
    case FOP(63, 16):
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        if (ctx->opcode & (1 << 6)) {
            check_cp1_64bitmode(ctx);
            gen_cmpabs_s(func-48, cc);
            opn = condnames_abs[func-48];
        } else {
            gen_cmp_s(func-48, cc);
            opn = condnames[func-48];
        }
        break;
    case FOP(0, 17):
        check_cp1_registers(ctx, fs | ft | fd);
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT1, ft);
        gen_op_float_add_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "add.d";
        optype = BINOP;
        break;
    case FOP(1, 17):
        check_cp1_registers(ctx, fs | ft | fd);
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT1, ft);
        gen_op_float_sub_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "sub.d";
        optype = BINOP;
        break;
    case FOP(2, 17):
        check_cp1_registers(ctx, fs | ft | fd);
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT1, ft);
        gen_op_float_mul_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "mul.d";
        optype = BINOP;
        break;
    case FOP(3, 17):
        check_cp1_registers(ctx, fs | ft | fd);
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT1, ft);
        gen_op_float_div_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "div.d";
        optype = BINOP;
        break;
    case FOP(4, 17):
        check_cp1_registers(ctx, fs | fd);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_sqrt_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "sqrt.d";
        break;
    case FOP(5, 17):
        check_cp1_registers(ctx, fs | fd);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_abs_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "abs.d";
        break;
    case FOP(6, 17):
        check_cp1_registers(ctx, fs | fd);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_mov_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "mov.d";
        break;
    case FOP(7, 17):
        check_cp1_registers(ctx, fs | fd);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_chs_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "neg.d";
        break;
    case FOP(8, 17):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_roundl_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "round.l.d";
        break;
    case FOP(9, 17):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_truncl_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "trunc.l.d";
        break;
    case FOP(10, 17):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_ceill_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "ceil.l.d";
        break;
    case FOP(11, 17):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_floorl_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "floor.l.d";
        break;
    case FOP(12, 17):
        check_cp1_registers(ctx, fs);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_roundw_d();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "round.w.d";
        break;
    case FOP(13, 17):
        check_cp1_registers(ctx, fs);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_truncw_d();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "trunc.w.d";
        break;
    case FOP(14, 17):
        check_cp1_registers(ctx, fs);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_ceilw_d();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "ceil.w.d";
        break;
    case FOP(15, 17):
        check_cp1_registers(ctx, fs);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_floorw_d();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "floor.w.d";
        break;
    case FOP(17, 17):
        GEN_LOAD_REG_TN(T0, ft);
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT2, fd);
        gen_movcf_d(ctx, (ft >> 2) & 0x7, ft & 0x1);
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "movcf.d";
        break;
    case FOP(18, 17):
        GEN_LOAD_REG_TN(T0, ft);
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT2, fd);
        gen_op_float_movz_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "movz.d";
        break;
    case FOP(19, 17):
        GEN_LOAD_REG_TN(T0, ft);
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT2, fd);
        gen_op_float_movn_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "movn.d";
        break;
    case FOP(21, 17):
        check_cp1_registers(ctx, fs | fd);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_recip_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "recip.d";
        break;
    case FOP(22, 17):
        check_cp1_registers(ctx, fs | fd);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_rsqrt_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "rsqrt.d";
        break;
    case FOP(28, 17):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT2, ft);
        gen_op_float_recip2_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "recip2.d";
        break;
    case FOP(29, 17):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_recip1_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "recip1.d";
        break;
    case FOP(30, 17):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_rsqrt1_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "rsqrt1.d";
        break;
    case FOP(31, 17):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT2, ft);
        gen_op_float_rsqrt2_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "rsqrt2.d";
        break;
    case FOP(48, 17):
    case FOP(49, 17):
    case FOP(50, 17):
    case FOP(51, 17):
    case FOP(52, 17):
    case FOP(53, 17):
    case FOP(54, 17):
    case FOP(55, 17):
    case FOP(56, 17):
    case FOP(57, 17):
    case FOP(58, 17):
    case FOP(59, 17):
    case FOP(60, 17):
    case FOP(61, 17):
    case FOP(62, 17):
    case FOP(63, 17):
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT1, ft);
        if (ctx->opcode & (1 << 6)) {
            check_cp1_64bitmode(ctx);
            gen_cmpabs_d(func-48, cc);
            opn = condnames_abs[func-48];
        } else {
            check_cp1_registers(ctx, fs | ft);
            gen_cmp_d(func-48, cc);
            opn = condnames[func-48];
        }
        break;
    case FOP(32, 17):
        check_cp1_registers(ctx, fs);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_cvts_d();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "cvt.s.d";
        break;
    case FOP(36, 17):
        check_cp1_registers(ctx, fs);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_cvtw_d();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "cvt.w.d";
        break;
    case FOP(37, 17):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_cvtl_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "cvt.l.d";
        break;
    case FOP(32, 20):
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_cvts_w();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "cvt.s.w";
        break;
    case FOP(33, 20):
        check_cp1_registers(ctx, fd);
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_cvtd_w();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "cvt.d.w";
        break;
    case FOP(32, 21):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_cvts_l();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "cvt.s.l";
        break;
    case FOP(33, 21):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(DT0, fs);
        gen_op_float_cvtd_l();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "cvt.d.l";
        break;
    case FOP(38, 20):
    case FOP(38, 21):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        gen_op_float_cvtps_pw();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "cvt.ps.pw";
        break;
    case FOP(0, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        GEN_LOAD_FREG_FTN(WTH1, ft);
        gen_op_float_add_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "add.ps";
        break;
    case FOP(1, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        GEN_LOAD_FREG_FTN(WTH1, ft);
        gen_op_float_sub_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "sub.ps";
        break;
    case FOP(2, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        GEN_LOAD_FREG_FTN(WTH1, ft);
        gen_op_float_mul_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "mul.ps";
        break;
    case FOP(5, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        gen_op_float_abs_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "abs.ps";
        break;
    case FOP(6, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        gen_op_float_mov_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "mov.ps";
        break;
    case FOP(7, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        gen_op_float_chs_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "neg.ps";
        break;
    case FOP(17, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_REG_TN(T0, ft);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT2, fd);
        GEN_LOAD_FREG_FTN(WTH2, fd);
        gen_movcf_ps(ctx, (ft >> 2) & 0x7, ft & 0x1);
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "movcf.ps";
        break;
    case FOP(18, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_REG_TN(T0, ft);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT2, fd);
        GEN_LOAD_FREG_FTN(WTH2, fd);
        gen_op_float_movz_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "movz.ps";
        break;
    case FOP(19, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_REG_TN(T0, ft);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT2, fd);
        GEN_LOAD_FREG_FTN(WTH2, fd);
        gen_op_float_movn_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "movn.ps";
        break;
    case FOP(24, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, ft);
        GEN_LOAD_FREG_FTN(WTH0, ft);
        GEN_LOAD_FREG_FTN(WT1, fs);
        GEN_LOAD_FREG_FTN(WTH1, fs);
        gen_op_float_addr_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "addr.ps";
        break;
    case FOP(26, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, ft);
        GEN_LOAD_FREG_FTN(WTH0, ft);
        GEN_LOAD_FREG_FTN(WT1, fs);
        GEN_LOAD_FREG_FTN(WTH1, fs);
        gen_op_float_mulr_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "mulr.ps";
        break;
    case FOP(28, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT2, fd);
        GEN_LOAD_FREG_FTN(WTH2, fd);
        gen_op_float_recip2_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "recip2.ps";
        break;
    case FOP(29, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        gen_op_float_recip1_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "recip1.ps";
        break;
    case FOP(30, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        gen_op_float_rsqrt1_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "rsqrt1.ps";
        break;
    case FOP(31, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT2, fd);
        GEN_LOAD_FREG_FTN(WTH2, fd);
        gen_op_float_rsqrt2_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "rsqrt2.ps";
        break;
    case FOP(32, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        gen_op_float_cvts_pu();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "cvt.s.pu";
        break;
    case FOP(36, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        gen_op_float_cvtpw_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "cvt.pw.ps";
        break;
    case FOP(40, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        gen_op_float_cvts_pl();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "cvt.s.pl";
        break;
    case FOP(44, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        gen_op_float_pll_ps();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "pll.ps";
        break;
    case FOP(45, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH1, ft);
        gen_op_float_plu_ps();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "plu.ps";
        break;
    case FOP(46, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        gen_op_float_pul_ps();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "pul.ps";
        break;
    case FOP(47, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WTH1, ft);
        gen_op_float_puu_ps();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "puu.ps";
        break;
    case FOP(48, 22):
    case FOP(49, 22):
    case FOP(50, 22):
    case FOP(51, 22):
    case FOP(52, 22):
    case FOP(53, 22):
    case FOP(54, 22):
    case FOP(55, 22):
    case FOP(56, 22):
    case FOP(57, 22):
    case FOP(58, 22):
    case FOP(59, 22):
    case FOP(60, 22):
    case FOP(61, 22):
    case FOP(62, 22):
    case FOP(63, 22):
        check_cp1_64bitmode(ctx);
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        GEN_LOAD_FREG_FTN(WTH1, ft);
        if (ctx->opcode & (1 << 6)) {
            gen_cmpabs_ps(func-48, cc);
            opn = condnames_abs[func-48];
        } else {
            gen_cmp_ps(func-48, cc);
            opn = condnames[func-48];
        }
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception (ctx, EXCP_RI);
        return;
    }
    switch (optype) {
    case BINOP:
        MIPS_DEBUG("%s %s, %s, %s", opn, fregnames[fd], fregnames[fs], fregnames[ft]);
        break;
    case CMPOP:
        MIPS_DEBUG("%s %s,%s", opn, fregnames[fs], fregnames[ft]);
        break;
    default:
        MIPS_DEBUG("%s %s,%s", opn, fregnames[fd], fregnames[fs]);
        break;
    }
}

/* Coprocessor 3 (FPU) */
static void gen_flt3_ldst (DisasContext *ctx, uint32_t opc,
                           int fd, int fs, int base, int index)
{
    const char *opn = "extended float load/store";
    int store = 0;

    /* All of those work only on 64bit FPUs. */
    check_cp1_64bitmode(ctx);
    if (base == 0) {
        if (index == 0)
            gen_op_reset_T0();
        else
            GEN_LOAD_REG_TN(T0, index);
    } else if (index == 0) {
        GEN_LOAD_REG_TN(T0, base);
    } else {
        GEN_LOAD_REG_TN(T0, base);
        GEN_LOAD_REG_TN(T1, index);
        gen_op_addr_add();
    }
    /* Don't do NOP if destination is zero: we must perform the actual
     * memory access
     */
    switch (opc) {
    case OPC_LWXC1:
        op_ldst(lwc1);
        GEN_STORE_FTN_FREG(fd, WT0);
        opn = "lwxc1";
        break;
    case OPC_LDXC1:
        op_ldst(ldc1);
        GEN_STORE_FTN_FREG(fd, DT0);
        opn = "ldxc1";
        break;
    case OPC_LUXC1:
        op_ldst(luxc1);
        GEN_STORE_FTN_FREG(fd, DT0);
        opn = "luxc1";
        break;
    case OPC_SWXC1:
        GEN_LOAD_FREG_FTN(WT0, fs);
        op_ldst(swc1);
        opn = "swxc1";
        store = 1;
        break;
    case OPC_SDXC1:
        GEN_LOAD_FREG_FTN(DT0, fs);
        op_ldst(sdc1);
        opn = "sdxc1";
        store = 1;
        break;
    case OPC_SUXC1:
        GEN_LOAD_FREG_FTN(DT0, fs);
        op_ldst(suxc1);
        opn = "suxc1";
        store = 1;
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s, %s(%s)", opn, fregnames[store ? fs : fd],
               regnames[index], regnames[base]);
}

static void gen_flt3_arith (DisasContext *ctx, uint32_t opc,
                            int fd, int fr, int fs, int ft)
{
    const char *opn = "flt3_arith";

    /* All of those work only on 64bit FPUs. */
    check_cp1_64bitmode(ctx);
    switch (opc) {
    case OPC_ALNV_PS:
        GEN_LOAD_REG_TN(T0, fr);
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT1, ft);
        gen_op_float_alnv_ps();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "alnv.ps";
        break;
    case OPC_MADD_S:
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        GEN_LOAD_FREG_FTN(WT2, fr);
        gen_op_float_muladd_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "madd.s";
        break;
    case OPC_MADD_D:
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT1, ft);
        GEN_LOAD_FREG_FTN(DT2, fr);
        gen_op_float_muladd_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "madd.d";
        break;
    case OPC_MADD_PS:
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        GEN_LOAD_FREG_FTN(WTH1, ft);
        GEN_LOAD_FREG_FTN(WT2, fr);
        GEN_LOAD_FREG_FTN(WTH2, fr);
        gen_op_float_muladd_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "madd.ps";
        break;
    case OPC_MSUB_S:
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        GEN_LOAD_FREG_FTN(WT2, fr);
        gen_op_float_mulsub_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "msub.s";
        break;
    case OPC_MSUB_D:
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT1, ft);
        GEN_LOAD_FREG_FTN(DT2, fr);
        gen_op_float_mulsub_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "msub.d";
        break;
    case OPC_MSUB_PS:
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        GEN_LOAD_FREG_FTN(WTH1, ft);
        GEN_LOAD_FREG_FTN(WT2, fr);
        GEN_LOAD_FREG_FTN(WTH2, fr);
        gen_op_float_mulsub_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "msub.ps";
        break;
    case OPC_NMADD_S:
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        GEN_LOAD_FREG_FTN(WT2, fr);
        gen_op_float_nmuladd_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "nmadd.s";
        break;
    case OPC_NMADD_D:
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT1, ft);
        GEN_LOAD_FREG_FTN(DT2, fr);
        gen_op_float_nmuladd_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "nmadd.d";
        break;
    case OPC_NMADD_PS:
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        GEN_LOAD_FREG_FTN(WTH1, ft);
        GEN_LOAD_FREG_FTN(WT2, fr);
        GEN_LOAD_FREG_FTN(WTH2, fr);
        gen_op_float_nmuladd_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "nmadd.ps";
        break;
    case OPC_NMSUB_S:
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        GEN_LOAD_FREG_FTN(WT2, fr);
        gen_op_float_nmulsub_s();
        GEN_STORE_FTN_FREG(fd, WT2);
        opn = "nmsub.s";
        break;
    case OPC_NMSUB_D:
        GEN_LOAD_FREG_FTN(DT0, fs);
        GEN_LOAD_FREG_FTN(DT1, ft);
        GEN_LOAD_FREG_FTN(DT2, fr);
        gen_op_float_nmulsub_d();
        GEN_STORE_FTN_FREG(fd, DT2);
        opn = "nmsub.d";
        break;
    case OPC_NMSUB_PS:
        GEN_LOAD_FREG_FTN(WT0, fs);
        GEN_LOAD_FREG_FTN(WTH0, fs);
        GEN_LOAD_FREG_FTN(WT1, ft);
        GEN_LOAD_FREG_FTN(WTH1, ft);
        GEN_LOAD_FREG_FTN(WT2, fr);
        GEN_LOAD_FREG_FTN(WTH2, fr);
        gen_op_float_nmulsub_ps();
        GEN_STORE_FTN_FREG(fd, WT2);
        GEN_STORE_FTN_FREG(fd, WTH2);
        opn = "nmsub.ps";
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception (ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s, %s, %s, %s", opn, fregnames[fd], fregnames[fr],
               fregnames[fs], fregnames[ft]);
}

/* ISA extensions (ASEs) */
/* MIPS16 extension to MIPS32 */
/* SmartMIPS extension to MIPS32 */

#ifdef TARGET_MIPS64

/* MDMX extension to MIPS64 */
/* MIPS-3D extension to MIPS64 */

#endif

static void decode_opc (CPUState *env, DisasContext *ctx)
{
    int32_t offset;
    int rs, rt, rd, sa;
    uint32_t op, op1, op2;
    int16_t imm;

    /* make sure instructions are on a word boundary */
    if (ctx->pc & 0x3) {
        env->CP0_BadVAddr = ctx->pc;
        generate_exception(ctx, EXCP_AdEL);
        return;
    }

    if ((ctx->hflags & MIPS_HFLAG_BMASK) == MIPS_HFLAG_BL) {
        int l1;
        /* Handle blikely not taken case */
        MIPS_DEBUG("blikely condition (" TARGET_FMT_lx ")", ctx->pc + 4);
        l1 = gen_new_label();
        gen_op_jnz_T2(l1);
        gen_op_save_state(ctx->hflags & ~MIPS_HFLAG_BMASK);
        gen_goto_tb(ctx, 1, ctx->pc + 4);
        gen_set_label(l1);
    }
    op = MASK_OP_MAJOR(ctx->opcode);
    rs = (ctx->opcode >> 21) & 0x1f;
    rt = (ctx->opcode >> 16) & 0x1f;
    rd = (ctx->opcode >> 11) & 0x1f;
    sa = (ctx->opcode >> 6) & 0x1f;
    imm = (int16_t)ctx->opcode;
    switch (op) {
    case OPC_SPECIAL:
        op1 = MASK_SPECIAL(ctx->opcode);
        switch (op1) {
        case OPC_SLL:          /* Arithmetic with immediate */
        case OPC_SRL ... OPC_SRA:
            gen_arith_imm(ctx, op1, rd, rt, sa);
            break;
        case OPC_SLLV:         /* Arithmetic */
        case OPC_SRLV ... OPC_SRAV:
        case OPC_MOVZ ... OPC_MOVN:
        case OPC_ADD ... OPC_NOR:
        case OPC_SLT ... OPC_SLTU:
            gen_arith(ctx, op1, rd, rs, rt);
            break;
        case OPC_MULT ... OPC_DIVU:
            gen_muldiv(ctx, op1, rs, rt);
            break;
        case OPC_JR ... OPC_JALR:
            gen_compute_branch(ctx, op1, rs, rd, sa);
            return;
        case OPC_TGE ... OPC_TEQ: /* Traps */
        case OPC_TNE:
            gen_trap(ctx, op1, rs, rt, -1);
            break;
        case OPC_MFHI:          /* Move from HI/LO */
        case OPC_MFLO:
            gen_HILO(ctx, op1, rd);
            break;
        case OPC_MTHI:
        case OPC_MTLO:          /* Move to HI/LO */
            gen_HILO(ctx, op1, rs);
            break;
        case OPC_PMON:          /* Pmon entry point, also R4010 selsl */
#ifdef MIPS_STRICT_STANDARD
            MIPS_INVAL("PMON / selsl");
            generate_exception(ctx, EXCP_RI);
#else
            gen_op_pmon(sa);
#endif
            break;
        case OPC_SYSCALL:
            generate_exception(ctx, EXCP_SYSCALL);
            break;
        case OPC_BREAK:
            /* XXX: Hack to work around wrong handling of self-modifying code. */
            ctx->pc += 4;
            save_cpu_state(ctx, 1);
            ctx->pc -= 4;
            generate_exception(ctx, EXCP_BREAK);
            break;
        case OPC_SPIM:
#ifdef MIPS_STRICT_STANDARD
            MIPS_INVAL("SPIM");
            generate_exception(ctx, EXCP_RI);
#else
           /* Implemented as RI exception for now. */
            MIPS_INVAL("spim (unofficial)");
            generate_exception(ctx, EXCP_RI);
#endif
            break;
        case OPC_SYNC:
            /* Treat as a noop. */
            break;

        case OPC_MOVCI:
            if (env->CP0_Config1 & (1 << CP0C1_FP)) {
                save_cpu_state(ctx, 1);
                check_cp1_enabled(ctx);
                gen_movci(ctx, rd, rs, (ctx->opcode >> 18) & 0x7,
                          (ctx->opcode >> 16) & 1);
            } else {
                generate_exception_err(ctx, EXCP_CpU, 1);
            }
            break;

#ifdef TARGET_MIPS64
       /* MIPS64 specific opcodes */
        case OPC_DSLL:
        case OPC_DSRL ... OPC_DSRA:
        case OPC_DSLL32:
        case OPC_DSRL32 ... OPC_DSRA32:
            if (!(ctx->hflags & MIPS_HFLAG_64))
                generate_exception(ctx, EXCP_RI);
            gen_arith_imm(ctx, op1, rd, rt, sa);
            break;
        case OPC_DSLLV:
        case OPC_DSRLV ... OPC_DSRAV:
        case OPC_DADD ... OPC_DSUBU:
            if (!(ctx->hflags & MIPS_HFLAG_64))
                generate_exception(ctx, EXCP_RI);
            gen_arith(ctx, op1, rd, rs, rt);
            break;
        case OPC_DMULT ... OPC_DDIVU:
            if (!(ctx->hflags & MIPS_HFLAG_64))
                generate_exception(ctx, EXCP_RI);
            gen_muldiv(ctx, op1, rs, rt);
            break;
#endif
        default:            /* Invalid */
            MIPS_INVAL("special");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_SPECIAL2:
        op1 = MASK_SPECIAL2(ctx->opcode);
        switch (op1) {
        case OPC_MADD ... OPC_MADDU: /* Multiply and add/sub */
        case OPC_MSUB ... OPC_MSUBU:
            gen_muldiv(ctx, op1, rs, rt);
            break;
        case OPC_MUL:
            gen_arith(ctx, op1, rd, rs, rt);
            break;
        case OPC_CLZ ... OPC_CLO:
            gen_cl(ctx, op1, rd, rs);
            break;
        case OPC_SDBBP:
            /* XXX: not clear which exception should be raised
             *      when in debug mode...
             */
            if (!(ctx->hflags & MIPS_HFLAG_DM)) {
                generate_exception(ctx, EXCP_DBp);
            } else {
                generate_exception(ctx, EXCP_DBp);
            }
            /* Treat as a noop */
            break;
#ifdef TARGET_MIPS64
        case OPC_DCLZ ... OPC_DCLO:
            if (!(ctx->hflags & MIPS_HFLAG_64))
                generate_exception(ctx, EXCP_RI);
            gen_cl(ctx, op1, rd, rs);
            break;
#endif
        default:            /* Invalid */
            MIPS_INVAL("special2");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_SPECIAL3:
         check_mips_r2(env, ctx);
         op1 = MASK_SPECIAL3(ctx->opcode);
         switch (op1) {
         case OPC_EXT:
         case OPC_INS:
             gen_bitops(ctx, op1, rt, rs, sa, rd);
             break;
         case OPC_BSHFL:
             op2 = MASK_BSHFL(ctx->opcode);
             switch (op2) {
             case OPC_WSBH:
                 GEN_LOAD_REG_TN(T1, rt);
                 gen_op_wsbh();
                 break;
             case OPC_SEB:
                 GEN_LOAD_REG_TN(T1, rt);
                 gen_op_seb();
                 break;
             case OPC_SEH:
                 GEN_LOAD_REG_TN(T1, rt);
                 gen_op_seh();
                 break;
             default:            /* Invalid */
                 MIPS_INVAL("bshfl");
                 generate_exception(ctx, EXCP_RI);
                 break;
            }
            GEN_STORE_TN_REG(rd, T0);
            break;
        case OPC_RDHWR:
            switch (rd) {
            case 0:
                save_cpu_state(ctx, 1);
                gen_op_rdhwr_cpunum();
                break;
            case 1:
                save_cpu_state(ctx, 1);
                gen_op_rdhwr_synci_step();
                break;
            case 2:
                save_cpu_state(ctx, 1);
                gen_op_rdhwr_cc();
                break;
            case 3:
                save_cpu_state(ctx, 1);
                gen_op_rdhwr_ccres();
                break;
            case 29:
#if defined (CONFIG_USER_ONLY)
                gen_op_tls_value ();
                break;
#endif
            default:            /* Invalid */
                MIPS_INVAL("rdhwr");
                generate_exception(ctx, EXCP_RI);
                break;
            }
            GEN_STORE_TN_REG(rt, T0);
            break;
#ifdef TARGET_MIPS64
        case OPC_DEXTM ... OPC_DEXT:
        case OPC_DINSM ... OPC_DINS:
            if (!(ctx->hflags & MIPS_HFLAG_64))
                generate_exception(ctx, EXCP_RI);
            gen_bitops(ctx, op1, rt, rs, sa, rd);
            break;
        case OPC_DBSHFL:
            if (!(ctx->hflags & MIPS_HFLAG_64))
                generate_exception(ctx, EXCP_RI);
            op2 = MASK_DBSHFL(ctx->opcode);
            switch (op2) {
            case OPC_DSBH:
                GEN_LOAD_REG_TN(T1, rt);
                gen_op_dsbh();
                break;
            case OPC_DSHD:
                GEN_LOAD_REG_TN(T1, rt);
                gen_op_dshd();
                break;
            default:            /* Invalid */
                MIPS_INVAL("dbshfl");
                generate_exception(ctx, EXCP_RI);
                break;
            }
            GEN_STORE_TN_REG(rd, T0);
#endif
        default:            /* Invalid */
            MIPS_INVAL("special3");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_REGIMM:
        op1 = MASK_REGIMM(ctx->opcode);
        switch (op1) {
        case OPC_BLTZ ... OPC_BGEZL: /* REGIMM branches */
        case OPC_BLTZAL ... OPC_BGEZALL:
            gen_compute_branch(ctx, op1, rs, -1, imm << 2);
            return;
        case OPC_TGEI ... OPC_TEQI: /* REGIMM traps */
        case OPC_TNEI:
            gen_trap(ctx, op1, rs, -1, imm);
            break;
        case OPC_SYNCI:
            check_mips_r2(env, ctx);
            /* treat as noop */
            break;
        default:            /* Invalid */
            MIPS_INVAL("regimm");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_CP0:
        save_cpu_state(ctx, 1);
        gen_op_cp0_enabled();
        op1 = MASK_CP0(ctx->opcode);
        switch (op1) {
        case OPC_MFC0:
        case OPC_MTC0:
#ifdef TARGET_MIPS64
        case OPC_DMFC0:
        case OPC_DMTC0:
#endif
            gen_cp0(env, ctx, op1, rt, rd);
            break;
        case OPC_C0_FIRST ... OPC_C0_LAST:
            gen_cp0(env, ctx, MASK_C0(ctx->opcode), rt, rd);
            break;
        case OPC_MFMC0:
            check_mips_r2(env, ctx);
            op2 = MASK_MFMC0(ctx->opcode);
            switch (op2) {
            case OPC_DI:
                gen_op_di();
                /* Stop translation as we may have switched the execution mode */
                ctx->bstate = BS_STOP;
                break;
            case OPC_EI:
                gen_op_ei();
                /* Stop translation as we may have switched the execution mode */
                ctx->bstate = BS_STOP;
                break;
            default:            /* Invalid */
                MIPS_INVAL("mfmc0");
                generate_exception(ctx, EXCP_RI);
                break;
            }
            GEN_STORE_TN_REG(rt, T0);
            break;
        case OPC_RDPGPR:
        case OPC_WRPGPR:
            check_mips_r2(env, ctx);
            /* Shadow registers not implemented. */
            GEN_LOAD_REG_TN(T0, rt);
            GEN_STORE_TN_REG(rd, T0);
            break;
        default:
            MIPS_INVAL("cp0");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_ADDI ... OPC_LUI: /* Arithmetic with immediate opcode */
         gen_arith_imm(ctx, op, rt, rs, imm);
         break;
    case OPC_J ... OPC_JAL: /* Jump */
         offset = (int32_t)(ctx->opcode & 0x3FFFFFF) << 2;
         gen_compute_branch(ctx, op, rs, rt, offset);
         return;
    case OPC_BEQ ... OPC_BGTZ: /* Branch */
    case OPC_BEQL ... OPC_BGTZL:
         gen_compute_branch(ctx, op, rs, rt, imm << 2);
         return;
    case OPC_LB ... OPC_LWR: /* Load and stores */
    case OPC_SB ... OPC_SW:
    case OPC_SWR:
    case OPC_LL:
    case OPC_SC:
         gen_ldst(ctx, op, rt, rs, imm);
         break;
    case OPC_CACHE:
        /* Treat as a noop */
        break;
    case OPC_PREF:
        /* Treat as a noop */
        break;

    /* Floating point (COP1). */
    case OPC_LWC1:
    case OPC_LDC1:
    case OPC_SWC1:
    case OPC_SDC1:
        if (env->CP0_Config1 & (1 << CP0C1_FP)) {
            save_cpu_state(ctx, 1);
            check_cp1_enabled(ctx);
            gen_flt_ldst(ctx, op, rt, rs, imm);
        } else {
            generate_exception_err(ctx, EXCP_CpU, 1);
        }
        break;

    case OPC_CP1:
        if (env->CP0_Config1 & (1 << CP0C1_FP)) {
            save_cpu_state(ctx, 1);
            check_cp1_enabled(ctx);
            op1 = MASK_CP1(ctx->opcode);
            switch (op1) {
            case OPC_MFHC1:
            case OPC_MTHC1:
                check_mips_r2(env, ctx);
            case OPC_MFC1:
            case OPC_CFC1:
            case OPC_MTC1:
            case OPC_CTC1:
#ifdef TARGET_MIPS64
            case OPC_DMFC1:
            case OPC_DMTC1:
#endif
                gen_cp1(ctx, op1, rt, rd);
                break;
            case OPC_BC1:
            case OPC_BC1ANY2:
            case OPC_BC1ANY4:
                gen_compute_branch1(ctx, MASK_BC1(ctx->opcode),
                                    (rt >> 2) & 0x7, imm << 2);
                return;
            case OPC_S_FMT:
            case OPC_D_FMT:
            case OPC_W_FMT:
            case OPC_L_FMT:
            case OPC_PS_FMT:
                gen_farith(ctx, MASK_CP1_FUNC(ctx->opcode), rt, rd, sa,
                           (imm >> 8) & 0x7);
                break;
            default:
                MIPS_INVAL("cp1");
                generate_exception (ctx, EXCP_RI);
                break;
            }
        } else {
            generate_exception_err(ctx, EXCP_CpU, 1);
        }
        break;

    /* COP2.  */
    case OPC_LWC2:
    case OPC_LDC2:
    case OPC_SWC2:
    case OPC_SDC2:
    case OPC_CP2:
        /* COP2: Not implemented. */
        generate_exception_err(ctx, EXCP_CpU, 2);
        break;

    case OPC_CP3:
        if (env->CP0_Config1 & (1 << CP0C1_FP)) {
            save_cpu_state(ctx, 1);
            check_cp1_enabled(ctx);
            op1 = MASK_CP3(ctx->opcode);
            switch (op1) {
            case OPC_LWXC1:
            case OPC_LDXC1:
            case OPC_LUXC1:
            case OPC_SWXC1:
            case OPC_SDXC1:
            case OPC_SUXC1:
                gen_flt3_ldst(ctx, op1, sa, rd, rs, rt);
                break;
            case OPC_PREFX:
                /* treat as noop */
                break;
            case OPC_ALNV_PS:
            case OPC_MADD_S:
            case OPC_MADD_D:
            case OPC_MADD_PS:
            case OPC_MSUB_S:
            case OPC_MSUB_D:
            case OPC_MSUB_PS:
            case OPC_NMADD_S:
            case OPC_NMADD_D:
            case OPC_NMADD_PS:
            case OPC_NMSUB_S:
            case OPC_NMSUB_D:
            case OPC_NMSUB_PS:
                gen_flt3_arith(ctx, op1, sa, rs, rd, rt);
                break;
            default:
                MIPS_INVAL("cp3");
                generate_exception (ctx, EXCP_RI);
                break;
            }
        } else {
            generate_exception_err(ctx, EXCP_CpU, 1);
        }
        break;

#ifdef TARGET_MIPS64
    /* MIPS64 opcodes */
    case OPC_LWU:
    case OPC_LDL ... OPC_LDR:
    case OPC_SDL ... OPC_SDR:
    case OPC_LLD:
    case OPC_LD:
    case OPC_SCD:
    case OPC_SD:
        if (!(ctx->hflags & MIPS_HFLAG_64))
            generate_exception(ctx, EXCP_RI);
        gen_ldst(ctx, op, rt, rs, imm);
        break;
    case OPC_DADDI ... OPC_DADDIU:
        if (!(ctx->hflags & MIPS_HFLAG_64))
            generate_exception(ctx, EXCP_RI);
        gen_arith_imm(ctx, op, rt, rs, imm);
        break;
#endif
#ifdef MIPS_HAS_MIPS16
    case OPC_JALX:
        /* MIPS16: Not implemented. */
#endif
#ifdef MIPS_HAS_MDMX
    case OPC_MDMX:
        /* MDMX: Not implemented. */
#endif
    default:            /* Invalid */
        MIPS_INVAL("major opcode");
        generate_exception(ctx, EXCP_RI);
        break;
    }
    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        int hflags = ctx->hflags & MIPS_HFLAG_BMASK;
        /* Branches completion */
        ctx->hflags &= ~MIPS_HFLAG_BMASK;
        ctx->bstate = BS_BRANCH;
        save_cpu_state(ctx, 0);
        switch (hflags) {
        case MIPS_HFLAG_B:
            /* unconditional branch */
            MIPS_DEBUG("unconditional branch");
            gen_goto_tb(ctx, 0, ctx->btarget);
            break;
        case MIPS_HFLAG_BL:
            /* blikely taken case */
            MIPS_DEBUG("blikely branch taken");
            gen_goto_tb(ctx, 0, ctx->btarget);
            break;
        case MIPS_HFLAG_BC:
            /* Conditional branch */
            MIPS_DEBUG("conditional branch");
            {
              int l1;
              l1 = gen_new_label();
              gen_op_jnz_T2(l1);
              gen_goto_tb(ctx, 1, ctx->pc + 4);
              gen_set_label(l1);
              gen_goto_tb(ctx, 0, ctx->btarget);
            }
            break;
        case MIPS_HFLAG_BR:
            /* unconditional branch to register */
            MIPS_DEBUG("branch to register");
            gen_op_breg();
            gen_op_reset_T0();
            gen_op_exit_tb();
            break;
        default:
            MIPS_DEBUG("unknown branch");
            break;
        }
    }
}

static inline int
gen_intermediate_code_internal (CPUState *env, TranslationBlock *tb,
                                int search_pc)
{
    DisasContext ctx;
    target_ulong pc_start;
    uint16_t *gen_opc_end;
    int j, lj = -1;

    if (search_pc && loglevel)
        fprintf (logfile, "search pc %d\n", search_pc);

    pc_start = tb->pc;
    gen_opc_ptr = gen_opc_buf;
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    gen_opparam_ptr = gen_opparam_buf;
    nb_gen_labels = 0;
    ctx.pc = pc_start;
    ctx.saved_pc = -1;
    ctx.tb = tb;
    ctx.bstate = BS_NONE;
    /* Restore delay slot state from the tb context.  */
    ctx.hflags = tb->flags;
    restore_cpu_state(env, &ctx);
#if defined(CONFIG_USER_ONLY)
    ctx.mem_idx = 0;
#else
    ctx.mem_idx = !((ctx.hflags & MIPS_HFLAG_MODE) == MIPS_HFLAG_UM);
#endif
#ifdef DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_CPU) {
        fprintf(logfile, "------------------------------------------------\n");
        /* FIXME: This may print out stale hflags from env... */
        cpu_dump_state(env, logfile, fprintf, 0);
    }
#endif
#ifdef MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM)
        fprintf(logfile, "\ntb %p super %d cond %04x\n",
                tb, ctx.mem_idx, ctx.hflags);
#endif
    while (ctx.bstate == BS_NONE && gen_opc_ptr < gen_opc_end) {
        if (env->nb_breakpoints > 0) {
            for(j = 0; j < env->nb_breakpoints; j++) {
                if (env->breakpoints[j] == ctx.pc) {
                    save_cpu_state(&ctx, 1);
                    ctx.bstate = BS_BRANCH;
                    gen_op_debug();
                    goto done_generating;
                }
            }
        }

        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    gen_opc_instr_start[lj++] = 0;
            }
            gen_opc_pc[lj] = ctx.pc;
            gen_opc_hflags[lj] = ctx.hflags & MIPS_HFLAG_BMASK;
            gen_opc_instr_start[lj] = 1;
        }
        ctx.opcode = ldl_code(ctx.pc);
        decode_opc(env, &ctx);
        ctx.pc += 4;

        if (env->singlestep_enabled)
            break;

        if ((ctx.pc & (TARGET_PAGE_SIZE - 1)) == 0)
            break;

#if defined (MIPS_SINGLE_STEP)
        break;
#endif
    }
    if (env->singlestep_enabled) {
        save_cpu_state(&ctx, ctx.bstate == BS_NONE);
        gen_op_debug();
    } else {
	switch (ctx.bstate) {
        case BS_STOP:
            gen_op_interrupt_restart();
            gen_goto_tb(&ctx, 0, ctx.pc);
            break;
        case BS_NONE:
            save_cpu_state(&ctx, 0);
            gen_goto_tb(&ctx, 0, ctx.pc);
            break;
        case BS_EXCP:
            gen_op_interrupt_restart();
            gen_op_reset_T0();
            gen_op_exit_tb();
            break;
        case BS_BRANCH:
        default:
            break;
	}
    }
done_generating:
    *gen_opc_ptr = INDEX_op_end;
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
        tb->size = 0;
    } else {
        tb->size = ctx.pc - pc_start;
    }
#ifdef DEBUG_DISAS
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM)
        fprintf(logfile, "\n");
#endif
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
        target_disas(logfile, pc_start, ctx.pc - pc_start, 0);
        fprintf(logfile, "\n");
    }
    if (loglevel & CPU_LOG_TB_OP) {
        fprintf(logfile, "OP:\n");
        dump_ops(gen_opc_buf, gen_opparam_buf);
        fprintf(logfile, "\n");
    }
    if (loglevel & CPU_LOG_TB_CPU) {
        fprintf(logfile, "---------------- %d %08x\n", ctx.bstate, ctx.hflags);
    }
#endif
    
    return 0;
}

int gen_intermediate_code (CPUState *env, struct TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 0);
}

int gen_intermediate_code_pc (CPUState *env, struct TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 1);
}

void fpu_dump_state(CPUState *env, FILE *f, 
                    int (*fpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags)
{
    int i;
    int is_fpu64 = !!(env->hflags & MIPS_HFLAG_F64);

#define printfpr(fp)                                                        \
    do {                                                                    \
        if (is_fpu64)                                                       \
            fpu_fprintf(f, "w:%08x d:%016lx fd:%13g fs:%13g psu: %13g\n",   \
                        (fp)->w[FP_ENDIAN_IDX], (fp)->d, (fp)->fd,          \
                        (fp)->fs[FP_ENDIAN_IDX], (fp)->fs[!FP_ENDIAN_IDX]); \
        else {                                                              \
            fpr_t tmp;                                                      \
            tmp.w[FP_ENDIAN_IDX] = (fp)->w[FP_ENDIAN_IDX];                  \
            tmp.w[!FP_ENDIAN_IDX] = ((fp) + 1)->w[FP_ENDIAN_IDX];           \
            fpu_fprintf(f, "w:%08x d:%016lx fd:%13g fs:%13g psu:%13g\n",    \
                        tmp.w[FP_ENDIAN_IDX], tmp.d, tmp.fd,                \
                        tmp.fs[FP_ENDIAN_IDX], tmp.fs[!FP_ENDIAN_IDX]);     \
        }                                                                   \
    } while(0)


    fpu_fprintf(f, "CP1 FCR0 0x%08x  FCR31 0x%08x  SR.FR %d  fp_status 0x%08x(0x%02x)\n",
                env->fcr0, env->fcr31, is_fpu64, env->fp_status, get_float_exception_flags(&env->fp_status));
    fpu_fprintf(f, "FT0: "); printfpr(&env->ft0);
    fpu_fprintf(f, "FT1: "); printfpr(&env->ft1);
    fpu_fprintf(f, "FT2: "); printfpr(&env->ft2);
    for (i = 0; i < 32; (is_fpu64) ? i++ : (i += 2)) {
        fpu_fprintf(f, "%3s: ", fregnames[i]);
        printfpr(&env->fpr[i]);
    }

#undef printfpr
}

void dump_fpu (CPUState *env)
{
    if (loglevel) { 
       fprintf(logfile, "pc=0x" TARGET_FMT_lx " HI=0x" TARGET_FMT_lx " LO=0x" TARGET_FMT_lx " ds %04x " TARGET_FMT_lx " %d\n",
               env->PC, env->HI, env->LO, env->hflags, env->btarget, env->bcond);
       fpu_dump_state(env, logfile, fprintf, 0);
    }
}

#if defined(TARGET_MIPS64) && defined(MIPS_DEBUG_SIGN_EXTENSIONS)
/* Debug help: The architecture requires 32bit code to maintain proper
   sign-extened values on 64bit machines.  */

#define SIGN_EXT_P(val) ((((val) & ~0x7fffffff) == 0) || (((val) & ~0x7fffffff) == ~0x7fffffff))

void cpu_mips_check_sign_extensions (CPUState *env, FILE *f,
                     int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                     int flags)
{
    int i;

    if (!SIGN_EXT_P(env->PC))
        cpu_fprintf(f, "BROKEN: pc=0x" TARGET_FMT_lx "\n", env->PC);
    if (!SIGN_EXT_P(env->HI))
        cpu_fprintf(f, "BROKEN: HI=0x" TARGET_FMT_lx "\n", env->HI);
    if (!SIGN_EXT_P(env->LO))
        cpu_fprintf(f, "BROKEN: LO=0x" TARGET_FMT_lx "\n", env->LO);
    if (!SIGN_EXT_P(env->btarget))
        cpu_fprintf(f, "BROKEN: btarget=0x" TARGET_FMT_lx "\n", env->btarget);

    for (i = 0; i < 32; i++) {
        if (!SIGN_EXT_P(env->gpr[i]))
            cpu_fprintf(f, "BROKEN: %s=0x" TARGET_FMT_lx "\n", regnames[i], env->gpr[i]);
    }

    if (!SIGN_EXT_P(env->CP0_EPC))
        cpu_fprintf(f, "BROKEN: EPC=0x" TARGET_FMT_lx "\n", env->CP0_EPC);
    if (!SIGN_EXT_P(env->CP0_LLAddr))
        cpu_fprintf(f, "BROKEN: LLAddr=0x" TARGET_FMT_lx "\n", env->CP0_LLAddr);
}
#endif

void cpu_dump_state (CPUState *env, FILE *f, 
                     int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                     int flags)
{
    int i;
    
    cpu_fprintf(f, "pc=0x" TARGET_FMT_lx " HI=0x" TARGET_FMT_lx " LO=0x" TARGET_FMT_lx " ds %04x " TARGET_FMT_lx " %d\n",
                env->PC, env->HI, env->LO, env->hflags, env->btarget, env->bcond);
    for (i = 0; i < 32; i++) {
        if ((i & 3) == 0)
            cpu_fprintf(f, "GPR%02d:", i);
        cpu_fprintf(f, " %s " TARGET_FMT_lx, regnames[i], env->gpr[i]);
        if ((i & 3) == 3)
            cpu_fprintf(f, "\n");
    }

    cpu_fprintf(f, "CP0 Status  0x%08x Cause   0x%08x EPC    0x" TARGET_FMT_lx "\n",
                env->CP0_Status, env->CP0_Cause, env->CP0_EPC);
    cpu_fprintf(f, "    Config0 0x%08x Config1 0x%08x LLAddr 0x" TARGET_FMT_lx "\n",
                env->CP0_Config0, env->CP0_Config1, env->CP0_LLAddr);
    if (env->hflags & MIPS_HFLAG_FPU)
        fpu_dump_state(env, f, cpu_fprintf, flags);
#if defined(TARGET_MIPS64) && defined(MIPS_DEBUG_SIGN_EXTENSIONS)
    cpu_mips_check_sign_extensions(env, f, cpu_fprintf, flags);
#endif
}

CPUMIPSState *cpu_mips_init (void)
{
    CPUMIPSState *env;

    env = qemu_mallocz(sizeof(CPUMIPSState));
    if (!env)
        return NULL;
    cpu_exec_init(env);
    cpu_reset(env);
    return env;
}

void cpu_reset (CPUMIPSState *env)
{
    memset(env, 0, offsetof(CPUMIPSState, breakpoints));

    tlb_flush(env, 1);

    /* Minimal init */
#if !defined(CONFIG_USER_ONLY)
    if (env->hflags & MIPS_HFLAG_BMASK) {
        /* If the exception was raised from a delay slot,
         * come back to the jump.  */
        env->CP0_ErrorEPC = env->PC - 4;
    } else {
        env->CP0_ErrorEPC = env->PC;
    }
#ifdef TARGET_MIPS64
    env->hflags = MIPS_HFLAG_64;
#else
    env->hflags = 0;
#endif
    env->PC = (int32_t)0xBFC00000;
    env->CP0_Wired = 0;
    /* SMP not implemented */
    env->CP0_EBase = 0x80000000;
    env->CP0_Status = (1 << CP0St_BEV) | (1 << CP0St_ERL);
    /* vectored interrupts not implemented, timer on int 7,
       no performance counters. */
    env->CP0_IntCtl = 0xe0000000;
    {
        int i;

        for (i = 0; i < 7; i++) {
            env->CP0_WatchLo[i] = 0;
            env->CP0_WatchHi[i] = 0x80000000;
        }
        env->CP0_WatchLo[7] = 0;
        env->CP0_WatchHi[7] = 0;
    }
    /* Count register increments in debug mode, EJTAG version 1 */
    env->CP0_Debug = (1 << CP0DB_CNT) | (0x1 << CP0DB_VER);
#endif
    env->exception_index = EXCP_NONE;
#if defined(CONFIG_USER_ONLY)
    env->hflags |= MIPS_HFLAG_UM;
    env->user_mode_only = 1;
#endif
}

#include "translate_init.c"
