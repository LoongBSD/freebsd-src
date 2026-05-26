/*-
 * Copyright 1996-1998 John D. Polstra.
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2016 Yukishige Shibata <y-shibat@mtd.biglobe.ne.jp>
 * Copyright (c) 2024 Xiaoqiang Zhao <zxq_yx_007@163.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/linker.h>
#include <sys/proc.h>
#include <sys/reg.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/imgact_elf.h>
#include <sys/syscall.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>

#include <machine/elf.h>
#include <machine/md_var.h>

u_long elf_hwcap;

static struct sysentvec elf64_freebsd_sysvec = {
	.sv_size	= SYS_MAXSYSCALL,
	.sv_table	= sysent,
	.sv_fixup	= __elfN(freebsd_fixup),
	.sv_sendsig	= sendsig,
	.sv_sigcode	= sigcode,
	.sv_szsigcode	= &szsigcode,
	.sv_name	= "FreeBSD ELF64",
	.sv_coredump	= __elfN(coredump),
	.sv_elf_core_osabi = ELFOSABI_FREEBSD,
	.sv_elf_core_abi_vendor = FREEBSD_ABI_VENDOR,
	.sv_elf_core_prepare_notes = __elfN(prepare_notes),
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_maxuser	= 0,	/* Filled in during boot. */
	.sv_usrstack	= 0,	/* Filled in during boot. */
	.sv_psstrings	= 0,	/* Filled in during boot. */
	.sv_psstringssz	= sizeof(struct ps_strings),
	.sv_stackprot	= VM_PROT_READ | VM_PROT_WRITE,
	.sv_copyout_auxargs = __elfN(freebsd_copyout_auxargs),
	.sv_copyout_strings	= exec_copyout_strings,
	.sv_setregs	= exec_setregs,
	.sv_fixlimit	= NULL,
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_FREEBSD | SV_LP64 | SV_SHP | SV_TIMEKEEP |
	    SV_ASLR | SV_RNG_SEED_VER | SV_SIGSYS,
	.sv_set_syscall_retval = cpu_set_syscall_retval,
	.sv_fetch_syscall_args = cpu_fetch_syscall_args,
	.sv_syscallnames = syscallnames,
	.sv_shared_page_base = 0,	/* Filled in during boot. */
	.sv_shared_page_len = PAGE_SIZE,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
	.sv_hwcap	= &elf_hwcap,
	.sv_onexec_old	= exec_onexec_old,
	.sv_onexit	= exit_onexit,
	.sv_regset_begin = SET_BEGIN(__elfN(regset)),
	.sv_regset_end  = SET_LIMIT(__elfN(regset)),
};
INIT_SYSENTVEC(elf64_sysvec, &elf64_freebsd_sysvec);

static Elf64_Brandinfo freebsd_brand_info = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_LOONGARCH,
	.compat_3_brand	= "FreeBSD",
	.interp_path	= "/libexec/ld-elf.so.1",
	.sysvec		= &elf64_freebsd_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &elf64_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};
SYSINIT(elf64, SI_SUB_EXEC, SI_ORDER_FIRST,
    (sysinit_cfunc_t)elf64_insert_brand_entry, &freebsd_brand_info);

static void
elf64_register_sysvec(void *arg)
{
	struct sysentvec *sv;

	sv = arg;
	sv->sv_maxuser = VM_MAX_USER_ADDRESS;
	sv->sv_usrstack = USRSTACK;
	sv->sv_psstrings = PS_STRINGS;
	sv->sv_shared_page_base = SHAREDPAGE;
}
SYSINIT(elf64_register_sysvec, SI_SUB_VM, SI_ORDER_ANY, elf64_register_sysvec,
    &elf64_freebsd_sysvec);

static bool debug_kld;
SYSCTL_BOOL(_debug, OID_AUTO, kld_reloc, CTLFLAG_RW, &debug_kld, 0,
    "Activate debug prints in elf_reloc_internal()");

struct type2str_ent {
	int type;
	const char *str;
};

void
elf64_dump_thread(struct thread *td, void *dst, size_t *off)
{

}

/*
 * LoongArch instruction encoding helper functions.
 *
 * LoongArch uses fixed-width 32-bit instructions. The immediate fields
 * are encoded in various formats depending on the instruction type.
 */

/*
 * Extract a bit field from a 32-bit value.
 * msb: most significant bit position of the field
 * lsb: least significant bit position of the field
 */
