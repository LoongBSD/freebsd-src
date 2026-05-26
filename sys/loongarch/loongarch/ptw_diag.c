/*-
 * Diagnostic helpers for PTW (Page Table Walk) debugging
 *
 * This file provides non-intrusive diagnostic functions to collect
 * information about the PTW configuration and page table structure.
 * These functions do NOT modify any behavior - they only read and print state.
 *
 * Usage:
 *   - Enable with options DIAGNOSTIC in kernel config
 *   - Call pmap_ptw_diagnose() from DDB or early init
 *
 * $FreeBSD$
 */

#include "opt_ddb.h"

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>

#include <machine/cpufunc.h>
#include <machine/loongarchreg.h>
#include <machine/pte.h>
#include <machine/cpu.h>
#include <machine/vmparam.h>

#define	_DIAG_L0_INDEX(va)	(((va) >> L0_SHIFT) & Ln_ADDR_MASK)
#define	_DIAG_L1_INDEX(va)	(((va) >> L1_SHIFT) & Ln_ADDR_MASK)
#define	_DIAG_L2_INDEX(va)	(((va) >> L2_SHIFT) & Ln_ADDR_MASK)
#define	_DIAG_L3_INDEX(va)	(((va) >> L3_SHIFT) & Ln_ADDR_MASK)
#define	_DIAG_PDE_LOAD(p)	atomic_load_64(p)

static const char *
pte_plv_str(uint64_t pte)
{
	uint64_t plv = (pte & PTE_PLV_MASK) >> PTE_PLV_SHIFT;
	switch (plv) {
	case 0: return "K(0)";
	case 3: return "U(3)";
	default: return "?(inv)";
	}
}

static const char *
pte_cache_str(uint64_t pte)
{
	uint64_t mat = (pte & PTE_CACHE_MASK) >> PTE_CACHE_SHIFT;
	switch (mat) {
	case 0: return "SUC";
	case 1: return "CC";
	case 2: return "WUC";
	default: return "?(inv)";
	}
}

static void
pmap_print_pte_detail(uint64_t pte, const char *level)
{
	printf("    %s raw=0x%016lx", level, pte);
	if (pte == 0) {
		printf(" [UNMAPPED]\n");
		return;
	}
	if (pte & PTE_HUGE) {
		printf(" HUGE");
	}
	printf(" V=%d D=%d PLV=%s MAT=%s G=%d",
	    (pte & PTE_V) ? 1 : 0,
	    (pte & PTE_D) ? 1 : 0,
	    pte_plv_str(pte),
	    pte_cache_str(pte),
	    (pte & PTE_G) ? 1 : 0);
	printf(" W=%d A=%d MGD=%d WIRED=%d",
	    (pte & PTE_W) ? 1 : 0,
	    (pte & PTE_A) ? 1 : 0,
	    (pte & PTE_SW_MANAGED) ? 1 : 0,
	    (pte & PTE_SW_WIRED) ? 1 : 0);
	printf(" NR=%d NX=%d RPLV=%d HW_P=%d",
	    (pte & PTE_NR) ? 1 : 0,
	    (pte & PTE_NX) ? 1 : 0,
	    (pte & PTE_RPLV) ? 1 : 0,
	    (pte & PTE_HW_P) ? 1 : 0);
	if (pte & PTE_HUGE) {
		printf(" HGLOBAL=%d", (pte & PTE_HUGE_G) ? 1 : 0);
	}
	printf("\n");

	if (pte & PTE_PFN_MASK) {
		vm_paddr_t pa = PTE_TO_PHYS(pte);
		printf("      PFN_PA=0x%lx", pa);
		if (pte & PTE_HUGE) {
			vm_paddr_t huge_pa = L2PTE_TO_PHYS(pte);
			printf(" HUGE_PA=0x%lx", huge_pa);
		}
		printf("\n");
	}
}

