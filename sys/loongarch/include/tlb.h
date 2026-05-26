/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
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
 *    distribution and/or other materials provided with the distribution.
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
 
#ifndef _MACHINE_TLB_H_
#define	_MACHINE_TLB_H_

#include <machine/loongarchreg.h>
#include <machine/pte.h>

/*
 * TLB Page Size values for TLBIDX register
 * These values are used with CSR_TLBIDX_PS field
 */
#define	PS_4K_VAL		0x0	/* 4KB page */
#define	PS_16K_VAL		0x1	/* 16KB page */
#define	PS_64K_VAL		0x2	/* 64KB page */
#define	PS_2M_VAL		0x3	/* 2MB page */
#define	PS_1G_VAL		0x4	/* 1GB page */

/* Page size constants */
#define	PS_4K_SIZE		(4 * 1024)
#define	PS_16K_SIZE		(16 * 1024)
#define	PS_64K_SIZE		(64 * 1024)
#define	PS_2M_SIZE		(2 * 1024 * 1024)
#define	PS_1G_SIZE		(1024 * 1024 * 1024)

/*
 * TLB Invalidate Flush operations
 * Reference: LoongArch Reference Manual Volume 2, Section 11.3
 */
static inline void tlbclr(void)
{
	__asm __volatile("tlbclr");
}

static inline void tlbflush(void)
{
	__asm __volatile("tlbflush");
}

/*
 * TLB R/W operations.
 */
static inline void tlb_probe(void)
{
	__asm __volatile("tlbsrch");
}

static inline void tlb_read(void)
{
	__asm __volatile("tlbrd");
}

static inline void tlb_write_indexed(void)
{
	__asm __volatile("tlbwr");
}

static inline void tlb_write_random(void)
{
	__asm __volatile("tlbfill");
}

/*
 * TLB refill debug support
 * These macros help diagnose TLB-related boot failures
 */
#define	TLB_REFILL_DEBUG(fmt, ...)	do { } while (0)
#define	TLB_FLUSH_DEBUG(fmt, ...)	do { } while (0)

/*
 * PTE to EntryLo conversion macros
 * Convert software PTE format to hardware EntryLo register format
 */
#define	PTE_TO_ENTRYLO(pte)						\
	(((pte) & (ENTRYLO_V | ENTRYLO_D | ENTRYLO_PLV |		\
		   ENTRYLO_C | ENTRYLO_G | ENTRYLO_NR | ENTRYLO_NX)))

/*
 * Validate PTE before TLB insertion
 * Returns non-zero if PTE is valid for TLB insertion
 * 
 * Updated for LoongArch hardware specification:
 * - PTE_V (bit 0) is the hardware Valid bit
 * - PTE_HW_P (bit 60) is used by hardware page table walker
 * - PTE_P is now defined as PTE_V for compatibility
 */
static __inline int
pte_validate(uint64_t pte)
{
	/* Must have Valid bit set (PTE_P is now alias for PTE_V) */
	if ((pte & PTE_V) == 0)
		return (0);

	/* If Accessed is set, Valid must also be set */
	if ((pte & PTE_A) && !(pte & PTE_V))
		return (0);

	/* For hardware page table walker, check Hardware Present bit */
	if ((pte & PTE_HW_P) == 0)
		return (0);

	return (1);
}

/*
 * Sync Valid bit with Accessed bit
 * Ensures TLB refill will work correctly
 */
static __inline uint64_t
pte_sync_valid(uint64_t pte)
{
	if (pte & PTE_A)
		pte |= PTE_V;
	return (pte);
}

enum invtlb_ops {
	/* Invalid all tlb */
	INVTLB_ALL = 0x0,
	/* Invalid current tlb */
	INVTLB_CURRENT_ALL = 0x1,
	/* Invalid all global=1 lines in current tlb */
	INVTLB_CURRENT_GTRUE = 0x2,
	/* Invalid all global=0 lines in current tlb */
	INVTLB_CURRENT_GFALSE = 0x3,
	/* Invalid global=0 and matched asid lines in current tlb */
	INVTLB_GFALSE_AND_ASID = 0x4,
	/* Invalid addr with global=0 and matched asid in current tlb */
	INVTLB_ADDR_GFALSE_AND_ASID = 0x5,
	/* Invalid addr with global=1 or matched asid in current tlb */
	INVTLB_ADDR_GTRUE_OR_ASID = 0x6,
	/* Invalid matched gid in guest tlb */
	INVGTLB_GID = 0x9,
	/* Invalid global=1, matched gid in guest tlb */
	INVGTLB_GID_GTRUE = 0xa,
	/* Invalid global=0, matched gid in guest tlb */
	INVGTLB_GID_GFALSE = 0xb,
	/* Invalid global=0, matched gid and asid in guest tlb */
	INVGTLB_GID_GFALSE_ASID = 0xc,
	/* Invalid global=0 , matched gid, asid and addr in guest tlb */
	INVGTLB_GID_GFALSE_ASID_ADDR = 0xd,
	/* Invalid global=1 , matched gid, asid and addr in guest tlb */
	INVGTLB_GID_GTRUE_ASID_ADDR = 0xe,
	/* Invalid all gid gva-->gpa guest tlb */
	INVGTLB_ALLGID_GVA_TO_GPA = 0x10,
	/* Invalid all gid gpa-->hpa tlb */
	INVTLB_ALLGID_GPA_TO_HPA = 0x11,
	/* Invalid all gid tlb, including  gva-->gpa and gpa-->hpa */
	INVTLB_ALLGID = 0x12,
	/* Invalid matched gid gva-->gpa guest tlb */
	INVGTLB_GID_GVA_TO_GPA = 0x13,
	/* Invalid matched gid gpa-->hpa tlb */
	INVTLB_GID_GPA_TO_HPA = 0x14,
	/* Invalid matched gid tlb,including gva-->gpa and gpa-->hpa */
	INVTLB_GID_ALL = 0x15,
	/* Invalid matched gid and addr gpa-->hpa tlb */
	INVTLB_GID_ADDR = 0x16,
};

/*
 * TLB invalidate operations.
 * The first operand of invtlb must be an immediate in range [0, 31].
 * We use macros to ensure the operation code is emitted as an immediate.
 */
#define	invtlb(op, info, addr) do {					\
	__asm __volatile(						\
		"invtlb %0, %1, %2\n\t"				\
		:							\
		: "i"(op), "r"(info), "r"(addr)			\
		: "memory");						\
} while (0)

#define	invtlb_addr(op, addr) do {					\
	__asm __volatile(						\
		"invtlb %0, $zero, %1\n\t"				\
		:							\
		: "i"(op), "r"(addr)					\
		: "memory");						\
} while (0)

#define	invtlb_info(op, info) do {					\
	__asm __volatile(						\
		"invtlb %0, %1, $zero\n\t"				\
		:							\
		: "i"(op), "r"(info)					\
		: "memory");						\
} while (0)

#define	invtlb_all(op) do {						\
	__asm __volatile(						\
		"invtlb %0, $zero, $zero\n\t"				\
		:							\
		: "i"(op)						\
		: "memory");						\
} while (0)

#endif
