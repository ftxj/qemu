/*
 * ARM virtual CPU header
 * 
 *  Copyright (c) 2003 Fabrice Bellard
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
#ifndef CPU_ARM_H
#define CPU_ARM_H

#define TARGET_LONG_BITS 32

#define ELF_MACHINE	EM_ARM

#include "cpu-defs.h"

#include "softfloat.h"

#define TARGET_HAS_ICE 1

#define EXCP_UDEF            1   /* undefined instruction */
#define EXCP_SWI             2   /* software interrupt */
#define EXCP_PREFETCH_ABORT  3
#define EXCP_DATA_ABORT      4
#define EXCP_IRQ             5
#define EXCP_FIQ             6
#define EXCP_BKPT            7

typedef void ARMWriteCPFunc(void *opaque, int cp_info,
                            int srcreg, int operand, uint32_t value);
typedef uint32_t ARMReadCPFunc(void *opaque, int cp_info,
                               int dstreg, int operand);

/* We currently assume float and double are IEEE single and double
   precision respectively.
   Doing runtime conversions is tricky because VFP registers may contain
   integer values (eg. as the result of a FTOSI instruction).
   s<2n> maps to the least significant half of d<n>
   s<2n+1> maps to the most significant half of d<n>
 */

typedef struct CPUARMState {
    /* Regs for current mode.  */
    uint32_t regs[16];
    /* Frequently accessed CPSR bits are stored separately for efficiently.
       This contains all the other bits.  Use cpsr_{read,write} to access
       the whole CPSR.  */
    uint32_t uncached_cpsr;
    uint32_t spsr;

    /* Banked registers.  */
    uint32_t banked_spsr[6];
    uint32_t banked_r13[6];
    uint32_t banked_r14[6];
    
    /* These hold r8-r12.  */
    uint32_t usr_regs[5];
    uint32_t fiq_regs[5];
    
    /* cpsr flag cache for faster execution */
    uint32_t CF; /* 0 or 1 */
    uint32_t VF; /* V is the bit 31. All other bits are undefined */
    uint32_t NZF; /* N is bit 31. Z is computed from NZF */
    uint32_t QF; /* 0 or 1 */

    int thumb; /* 0 = arm mode, 1 = thumb mode */

    /* System control coprocessor (cp15) */
    struct {
        uint32_t c0_cpuid;
        uint32_t c0_cachetype;
        uint32_t c1_sys; /* System control register.  */
        uint32_t c1_coproc; /* Coprocessor access register.  */
        uint32_t c2_base; /* MMU translation table base.  */
        uint32_t c2_data; /* MPU data cachable bits.  */
        uint32_t c2_insn; /* MPU instruction cachable bits.  */
        uint32_t c3; /* MMU domain access control register
                        MPU write buffer control.  */
        uint32_t c5_insn; /* Fault status registers.  */
        uint32_t c5_data;
        uint32_t c6_region[8]; /* MPU base/size registers.  */
        uint32_t c6_insn; /* Fault address registers.  */
        uint32_t c6_data;
        uint32_t c9_insn; /* Cache lockdown registers.  */
        uint32_t c9_data;
        uint32_t c13_fcse; /* FCSE PID.  */
        uint32_t c13_context; /* Context ID.  */
        uint32_t c15_cpar; /* XScale Coprocessor Access Register */
    } cp15;

    /* Coprocessor IO used by peripherals */
    struct {
        ARMReadCPFunc *cp_read;
        ARMWriteCPFunc *cp_write;
        void *opaque;
    } cp[15];

    /* Internal CPU feature flags.  */
    uint32_t features;

    /* exception/interrupt handling */
    jmp_buf jmp_env;
    int exception_index;
    int interrupt_request;
    int user_mode_only;
    int halted;

    /* VFP coprocessor state.  */
    struct {
        float64 regs[16];

        uint32_t xregs[16];
        /* We store these fpcsr fields separately for convenience.  */
        int vec_len;
        int vec_stride;

        /* Temporary variables if we don't have spare fp regs.  */
        float32 tmp0s, tmp1s;
        float64 tmp0d, tmp1d;
        
        float_status fp_status;
    } vfp;

    /* iwMMXt coprocessor state.  */
    struct {
        uint64_t regs[16];
        uint64_t val;

        uint32_t cregs[16];
    } iwmmxt;

#if defined(CONFIG_USER_ONLY)
    /* For usermode syscall translation.  */
    int eabi;
#endif

    CPU_COMMON

    /* These fields after the common ones so they are preserved on reset.  */
    int ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
    int board_id;
    target_phys_addr_t loader_start;
} CPUARMState;

CPUARMState *cpu_arm_init(void);
int cpu_arm_exec(CPUARMState *s);
void cpu_arm_close(CPUARMState *s);
void do_interrupt(CPUARMState *);
void switch_mode(CPUARMState *, int);

/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
int cpu_arm_signal_handler(int host_signum, void *pinfo, 
                           void *puc);

#define CPSR_M (0x1f)
#define CPSR_T (1 << 5)
#define CPSR_F (1 << 6)
#define CPSR_I (1 << 7)
#define CPSR_A (1 << 8)
#define CPSR_E (1 << 9)
#define CPSR_IT_2_7 (0xfc00)
/* Bits 20-23 reserved.  */
#define CPSR_J (1 << 24)
#define CPSR_IT_0_1 (3 << 25)
#define CPSR_Q (1 << 27)
#define CPSR_NZCV (0xf << 28)

