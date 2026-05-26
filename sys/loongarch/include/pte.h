/*-
 * Copyright (c) 2014 Andrew Turner
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2024 Shanwei Yu <mpysw@vip.163.com>
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

#ifndef _MACHINE_PTE_H_
#define	_MACHINE_PTE_H_

#ifndef LOCORE
typedef	uint64_t	pd_entry_t;		/* page directory entry */
typedef	uint64_t	pt_entry_t;		/* page table entry */
typedef	uint64_t	pn_t;			/* page number */
#endif

/* Level 0 table, 512GiB per entry */
#define	L0_SHIFT	39
#define	L0_SIZE		(1UL << L0_SHIFT)
#define	L0_OFFSET	(L0_SIZE - 1)

/* Level 1 table, 1GiB per entry */
#define	L1_SHIFT	30
#define	L1_SIZE		(1UL << L1_SHIFT)
#define	L1_OFFSET	(L1_SIZE - 1)

/* Level 2 table, 2MiB per entry */
#define	L2_SHIFT	21
#define	L2_SIZE		(1UL << L2_SHIFT)
#define	L2_OFFSET	(L2_SIZE - 1)

/* Level 3 table, 4KiB per entry */
#define	L3_SHIFT	12
#define	L3_SIZE		(1UL << L3_SHIFT)
#define	L3_OFFSET	(L3_SIZE - 1)

#define	Ln_ENTRIES_SHIFT 9
#define	Ln_ENTRIES	(1 << Ln_ENTRIES_SHIFT)
#define	Ln_ADDR_MASK	(Ln_ENTRIES - 1)

#define	L1_S_SIZE	L2_SIZE
#define	L1_S_OFFSET	(L1_S_SIZE - 1)
#define	L1_S_FRAME	(~L1_S_OFFSET)

/*
 * LoongArch PTE Format (64-bit):
 *  63   62   61   60  59-48  47-12  11-6  5-4  3-2  1  0
 * +----+----+----+----+------+-------+-----+----+----+--+--+
 * |RPLV| NX | NR | P  | Res  | PFN  |Flags|MAT |PLV | D| V|
 * +----+----+----+----+------+-------+-----+----+----+--+--+
 *
 * Hardware bits: V(0), D(1), PLV(3:2), MAT(5:4), G(6), P(60), NR(61), NX(62), RPLV(63)
 * Software bits: 7-11 (ignored by hardware, available for OS use)
 *
 * Note: Bit 6 is GLOBAL in PTE (L3) context but HUGE in PMD (L2) context.
 * For huge pages, the global flag is stored in PTE_HUGE_G (bit 12).
 */

#define	PTE_V		(1UL << 0)
#define	PTE_D		(1UL << 1)
#define	PTE_PLV_SHIFT	2
#define	PTE_PLV_MASK	(0x3UL << PTE_PLV_SHIFT)
#define	PTE_K		(0 << PTE_PLV_SHIFT)
#define	PTE_U		(0x3 << PTE_PLV_SHIFT)
#define	PTE_CACHE_SHIFT	4
#define	PTE_CACHE_MASK	(0x3UL << PTE_CACHE_SHIFT)
#define	PTE_SUC		(0 << PTE_CACHE_SHIFT)
#define	PTE_CC		(1 << PTE_CACHE_SHIFT)
#define	PTE_WUC		(2 << PTE_CACHE_SHIFT)
#define	PTE_G		(1UL << 6)

#define	PTE_SW_SHIFT	7
#define	PTE_SW_MASK	(0x1FUL << PTE_SW_SHIFT)

#define	PTE_HW_P	(1UL << 60)
#define	PTE_P		PTE_V

#define	PTE_W		(1UL << 7)
#define	PTE_SW_WIRED	(1UL << 8)
#define	PTE_A		(1UL << 9)
#define	PTE_SW_MANAGED	(1UL << 10)
#define	PTE_SW_SOFT4	(1UL << 11)

#define	PTE_HUGE	(1UL << 6)
#define	PTE_HUGE_G	(1UL << 12)
#define	PTE_NR		(1UL << 61)
#define	PTE_NX		(1UL << 62)
#define	PTE_RPLV	(1UL << 63)

#define	PTE_PFN_MASK	0x0000fffffffff000ULL
#define	L2PTE_PFN_MASK	(PTE_PFN_MASK & ~PTE_HUGE_G)

#define	PTE_TO_PHYS(pte)	((vm_paddr_t)((pte) & PTE_PFN_MASK))
#define	L2PTE_TO_PHYS(l2)	((vm_paddr_t)((l2) & L2PTE_PFN_MASK))
#define	L1PTE_TO_PHYS(l1)	((vm_paddr_t)((l1) & L2PTE_PFN_MASK))

#define	PTE_PFN(pte)		(((pte) & PTE_PFN_MASK) >> L3_SHIFT)
#define	PFN_TO_PTE(pfn)		(((vm_paddr_t)(pfn)) << L3_SHIFT)

#define	PHYS_TO_PFN(pa)		((vm_paddr_t)(pa) >> PAGE_SHIFT)
#define	PFN_TO_PHYS(pfn)	((vm_paddr_t)(pfn) << PAGE_SHIFT)

