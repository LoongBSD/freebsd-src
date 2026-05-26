/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2019-2021 Mitchell Horne <mhorne@FreeBSD.org>
 * Copyright (c) 2022-2024 The FreeBSD Foundation
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
 * Portions of this software were developed by Mitchell Horne
 * <mhorne@FreeBSD.org> under sponsorship from the FreeBSD Foundation.
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

#include "assym.inc"

#include <sys/syscall.h>
#include <machine/asm.h>
#include <machine/param.h>
#include <machine/trap.h>
#include <machine/loongarchreg.h>
#include <machine/pte.h>

	.globl	kernbase
	.set	kernbase, KERNBASE

	.text
/*
 * Main entry point. This routine is marked as the ELF entry, and is where
 * loader(8) will enter the kernel. Arguments are as follows:
 *  - a0 = modulep
 *  - a1 = ???
 *
 * It is expected that only a single CPU will enter here.
 */
	.globl _start
_start:
	move		$a1, $zero

	/* disable PG */
	li.w		$t0, 0xa8		# PLV=0, IE=0, PG=0, DA=1
	csrwr		$t0, LOONGARCH_CSR_CRMD

	/*
	 * Initialize Direct Map Windows before enabling paging.
	 * DMW0: 0x8000_xxxx_xxxx_xxxx -> phys, SUC, PLV0
	 * DMW1: 0x9000_xxxx_xxxx_xxxx -> phys, CAC, PLV0
	 * Must be done in DA mode before PG is enabled.
	 */
	li.d		$t0, CSR_DMW0_INIT
	csrwr		$t0, LOONGARCH_CSR_DMWIN0
	li.d		$t0, CSR_DMW1_INIT
	csrwr		$t0, LOONGARCH_CSR_DMWIN1
	li.d		$t0, 0x0
	csrwr		$t0, LOONGARCH_CSR_DMWIN2
	li.d		$t0, 0x0
	csrwr		$t0, LOONGARCH_CSR_DMWIN3

pagetables:
	bl		get_physmem

	/* Map KERNBASE using 2MB hugepages: L0 -> L1 -> L2 */
	la.pcrel	$t0, pagetable_l0
	la.pcrel	$t1, pagetable_l1
	li.d		$t2, KERNBASE
	bl		create_pagetables_l0_to_l1

	la.pcrel	$t0, pagetable_l1
	la.pcrel	$t1, pagetable_l2
	li.d		$t2, KERNBASE
	move		$t3, $s1
	li.d		$t4, L1_SIZE
	bl		create_pagetables_to_l2

	/* User space identity map: L0 -> L1 -> L2 */
	la.pcrel	$t0, pagetable_l0
	la.pcrel	$t1, pagetable_id_l1
	li.d		$t2, 0
	bl		create_pagetables_l0_to_l1

	la.pcrel	$t0, pagetable_id_l1
	la.pcrel	$t1, pagetable_id_l2
	li.d		$t2, 0
	li.d		$t3, 0
	li.d		$t4, L1_SIZE
	bl		create_pagetables_to_l2

	/* Link DMAP L1 table into L0 (index = DMAP_MIN_ADDRESS >> L0_SHIFT = 384) */
	la.pcrel	$t0, pagetable_l0
	la.pcrel	$t1, pagetable_l1_dmap
	srli.d		$a2, $t1, PAGE_SHIFT
	slli.d		$t5, $a2, PPNS
	li.d		$t6, 3072
	stx.d		$t5, $t0, $t6

	/* Map devmap L1 pagetable */
	la.pcrel	$t0, pagetable_l1
	la.pcrel	$t1, pagetable_l2_devmap
	li.d		$t2, (VM_MAX_KERNEL_ADDRESS - PMAP_MAPDEV_EARLY_SIZE)
	bl		create_pagetables_to_l1

	/* setup 4KB page size */
	li.w		$t0, 0xc
	csrwr		$t0, LOONGARCH_CSR_STLBPGSIZE

	/* setup TLBIDX page size */
	li.w		$t0, (0xc << CSR_TLBIDX_PS_SHIFT)
	csrwr		$t0, LOONGARCH_CSR_TLBIDX

	/* set TLBREHI page size */
	li.d		$t0, 0xc
	csrwr		$t0, LOONGARCH_CSR_TLBREHI

	/* set PWCL */
	li.d		$t0, PWCL_BOOT
	csrwr		$t0, LOONGARCH_CSR_PWCTL0

	/* set PWCH */
	li.d		$t0, PWCH_BOOT
	csrwr		$t0, LOONGARCH_CSR_PWCTL1

	/* setup PGD */
	la.pcrel	$t0, pagetable_l0
	csrwr		$t0, LOONGARCH_CSR_PGDH		/* high half */
	la.pcrel	$t0, pagetable_l0
	csrwr		$t0, LOONGARCH_CSR_PGDL		/* low half - same L0 table */

	/* Initialize PTW diagnostic storage */
	la.pcrel	$t0, ptw_diag_store
	li.d		$t1, 0
	st.d		$t1, $t0, 0
	st.d		$t1, $t0, 8
	st.d		$t1, $t0, 16
	st.d		$t1, $t0, 24
	st.d		$t1, $t0, 32
	st.d		$t1, $t0, 40
	st.d		$t1, $t0, 48
	st.d		$t1, $t0, 56
	st.d		$t1, $t0, 64
	st.d		$t1, $t0, 72
	st.d		$t1, $t0, 80

	/* Check hardware PTW support */
	li.w		$t0, LOONGARCH_CPUCFG2
	cpucfg		$t0, $t0
	srli.d		$t1, $t0, 24
	andi		$t1, $t1, 1
	bnez		$t1, use_hw_ptw

	/*
	 * No hardware PTW: use handle_tlbrefill_sw which uses
	 * CSR_PGD auto-selection and checks for invalid entries.
	 */
	la.pcrel	$t0, handle_tlbrefill_sw
	csrwr		$t0, LOONGARCH_CSR_TLBRENTRY
	b		ptw_done

