#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "exec-all.h"

//#define TRACE_PC

static inline void set_feature(CPUARMState *env, int feature)
{
    env->features |= 1u << feature;
}

void helper_dump_pc(target_ulong pc)
{
#ifdef TRACE_PC
	if (pc & 1) { // thumb
		printf("PCt= %08x\n", pc);
	} else { // arm
		printf("PC = %08x\n", pc);
	}

	fflush(stdout);
#endif
}

static void cpu_reset_model_id(CPUARMState *env, uint32_t id)
{
    env->cp15.c0_cpuid = id;
    switch (id) {
    case ARM_CPUID_ARM926:
        set_feature(env, ARM_FEATURE_VFP);
        env->vfp.xregs[ARM_VFP_FPSID] = 0x41011090;
        env->cp15.c0_cachetype = 0x1dd20d2;
        break;
    case ARM_CPUID_ARM946:
        set_feature(env, ARM_FEATURE_MPU);
        env->cp15.c0_cachetype = 0x0f004006;
        break;
    case ARM_CPUID_ARM1026:
        set_feature(env, ARM_FEATURE_VFP);
        set_feature(env, ARM_FEATURE_AUXCR);
        env->vfp.xregs[ARM_VFP_FPSID] = 0x410110a0;
        env->cp15.c0_cachetype = 0x1dd20d2;
        break;
    case ARM_CPUID_PXA250:
    case ARM_CPUID_PXA255:
    case ARM_CPUID_PXA260:
    case ARM_CPUID_PXA261:
    case ARM_CPUID_PXA262:
        set_feature(env, ARM_FEATURE_XSCALE);
        /* JTAG_ID is ((id << 28) | 0x09265013) */
        env->cp15.c0_cachetype = 0xd172172;
        break;
    case ARM_CPUID_PXA270_A0:
    case ARM_CPUID_PXA270_A1:
    case ARM_CPUID_PXA270_B0:
    case ARM_CPUID_PXA270_B1:
    case ARM_CPUID_PXA270_C0:
    case ARM_CPUID_PXA270_C5:
        set_feature(env, ARM_FEATURE_XSCALE);
        /* JTAG_ID is ((id << 28) | 0x09265013) */
        set_feature(env, ARM_FEATURE_IWMMXT);
        env->iwmmxt.cregs[ARM_IWMMXT_wCID] = 0x69051000 | 'Q';
        env->cp15.c0_cachetype = 0xd172172;
        break;
    default:
        cpu_abort(env, "Bad CPU ID: %x\n", id);
        break;
    }
}

void cpu_reset(CPUARMState *env)
{
    uint32_t id;
    id = env->cp15.c0_cpuid;
    memset(env, 0, offsetof(CPUARMState, breakpoints));
    if (id)
        cpu_reset_model_id(env, id);
#if defined (CONFIG_USER_ONLY)
    env->uncached_cpsr = ARM_CPU_MODE_USR;
    env->vfp.xregs[ARM_VFP_FPEXC] = 1 << 30;
#else
    /* SVC mode with interrupts disabled.  */
    env->uncached_cpsr = ARM_CPU_MODE_SVC | CPSR_A | CPSR_F | CPSR_I;
    env->vfp.xregs[ARM_VFP_FPEXC] = 0;
#endif
    env->regs[15] = 0;
    tlb_flush(env, 1);
}

CPUARMState *cpu_arm_init(void)
{
    CPUARMState *env;

    env = qemu_mallocz(sizeof(CPUARMState));
    if (!env)
        return NULL;
    cpu_exec_init(env);
    cpu_reset(env);
    return env;
}

struct arm_cpu_t {
    uint32_t id;
    const char *name;
};