/*
 * PTE_READABLE: Valid entry with PTE_W set for hardware PTW compatibility.
 *
 * When QEMU's hardware PTW is enabled, pte_present() checks bit 7 (P).
 * Since PTE_W occupies bit 7, ALL valid leaf entries must have it set.
 * The pmap write-permission check uses PTE_SW_WIRED (bit 8) instead.
 *
 * PTE_WRITEABLE: Includes PTE_SW_WIRED (bit 8) so QEMU's pte_write()
 * sees W=1 for writable entries when hardware PTW is enabled.
 */
#define	PTE_READABLE	(PTE_V | PTE_W)
#define	PTE_WRITEABLE	(PTE_D | PTE_W | PTE_SW_WIRED)

#define	PTE_PROMOTE_BITS	(PTE_V | PTE_W | PTE_D | PTE_A | PTE_G | PTE_U | \
				 PTE_SW_MANAGED | PTE_SW_WIRED | PTE_HW_P)

/*
 * PTE_RX/PTE_RWX: Test if entry is a leaf (not a directory entry).
 * A directory entry has only PPN set (no V/W/NR/NX), so these return false.
 */
#define	PTE_RX(pte)	(((pte) & PTE_V) != 0 && \
    (((pte) & PTE_NR) == 0 || ((pte) & PTE_NX) == 0))
#define	PTE_RWX(pte)	(PTE_RX(pte) || (((pte) & (PTE_V | PTE_W)) == (PTE_V | PTE_W)))

#define	PTE_SYNC_VALID(pte)	do {			\
	if ((pte) & PTE_A)				\
		(pte) |= PTE_V;				\
} while (0)

/*
 * PDE (non-leaf directory entry) format:
 *
 * PDE entries contain only PPN (bits [47:12]); a zero entry indicates unmapped.
 * The lddir instruction computes next-level physical address as:
 *   phys = (entry & TARGET_PHYS_MASK) | (index << 3)
 * TARGET_PHYS_MASK = 0x0000FFFFFFFFFFFF for LA48, which clears bits [63:48].
 * PDE entries must NOT include flags in bits [11:0] because lddir ORs
 * (index << 3) into the result, and any low bits would corrupt the address.
 * Bits [63:48] are also cleared by TARGET_PHYS_MASK, so HW_P/NR/NX/RPLV
 * have no effect in directory entries.
 */
#define	PDE_BASE	0

#define	PDE_VALID(pde)	(((pde) & PTE_PFN_MASK) != 0)

#define	PMD_KERN	(PTE_K | PTE_READABLE | PTE_WRITEABLE | PTE_CC | PTE_G | PTE_A | PTE_V | PTE_HW_P)
#define	PMD_KERN_HUGE	(PTE_K | PTE_READABLE | PTE_WRITEABLE | PTE_CC | PTE_G | PTE_A | PTE_V | PTE_HW_P | PTE_HUGE | PTE_HUGE_G)

#define	PTE_KERN	(PTE_K | PTE_READABLE | PTE_WRITEABLE | PTE_CC | PTE_G | PTE_A | PTE_V | PTE_HW_P)
#define	PTE_KERN_SUC	(PTE_K | PTE_READABLE | PTE_WRITEABLE | PTE_SUC | PTE_G | PTE_A | PTE_V | PTE_HW_P)
#define	PTE_KERN_WUC	(PTE_K | PTE_READABLE | PTE_WRITEABLE | PTE_WUC | PTE_G | PTE_A | PTE_V | PTE_HW_P)

#define	PPNS		L3_SHIFT
#define	PPNS_HUGE_2M	L2_SHIFT

#define	PTE_SIZE	8

/*
 * PWCTL (Page Walk Control) register configuration
 *
 * 4-level page table (LA48): PGD[47:39] -> PUD[38:30] -> PMD[29:21] -> PTE[20:12]
 *
 * lddir level mapping (level parameter directly selects DIRn in PWCL/PWCH):
 *   level=1 -> PWCL.DIR1: PMD (VA[29:21])
 *   level=2 -> PWCL.DIR2: PUD (VA[38:30])
 *   level=3 -> PWCH.DIR3: PGD (VA[47:39])
 * Traversal order: lddir 3 -> lddir 2 -> lddir 1 -> ldpte -> tlbfill
 */

#define	PTE_WIDTH	0

#define	PT_BASE		12
#define	PT_WIDTH	9

#define	DIR1_BASE	21
#define	DIR1_WIDTH	9

#define	DIR2_BASE	30
#define	DIR2_WIDTH	9

#define	DIR3_BASE	39
#define	DIR3_WIDTH	9

/*
 * CSR_PWCL: [31:30]=PTEW, [29:25]=DIR2W, [24:20]=DIR2B,
 *           [19:15]=DIR1W, [14:10]=DIR1B, [9:5]=PTW, [4:0]=PTB
 */
#define	PWCL_BOOT	\
    ((PTE_WIDTH << 30)|(DIR2_WIDTH << 25)|(DIR2_BASE << 20)|\
     (DIR1_WIDTH << 15)|(DIR1_BASE << 10)|(PT_WIDTH << 5)|PT_BASE)

/*
 * CSR_PWCH: [24]=PTW, [23:18]=DIR4W, [17:12]=DIR4B,
 *           [11:6]=DIR3W, [5:0]=DIR3B
 *
 * PTW (bit 24) must NOT be enabled during bootstrap.
 * It is enabled later in initloongarch() after pmap_bootstrap()
 * when the CPU supports it (CPUCFG2 bit 24).
 */
#define	PWCH_BOOT	((DIR3_WIDTH << 6) | DIR3_BASE)

#endif /* !_MACHINE_PTE_H_ */