static void
pmap_print_pde_detail(uint64_t pde, const char *level)
{
	printf("    %s raw=0x%016lx", level, pde);
	if (pde == 0) {
		printf(" [UNMAPPED]\n");
		return;
	}
	if ((pde & PTE_PFN_MASK) == 0) {
		printf(" [NO_PPN]\n");
		return;
	}
	vm_paddr_t next_pa = PTE_TO_PHYS(pde);
	printf(" -> next_table_PA=0x%lx", next_pa);
	if (pde & 0xFFF) {
		printf(" [WARN: low bits set! low12=0x%03lx]", pde & 0xFFF);
	}
	if (pde & ~0x0000ffffffffffffULL) {
		printf(" [WARN: high bits set! high16=0x%04lx]",
		    (pde >> 48) & 0xFFFF);
	}
	printf("\n");
}

void
pmap_print_pwctl_config(void)
{
	uint64_t pwcl, pwch;
	uint64_t pgd, pgdl, pgdh;
	uint64_t ptw_en;

	printf("=== PTW (Page Table Walk) Configuration ===\n");

	pwcl = csr_read64(LOONGARCH_CSR_PWCTL0);
	pwch = csr_read64(LOONGARCH_CSR_PWCTL1);

	printf("PWCL0: 0x%016lx\n", pwcl);
	printf("  PTBASE:    %2lu  (L3/PTE base bit position)\n",
	    pwcl & 0x1f);
	printf("  PTWIDTH:   %2lu  (L3/PTE index width)\n",
	    (pwcl >> 5) & 0x1f);
	printf("  DIR0BASE:  %2lu  (L2/PMD base bit position)\n",
	    (pwcl >> 10) & 0x1f);
	printf("  DIR0WIDTH: %2lu  (L2/PMD index width)\n",
	    (pwcl >> 15) & 0x1f);
	printf("  DIR1BASE:  %2lu  (L1/PUD base bit position)\n",
	    (pwcl >> 20) & 0x1f);
	printf("  DIR1WIDTH: %2lu  (L1/PUD index width)\n",
	    (pwcl >> 25) & 0x1f);
	printf("  PTEW:      %2lu  (PTE width: 0=64-bit)\n",
	    (pwcl >> 30) & 0x3);

	printf("PWCH1: 0x%016lx\n", pwch);
	printf("  DIR2BASE:  %2lu  (L0/PGD base bit position)\n",
	    pwch & 0x3f);
	printf("  DIR2WIDTH: %2lu  (L0/PGD index width)\n",
	    (pwch >> 6) & 0x3f);
	ptw_en = (pwch >> 24) & 0x1;
	printf("  PTW_EN:    %2lu  (Hardware PTW enable: %s)\n",
	    ptw_en, ptw_en ? "YES" : "NO");

	pgd = csr_read64(LOONGARCH_CSR_PGD);
	pgdl = csr_read64(LOONGARCH_CSR_PGDL);
	pgdh = csr_read64(LOONGARCH_CSR_PGDH);

	printf("\nPGD Registers:\n");
	printf("  PGD:  0x%016lx\n", pgd);
	printf("  PGDL: 0x%016lx (low half - user space)\n", pgdl);
	printf("  PGDH: 0x%016lx (high half - kernel space)\n", pgdh);

	printf("\nExpected Configuration (LA48 4-level):\n");
	printf("  L0(PGD): bits [47:39], 9 bits, 512 entries\n");
	printf("  L1(PUD): bits [38:30], 9 bits, 512 entries\n");
	printf("  L2(PMD): bits [29:21], 9 bits, 512 entries\n");
	printf("  L3(PTE): bits [20:12], 9 bits, 512 entries\n");
	printf("  Page:    bits [11:0],  12 bits, 4KB\n");

	printf("\nConfiguration Validation:\n");
	if ((pwcl & 0x1f) == 12)
		printf("  [OK] PTBASE = 12\n");
	else
		printf("  [WARN] PTBASE = %lu (expected 12)\n", pwcl & 0x1f);

	if (((pwcl >> 5) & 0x1f) == 9)
		printf("  [OK] PTWIDTH = 9\n");
	else
		printf("  [WARN] PTWIDTH = %lu (expected 9)\n", (pwcl >> 5) & 0x1f);

	if (((pwcl >> 10) & 0x1f) == 21)
		printf("  [OK] DIR0BASE = 21\n");
	else
		printf("  [WARN] DIR0BASE = %lu (expected 21)\n", (pwcl >> 10) & 0x1f);

	if (((pwcl >> 15) & 0x1f) == 9)
		printf("  [OK] DIR0WIDTH = 9\n");
	else
		printf("  [WARN] DIR0WIDTH = %lu (expected 9)\n", (pwcl >> 15) & 0x1f);

	if (((pwcl >> 20) & 0x1f) == 30)
		printf("  [OK] DIR1BASE = 30\n");
	else
		printf("  [WARN] DIR1BASE = %lu (expected 30)\n", (pwcl >> 20) & 0x1f);

	if (((pwcl >> 25) & 0x1f) == 9)
		printf("  [OK] DIR1WIDTH = 9\n");
	else
		printf("  [WARN] DIR1WIDTH = %lu (expected 9)\n", (pwcl >> 25) & 0x1f);

	if ((pwch & 0x3f) == 39)
		printf("  [OK] DIR2BASE = 39\n");
	else
		printf("  [WARN] DIR2BASE = %lu (expected 39)\n", pwch & 0x3f);

	if (((pwch >> 6) & 0x3f) == 9)
		printf("  [OK] DIR2WIDTH = 9\n");
	else
		printf("  [WARN] DIR2WIDTH = %lu (expected 9)\n", (pwch >> 6) & 0x3f);
}