use_hw_ptw:
	la.pcrel	$t0, handle_tlbrefill_boot
	csrwr		$t0, LOONGARCH_CSR_TLBRENTRY

ptw_done:

	la.pcrel	$t0, ptw_diag_store
	li.d		$t1, 0x5054575f4857
	st.d		$t1, $t0, 0

pagetables_done:

	li.d		$t0, 0x800000001FE001E0
	li.w		$t1, 0x41
	st.b		$t1, $t0, 0

	/* record virtdone virt addr*/
	la.pcrel	$t1, Lvirtdone
	ld.d		$t5, $t1, 0
        li.w            $t0, 0xb0                # PLV=0, IE=0, PG=1
        csrwr           $t0, LOONGARCH_CSR_CRMD
        li.w            $t0, 0x04                # PLV=0, PIE=1, PWE=0
        csrwr           $t0, LOONGARCH_CSR_PRMD
        li.w            $t0, 0x00                # FPE=0, SXE=0, ASXE=0, BTE=0
        csrwr           $t0, LOONGARCH_CSR_EUEN

	li.d		$t0, 0x800000001FE001E0
	li.w		$t1, 0x42
	st.b		$t1, $t0, 0

	/* setup exception */
	csrrd		$t0, LOONGARCH_CSR_ECFG
	li.d		$t1, ~0x70000
	and		$t0, $t0, $t1
	/* enable timer and ipi */
	li.d		$t1, 0x1800	/* (1 << IRQ_TI) | (1 << IRQ_IPI) */
	or		$t0, $t0, $t1
	csrwr		$t0, LOONGARCH_CSR_ECFG

	la.pcrel	$t0, cpu_exception_handler
	la.pcrel	$t1, virt_map	/* physical address of virt_map */
	ld.d		$t2, $t1, 0	/* virtual address of virt_map */
	sub.d		$t3, $t2, $t1	/* calculate phys->virt delta */
	add.d		$t0, $t0, $t3
	csrwr		$t0, LOONGARCH_CSR_EENTRY

	/* flush tlb (global) */
	invtlb		0, $zero, $zero

	/* jump to virtual address space */
	jirl		$zero, $t5, 0

