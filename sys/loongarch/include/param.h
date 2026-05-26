/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 2024 Shanwei Yu <mpysw@vip.163.com>
 * Copyright (c) 2024 Xiaoqiang Zhao <zxq_yx_007@163.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _MACHINE_PARAM_H_
#define	_MACHINE_PARAM_H_

/*
 * Machine dependent constants for LoongArch.
 *
 * Based on LoongArch ELF psABI specification (lapcs.adoc, laelf.adoc)
 * and LoongArch toolchain conventions (la-toolchain-conventions.adoc).
 */

#include <machine/_align.h>

/*
 * Compiler built-in macro definitions for LoongArch.
 * These definitions match the LoongArch toolchain conventions.
 *
 * Note: The actual compiler built-in macros (__loongarch__, __loongarch_grlen, etc.)
 * are defined by the compiler (GCC/Clang) itself. We define FreeBSD-specific
 * macros here for consistency.
 */

/* Architecture identification */
#ifndef MACHINE
#define	MACHINE		"loongarch"
#endif
#ifndef MACHINE_ARCH
#define	MACHINE_ARCH	"loongarch64"
#endif

/*
 * Data model: LP64
 * - long: 64 bits
 * - pointer: 64 bits
 * - int: 32 bits
 *
 * This matches the lp64d ABI (the default for LoongArch64).
 */
#define	__LOONGARCH_LP64	1

/*
 * Register widths (for documentation purposes)
 * The actual values are provided by compiler built-ins:
 * - __loongarch_grlen: General-purpose register width (64 for LA64)
 * - __loongarch_frlen: Floating-point register width (64 for double-precision FPU)
 */
#define	LOONGARCH_GRLEN		64	/* General-purpose register width */
#define	LOONGARCH_FRLEN		64	/* Floating-point register width (default) */

/*
 * ABI type: lp64d (default)
 * - Uses 64-bit GPRs for parameter passing
 * - Uses 64-bit FPRs for floating-point parameter passing
 * - LP64 data model
 *
 * Other possible ABI types (not currently supported):
 * - lp64f: 64-bit GPRs, 32-bit FPRs
 * - lp64s: 64-bit GPRs, soft-float
 */
#define	__LOONGARCH_ABI		"lp64d"
#define	__LOONGARCH_HARD_FLOAT	1
#define	__LOONGARCH_DOUBLE_FLOAT 1

/*
 * Stack alignment requirement: 16 bytes
 * Per LoongArch ELF psABI specification (lapcs.adoc).
 */
#define	STACKALIGNBYTES	(16 - 1)
#define	STACKALIGN(p)	((uint64_t)(p) & ~STACKALIGNBYTES)

/* Feature flags */
#define	__HAVE_STATIC_DEVMAP
#define	__PCI_REROUTE_INTERRUPT

/*
 * CPU multiprocessor support
 */
#ifndef MAXCPU
#define	MAXCPU		16
#endif

#ifndef MAXMEMDOM
#define	MAXMEMDOM	1
#endif

#define	ALIGNBYTES	_ALIGNBYTES
#define	ALIGN(p)	_ALIGN(p)
/*
 * ALIGNED_POINTER is a boolean macro that checks whether an address
 * is valid to fetch data elements of type t from on this architecture.
 * This does not reflect the optimal alignment, just the possibility
 * (within reasonable limits).
 */
#define	ALIGNED_POINTER(p, t)	((((u_long)(p)) & (sizeof(t) - 1)) == 0)

/*
 * CACHE_LINE_SIZE is the compile-time maximum cache line size for an
 * architecture.  It should be used with appropriate caution.
 */
#define	CACHE_LINE_SHIFT	6
#define	CACHE_LINE_SIZE		(1 << CACHE_LINE_SHIFT)

#define	PAGE_SHIFT	12
#define	PAGE_SIZE	(1 << PAGE_SHIFT)	/* Page size */
#define	PAGE_MASK	(PAGE_SIZE - 1)

#define	MAXPAGESIZES	3	/* maximum number of supported page sizes */

#ifndef KSTACK_PAGES
#define	KSTACK_PAGES	16	/* pages of kernel stack (with pcb) */
#endif

#define	KSTACK_GUARD_PAGES	1	/* pages of kstack guard; 0 disables */
#define	PCPU_PAGES		1

/*
 * Mach derived conversion macros
 */
#define	loongarch_btop(x)	((unsigned long)(x) >> PAGE_SHIFT)
#define	loongarch_ptob(x)	((unsigned long)(x) << PAGE_SHIFT)

/*
 * Large page (2MB) alignment macros for devmap compatibility
 * Note: LoongArch uses 2MB large pages, not ARM's 1MB sections
 */
#define	L1_S_SHIFT	21		/* 2MB large page shift */
#define	L1_S_MASK	((1UL << L1_S_SHIFT) - 1)
#define	trunc_1mpage(x)		((unsigned long)(x) & ~L1_S_MASK)
#define	round_1mpage(x)		((((unsigned long)(x)) + L1_S_MASK) & ~L1_S_MASK)

#endif /* !_MACHINE_PARAM_H_ */