#define CACHED_CPSR_BITS (CPSR_T | CPSR_Q | CPSR_NZCV)
/* Return the current CPSR value.  */
static inline uint32_t cpsr_read(CPUARMState *env)
{
    int ZF;
    ZF = (env->NZF == 0);
    return env->uncached_cpsr | (env->NZF & 0x80000000) | (ZF << 30) | 
        (env->CF << 29) | ((env->VF & 0x80000000) >> 3) | (env->QF << 27)
        | (env->thumb << 5);
}

/* Set the CPSR.  Note that some bits of mask must be all-set or all-clear.  */
static inline void cpsr_write(CPUARMState *env, uint32_t val, uint32_t mask)
{
    /* NOTE: N = 1 and Z = 1 cannot be stored currently */
    if (mask & CPSR_NZCV) {
        env->NZF = (val & 0xc0000000) ^ 0x40000000;
        env->CF = (val >> 29) & 1;
        env->VF = (val << 3) & 0x80000000;
    }
    if (mask & CPSR_Q)
        env->QF = ((val & CPSR_Q) != 0);
    if (mask & CPSR_T)
        env->thumb = ((val & CPSR_T) != 0);

    if ((env->uncached_cpsr ^ val) & mask & CPSR_M) {
        switch_mode(env, val & CPSR_M);
    }
    mask &= ~CACHED_CPSR_BITS;
    env->uncached_cpsr = (env->uncached_cpsr & ~mask) | (val & mask);
}

enum arm_cpu_mode {
  ARM_CPU_MODE_USR = 0x10,
  ARM_CPU_MODE_FIQ = 0x11,
  ARM_CPU_MODE_IRQ = 0x12,
  ARM_CPU_MODE_SVC = 0x13,
  ARM_CPU_MODE_ABT = 0x17,
  ARM_CPU_MODE_UND = 0x1b,
  ARM_CPU_MODE_SYS = 0x1f
};

/* VFP system registers.  */
#define ARM_VFP_FPSID   0
#define ARM_VFP_FPSCR   1
#define ARM_VFP_FPEXC   8
#define ARM_VFP_FPINST  9
#define ARM_VFP_FPINST2 10

/* iwMMXt coprocessor control registers.  */
#define ARM_IWMMXT_wCID		0
#define ARM_IWMMXT_wCon		1
#define ARM_IWMMXT_wCSSF	2
#define ARM_IWMMXT_wCASF	3
#define ARM_IWMMXT_wCGR0	8
#define ARM_IWMMXT_wCGR1	9
#define ARM_IWMMXT_wCGR2	10
#define ARM_IWMMXT_wCGR3	11

enum arm_features {
    ARM_FEATURE_VFP,
    ARM_FEATURE_AUXCR,  /* ARM1026 Auxiliary control register.  */
    ARM_FEATURE_XSCALE, /* Intel XScale extensions.  */
    ARM_FEATURE_IWMMXT, /* Intel iwMMXt extension.  */
    ARM_FEATURE_MPU     /* Only has Memory Protection Unit, not full MMU.  */
};

static inline int arm_feature(CPUARMState *env, int feature)
{
    return (env->features & (1u << feature)) != 0;
}

void arm_cpu_list(void);
void cpu_arm_set_model(CPUARMState *env, const char *name);

void cpu_arm_set_cp_io(CPUARMState *env, int cpnum,
                       ARMReadCPFunc *cp_read, ARMWriteCPFunc *cp_write,
                       void *opaque);

#define ARM_CPUID_ARM1026   0x4106a262
#define ARM_CPUID_ARM926    0x41069265
#define ARM_CPUID_ARM946    0x41059461
#define ARM_CPUID_PXA250    0x69052100
#define ARM_CPUID_PXA255    0x69052d00
#define ARM_CPUID_PXA260    0x69052903
#define ARM_CPUID_PXA261    0x69052d05
#define ARM_CPUID_PXA262    0x69052d06
#define ARM_CPUID_PXA270    0x69054110
#define ARM_CPUID_PXA270_A0 0x69054110
#define ARM_CPUID_PXA270_A1 0x69054111
#define ARM_CPUID_PXA270_B0 0x69054112
#define ARM_CPUID_PXA270_B1 0x69054113
#define ARM_CPUID_PXA270_C0 0x69054114
#define ARM_CPUID_PXA270_C5 0x69054117

#if defined(CONFIG_USER_ONLY)
#define TARGET_PAGE_BITS 12
#else
/* The ARM MMU allows 1k pages.  */
/* ??? Linux doesn't actually use these, and they're deprecated in recent
   architecture revisions.  Maybe an a configure option to disable them.  */
#define TARGET_PAGE_BITS 10
#endif

#define CPUState CPUARMState
#define cpu_init cpu_arm_init
#define cpu_exec cpu_arm_exec
#define cpu_gen_code cpu_arm_gen_code
#define cpu_signal_handler cpu_arm_signal_handler

#include "cpu-all.h"

#endif