virtdone:
	li.d		$t0, 0x800000001FE001E0
	li.w		$t1, 0x43
	st.b		$t1, $t0, 0

	/*
	 * Remove the user space identity mapping from the kernel L0 table.
	 * The identity mapping (VA 0x0 -> PA 0x0) was needed during the
	 * transition from physical to virtual addressing, but is no longer
	 * required now that we run at KERNBASE.  Leaving it in place would
	 * cause TLB refills for user-space addresses to find kernel page
	 * table entries, potentially mapping user VAs to arbitrary physical
	 * addresses (including MMIO regions).
	 *
	 * Note: we only clear the L0 entry here.  The stale TLB entries
	 * for the identity mapping (which are global) will be invalidated
	 * later by invtlb_all(INVTLB_CURRENT_GTRUE) in pmap_bootstrap().
	 */
	la.pcrel	$t0, pagetable_l0
	st.d		$zero, $t0, 0

	la.pcrel	$t0, Lbss
        ld.d		$t0, $t0, 0         # clear .bss
        st.d            $zero, $t0, 0
	la.pcrel	$t1, Lend
	ld.d		$t1, $t1, 0
1:
        addi.d          $t0, $t0, 8
        st.d            $zero, $t0, 0
        bne             $t0, $t1, 1b

#ifdef CONFIG_PAGE_SIZE_4KB
        li.d            $t0, 0
        li.d            $t1, CSR_STFILL
        csrxchg         $t0, $t1, LOONGARCH_CSR_IMPCTL1
#endif

	/*
	 * Initialize percpu base address.
	 * $r21 is used as shadow register for fast percpu access.
	 * Set to 0 here; initloongarch() will set the proper value.
	 */
	csrwr           $zero, PERCPU_BASE_KS
	move            $r21, $zero

	la.pcrel	$tp, initstack

	/* Initialize stack pointer */
	la.pcrel	$sp, initstack_end

	/* Allocate space for thread0 PCB and bootparams*/
	li.d		$t0, (-(PCB_SIZE + LOONGARCH_BOOTPARAMS_SIZE) & ~STACKALIGNBYTES)
	add.d		$sp, $sp, $t0

	/*
	 * Establish stack frame for GDB unwinding.
	 * Save old frame pointer (zero) and set up new frame.
	 * This allows GDB to properly unwind the stack.
	 */
	move		$fp, $sp

	/* fill bootparams */
	la.pcrel	$t0, initstack
	st.d		$t0, $sp, LOONGARCH_BOOTPARAMS_KERN_STACK
	st.d		$s1, $sp, LOONGARCH_BOOTPARAMS_KERN_PHYS
	la.pcrel	$t0, pagetable_l0
	st.d		$t0, $sp, LOONGARCH_BOOTPARAMS_KERN_L1PT	/* L0 (PGD) table */
	st.d		$a0, $sp, LOONGARCH_BOOTPARAMS_MODULEP

	move	$a0, $sp

	li.d		$t0, 0x800000001FE001E0
	li.w		$t1, 0x44
	st.b		$t1, $t0, 0

	bl	_C_LABEL(initloongarch) /* Off we go */

	li.d		$t0, 0x800000001FE001E0
	li.w		$t1, 0x45
	st.b		$t1, $t0, 0

	bl	_C_LABEL(mi_startup)

	/* We should never reach here, but if so just hang. */
2:
	idle	0
	b	2b

/*
 * Get the physical address the kernel is loaded to. Returned in s1.
 */
get_physmem:
	la.pcrel	$t0, virt_map	/* physical address of virt_map */
	ld.d	$t1, $t0, 0	/* virtual address of virt_map */
	sub.d	$t1, $t1, $t0	/* calculate phys->virt delta */
	li.d	$t2, KERNBASE
	sub.d	$s1, $t2, $t1	/* s1 = physmem base */
	jr	$ra

/*
 * Create L0 -> L1 pagetable link
 * @parms
 * t0: l0 address
 * t1: l1 address
 * t2: virtual address
 */
create_pagetables_l0_to_l1:
	srli.d		$a2, $t1, PAGE_SHIFT

	srli.d		$a3, $t2, L0_SHIFT
	andi		$a3, $a3, Ln_ADDR_MASK	/* L0 index */

	slli.d		$t5, $a2, PPNS		/* L0 pte = PPN << 12 */

	li.d		$t6, PTE_SIZE
	mul.d		$a3, $a3, $t6		/* L0 slot */
	stx.d		$t5, $t0, $a3		/* save L0 pte */
	jr		$ra