static uint32_t
extract_bits(uint32_t val, int msb, int lsb)
{

	return ((val >> lsb) & ((1U << (msb - lsb + 1)) - 1));
}

/*
 * Insert a value into a bit field of a 32-bit instruction.
 * Returns the modified instruction.
 */
static uint32_t
insert_imm(uint32_t insn, uint32_t imm, int msb, int lsb)
{
	uint32_t mask = ((1U << (msb - lsb + 1)) - 1) << lsb;

	return ((insn & ~mask) | ((imm << lsb) & mask));
}

/*
 * Sign-extend a value from a given bit width to 64 bits.
 */
static int64_t
sign_extend(uint64_t val, int bits)
{

	if (val & (1ULL << (bits - 1)))
		return (val | (~0ULL << bits));
	return (val);
}

static const struct type2str_ent t2s[] = {
	{ R_LARCH_NONE,		"R_LARCH_NONE"		},
	{ R_LARCH_32,		"R_LARCH_32"		},
	{ R_LARCH_64,		"R_LARCH_64"		},
	{ R_LARCH_RELATIVE,	"R_LARCH_RELATIVE"	},
	{ R_LARCH_COPY,		"R_LARCH_COPY"		},
	{ R_LARCH_JUMP_SLOT,	"R_LARCH_JUMP_SLOT"	},
	{ R_LARCH_IRELATIVE,	"R_LARCH_IRELATIVE"	},
	{ R_LARCH_B16,		"R_LARCH_B16"		},
	{ R_LARCH_B21,		"R_LARCH_B21"		},
	{ R_LARCH_B26,		"R_LARCH_B26"		},
	{ R_LARCH_ABS_HI20,	"R_LARCH_ABS_HI20"	},
	{ R_LARCH_ABS_LO12,	"R_LARCH_ABS_LO12"	},
	{ R_LARCH_ABS64_LO20,	"R_LARCH_ABS64_LO20"	},
	{ R_LARCH_ABS64_HI12,	"R_LARCH_ABS64_HI12"	},
	{ R_LARCH_PCALA_HI20,	"R_LARCH_PCALA_HI20"	},
	{ R_LARCH_PCALA_LO12,	"R_LARCH_PCALA_LO12"	},
	{ R_LARCH_PCALA64_LO20,	"R_LARCH_PCALA64_LO20"	},
	{ R_LARCH_PCALA64_HI12,	"R_LARCH_PCALA64_HI12"	},
	{ R_LARCH_GOT_HI20,	"R_LARCH_GOT_HI20"	},
	{ R_LARCH_GOT_LO12,	"R_LARCH_GOT_LO12"	},
	{ R_LARCH_GOT64_LO20,	"R_LARCH_GOT64_LO20"	},
	{ R_LARCH_GOT64_HI12,	"R_LARCH_GOT64_HI12"	},
	{ R_LARCH_TLS_LE_HI20,	"R_LARCH_TLS_LE_HI20"	},
	{ R_LARCH_TLS_LE_LO12,	"R_LARCH_TLS_LE_LO12"	},
	{ R_LARCH_TLS_IE_HI20,	"R_LARCH_TLS_IE_HI20"	},
	{ R_LARCH_TLS_IE_LO12,	"R_LARCH_TLS_IE_LO12"	},
	{ R_LARCH_TLS_LD_HI20,	"R_LARCH_TLS_LD_HI20"	},
	{ R_LARCH_TLS_GD_HI20,	"R_LARCH_TLS_GD_HI20"	},
	{ R_LARCH_32_PCREL,	"R_LARCH_32_PCREL"	},
	{ R_LARCH_64_PCREL,	"R_LARCH_64_PCREL"	},
	{ R_LARCH_PCREL20_S2,	"R_LARCH_PCREL20_S2"	},
	{ R_LARCH_CALL36,	"R_LARCH_CALL36"	},
	{ R_LARCH_ALIGN,	"R_LARCH_ALIGN"		},
	{ R_LARCH_RELAX,	"R_LARCH_RELAX"		},
};

static const char *
reloctype_to_str(int type)
{
	int i;

	for (i = 0; i < nitems(t2s); i++) {
		if (type == t2s[i].type)
			return (t2s[i].str);
	}

	return ("*unknown*");
}