void
pmap_walk_pt_va(vm_offset_t va)
{
	pd_entry_t *l0p, l0e;
	pd_entry_t *l1p, l1e;
	pd_entry_t *l2p, l2e;
	pt_entry_t *l3p, l3e;
	vm_paddr_t pa;
	u_int l0_idx, l1_idx, l2_idx, l3_idx;

	l0_idx = _DIAG_L0_INDEX(va);
	l1_idx = _DIAG_L1_INDEX(va);
	l2_idx = _DIAG_L2_INDEX(va);
	l3_idx = _DIAG_L3_INDEX(va);

	printf("\n=== Page Table Walk: VA 0x%lx ===\n", va);
	printf("  Indices: L0[%u] L1[%u] L2[%u] L3[%u]\n",
	    l0_idx, l1_idx, l2_idx, l3_idx);
	printf("  VA bits: [47:39]=%u [38:30]=%u [29:21]=%u [20:12]=%u\n",
	    l0_idx, l1_idx, l2_idx, l3_idx);

	if (va >= DMAP_MIN_ADDRESS && va < DMAP_MAX_ADDRESS) {
		pa = DMAP_TO_PHYS(va);
		printf("  [DMAP] Direct map: VA 0x%lx -> PA 0x%lx\n", va, pa);
		printf("  DMAP range: 0x%lx-0x%lx phys: 0x%lx-0x%lx\n",
		    DMAP_MIN_ADDRESS, dmap_max_addr,
		    dmap_phys_base, dmap_phys_max);
		return;
	}

	l0p = &kernel_pmap->pm_top[l0_idx];
	if (l0p == NULL) {
		printf("  L0: pm_top is NULL!\n");
		return;
	}
	l0e = _DIAG_PDE_LOAD(l0p);
	printf("  L0: ptr=%p entry=0x%016lx\n", l0p, l0e);
	pmap_print_pde_detail(l0e, "L0(PGD)");

	if (l0e == 0) {
		printf("  [FAIL] L0 entry is UNMAPPED\n");
		return;
	}

	if ((l0e & PTE_PFN_MASK) == 0) {
		printf("  [FAIL] L0 entry has no valid PPN\n");
		return;
	}

	pa = PTE_TO_PHYS(l0e);
	l1p = (pd_entry_t *)PHYS_TO_DMAP(pa);
	l1p = &l1p[l1_idx];
	l1e = _DIAG_PDE_LOAD(l1p);
	printf("  L1: ptr=%p entry=0x%016lx (from PA 0x%lx + idx*8)\n",
	    l1p, l1e, pa);
	pmap_print_pde_detail(l1e, "L1(PUD)");

	if (l1e == 0) {
		printf("  [FAIL] L1 entry is UNMAPPED\n");
		return;
	}

	if (l1e & PTE_HUGE) {
		printf("  [HUGE] L1 is a 1GB huge page\n");
		pmap_print_pte_detail(l1e, "L1(huge)");
		pa = L2PTE_TO_PHYS(l1e) | (va & L1_OFFSET);
		printf("  RESULT: VA 0x%lx -> PA 0x%lx (1GB page)\n", va, pa);
		return;
	}

	if ((l1e & PTE_PFN_MASK) == 0) {
		printf("  [FAIL] L1 entry has no valid PPN\n");
		return;
	}

	pa = PTE_TO_PHYS(l1e);
	l2p = (pd_entry_t *)PHYS_TO_DMAP(pa);
	l2p = &l2p[l2_idx];
	l2e = _DIAG_PDE_LOAD(l2p);
	printf("  L2: ptr=%p entry=0x%016lx (from PA 0x%lx + idx*8)\n",
	    l2p, l2e, pa);
	pmap_print_pde_detail(l2e, "L2(PMD)");

	if (l2e == 0) {
		printf("  [FAIL] L2 entry is UNMAPPED\n");
		return;
	}

	if (l2e & PTE_HUGE) {
		printf("  [HUGE] L2 is a 2MB huge page\n");
		pmap_print_pte_detail(l2e, "L2(huge)");
		pa = L2PTE_TO_PHYS(l2e) | (va & L2_OFFSET);
		printf("  RESULT: VA 0x%lx -> PA 0x%lx (2MB page)\n", va, pa);
		return;
	}

	if ((l2e & PTE_PFN_MASK) == 0) {
		printf("  [FAIL] L2 entry has no valid PPN\n");
		return;
	}

	pa = PTE_TO_PHYS(l2e);
	l3p = (pt_entry_t *)PHYS_TO_DMAP(pa);
	l3p = &l3p[l3_idx];
	l3e = _DIAG_PDE_LOAD(l3p);
	printf("  L3: ptr=%p entry=0x%016lx (from PA 0x%lx + idx*8)\n",
	    l3p, l3e, pa);
	pmap_print_pte_detail(l3e, "L3(PTE)");

	if (l3e == 0) {
		printf("  [FAIL] L3 entry is UNMAPPED\n");
		return;
	}

	pa = PTE_TO_PHYS(l3e) | (va & L3_OFFSET);
	printf("  RESULT: VA 0x%lx -> PA 0x%lx (4KB page)\n", va, pa);

	if ((l3e & PTE_V) == 0)
		printf("  [WARN] PTE_V not set - TLB refill will fail!\n");
	if ((l3e & PTE_HW_P) == 0)
		printf("  [WARN] PTE_HW_P not set - HW PTW will skip this entry\n");
}