static const struct arm_cpu_t arm_cpu_names[] = {
    { ARM_CPUID_ARM926, "arm926"},
    { ARM_CPUID_ARM946, "arm946"},
    { ARM_CPUID_ARM1026, "arm1026"},
    { ARM_CPUID_PXA250, "pxa250" },
    { ARM_CPUID_PXA255, "pxa255" },
    { ARM_CPUID_PXA260, "pxa260" },
    { ARM_CPUID_PXA261, "pxa261" },
    { ARM_CPUID_PXA262, "pxa262" },
    { ARM_CPUID_PXA270, "pxa270" },
    { ARM_CPUID_PXA270_A0, "pxa270-a0" },
    { ARM_CPUID_PXA270_A1, "pxa270-a1" },
    { ARM_CPUID_PXA270_B0, "pxa270-b0" },
    { ARM_CPUID_PXA270_B1, "pxa270-b1" },
    { ARM_CPUID_PXA270_C0, "pxa270-c0" },
    { ARM_CPUID_PXA270_C5, "pxa270-c5" },
    { 0, NULL}
};

void arm_cpu_list(void)
{
    int i;

    printf ("Available CPUs:\n");
    for (i = 0; arm_cpu_names[i].name; i++) {
        printf("  %s\n", arm_cpu_names[i].name);
    }
}

void cpu_arm_set_model(CPUARMState *env, const char *name)
{
    int i;
    uint32_t id;

    id = 0;
    i = 0;
    for (i = 0; arm_cpu_names[i].name; i++) {
        if (strcmp(name, arm_cpu_names[i].name) == 0) {
            id = arm_cpu_names[i].id;
            break;
        }
    }
    if (!id) {
        cpu_abort(env, "Unknown CPU '%s'", name);
        return;
    }
    cpu_reset_model_id(env, id);
}

void cpu_arm_close(CPUARMState *env)
{
    free(env);
}

#if defined(CONFIG_USER_ONLY) 

void do_interrupt (CPUState *env)
{
    env->exception_index = -1;
}

int cpu_arm_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int is_user, int is_softmmu)
{
    if (rw == 2) {
        env->exception_index = EXCP_PREFETCH_ABORT;
        env->cp15.c6_insn = address;
    } else {
        env->exception_index = EXCP_DATA_ABORT;
        env->cp15.c6_data = address;
    }
    return 1;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    return addr;
}

/* These should probably raise undefined insn exceptions.  */
void helper_set_cp(CPUState *env, uint32_t insn, uint32_t val)
{
    int op1 = (insn >> 8) & 0xf;
    cpu_abort(env, "cp%i insn %08x\n", op1, insn);
    return;
}

uint32_t helper_get_cp(CPUState *env, uint32_t insn)
{
    int op1 = (insn >> 8) & 0xf;
    cpu_abort(env, "cp%i insn %08x\n", op1, insn);
    return 0;
}

void helper_set_cp15(CPUState *env, uint32_t insn, uint32_t val)
{
    cpu_abort(env, "cp15 insn %08x\n", insn);
}

uint32_t helper_get_cp15(CPUState *env, uint32_t insn)
{
    cpu_abort(env, "cp15 insn %08x\n", insn);
    return 0;
}

void switch_mode(CPUState *env, int mode)
{
    if (mode != ARM_CPU_MODE_USR)
        cpu_abort(env, "Tried to switch out of user mode\n");
}

#else

extern int semihosting_enabled;

/* Map CPU modes onto saved register banks.  */
static inline int bank_number (int mode)
{
    switch (mode) {
    case ARM_CPU_MODE_USR:
    case ARM_CPU_MODE_SYS:
        return 0;
    case ARM_CPU_MODE_SVC:
        return 1;
    case ARM_CPU_MODE_ABT:
        return 2;
    case ARM_CPU_MODE_UND:
        return 3;
    case ARM_CPU_MODE_IRQ:
        return 4;
    case ARM_CPU_MODE_FIQ:
        return 5;
    }
    cpu_abort(cpu_single_env, "Bad mode %x\n", mode);
    return -1;
}