bool
elf_is_ifunc_reloc(Elf_Size r_info)
{

	return (ELF_R_TYPE(r_info) == R_LARCH_IRELATIVE);
}

/*
 * Apply relocations to kernel loadable modules.
 *
 * LoongArch uses RELA relocations with explicit addends.
 */
static int
elf_reloc_internal(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, int local, elf_lookup_fn lookup)
{
	Elf_Size rtype, symidx;
	const Elf_Rela *rela;
	Elf_Addr addr;
	Elf64_Addr *where;
	Elf_Addr addend;
	uint32_t *insn32p;
	int error;

	switch (type) {
	case ELF_RELOC_RELA:
		rela = (const Elf_Rela *)data;
		where = (Elf_Addr *)(relocbase + rela->r_offset);
		insn32p = (uint32_t *)where;
		addend = rela->r_addend;
		rtype = ELF_R_TYPE(rela->r_info);
		symidx = ELF_R_SYM(rela->r_info);
		break;
	default:
		printf("%s:%d unknown reloc type %d\n",
		    __func__, __LINE__, type);
		return (-1);
	}

	switch (rtype) {
	case R_LARCH_NONE:
		break;

	case R_LARCH_64:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		*where = addr + addend;
		if (debug_kld)
			printf("%p %c %-24s %016lx -> %016lx\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    (unsigned long)*where - addr - addend, *where);
		break;

	case R_LARCH_JUMP_SLOT:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		*where = addr;
		if (debug_kld)
			printf("%p %c %-24s %016lx\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    (unsigned long)*where);
		break;

	case R_LARCH_RELATIVE:
		*where = elf_relocaddr(lf, relocbase + addend);
		if (debug_kld)
			printf("%p %c %-24s %016lx\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    (unsigned long)*where);
		break;

	case R_LARCH_IRELATIVE:
		/*
		 * IRELATIVE relocations are handled by the runtime linker.
		 * For kernel modules, we resolve them like RELATIVE.
		 */
		*where = elf_relocaddr(lf, relocbase + addend);
		if (debug_kld)
			printf("%p %c %-24s %016lx\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    (unsigned long)*where);
		break;

	case R_LARCH_B16:
		/*
		 * B16: PC-relative branch with 16-bit offset.
		 * Format: offs[15:0] at bits [25:10].
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addend = sign_extend(extract_bits(*insn32p, 25, 10), 16) << 2;
		addr = addr - (Elf_Addr)where + addend;

		if ((int64_t)addr < -(1 << 17) || (int64_t)addr >= (1 << 17)) {
			printf("kldload: offset too large for R_LARCH_B16\n");
			return (-1);
		}

		*insn32p = insert_imm(*insn32p, (addr >> 2) & 0xffff, 25, 10);
		if (debug_kld)
			printf("%p %c %-24s %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    *insn32p);
		break;

	case R_LARCH_B21:
		/*
		 * B21: PC-relative branch with 21-bit offset.
		 * Format: offs[20:0] split across bits [25:10] and [9:0].
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addend = sign_extend(
		    (extract_bits(*insn32p, 25, 16) << 10) |
		    extract_bits(*insn32p, 9, 0), 20) << 2;
		addr = addr - (Elf_Addr)where + addend;

		if ((int64_t)addr < -(1 << 22) || (int64_t)addr >= (1 << 22)) {
			printf("kldload: offset too large for R_LARCH_B21\n");
			return (-1);
		}

		addr = (uint64_t)addr >> 2;
		*insn32p = insert_imm(*insn32p, extract_bits(addr, 15, 0), 25, 10);
		*insn32p = insert_imm(*insn32p, extract_bits(addr, 20, 16), 9, 5);
		if (debug_kld)
			printf("%p %c %-24s %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    *insn32p);
		break;

	case R_LARCH_B26:
		/*
		 * B26: PC-relative jump with 26-bit offset.
		 * Format: offs[25:0] split across bits [25:10] and [9:0].
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addend = sign_extend(
		    (extract_bits(*insn32p, 25, 10) << 16) |
		    extract_bits(*insn32p, 9, 0), 26) << 2;
		addr = addr - (Elf_Addr)where + addend;

		if ((int64_t)addr < -(1 << 27) || (int64_t)addr >= (1 << 27)) {
			printf("kldload: offset too large for R_LARCH_B26\n");
			return (-1);
		}

		addr = (uint64_t)addr >> 2;
		*insn32p = insert_imm(*insn32p, extract_bits(addr, 15, 0), 25, 10);
		*insn32p = insert_imm(*insn32p, extract_bits(addr, 25, 16), 9, 0);
		if (debug_kld)
			printf("%p %c %-24s %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    *insn32p);
		break;

	case R_LARCH_ABS_HI20:
		/*
		 * ABS_HI20: Absolute address, high 20 bits.
		 * Used with LU12I.W instruction.
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addr += addend;
		*insn32p = insert_imm(*insn32p, (addr >> 12) & 0xfffff, 24, 5);
		if (debug_kld)
			printf("%p %c %-24s %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    *insn32p);
		break;

	case R_LARCH_ABS_LO12:
		/*
		 * ABS_LO12: Absolute address, low 12 bits.
		 * Used with ORI, ADDI.W, etc.
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addr += addend;
		*insn32p = insert_imm(*insn32p, addr & 0xfff, 21, 10);
		if (debug_kld)
			printf("%p %c %-24s %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    *insn32p);
		break;

	case R_LARCH_PCALA_HI20:
		/*
		 * PCALA_HI20: PC-relative address, high 20 bits.
		 * Used with PCADDU12I instruction.
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addr = addr - (Elf_Addr)where + addend;
		addr = (addr + 0x800) >> 12;	/* Adjust for sign extension */
		*insn32p = insert_imm(*insn32p, addr & 0xfffff, 24, 5);
		if (debug_kld)
			printf("%p %c %-24s %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    *insn32p);
		break;

	case R_LARCH_PCALA_LO12:
		/*
		 * PCALA_LO12: PC-relative address, low 12 bits.
		 * Used with ADDI.W, LD.W, ST.W, etc.
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addr = addr - (Elf_Addr)where + addend;
		*insn32p = insert_imm(*insn32p, addr & 0xfff, 21, 10);
		if (debug_kld)
			printf("%p %c %-24s %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    *insn32p);
		break;

	case R_LARCH_32:
		/*
		 * R_LARCH_32: 32-bit absolute relocation.
		 * *(uint32_t *)PC = S + A
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		*(uint32_t *)where = (uint32_t)(addr + addend);
		if (debug_kld)
			printf("%p %c %-24s %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    *(uint32_t *)where);
		break;

	case R_LARCH_32_PCREL:
		/*
		 * R_LARCH_32_PCREL: 32-bit PC-relative relocation.
		 * *(uint32_t *)PC = S + A - PC
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addr = addr - (Elf_Addr)where + addend;
		if ((int64_t)addr < -(1LL << 31) || (int64_t)addr >= (1LL << 31)) {
			printf("kldload: offset too large for R_LARCH_32_PCREL\n");
			return (-1);
		}
		*(uint32_t *)where = (uint32_t)addr;
		if (debug_kld)
			printf("%p %c %-24s %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    *(uint32_t *)where);
		break;

	case R_LARCH_64_PCREL:
		/*
		 * R_LARCH_64_PCREL: 64-bit PC-relative relocation.
		 * *(uint64_t *)PC = S + A - PC
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addr = addr - (Elf_Addr)where + addend;
		*(uint64_t *)where = addr;
		if (debug_kld)
			printf("%p %c %-24s %016lx\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    *(uint64_t *)where);
		break;

	case R_LARCH_PCREL20_S2:
		/*
		 * R_LARCH_PCREL20_S2: 22-bit PC-relative offset.
		 * Format: offs[21:2] at bits [24:5]
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addr = addr - (Elf_Addr)where + addend;
		if ((int64_t)addr < -(1 << 21) || (int64_t)addr >= (1 << 21)) {
			printf("kldload: offset too large for R_LARCH_PCREL20_S2\n");
			return (-1);
		}
		if ((addr & 0x3) != 0) {
			printf("kldload: unaligned offset for R_LARCH_PCREL20_S2\n");
			return (-1);
		}
		*insn32p = insert_imm(*insn32p, (addr >> 2) & 0xfffff, 24, 5);
		if (debug_kld)
			printf("%p %c %-24s %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    *insn32p);
		break;

	case R_LARCH_ABS64_HI12:
		/*
		 * R_LARCH_ABS64_HI12: Absolute address, bits [63:52].
		 * Used with LU52I.D instruction for 64-bit addresses.
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addr += addend;
		*insn32p = insert_imm(*insn32p, (addr >> 52) & 0xfff, 21, 10);
		if (debug_kld)
			printf("%p %c %-24s %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    *insn32p);
		break;

	case R_LARCH_ABS64_LO20:
		/*
		 * R_LARCH_ABS64_LO20: Absolute address, bits [51:32].
		 * Used with LU32I.D instruction for 64-bit addresses.
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addr += addend;
		*insn32p = insert_imm(*insn32p, (addr >> 32) & 0xfffff, 24, 5);
		if (debug_kld)
			printf("%p %c %-24s %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    *insn32p);
		break;

	case R_LARCH_CALL36:
		/*
		 * R_LARCH_CALL36: 38-bit PC-relative call sequence.
		 * Used for medium code model function call: pcaddu18i + jirl
		 * The two instructions must be adjacent.
		 * Format: offs[37:18] in first instruction [24:5]
		 *         offs[17:2] in second instruction [25:10]
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addr = addr - (Elf_Addr)where + addend;
		if ((int64_t)addr < -(1LL << 37) || (int64_t)addr >= (1LL << 37)) {
			printf("kldload: offset too large for R_LARCH_CALL36\n");
			return (-1);
		}
		if ((addr & 0x3) != 0) {
			printf("kldload: unaligned offset for R_LARCH_CALL36\n");
			return (-1);
		}
		/* First instruction: pcaddu18i - bits [37:18] */
		insn32p[0] = insert_imm(insn32p[0], (addr >> 18) & 0xfffff, 24, 5);
		/* Second instruction: jirl - bits [17:2] */
		insn32p[1] = insert_imm(insn32p[1], (addr >> 2) & 0xffff, 25, 10);
		if (debug_kld)
			printf("%p %c %-24s %08x %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    insn32p[0], insn32p[1]);
		break;

	case R_LARCH_ALIGN:
		/*
		 * R_LARCH_ALIGN: Alignment directive.
		 * The addend indicates the number of bytes occupied by nop
		 * instructions. The alignment boundary is the addend
		 * rounded up to the next power of two.
		 * This is handled by the linker, nothing to do here.
		 */
		if (debug_kld)
			printf("%p %c %-24s alignment (%ld bytes)\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    (long)addend);
		break;

	case R_LARCH_RELAX:
		/*
		 * R_LARCH_RELAX: Marker for instruction relaxation.
		 * Paired with another relocation at the same address.
		 * This is handled by the linker, nothing to do here.
		 */
		if (debug_kld)
			printf("%p %c %-24s relaxation marker\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype));
		break;

	case R_LARCH_GOT_HI20:
	case R_LARCH_GOT_LO12:
	case R_LARCH_GOT64_LO20:
	case R_LARCH_GOT64_HI12:
	case R_LARCH_TLS_LE_HI20:
	case R_LARCH_TLS_LE_LO12:
	case R_LARCH_TLS_IE_HI20:
	case R_LARCH_TLS_IE_LO12:
	case R_LARCH_TLS_LD_HI20:
	case R_LARCH_TLS_GD_HI20:
		/*
		 * GOT and TLS relocations are typically handled by the
		 * dynamic linker. For kernel modules, these should not
		 * appear in normal circumstances.
		 */
		printf("kldload: unsupported relocation type %s "
		    "(GOT/TLS relocations require dynamic linker)\n",
		    reloctype_to_str(rtype));
		return (-1);

	default:
		printf("kldload: unexpected relocation type %ld, "
		    "symbol index %ld\n", (long)rtype, (long)symidx);
		return (-1);
	}

	return (0);
}

int
elf_reloc(linker_file_t lf, Elf_Addr relocbase, const void *data, int type,
    elf_lookup_fn lookup)
{

	return (elf_reloc_internal(lf, relocbase, data, type, 0, lookup));
}

int
elf_reloc_local(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, elf_lookup_fn lookup)
{

	return (elf_reloc_internal(lf, relocbase, data, type, 1, lookup));
}

int
elf_cpu_load_file(linker_file_t lf __unused)
{

	return (0);
}

int
elf_cpu_unload_file(linker_file_t lf __unused)
{

	return (0);
}

int
elf_cpu_parse_dynamic(caddr_t loadbase __unused, Elf_Dyn *dynamic __unused)
{

	return (0);
}