/*
 * Create L1 pagetables
 * @parms
 * t0: l1 address
 * t1: l2 address
 * t2: virtual adress
 */
create_pagetables_to_l1:
	/* level 1 */
	srli.d		$a2, $t1, PAGE_SHIFT

	srli.d		$a3, $t2, L1_SHIFT
	andi		$a3, $a3, Ln_ADDR_MASK	/* L1 index */

	slli.d		$t5, $a2, PPNS		/* L1 pte */

	li.d		$t6, PTE_SIZE
	mul.d		$a3, $a3, $t6		/* L1 slot */
	stx.d		$t5, $t0, $a3		/* save L1 pte */
	jr		$ra

/*
 * Create L1 and L2 pagetables use 2MB huge page
 * @parms
 * t0: l1 address
 * t1: l2 address
 * t2: virtual adress
 * t3: physical address
 * t4: map size
 */
create_pagetables_to_l2:
	/* level 1 */
	srli.d		$a2, $t1, PAGE_SHIFT

	srli.d		$a3, $t2, L1_SHIFT
	andi		$a3, $a3, Ln_ADDR_MASK	/* L1 index */

	slli.d		$t5, $a2, PPNS		/* L1 pte */

	li.d		$t6, PTE_SIZE
	mul.d		$a3, $a3, $t6		/* L1 slot */
	stx.d		$t5, $t0, $a3		/* save L1 pte */

	/* level 2 huge page  */
	srli.d		$t5, $t2, L2_SHIFT
	andi		$t5, $t5, Ln_ADDR_MASK	/* L2 index */
	srli.d		$t6, $t4, L2_SHIFT	/* L2 count */
	add.d		$t6, $t6, $t5		/* t6 => map end idex */

	li.d		$a2, PMD_KERN_HUGE	/* pte entry */
	li.d		$a3, PTE_SIZE
	mul.d		$a4, $t5, $a3		/* L2 slot */
	srli.d		$t4, $t3, L2_SHIFT
1:
	slli.d		$a3, $t4, PPNS_HUGE_2M
	or		$a3, $a3, $a2		/* make L2 pte */
	stx.d		$a3, $t1, $a4		/* save L2 pte */

	addi.d		$a4, $a4, PTE_SIZE	/* update l2 slot */
	addi.d		$t4, $t4, 1		/* update physical address */
	addi.d		$t5, $t5, 1		/* update l2 index */
	bltu		$t5, $t6, 1b		/* build all 512 entries */
	jr		$ra

/*
 * TLB refill handler for boot stage.
 *
 * Traversal: lddir 3 (PGD->PUD) -> lddir 2 (PUD->PMD) -> lddir 1 (PMD->PTE)
 *            -> ldpte 0/1 -> tlbfill
 *
 * IMPORTANT: PDE entries must NOT include flags in bits [11:0].
 * lddir computes phys = (entry & TARGET_PHYS_MASK) | (index << 3),
 * and TARGET_PHYS_MASK only clears bits [63:48], so low bits corrupt the address.
 *
 * NOTE: This handler only uses $t0 (saved/restored via TLBRSAVE).
 * No other GPR may be modified — they belong to the interrupted context.
 * Huge page entries (PTE_HUGE) are handled correctly by the hardware PTW
 * enabled in initloongarch().  This handler is only active during early boot
 * before hardware PTW is enabled.
 */
/*
 * TLB Refill Handler (HW PTW enabled, QEMU/LA464 with PTW support).
 *
 * When hardware PTW is available, this handler only fires when PTW
 * fails — meaning the page table entry was found by the architectural
 * walk (using PTE_V/bit0) but was missed by QEMU's PTW (which checks
 * PTE_W/bit7).  In that case we fill the TLB via lddir+ldpte+tlbfill
 * so the access succeeds.
 *
 * CSR_PGD is auto-loaded by hardware from PGDL or PGDH on TLBR
 * exception based on VA[VALEN-1]; we use it directly instead of
 * manually checking VA[47].
 *
 * Register usage: $t0 = work (saved to/restored from TLBRSAVE).
 */