void switch_mode(CPUState *env, int mode)
{
    int old_mode;
    int i;

    old_mode = env->uncached_cpsr & CPSR_M;
    if (mode == old_mode)
        return;

    if (old_mode == ARM_CPU_MODE_FIQ) {
        memcpy (env->fiq_regs, env->regs + 8, 5 * sizeof(uint32_t));
        memcpy (env->regs + 8, env->usr_regs, 5 * sizeof(uint32_t));
    } else if (mode == ARM_CPU_MODE_FIQ) {
        memcpy (env->usr_regs, env->regs + 8, 5 * sizeof(uint32_t));
        memcpy (env->regs + 8, env->fiq_regs, 5 * sizeof(uint32_t));
    }

    i = bank_number(old_mode);
    env->banked_r13[i] = env->regs[13];
    env->banked_r14[i] = env->regs[14];
    env->banked_spsr[i] = env->spsr;

    i = bank_number(mode);
    env->regs[13] = env->banked_r13[i];
    env->regs[14] = env->banked_r14[i];
    env->spsr = env->banked_spsr[i];
}

/* Handle a CPU exception.  */
void do_interrupt(CPUARMState *env)
{
    uint32_t addr;
    uint32_t mask;
    int new_mode;
    uint32_t offset;

    /* TODO: Vectored interrupt controller.  */
    switch (env->exception_index) {
    case EXCP_UDEF:
        new_mode = ARM_CPU_MODE_UND;
        addr = 0x04;
        mask = CPSR_I;
        if (env->thumb)
            offset = 2;
        else
            offset = 4;
        break;
    case EXCP_SWI:
        if (semihosting_enabled) {
            /* Check for semihosting interrupt.  */
            if (env->thumb) {
                mask = lduw_code(env->regs[15] - 2) & 0xff;
            } else {
                mask = ldl_code(env->regs[15] - 4) & 0xffffff;
            }
            /* Only intercept calls from privileged modes, to provide some
               semblance of security.  */
            if (((mask == 0x123456 && !env->thumb)
                    || (mask == 0xab && env->thumb))
                  && (env->uncached_cpsr & CPSR_M) != ARM_CPU_MODE_USR) {
                env->regs[0] = do_arm_semihosting(env);
                return;
            }
        }
        new_mode = ARM_CPU_MODE_SVC;
        addr = 0x08;
        mask = CPSR_I;
        /* The PC already points to the next instructon.  */
        offset = 0;
        break;
    case EXCP_PREFETCH_ABORT:
    case EXCP_BKPT:
        new_mode = ARM_CPU_MODE_ABT;
        addr = 0x0c;
        mask = CPSR_A | CPSR_I;
        offset = 4;
        break;
    case EXCP_DATA_ABORT:
        new_mode = ARM_CPU_MODE_ABT;
        addr = 0x10;
        mask = CPSR_A | CPSR_I;
        offset = 8;
        break;
    case EXCP_IRQ:
        new_mode = ARM_CPU_MODE_IRQ;
        addr = 0x18;
        /* Disable IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I;
        offset = 4;
        break;
    case EXCP_FIQ:
        new_mode = ARM_CPU_MODE_FIQ;
        addr = 0x1c;
        /* Disable FIQ, IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I | CPSR_F;
        offset = 4;
        break;
    default:
        cpu_abort(env, "Unhandled exception 0x%x\n", env->exception_index);
        return; /* Never happens.  Keep compiler happy.  */
    }
    /* High vectors.  */
    if (env->cp15.c1_sys & (1 << 13)) {
        addr += 0xffff0000;
    }
    switch_mode (env, new_mode);
    env->spsr = cpsr_read(env);
    /* Switch to the new mode, and switch to Arm mode.  */
    /* ??? Thumb interrupt handlers not implemented.  */
    env->uncached_cpsr = (env->uncached_cpsr & ~CPSR_M) | new_mode;
    env->uncached_cpsr |= mask;
    env->thumb = 0;
    env->regs[14] = env->regs[15] + offset;
    env->regs[15] = addr;
    env->interrupt_request |= CPU_INTERRUPT_EXITTB;
}