void
pmap_ptw_diagnose_kernel(void)
{
	vm_offset_t test_va;
	vm_paddr_t test_pa;

	printf("\n=== Kernel Page Table Diagnosis ===\n");

	pmap_print_pwctl_config();

	printf("\n--- Walking Key Kernel Virtual Addresses ---\n");

	pmap_walk_pt_va(KERNBASE);
	pmap_walk_pt_va(KERNBASE + 0x100000);
	pmap_walk_pt_va(DMAP_MIN_ADDRESS);
	pmap_walk_pt_va(VM_MAX_KERNEL_ADDRESS - PMAP_MAPDEV_EARLY_SIZE);

	printf("\n--- Verifying DMAP Consistency ---\n");
	test_va = DMAP_MIN_ADDRESS;
	test_pa = pmap_kextract(test_va);
	printf("DMAP_MIN_ADDRESS: VA 0x%lx -> PA 0x%lx (expect PA=0x%lx)\n",
	    test_va, test_pa, dmap_phys_base);
	if (test_pa == dmap_phys_base)
		printf("  [OK] DMAP base matches\n");
	else
		printf("  [FAIL] DMAP base mismatch! expected 0x%lx got 0x%lx\n",
		    dmap_phys_base, test_pa);

	printf("\n--- Verifying pmap_kextract for kernel addresses ---\n");
	vm_offset_t addrs[] = {
		KERNBASE,
		KERNBASE + PAGE_SIZE,
		KERNBASE + L2_SIZE,
	};
	int i;

	for (i = 0; i < nitems(addrs); i++) {
		test_va = addrs[i];
		test_pa = pmap_kextract(test_va);
		printf("kextract: VA 0x%lx -> PA 0x%lx %s\n",
		    test_va, test_pa,
		    test_pa != 0 ? "[OK]" : "[FAILED]");
	}
}