handle_tlbrefill_boot:
	csrwr	$t0, LOONGARCH_CSR_TLBRSAVE
	csrrd	$t0, LOONGARCH_CSR_PGD
	lddir	$t0, $t0, 3
	beqz	$t0, tlb_refill_invalid
	lddir	$t0, $t0, 2
	beqz	$t0, tlb_refill_invalid
	lddir	$t0, $t0, 1
	beqz	$t0, tlb_refill_invalid
	ldpte	$t0, 0
	ldpte	$t0, 1
	tlbfill
	csrrd	$t0, LOONGARCH_CSR_TLBRSAVE
	ertn

/*
 * TLB refill error path — fills TLB with V=0 entries so the CPU
 * retry triggers a proper TLB exception (TLBL/TLBS/TLBI) through
 * the general exception path, which calls page_fault_handler.
 */
tlb_refill_invalid:
	csrwr	$zero, LOONGARCH_CSR_TLBRELO0
	csrwr	$zero, LOONGARCH_CSR_TLBRELO1
	csrrd	$t0, LOONGARCH_CSR_TLBRSAVE
	tlbfill
	ertn

	.align		8
	.global	ptw_diag_store
ptw_diag_store:
	.space		88

/*
 * Invalid page directory (all zeros) used as PGDL during boot and
 * when no user process is active.  Mirrors Linux invalid_pg_dir:
 * hardware PTW immediately fails, TLBR fires, fills V=0 TLB entry,
 * and the resulting TLBL/TLBS exception triggers normal page fault
 * resolution.  This prevents HW PTW from accidentally matching stale
 * user-space entries in the kernel page table during early boot.
 */
	.align		12
	.global	invalid_pgdir
invalid_pgdir:
	.space		PAGE_SIZE

/*
 * Software TLB Refill Handler (no HW PTW support).
 *
 * Uses CSR_PGD (auto-loaded by hardware from PGDL/PGDH on TLBR).
 * Walks L0→L1→L2 via lddir; for non-huge L2 entries falls back
 * to ldpte/tlbfill.  For 2MB huge pages (PTE_HUGE at L2), manually
 * constructs TLBELO0/1 from the PMD entry, forces PTE_V=1, and
 * fills the TLB — avoiding ldpte garbage-read into the 2MB data page.
 *
 * IMPORTANT: This handler is copied to a dedicated page during boot.
 * All branch targets MUST be within the copied block; PC-relative
 * branches cannot reference labels outside this handler.
 *
 * Register usage:
 *   TLBRSAVE — saves/restores original $t0
 *   KS1      — saves/restores original $t1
 *   $t0/$t1  — work (free within handler, restored before ertn)
 */
	.align		4
	.globl	handle_tlbrefill_sw
handle_tlbrefill_sw:
	csrwr	$t0, LOONGARCH_CSR_TLBRSAVE
	csrwr	$t1, LOONGARCH_CSR_KS1		/* save user t1 to KS1 */
	csrrd	$t0, LOONGARCH_CSR_PGD
	lddir	$t0, $t0, 3
	beqz	$t0, invalid
	lddir	$t0, $t0, 2
	beqz	$t0, invalid
	lddir	$t0, $t0, 1
	beqz	$t0, invalid
	/* t0 = L2 entry.  Check PTE_HUGE (bit 6). */
	bstrpick.d	$t1, $t0, 6, 6
	bnez		$t1, huge_2m
	/* Non-huge (4KB): ldpte + tlbfill.  t0 preserved by bstrpick. */
	ldpte	$t0, 0
	ldpte	$t0, 1
	tlbfill
	csrrd	$t1, LOONGARCH_CSR_KS1		/* restore user t1 */
	csrrd	$t0, LOONGARCH_CSR_TLBRSAVE
	ertn