/* Check section/page access permissions.
   Returns the page protection flags, or zero if the access is not
   permitted.  */
static inline int check_ap(CPUState *env, int ap, int domain, int access_type,
                           int is_user)
{
  if (domain == 3) 
    return PAGE_READ | PAGE_WRITE;

  switch (ap) {
  case 0:
      if (access_type == 1)
          return 0;
      switch ((env->cp15.c1_sys >> 8) & 3) {
      case 1:
          return is_user ? 0 : PAGE_READ;
      case 2:
          return PAGE_READ;
      default:
          return 0;
      }
  case 1:
      return is_user ? 0 : PAGE_READ | PAGE_WRITE;
  case 2:
      if (is_user)
          return (access_type == 1) ? 0 : PAGE_READ;
      else
          return PAGE_READ | PAGE_WRITE;
  case 3:
      return PAGE_READ | PAGE_WRITE;
  default:
      abort();
  }
}

static int get_phys_addr(CPUState *env, uint32_t address, int access_type,
                         int is_user, uint32_t *phys_ptr, int *prot)
{
    int code;
    uint32_t table;
    uint32_t desc;
    int type;
    int ap;
    int domain;
    uint32_t phys_addr;

    /* Fast Context Switch Extension.  */
    if (address < 0x02000000)
        address += env->cp15.c13_fcse;

    if ((env->cp15.c1_sys & 1) == 0) {
        /* MMU/MPU disabled.  */
        *phys_ptr = address;
        *prot = PAGE_READ | PAGE_WRITE;
    } else if (arm_feature(env, ARM_FEATURE_MPU)) {
        int n;
        uint32_t mask;
        uint32_t base;

        *phys_ptr = address;
        for (n = 7; n >= 0; n--) {
            base = env->cp15.c6_region[n];
            if ((base & 1) == 0)
                continue;
            mask = 1 << ((base >> 1) & 0x1f);
            /* Keep this shift separate from the above to avoid an
               (undefined) << 32.  */
            mask = (mask << 1) - 1;
            if (((base ^ address) & ~mask) == 0)
                break;
        }
        if (n < 0)
            return 2;

        if (access_type == 2) {
            mask = env->cp15.c5_insn;
        } else {
            mask = env->cp15.c5_data;
        }
        mask = (mask >> (n * 4)) & 0xf;
        switch (mask) {
        case 0:
            return 1;
        case 1:
            if (is_user)
              return 1;
            *prot = PAGE_READ | PAGE_WRITE;
            break;
        case 2:
            *prot = PAGE_READ;
            if (!is_user)
                *prot |= PAGE_WRITE;
            break;
        case 3:
            *prot = PAGE_READ | PAGE_WRITE;
            break;
        case 5:
            if (is_user)
                return 1;
            *prot = PAGE_READ;
            break;
        case 6:
            *prot = PAGE_READ;
            break;
        default:
            /* Bad permission.  */
            return 1;
        }
    } else {
        /* Pagetable walk.  */
        /* Lookup l1 descriptor.  */
        table = (env->cp15.c2_base & 0xffffc000) | ((address >> 18) & 0x3ffc);
        desc = ldl_phys(table);
        type = (desc & 3);
        domain = (env->cp15.c3 >> ((desc >> 4) & 0x1e)) & 3;
        if (type == 0) {
            /* Secton translation fault.  */
            code = 5;
            goto do_fault;
        }
        if (domain == 0 || domain == 2) {
            if (type == 2)
                code = 9; /* Section domain fault.  */
            else
                code = 11; /* Page domain fault.  */
            goto do_fault;
        }
        if (type == 2) {
            /* 1Mb section.  */
            phys_addr = (desc & 0xfff00000) | (address & 0x000fffff);
            ap = (desc >> 10) & 3;
            code = 13;
        } else {
            /* Lookup l2 entry.  */
            if (type == 1) {
                /* Coarse pagetable.  */
                table = (desc & 0xfffffc00) | ((address >> 10) & 0x3fc);
            } else {
                /* Fine pagetable.  */
                table = (desc & 0xfffff000) | ((address >> 8) & 0xffc);
            }
            desc = ldl_phys(table);
            switch (desc & 3) {
            case 0: /* Page translation fault.  */
                code = 7;
                goto do_fault;
            case 1: /* 64k page.  */
                phys_addr = (desc & 0xffff0000) | (address & 0xffff);
                ap = (desc >> (4 + ((address >> 13) & 6))) & 3;
                break;
            case 2: /* 4k page.  */
                phys_addr = (desc & 0xfffff000) | (address & 0xfff);
                ap = (desc >> (4 + ((address >> 13) & 6))) & 3;
                break;
            case 3: /* 1k page.  */
                if (arm_feature(env, ARM_FEATURE_XSCALE))
                    phys_addr = (desc & 0xfffff000) | (address & 0xfff);
                else {
                    if (type == 1) {
                        /* Page translation fault.  */
                        code = 7;
                        goto do_fault;
                    }
                    phys_addr = (desc & 0xfffffc00) | (address & 0x3ff);
                }
                ap = (desc >> 4) & 3;
                break;
            default:
                /* Never happens, but compiler isn't smart enough to tell.  */
                abort();
            }
            code = 15;
        }
	if (address < 0x4000) { /* disable memory prot for first page,
				   this seems to be an oddity of the LD
				   */
		*prot = PAGE_READ | PAGE_WRITE;
	} else {
        	*prot = check_ap(env, ap, domain, access_type, is_user);
	}
        if (!*prot) {
            /* Access permission fault.  */
            goto do_fault;
        }
        *phys_ptr = phys_addr;
    }
    return 0;
do_fault:
    return code | (domain << 4);
}