void
pmap_diagnose_l0_table(void)
{
	pd_entry_t *l0;
	int i;
	int valid_count = 0;

	printf("\n=== L0 (PGD) Table Diagnostic ===\n");
	printf("pm_top=%p (kernel_pmap)\n", kernel_pmap->pm_top);

	l0 = kernel_pmap->pm_top;
	for (i = 0; i < Ln_ENTRIES; i++) {
		if (l0[i] != 0) {
			vm_paddr_t next_pa = PTE_TO_PHYS(l0[i]);
			printf("  L0[%d]: 0x%016lx -> PA 0x%lx", i, l0[i], next_pa);
			if (l0[i] & 0xFFF)
				printf(" [WARN: low12=0x%03lx]", l0[i] & 0xFFF);
			if (l0[i] & ~0x0000ffffffffffffULL)
				printf(" [WARN: high16=0x%04lx]",
				    (l0[i] >> 48) & 0xFFFF);
			printf("\n");
			valid_count++;
		}
	}
	printf("  Total valid L0 entries: %d / %d\n", valid_count, Ln_ENTRIES);

	printf("\n  Expected L0 entries for kernel:\n");
	printf("    L0[%lu]: KERNBASE (0x%lx)\n",
	    _DIAG_L0_INDEX(KERNBASE), KERNBASE);
	printf("    L0[%lu]: DMAP (0x%lx)\n",
	    _DIAG_L0_INDEX(DMAP_MIN_ADDRESS), DMAP_MIN_ADDRESS);
}

void
pmap_diagnose_l1_table(vm_offset_t va_base)
{
	pd_entry_t *l0, *l1;
	pd_entry_t l0e;
	int i;
	int valid_count = 0;

	u_int l0_idx = _DIAG_L0_INDEX(va_base);

	printf("\n=== L1 (PUD) Table Diagnostic for VA base 0x%lx ===\n", va_base);

	l0 = &kernel_pmap->pm_top[l0_idx];
	l0e = _DIAG_PDE_LOAD(l0);
	printf("L0[%u]: 0x%016lx\n", l0_idx, l0e);

	if (l0e == 0) {
		printf("  [FAIL] L0 entry is unmapped\n");
		return;
	}

	vm_paddr_t l1_pa = PTE_TO_PHYS(l0e);
	l1 = (pd_entry_t *)PHYS_TO_DMAP(l1_pa);
	printf("L1 table at PA 0x%lx (VA %p):\n", l1_pa, l1);

	for (i = 0; i < Ln_ENTRIES; i++) {
		if (l1[i] != 0) {
			printf("  L1[%d]: 0x%016lx", i, l1[i]);
			if (l1[i] & PTE_HUGE) {
				vm_paddr_t huge_pa = L2PTE_TO_PHYS(l1[i]);
				printf(" [1GB HUGE] PA=0x%lx", huge_pa);
			} else {
				vm_paddr_t next_pa = PTE_TO_PHYS(l1[i]);
				printf(" -> PA 0x%lx", next_pa);
			}
			if (l1[i] & 0xFFF)
				printf(" [WARN: low12=0x%03lx]", l1[i] & 0xFFF);
			printf("\n");
			valid_count++;
		}
	}
	printf("  Total valid L1 entries: %d / %d\n", valid_count, Ln_ENTRIES);
}