huge_2m:
	/* 2MB huge page: manually construct TLB entry.
	 * TLBRELO0 = L2 entry | PTE_V  (force V=1 in TLB).
	 * TLBRELO1 = same (paired 2MB slot uses identical entry).
	 * TLBIDX.PS = 0x3 (2MB).
	 */
	move	$t1, $t0			/* save L2 entry */
	li.d	$t0, 0x03000000		/* PS=0x3 (2MB) << 24 */
	csrwr	$t0, LOONGARCH_CSR_TLBIDX
	ori	$t0, $t1, 0x1		/* PTE_V */
	csrwr	$t0, LOONGARCH_CSR_TLBRELO0
	csrwr	$t0, LOONGARCH_CSR_TLBRELO1
	/* TLBEHI = VPN aligned to 2MB | ASID */
	csrrd	$t0, LOONGARCH_CSR_TLBRBADV
	srli.d	$t0, $t0, 21
	slli.d	$t0, $t0, 21
	csrrd	$t1, LOONGARCH_CSR_ASID
	andi	$t1, $t1, 0xfff
	or	$t0, $t0, $t1
	csrwr	$t0, LOONGARCH_CSR_TLBEHI
	tlbfill
	csrrd	$t1, LOONGARCH_CSR_KS1		/* restore user t1 */
	csrrd	$t0, LOONGARCH_CSR_TLBRSAVE
	ertn

invalid:
	/* Fill V=0 TLB entry → general exception resolves page fault. */
	csrrd	$t1, LOONGARCH_CSR_KS1		/* restore user t1 */
	csrrd	$t0, LOONGARCH_CSR_TLBRSAVE
	csrwr	$zero, LOONGARCH_CSR_TLBRELO0
	csrwr	$zero, LOONGARCH_CSR_TLBRELO1
	tlbfill
	ertn

	.align  4
initstack:
	.space  (PAGE_SIZE * KSTACK_PAGES)
initstack_end:

        .align	12
pagetable_l0:
	.space	PAGE_SIZE
pagetable_l1:
        .space  PAGE_SIZE
pagetable_l1_dmap:
	.space	PAGE_SIZE
	.globl pagetable_l1_dmap
pagetable_l2:
        .space  PAGE_SIZE
pagetable_l2_devmap:
        .space  PAGE_SIZE
pagetable_id_l1:
	.space	PAGE_SIZE
pagetable_id_l2:
	.space	PAGE_SIZE

        .align 3

virt_map:
	.quad   virt_map
Lvirtdone:
	.quad	virtdone
Lbss:
	.quad	__bss_start
Lend:
	.quad	_end

	.globl init_pt_va
init_pt_va:
	.quad pagetable_l2	/* XXX: Keep page tables VA */

/*
 * mpentry - Entry point for secondary CPUs (APs)
 *
 * Read hart ID directly from CSR_CPUID instead of relying on
 * firmware to pass it in $a0 (QEMU does not set $a0 for APs).
 * Performs the same initialization as the BSP _start, but reuses
 * the page tables already created by the BSP.
 */
ENTRY(mpentry)
	/* Read hart ID from hardware CSR */
	csrrd	$s0, LOONGARCH_CSR_CPUID

	/* disable PG */
	li.w	$t0, 0xa8		# PLV=0, IE=0, PG=0, DA=1
	csrwr	$t0, LOONGARCH_CSR_CRMD

	/* Initialize Direct Map Windows */
	li.d	$t0, CSR_DMW0_INIT
	csrwr	$t0, LOONGARCH_CSR_DMWIN0
	li.d	$t0, CSR_DMW1_INIT
	csrwr	$t0, LOONGARCH_CSR_DMWIN1
	li.d	$t0, 0x0
	csrwr	$t0, LOONGARCH_CSR_DMWIN2
	csrwr	$t0, LOONGARCH_CSR_DMWIN3

	/* setup 4KB page size */
	li.w	$t0, 0xc
	csrwr	$t0, LOONGARCH_CSR_STLBPGSIZE

	/* setup TLBIDX page size */
	li.w	$t0, (0xc << CSR_TLBIDX_PS_SHIFT)
	csrwr	$t0, LOONGARCH_CSR_TLBIDX

	/* set TLBREHI page size */
	li.d	$t0, 0xc
	csrwr	$t0, LOONGARCH_CSR_TLBREHI

	/* set PWCL */
	li.d	$t0, PWCL_BOOT
	csrwr	$t0, LOONGARCH_CSR_PWCTL0

	/* set PWCH */
	li.d	$t0, PWCH_BOOT
	csrwr	$t0, LOONGARCH_CSR_PWCTL1

	/* setup PGD - reuse BSP's page tables */
	la.pcrel	$t0, pagetable_l0
	csrwr	$t0, LOONGARCH_CSR_PGDH
	csrwr	$t0, LOONGARCH_CSR_PGDL

	/* Check hardware PTW support */
	li.w	$t0, LOONGARCH_CPUCFG2
	cpucfg	$t0, $t0
	srli.d	$t1, $t0, 24
	andi	$t1, $t1, 1
	bnez	$t1, 1f

	la.pcrel	$t0, handle_tlbrefill_sw
	csrwr	$t0, LOONGARCH_CSR_TLBRENTRY
	b	2f
