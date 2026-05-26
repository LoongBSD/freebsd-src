/*-
 * Copyright (c) 1996-1997 John D. Polstra.
 * Copyright (c) 2024 Shanwei Yu <mpysw@vip.163.com>
 * Copyright (c) 2024 Xiaoqiang Zhao <zxq_yx_007@163.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 * All rights reserved.
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

#ifndef	_MACHINE_ELF_H_
#define	_MACHINE_ELF_H_

/*
 * ELF definitions for the LoongArch architecture.
 */

#include <sys/elf32.h>	/* Definitions common to all 32 bit architectures. */
#include <sys/elf64.h>	/* Definitions common to all 64 bit architectures. */

#define	__ELF_WORD_SIZE	64	/* Used by <sys/elf_generic.h> */
#include <sys/elf_generic.h>

/*
 * Auxiliary vector entries for passing information to the interpreter.
 */

typedef struct {	/* Auxiliary vector entry on initial stack */
	int	a_type;			/* Entry type. */
	union {
		int	a_val;		/* Integer value. */
	} a_un;
} Elf32_Auxinfo;

typedef struct {	/* Auxiliary vector entry on initial stack */
	long	a_type;			/* Entry type. */
	union {
		long	a_val;		/* Integer value. */
		void	*a_ptr;		/* Address. */
		void	(*a_fcn)(void);	/* Function pointer (not used). */
	} a_un;
} Elf64_Auxinfo;

__ElfType(Auxinfo);

#define	ELF_ARCH	EM_LOONGARCH

#define	ELF_MACHINE_OK(x) ((x) == (ELF_ARCH))

/* Define "machine" characteristics */
#define	ELF_TARG_CLASS	ELFCLASS64
#define	ELF_TARG_DATA	ELFDATA2LSB
#define	ELF_TARG_MACH	EM_LOONGARCH
#define	ELF_TARG_VER	1

#define	ET_DYN_LOAD_ADDR 0x01000000

/*
 * Flags passed in AT_HWCAP
 *
 * These definitions are compatible with the Linux kernel LoongArch HWCAP
 * definitions. See Linux arch/loongarch/include/uapi/asm/hwcap.h
 *
 * HWCAP (Hardware Capability) flags for LoongArch.
 * Based on la-softdev-convention.adoc Table 1 HWCAP Definitions.
 */
#define HWCAP_LOONGARCH_CPUCFG		(1 << 0)	/* Supports cpucfg instruction */
#define HWCAP_LOONGARCH_LAM		(1 << 1)	/* Supports atomic instructions (LAM) */
#define HWCAP_LOONGARCH_UAL		(1 << 2)	/* Supports unaligned access */
#define HWCAP_LOONGARCH_FPU		(1 << 3)	/* Supports single/double precision FPU */
#define HWCAP_LOONGARCH_CRC32		(1 << 6)	/* Supports 32-bit CRC instruction */
#define HWCAP_LOONGARCH_COMPLEX		(1 << 7)	/* Supports complex vector operation */
#define HWCAP_LOONGARCH_CRYPTO		(1 << 8)	/* Supports cryptographic vector instruction */
#define HWCAP_LOONGARCH_LVZ		(1 << 9)	/* Supports virtualization extension (LVZ) */
#define HWCAP_LOONGARCH_LBT_X86		(1 << 10)	/* Supports x86 binary translation (LBT) */
#define HWCAP_LOONGARCH_LBT_ARM		(1 << 11)	/* Supports ARM binary translation (LBT) */
#define HWCAP_LOONGARCH_LBT_MIPS	(1 << 12)	/* Supports MIPS binary translation (LBT) */
#define HWCAP_LOONGARCH_PTW		(1 << 13)	/* Supports page table walk (PTW) */
#define HWCAP_LOONGARCH_LAMCAS		(1 << 14)	/* Supports LAMCAS instruction (v1.1) */
#define HWCAP_LOONGARCH_ITMC		(1 << 15)	/* Supports instruction timing counter */
#define HWCAP_LOONGARCH_EIEN		(1 << 16)	/* Supports external interrupt enable */
#define HWCAP_LOONGARCH_FANV		(1 << 17)	/* Supports FPU advanced instructions */
#define HWCAP_LOONGARCH_LAMNE		(1 << 18)	/* Supports LAMNE instruction */
#define HWCAP_LOONGARCH_LAMMA		(1 << 19)	/* Supports LAMMA instruction */
/* Bits 20-31 are reserved for future use */

/*
 * HWCAP2 flags (extended capability flags)
 * These are passed via AT_HWCAP2 auxiliary vector entry.
 */
#define HWCAP2_LOONGARCH_CPUCFG2	(1 << 0)	/* Extended cpucfg support */
#define HWCAP2_LOONGARCH_V1P1		(1 << 1)	/* LoongArch v1.1 ISA */
/* Bits 2-31 are reserved for future use */

#endif /* !_MACHINE_ELF_H_ */