int cpu_arm_handle_mmu_fault (CPUState *env, target_ulong address,
                              int access_type, int is_user, int is_softmmu)
{
    uint32_t phys_addr;
    int prot;
    int ret;

    ret = get_phys_addr(env, address, access_type, is_user, &phys_addr, &prot);
    if (ret == 0) {
        /* Map a single [sub]page.  */
        phys_addr &= ~(uint32_t)0x3ff;
        address &= ~(uint32_t)0x3ff;
        return tlb_set_page (env, address, phys_addr, prot, is_user,
                             is_softmmu);
    }

    if (access_type == 2) {
        env->cp15.c5_insn = ret;
        env->cp15.c6_insn = address;
        env->exception_index = EXCP_PREFETCH_ABORT;
    } else {
        env->cp15.c5_data = ret;
        env->cp15.c6_data = address;
        env->exception_index = EXCP_DATA_ABORT;
    }
    return 1;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    uint32_t phys_addr;
    int prot;
    int ret;

    ret = get_phys_addr(env, addr, 0, 0, &phys_addr, &prot);

    if (ret != 0)
        return -1;

    return phys_addr;
}

void helper_set_cp(CPUState *env, uint32_t insn, uint32_t val)
{
    int cp_num = (insn >> 8) & 0xf;
    int cp_info = (insn >> 5) & 7;
    int src = (insn >> 16) & 0xf;
    int operand = insn & 0xf;

    if (env->cp[cp_num].cp_write)
        env->cp[cp_num].cp_write(env->cp[cp_num].opaque,
                                 cp_info, src, operand, val);
}

uint32_t helper_get_cp(CPUState *env, uint32_t insn)
{
    int cp_num = (insn >> 8) & 0xf;
    int cp_info = (insn >> 5) & 7;
    int dest = (insn >> 16) & 0xf;
    int operand = insn & 0xf;

    if (env->cp[cp_num].cp_read)
        return env->cp[cp_num].cp_read(env->cp[cp_num].opaque,
                                       cp_info, dest, operand);
    return 0;
}