1:
	la.pcrel	$t0, handle_tlbrefill_boot
	csrwr	$t0, LOONGARCH_CSR_TLBRENTRY
2:

	la.pcrel	$t0, 1f
	li.d		$t1, CSR_DMW0_BASE
	add.d		$t0, $t0, $t1
	jirl		$zero, $t0, 0
1:
	li.w	$t0, 0xb0
	csrwr	$t0, LOONGARCH_CSR_CRMD

	li.w	$t0, 0x04
	csrwr	$t0, LOONGARCH_CSR_PRMD

	li.w	$t0, 0x00
	csrwr	$t0, LOONGARCH_CSR_EUEN

	csrrd	$t0, LOONGARCH_CSR_ECFG
	li.d	$t1, ~0x70000
	and	$t0, $t0, $t1
	/* Enable IPI (bit12) only; timer (bit11) is enabled later in
	 * init_secondary() after DPCPU and scheduler are fully initialized. */
	li.d	$t1, 0x1000
	or	$t0, $t0, $t1
	csrwr	$t0, LOONGARCH_CSR_ECFG

	la.pcrel	$t4, virt_map
	ld.d		$t2, $t4, 0
	sub.d		$t4, $t2, $t4

	la.pcrel	$t0, cpu_exception_handler
	add.d		$t0, $t0, $t4
	csrwr		$t0, LOONGARCH_CSR_EENTRY

	la.pcrel	$t5, LSvirtdone
	ld.d		$t5, $t5, 0

	invtlb		0, $zero, $zero

	jirl		$zero, $t5, 0

virtdone_mp:
	/*
	 * Convert hart ID ($s0 read from CSR_CPUID) to cpuid.
	 * Same logic as init_secondary():
	 *   cpuid = hart_id;
	 *   if (hart_id < boot_hart) cpuid += mp_maxid + 1;
	 *   cpuid -= boot_hart;
	 */
	move	$s1, $s0			# s1 = hart_id (will become cpuid)

	la.pcrel $t0, boot_hart
	ld.w	$t0, $t0, 0			# t0 = boot_hart (uint32_t)

	la.pcrel $t1, mp_maxid
	ld.w	$t1, $t1, 0			# t1 = mp_maxid (u_int)

	bltu	$s1, $t0, 1f			# if hart_id < boot_hart
	b	2f
1:	addi.w	$t1, $t1, 1			# t1 = mp_maxid + 1
	add.w	$s1, $s1, $t1			# s1 += mp_maxid + 1
2:	sub.w	$s1, $s1, $t0			# s1 = cpuid

	/* Load bootstack for this CPU (indexed by cpuid) */
	la.pcrel $t0, bootstacks
	slli.d	$t1, $s1, 3			# cpuid * 8
	ldx.d	$t2, $t0, $t1
	li.d	$t3, (PAGE_SIZE * KSTACK_PAGES)
	add.d	$t2, $t2, $t3
	move	$sp, $t2

	/* Load bootpcpu for this CPU (indexed by cpuid) and set PCPU pointer */
	la.pcrel $t0, bootpcpu
	slli.d	$t1, $s1, 3			# cpuid * 8
	ldx.d	$t1, $t0, $t1			# bootpcpu[cpuid]
	move	$r21, $t1
	csrwr	$t1, PERCPU_BASE_KS

	/* Call init_secondary with raw hart_id */
	move	$a0, $s0
	move	$ra, $zero			# Terminate backtrace
	b	init_secondary
END(mpentry)

	.align 3
LSvirtdone:
	.quad	virtdone_mp