void
pmap_diagnose_l2_table(vm_offset_t va_base)
{
	pd_entry_t *l0, *l1, *l2;
	pd_entry_t l0e, l1e;
	int i;
	int valid_count = 0;
	int huge_count = 0;

	printf("\n=== L2 (PMD) Table Diagnostic for VA base 0x%lx ===\n", va_base);

	l0 = &kernel_pmap->pm_top[_DIAG_L0_INDEX(va_base)];
	l0e = _DIAG_PDE_LOAD(l0);
	if (l0e == 0) {
		printf("  [FAIL] L0 entry is unmapped\n");
		return;
	}
	l1 = (pd_entry_t *)PHYS_TO_DMAP(PTE_TO_PHYS(l0e));
	l1 = &l1[_DIAG_L1_INDEX(va_base)];
	if (l1 == NULL) {
		printf("  [FAIL] L1 is NULL\n");
		return;
	}
	l1e = _DIAG_PDE_LOAD(l1);
	printf("L1 entry: 0x%016lx\n", l1e);

	if (l1e == 0) {
		printf("  [FAIL] L1 entry is unmapped\n");
		return;
	}

	if (l1e & PTE_HUGE) {
		printf("  L1 is a 1GB huge page, no L2 table\n");
		return;
	}

	vm_paddr_t l2_pa = PTE_TO_PHYS(l1e);
	l2 = (pd_entry_t *)PHYS_TO_DMAP(l2_pa);
	printf("L2 table at PA 0x%lx (VA %p):\n", l2_pa, l2);

	for (i = 0; i < Ln_ENTRIES; i++) {
		if (l2[i] != 0) {
			if (l2[i] & PTE_HUGE) {
				vm_paddr_t huge_pa = L2PTE_TO_PHYS(l2[i]);
				printf("  L2[%d]: 0x%016lx [2MB HUGE] PA=0x%lx\n",
				    i, l2[i], huge_pa);
				huge_count++;
			} else {
				vm_paddr_t next_pa = PTE_TO_PHYS(l2[i]);
				printf("  L2[%d]: 0x%016lx -> PA 0x%lx", i, l2[i], next_pa);
				if (l2[i] & 0xFFF)
					printf(" [WARN: low12=0x%03lx]", l2[i] & 0xFFF);
				printf("\n");
			}
			valid_count++;
		}
	}
	printf("  Total valid L2 entries: %d / %d (huge: %d, table: %d)\n",
	    valid_count, Ln_ENTRIES, huge_count, valid_count - huge_count);
}

void
pmap_full_diagnose(void)
{
	printf("\n");
	printf("============================================================\n");
	printf("  PMAP FULL DIAGNOSTIC - 4-Level Page Table Verification\n");
	printf("============================================================\n");

	pmap_print_pwctl_config();

	pmap_diagnose_l0_table();

	pmap_diagnose_l1_table(KERNBASE);
	pmap_diagnose_l1_table(DMAP_MIN_ADDRESS);

	pmap_diagnose_l2_table(KERNBASE);
	pmap_diagnose_l2_table(KERNBASE + L1_SIZE);

	printf("\n--- Walking Critical Kernel Addresses ---\n");
	pmap_walk_pt_va(KERNBASE);
	pmap_walk_pt_va(KERNBASE + 0x200000);
	pmap_walk_pt_va(DMAP_MIN_ADDRESS);
	pmap_walk_pt_va(DMAP_MIN_ADDRESS + 0x200000);

	printf("\n--- DMAP Range Verification ---\n");
	printf("DMAP: VA 0x%lx-0x%lx -> PA 0x%lx-0x%lx\n",
	    DMAP_MIN_ADDRESS, dmap_max_addr,
	    dmap_phys_base, dmap_phys_max);
	printf("PHYS_IN_DMAP(0) = %d\n", PHYS_IN_DMAP(0));
	printf("VIRT_IN_DMAP(DMAP_MIN) = %d\n", VIRT_IN_DMAP(DMAP_MIN_ADDRESS));

	vm_paddr_t test_pa;
	vm_offset_t test_va;
	test_va = DMAP_MIN_ADDRESS;
	test_pa = pmap_kextract(test_va);
	printf("kextract(DMAP_MIN) = PA 0x%lx (expect 0x%lx) %s\n",
	    test_pa, dmap_phys_base,
	    test_pa == dmap_phys_base ? "[OK]" : "[MISMATCH]");

	printf("\n--- PDE Format Consistency Check ---\n");
	printf("Checking that all PDE entries have clean format (no low bits)...\n");
	int pde_errors = 0;

	pd_entry_t *l0 = kernel_pmap->pm_top;
	int i;
	for (i = 0; i < Ln_ENTRIES; i++) {
		if (l0[i] != 0 && (l0[i] & 0xFFF)) {
			printf("  [ERROR] L0[%d] has low bits set: 0x%016lx (low12=0x%03lx)\n",
			    i, l0[i], l0[i] & 0xFFF);
			pde_errors++;
		}
	}

	if (pde_errors == 0)
		printf("  [OK] All L0 PDE entries have clean format\n");
	else
		printf("  [FAIL] %d L0 PDE entries have corrupted format\n", pde_errors);

	printf("\n============================================================\n");
	printf("  END PMAP FULL DIAGNOSTIC\n");
	printf("============================================================\n");
}