/* Return basic MPU access permission bits.  */
static uint32_t simple_mpu_ap_bits(uint32_t val)
{
    uint32_t ret;
    uint32_t mask;
    int i;
    ret = 0;
    mask = 3;
    for (i = 0; i < 16; i += 2) {
        ret |= (val >> i) & mask;
        mask <<= 2;
    }
    return ret;
}

/* Pad basic MPU access permission bits to extended format.  */
static uint32_t extended_mpu_ap_bits(uint32_t val)
{
    uint32_t ret;
    uint32_t mask;
    int i;
    ret = 0;
    mask = 3;
    for (i = 0; i < 16; i += 2) {
        ret |= (val & mask) << i;
        mask <<= 2;
    }
    return ret;
}

void helper_set_cp15(CPUState *env, uint32_t insn, uint32_t val)
{
    uint32_t op2;
    uint32_t crm;

    op2 = (insn >> 5) & 7;
    crm = insn & 0xf;
    switch ((insn >> 16) & 0xf) {
    case 0: /* ID codes.  */
        goto bad_reg;
    case 1: /* System configuration.  */
        switch (op2) {
        case 0:
            if (!arm_feature(env, ARM_FEATURE_XSCALE) || crm == 0)
                env->cp15.c1_sys = val;
            /* ??? Lots of these bits are not implemented.  */
            /* This may enable/disable the MMU, so do a TLB flush.  */
            tlb_flush(env, 1);
            break;
        case 1:
            /* XScale doesn't implement AUX CR (P-Bit) but allows
             * writing with zero and reading.  */
            if (arm_feature(env, ARM_FEATURE_XSCALE))
                break;
            goto bad_reg;
        case 2:
            env->cp15.c1_coproc = val;
            /* ??? Is this safe when called from within a TB?  */
            tb_flush(env);
            break;
        default:
            goto bad_reg;
        }
        break;
    case 2: /* MMU Page table control / MPU cache control.  */
        if (arm_feature(env, ARM_FEATURE_MPU)) {
            switch (op2) {
            case 0:
                env->cp15.c2_data = val;
                break;
            case 1:
                env->cp15.c2_insn = val;
                break;
            default:
                goto bad_reg;
            }
        } else {
            env->cp15.c2_base = val;
        }
        break;
    case 3: /* MMU Domain access control / MPU write buffer control.  */
        env->cp15.c3 = val;
        break;
    case 4: /* Reserved.  */
        goto bad_reg;
    case 5: /* MMU Fault status / MPU access permission.  */
        switch (op2) {
        case 0:
            if (arm_feature(env, ARM_FEATURE_MPU))
                val = extended_mpu_ap_bits(val);
            env->cp15.c5_data = val;
            break;
        case 1:
            if (arm_feature(env, ARM_FEATURE_MPU))
                val = extended_mpu_ap_bits(val);
            env->cp15.c5_insn = val;
            break;
        case 2:
            if (!arm_feature(env, ARM_FEATURE_MPU))
                goto bad_reg;
            env->cp15.c5_data = val;
            break;
        case 3:
            if (!arm_feature(env, ARM_FEATURE_MPU))
                goto bad_reg;
            env->cp15.c5_insn = val;
            break;
        default:
            goto bad_reg;
        }
        break;
    case 6: /* MMU Fault address / MPU base/size.  */
        if (arm_feature(env, ARM_FEATURE_MPU)) {
            if (crm >= 8)
                goto bad_reg;
            env->cp15.c6_region[crm] = val;
        } else {
            switch (op2) {
            case 0:
                env->cp15.c6_data = val;
                break;
            case 1:
                env->cp15.c6_insn = val;
                break;
            default:
                goto bad_reg;
            }
        }
        break;
    case 7: /* Cache control.  */
        /* No cache, so nothing to do.  */
        break;
    case 8: /* MMU TLB control.  */
        switch (op2) {
        case 0: /* Invalidate all.  */
            tlb_flush(env, 0);
            break;
        case 1: /* Invalidate single TLB entry.  */
#if 0
            /* ??? This is wrong for large pages and sections.  */
            /* As an ugly hack to make linux work we always flush a 4K
               pages.  */
            val &= 0xfffff000;
            tlb_flush_page(env, val);
            tlb_flush_page(env, val + 0x400);
            tlb_flush_page(env, val + 0x800);
            tlb_flush_page(env, val + 0xc00);
#else
            tlb_flush(env, 1);
#endif
            break;
        default:
            goto bad_reg;
        }
        break;
    case 9:
        switch (crm) {
        case 0: /* Cache lockdown.  */
            switch (op2) {
            case 0:
                env->cp15.c9_data = val;
                break;
            case 1:
                env->cp15.c9_insn = val;
                break;
            default:
                goto bad_reg;
            }
            break;
        case 1: /* TCM memory region registers.  */
            /* Not implemented.  */
            goto bad_reg;
        default:
            goto bad_reg;
        }
        break;
    case 10: /* MMU TLB lockdown.  */
        /* ??? TLB lockdown not implemented.  */
        break;
    case 12: /* Reserved.  */
        goto bad_reg;
    case 13: /* Process ID.  */
        switch (op2) {
        case 0:
            if (!arm_feature(env, ARM_FEATURE_MPU))
                goto bad_reg;
            /* Unlike real hardware the qemu TLB uses virtual addresses,
               not modified virtual addresses, so this causes a TLB flush.
             */
            if (env->cp15.c13_fcse != val)
              tlb_flush(env, 1);
            env->cp15.c13_fcse = val;
            break;
        case 1:
            /* This changes the ASID, so do a TLB flush.  */
            if (env->cp15.c13_context != val
                && !arm_feature(env, ARM_FEATURE_MPU))
              tlb_flush(env, 0);
            env->cp15.c13_context = val;
            break;
        default:
            goto bad_reg;
        }
        break;
    case 14: /* Reserved.  */
        goto bad_reg;
    case 15: /* Implementation specific.  */
        if (arm_feature(env, ARM_FEATURE_XSCALE)) {
            if (op2 == 0 && crm == 1) {
                /* Changes cp0 to cp13 behavior, so needs a TB flush.  */
                tb_flush(env);
                env->cp15.c15_cpar = (val & 0x3fff) | 2;
                break;
            }
            goto bad_reg;
        }
        break;
    }
    return;
bad_reg:
    /* ??? For debugging only.  Should raise illegal instruction exception.  */
    //cpu_abort(env, "Unimplemented cp15 register write\n");
    fprintf(stderr, "Unimplemented cp15 register write\n");
}

uint32_t helper_get_cp15(CPUState *env, uint32_t insn)
{
    uint32_t op2;

    op2 = (insn >> 5) & 7;
    switch ((insn >> 16) & 0xf) {
    case 0: /* ID codes.  */
        switch (op2) {
        default: /* Device ID.  */
            return env->cp15.c0_cpuid;
        case 1: /* Cache Type.  */
            return env->cp15.c0_cachetype;
        case 2: /* TCM status.  */
            return 0;
        }
    case 1: /* System configuration.  */
        switch (op2) {
        case 0: /* Control register.  */
            return env->cp15.c1_sys;
        case 1: /* Auxiliary control register.  */
            if (arm_feature(env, ARM_FEATURE_AUXCR))
                return 1;
            if (arm_feature(env, ARM_FEATURE_XSCALE))
                return 0;
            goto bad_reg;
        case 2: /* Coprocessor access register.  */
            return env->cp15.c1_coproc;
        default:
            goto bad_reg;
        }
    case 2: /* MMU Page table control / MPU cache control.  */
        if (arm_feature(env, ARM_FEATURE_MPU)) {
            switch (op2) {
            case 0:
                return env->cp15.c2_data;
                break;
            case 1:
                return env->cp15.c2_insn;
                break;
            default:
                goto bad_reg;
            }
        } else {
            return env->cp15.c2_base;
        }
    case 3: /* MMU Domain access control / MPU write buffer control.  */
        return env->cp15.c3;
    case 4: /* Reserved.  */
        goto bad_reg;
    case 5: /* MMU Fault status / MPU access permission.  */
        switch (op2) {
        case 0:
            if (arm_feature(env, ARM_FEATURE_MPU))
                return simple_mpu_ap_bits(env->cp15.c5_data);
            return env->cp15.c5_data;
        case 1:
            if (arm_feature(env, ARM_FEATURE_MPU))
                return simple_mpu_ap_bits(env->cp15.c5_data);
            return env->cp15.c5_insn;
        case 2:
            if (!arm_feature(env, ARM_FEATURE_MPU))
                goto bad_reg;
            return env->cp15.c5_data;
        case 3:
            if (!arm_feature(env, ARM_FEATURE_MPU))
                goto bad_reg;
            return env->cp15.c5_insn;
        default:
            goto bad_reg;
        }
    case 6: /* MMU Fault address / MPU base/size.  */
        if (arm_feature(env, ARM_FEATURE_MPU)) {
            int n;
            n = (insn & 0xf);
            if (n >= 8)
                goto bad_reg;
            return env->cp15.c6_region[n];
        } else {
            switch (op2) {
            case 0:
                return env->cp15.c6_data;
            case 1:
                /* Arm9 doesn't have an IFAR, but implementing it anyway
                   shouldn't do any harm.  */
                return env->cp15.c6_insn;
            default:
                goto bad_reg;
            }
        }
    case 7: /* Cache control.  */
        /* ??? This is for test, clean and invaidate operations that set the
           Z flag.  We can't represent N = Z = 1, so it also clears
           the N flag.  Oh well.  */
        env->NZF = 0;
        return 0;
    case 8: /* MMU TLB control.  */
        goto bad_reg;
    case 9: /* Cache lockdown.  */
        switch (op2) {
        case 0:
            return env->cp15.c9_data;
        case 1:
            return env->cp15.c9_insn;
        default:
            goto bad_reg;
        }
    case 10: /* MMU TLB lockdown.  */
        /* ??? TLB lockdown not implemented.  */
        return 0;
    case 11: /* TCM DMA control.  */
    case 12: /* Reserved.  */
        goto bad_reg;
    case 13: /* Process ID.  */
        switch (op2) {
        case 0:
            return env->cp15.c13_fcse;
        case 1:
            return env->cp15.c13_context;
        default:
            goto bad_reg;
        }
    case 14: /* Reserved.  */
        goto bad_reg;
    case 15: /* Implementation specific.  */
        if (arm_feature(env, ARM_FEATURE_XSCALE)) {
            if (op2 == 0 && (insn & 0xf) == 1)
                return env->cp15.c15_cpar;

            goto bad_reg;
        }
        return 0;
    }
bad_reg:
    /* ??? For debugging only.  Should raise illegal instruction exception.  */
    //cpu_abort(env, "Unimplemented cp15 register read\n");
    fprintf(stderr, "Unimplemented cp15 register read\n");
    return 0;
}

void cpu_arm_set_cp_io(CPUARMState *env, int cpnum,
                ARMReadCPFunc *cp_read, ARMWriteCPFunc *cp_write,
                void *opaque)
{
    if (cpnum < 0 || cpnum > 14) {
        cpu_abort(env, "Bad coprocessor number: %i\n", cpnum);
        return;
    }

    env->cp[cpnum].cp_read = cp_read;
    env->cp[cpnum].cp_write = cp_write;
    env->cp[cpnum].opaque = opaque;
}

#endif
