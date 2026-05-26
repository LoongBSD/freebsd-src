/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
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
#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/sysent.h>
#include <sys/sdt.h>
#ifdef KDB
#include <sys/kdb.h>
#endif

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_param.h>
#include <vm/vm_extern.h>

#include <machine/fpe.h>
#include <machine/fpu.h>
#include <machine/frame.h>
#include <machine/pcb.h>
#include <machine/pcpu.h>

#include <machine/resource.h>
#include <machine/intr.h>
#include <machine/tlb.h>

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>
#endif

#ifdef DDB
#include <ddb/ddb.h>
#include <ddb/db_sym.h>
#endif

/* TLBM fast path diagnostics (defined in exception.S) */
struct tlbm_fast_diag {
	uint64_t hit;		/* successful ertn from fast path */
	uint64_t fail;		/* routed to tlbm_fail */
	uint64_t last_reason;	/* 1=L0,2=L1,3=L2 zero, 5=!SW_WIRED, 6=!V */
	uint64_t last_badv;
	uint64_t last_pgdl;
	uint64_t last_pte;
	uint64_t user_entry_count;  /* total times cpu_exception_handler_user entered */
	uint64_t tlb_cnt;	    /* TLBM matched at entry check */
	uint64_t saved_l2_dmw;	    /* L2 DMW VA for huge page writeback */
};
extern struct tlbm_fast_diag tlbm_fast_diag;

/*
 * Early boot printf support for debugging
 * Enable with options BOOTPRINT in kernel config
 */
#ifdef DEBUG
#define	EARLY_DEBUG_PRINTF(fmt, ...)	printf(fmt, ##__VA_ARGS__)
#else
#define	EARLY_DEBUG_PRINTF(fmt, ...)
#endif


#define EXC_CODE(estat) ((estat & CSR_ESTAT_EXC) >> CSR_ESTAT_EXC_SHIFT)

int (*dtrace_invop_jump_addr)(struct trapframe *);

/* External variables from pmap.c for debugging */
extern vm_paddr_t dmap_phys_base;
extern vm_paddr_t dmap_phys_max;
extern vm_offset_t dmap_max_addr;

/*
 * Trap debugging support
 */
#define	TRAP_DEBUG(fmt, ...)

/* Called from exception.S */
void do_trap_supervisor(struct trapframe *);
void do_trap_user(struct trapframe *);

static __inline void
call_trapsignal(struct thread *td, int sig, int code, void *addr, int trapno)
{
	ksiginfo_t ksi;

	ksiginfo_init_trap(&ksi);
	ksi.ksi_signo = sig;
	ksi.ksi_code = code;
	ksi.ksi_addr = addr;
	ksi.ksi_trapno = trapno;
	trapsignal(td, &ksi);
}

int
cpu_fetch_syscall_args(struct thread *td)
{
	struct proc *p;
	syscallarg_t *ap, *dst_ap;
	struct syscall_args *sa;

	p = td->td_proc;
	sa = &td->td_sa;
	ap = &td->td_frame->tf_a[0];
	dst_ap = &sa->args[0];

	sa->code = td->td_frame->tf_a[7];
	sa->original_code = sa->code;

	if (__predict_false(sa->code == SYS_syscall || sa->code == SYS___syscall)) {
		sa->code = *ap++;
	} else {
		*dst_ap++ = *ap++;
	}

	if (__predict_false(sa->code >= p->p_sysent->sv_size))
		sa->callp = &nosys_sysent;
	else
		sa->callp = &p->p_sysent->sv_table[sa->code];

	KASSERT(sa->callp->sy_narg <= nitems(sa->args),
	    ("Syscall %d takes too many arguments", sa->code));

	memcpy(dst_ap, ap, (NARGREG - 1) * sizeof(*dst_ap));

	td->td_retval[0] = 0;
	td->td_retval[1] = 0;

	return (0);
}

#include "../../kern/subr_syscall.c"

static void
print_with_symbol(const char *name, uint64_t value)
{
#ifdef DDB
	c_db_sym_t sym;
	db_expr_t sym_value;
	db_expr_t offset;
	const char *sym_name;
#endif

	printf("%7s: 0x%016lx", name, value);

#ifdef DDB
	if (value >= VM_MIN_KERNEL_ADDRESS) {
		sym = db_search_symbol(value, DB_STGY_ANY, &offset);
		if (sym != C_DB_SYM_NULL) {
			db_symbol_values(sym, &sym_name, &sym_value);
			printf(" (%s + 0x%lx)", sym_name, offset);
		}
	}
#endif
	printf("\n");
}

static void
dump_regs(struct trapframe *frame)
{
	char name[6];
	int i;

	for (i = 0; i < nitems(frame->tf_t); i++) {
		snprintf(name, sizeof(name), "t[%d]", i);
		print_with_symbol(name, frame->tf_t[i]);
	}

	for (i = 0; i < nitems(frame->tf_s); i++) {
		snprintf(name, sizeof(name), "s[%d]", i);
		print_with_symbol(name, frame->tf_s[i]);
	}

	for (i = 0; i < nitems(frame->tf_a); i++) {
		snprintf(name, sizeof(name), "a[%d]", i);
		print_with_symbol(name, frame->tf_a[i]);
	}

	print_with_symbol("ra", frame->tf_ra);
	print_with_symbol("sp", frame->tf_sp);
	print_with_symbol("tp", frame->tf_tp);
	print_with_symbol("fp", frame->tf_fp);
	printf("era: 0x%016lx\n", frame->tf_era);
	printf("crmd: 0x%016lx\n", frame->tf_crmd);
	printf("prmd: 0x%016lx\n", frame->tf_prmd);
	printf("ecfg: 0x%016lx\n", frame->tf_ecfg);
	printf("estat: 0x%016lx\n", frame->tf_estat);
}

static volatile int ecall_hit_count = 0;
static volatile long ecall_last_sysnum = -1;

static void
ecall_handler(void)
{
	struct thread *td;
	struct trapframe *frame;
	long sysnum;

	td = curthread;
	frame = td->td_frame;
	sysnum = frame->tf_regs[11];

	ecall_hit_count++;
	ecall_last_sysnum = sysnum;

	if (td->td_proc != NULL && td->td_proc->p_pid >= 16) {
#if 0
		printf("ECALL: pid=%d syscall=%ld\n",
		    td->td_proc->p_pid, sysnum);
#endif
	}

	/*
	 * Capture writes to fd 1/2 for all processes and print through
	 * kernel printf.  This works around a LoongArch UART interrupt
	 * routing bug where TTY interrupt-driven output never drains,
	 * causing user processes to sleep forever on "ttyout".
	 * XXX: Remove when UART interrupt routing is fixed upstream.
	 */
	if (td->td_proc != NULL && sysnum == 4) {	/* SYS_write */
		int fd = (int)frame->tf_regs[4];
		size_t len = (size_t)frame->tf_regs[6];

		if ((fd == 1 || fd == 2) && len > 0 && len <= 4096) {
			static char tmp[4097];
			if (copyin((void *)frame->tf_regs[5], tmp, len) == 0) {
				tmp[len] = '\0';
				printf("pid%d: %s", td->td_proc->p_pid, tmp);
			}
			/* Fake success only for stdio writes */
			frame->tf_regs[4] = (long)len;
			frame->tf_regs[12] = 0;
			return;
		}
	}

	syscallenter(td);
	syscallret(td);

	/* Diagnostic: print spinlock state after syscallret */
#if 0
	printf("DEV-DEBUG: ecall_handler POST: pid=%d comm=%s "
	    "sysnum=%ld retval0=%#lx spinlock_cnt=%d era=%#lx\n",
	    td->td_proc->p_pid, td->td_proc->p_comm,
	    sysnum, (unsigned long)td->td_retval[0],
	    td->td_md.md_spinlock_count,
	    (unsigned long)frame->tf_era);
#endif
}

static void
page_fault_handler(struct trapframe *frame, int usermode)
{
	struct vm_map *map;
	uint64_t evaddr;
	struct thread *td;
	struct pcb *pcb;
	vm_prot_t ftype;
	vm_offset_t va;
	struct proc *p;
	int error = KERN_SUCCESS, sig = 0, ucode = 0;
#ifdef KDB
	bool handled;
#endif

	evaddr = frame->tf_badvaddr;

#ifdef KDB
	if (kdb_active) {
		kdb_reenter();
		return;
	}
#endif

	td = curthread;
	p = td->td_proc;
	pcb = td->td_pcb;

	if (td->td_critnest != 0 || td->td_intr_nesting_level != 0 ||
	    WITNESS_CHECK(WARN_SLEEPOK | WARN_GIANTOK, NULL,
	    "Kernel page fault") != 0) {
		/*
		 * For supervisor TLBM, try an inline fix using DMW1
		 * page table walk (bypasses TLB, cannot trigger nested
		 * faults).  This breaks the recursive page_fault_handler
		 * → panic loop that would otherwise deadlock the system.
		 *
		 * Covers both kernel (PGDH) and user (PGDL) addresses;
		 * the kernel may access user pages while holding a
		 * critical section (e.g. copyin/copyout paths).
		 */
		if (!usermode && EXC_CODE(frame->tf_estat) == EXCCODE_TLBM) {
			uint64_t pgdh, l0e, l1e, l2e, val;
			volatile uint64_t *pte;
			vm_offset_t dmw_off;
			vm_offset_t dmw = 0x9000UL << 48;

			if (evaddr >= VM_MIN_KERNEL_ADDRESS)
				pgdh = csr_read64(LOONGARCH_CSR_PGDH);
			else
				pgdh = csr_read64(LOONGARCH_CSR_PGDL);

			/* L0 walk via DMW1 */
			dmw_off = pgdh +
			    (((evaddr >> L0_SHIFT) & Ln_ADDR_MASK) * 8);
			l0e = *(uint64_t *)(dmw + dmw_off);
			if ((l0e & PTE_V) == 0)
				goto pfdiag;

			/* L1 walk via DMW1 */
			dmw_off = (l0e & PTE_PFN_MASK) +
			    (((evaddr >> L1_SHIFT) & Ln_ADDR_MASK) * 8);
			l1e = *(uint64_t *)(dmw + dmw_off);
			if ((l1e & PTE_V) == 0)
				goto pfdiag;

			if (l1e & PTE_HUGE) {
				pte = (volatile uint64_t *)(dmw + dmw_off);
				val = l1e;
				goto fixup;
			}

			/* L2 walk via DMW1 */
			dmw_off = (l1e & PTE_PFN_MASK) +
			    (((evaddr >> L2_SHIFT) & Ln_ADDR_MASK) * 8);
			l2e = *(uint64_t *)(dmw + dmw_off);
			if ((l2e & PTE_V) == 0)
				goto pfdiag;

			if (l2e & PTE_HUGE) {
				pte = (volatile uint64_t *)(dmw + dmw_off);
				val = l2e;
				goto fixup;
			}

			/* L3 walk via DMW1 */
			dmw_off = (l2e & PTE_PFN_MASK) +
			    (((evaddr >> L3_SHIFT) & Ln_ADDR_MASK) * 8);
			pte = (volatile uint64_t *)(dmw + dmw_off);
			val = *pte;
			if ((val & PTE_V) == 0)
				goto pfdiag;

		fixup:
		/*
		 * TLBM means hardware PTW already verified the
		 * page is valid; need to set PTE_D and invalidate
		 * the stale TLB entry.
		 *
		 * For user addresses (accessed via copyin/copyout),
		 * use PTE_SW_WIRED (bit 8) — the real write-permission
		 * flag — instead of PTE_W (bit 7) which is always set
		 * for QEMU hardware PTW compatibility.
		 * Kernel pages always have PTE_SW_WIRED set, so PTE_W
		 * is equivalent for kernel addresses.
		 */
		if ((evaddr < VM_MIN_KERNEL_ADDRESS &&
		     (val & PTE_SW_WIRED) != 0) ||
		    (evaddr >= VM_MIN_KERNEL_ADDRESS &&
		     (val & PTE_W) != 0)) {
			*pte = val | PTE_D;
			__asm __volatile("dbar 0" ::: "memory");
			if (evaddr >= VM_MIN_KERNEL_ADDRESS)
				invtlb_addr(
				    INVTLB_ADDR_GTRUE_OR_ASID,
				    evaddr);
			else
				invtlb(
				    INVTLB_ADDR_GFALSE_AND_ASID,
				    csr_read64(LOONGARCH_CSR_ASID)
				    & CSR_ASID_ASID, evaddr);
			return;
		}
	}

	pfdiag:
		/*
		 * Diagnostic: dump page table state for the faulting address
		 * before panicking.  This helps diagnose whether the page
		 * table entry is missing, corrupted, or has wrong permissions.
		 */
		{
			uint64_t dbg_pgd, dbg_l0e, dbg_l1e, dbg_l2e, dbg_l3e;
			vm_paddr_t dbg_l0pa, dbg_l1pa, dbg_l2pa, dbg_l3pa;
			vm_offset_t dbg_dmw = 0x9000UL << 48;

			dbg_pgd = csr_read64(LOONGARCH_CSR_PGDL);
			if (evaddr >= VM_MIN_KERNEL_ADDRESS)
				dbg_pgd = csr_read64(LOONGARCH_CSR_PGDH);
			dbg_l0pa = dbg_pgd +
			    (((evaddr >> L0_SHIFT) & Ln_ADDR_MASK) * 8);
			dbg_l0e = *(uint64_t *)(dbg_dmw + dbg_l0pa);
			printf("PF_DIAG: va=0x%lx estat=0x%lx era=0x%lx\n",
			    evaddr, (unsigned long)frame->tf_estat,
			    (unsigned long)frame->tf_era);
			printf("PF_DIAG: PGDH=0x%lx L0[0x%lx]=0x%lx\n",
			    dbg_pgd,
			    (evaddr >> L0_SHIFT) & Ln_ADDR_MASK, dbg_l0e);
			if (dbg_l0e & PTE_PFN_MASK) {
				dbg_l1pa = (dbg_l0e & PTE_PFN_MASK) +
				    (((evaddr >> L1_SHIFT) & Ln_ADDR_MASK) * 8);
				dbg_l1e = *(uint64_t *)(dbg_dmw + dbg_l1pa);
				printf("PF_DIAG: L1[0x%lx]=0x%lx"
				    " HUGE=%d\n",
				    (evaddr >> L1_SHIFT) & Ln_ADDR_MASK,
				    dbg_l1e,
				    (dbg_l1e & PTE_HUGE) ? 1 : 0);
				/* Stop at L1 if it's a huge page */
				if ((dbg_l1e & PTE_PFN_MASK) &&
				    !(dbg_l1e & PTE_HUGE)) {
					dbg_l2pa = (dbg_l1e & PTE_PFN_MASK) +
					    (((evaddr >> L2_SHIFT) & Ln_ADDR_MASK) * 8);
					dbg_l2e = *(uint64_t *)(dbg_dmw + dbg_l2pa);
					printf("PF_DIAG: L2[0x%lx]=0x%lx "
					    "HUGE=%d\n",
					    (evaddr >> L2_SHIFT) & Ln_ADDR_MASK,
					    dbg_l2e,
					    (dbg_l2e & PTE_HUGE) ? 1 : 0);
					if ((dbg_l2e & PTE_PFN_MASK) &&
					    !(dbg_l2e & PTE_HUGE)) {
						dbg_l3pa = (dbg_l2e & PTE_PFN_MASK) +
						    (((evaddr >> L3_SHIFT) & Ln_ADDR_MASK) * 8);
						dbg_l3e = *(uint64_t *)(dbg_dmw + dbg_l3pa);
						printf("PF_DIAG: L3[0x%lx]=0x%lx "
						    "V=%d W=%d\n",
						    (evaddr >> L3_SHIFT) & Ln_ADDR_MASK,
						    dbg_l3e,
						    (dbg_l3e & PTE_V) ? 1 : 0,
						    (dbg_l3e & PTE_W) ? 1 : 0);
					}
				}
			}
		}
		goto fatal;
	}

	if (usermode) {
		if (!VIRT_IS_VALID(evaddr)) {
			call_trapsignal(td, SIGSEGV, SEGV_MAPERR, (void *)evaddr,
			    EXC_CODE(frame->tf_estat));
			goto done;
		}
		map = &p->p_vmspace->vm_map;
	} else {
		/*
		 * Enable interrupts for the duration of the page fault,
		 * but only if they were already enabled before the exception.
		 * This mirrors the behavior of arm64 (PSR_DAIF_INTR check)
		 * and RISC-V (SSTATUS_SPIE check).  Unconditionally enabling
		 * interrupts here can cause nested page faults if an
		 * interrupt handler triggers a copyin/copyout or kernel
		 * data access that faults while the first fault is still
		 * being resolved.
		 */
		if ((frame->tf_prmd & CSR_PRMD_PIE) != 0)
			intr_enable();

		if (evaddr >= VM_MIN_KERNEL_ADDRESS) {
			map = kernel_map;
		} else {
			if (pcb->pcb_onfault == 0)
				goto fatal;
			map = &p->p_vmspace->vm_map;
		}
	}

	va = trunc_page(evaddr);

	if (EXC_CODE(frame->tf_estat) == EXCCODE_TLBS) {
		ftype = VM_PROT_WRITE;
	} else if (EXC_CODE(frame->tf_estat) == EXCCODE_TLBNX) {
		ftype = VM_PROT_EXECUTE;
	} else if (EXC_CODE(frame->tf_estat) == EXCCODE_TLBPE) {
		ftype = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
	} else if (EXC_CODE(frame->tf_estat) == EXCCODE_TLBM) {
		ftype = VM_PROT_WRITE;
	} else if (EXC_CODE(frame->tf_estat) == EXCCODE_TLBI) {
		ftype = VM_PROT_EXECUTE;
	} else {
		ftype = VM_PROT_READ;
	}

	/*
	 * Try to resolve the fault in pmap first. For user addresses,
	 * check validity. For kernel addresses, always try pmap_fault.
	 *
	 * pmap_fault() already calls pmap_invalidate_page() on success,
	 * so we can skip straight to done — the hardware PTW (or the
	 * fast assembly TLBR handler) will refill the TLB on retry.
	 * This eliminates the heavy C-language 4-level manual walk
	 * previously at the tlb_fill label.
	 */
	if (evaddr >= VM_MIN_KERNEL_ADDRESS) {
		if (pmap_fault(map->pmap, va, ftype))
			goto done;
	} else if (VIRT_IS_VALID(va) && pmap_fault(map->pmap, va, ftype)) {
		goto done;
	}

	/*
	 * For supervisor-mode faults on user addresses (copyin/copyout),
	 * TDP_NOFAULTING may be set by e.g. vn_io_fault_doio, preventing
	 * vm_fault from resolving COW breaks or page-in.  Temporarily
	 * allow faults so the page can be made writable — this mirrors
	 * the intent of vn_io_fault_prefault_user() but catches every
	 * page in the I/O range, not just the first and last.
	 *
	 * TDP_RESETSPUR is cleared alongside TDP_NOFAULTING because
	 * vm_fault_disable_pagefaults() sets both atomically.
	 */
	if (!usermode) {
		int pflags_save = curthread->td_pflags;
		curthread_pflags_restore(pflags_save &
		    ~(TDP_NOFAULTING | TDP_RESETSPUR));
		error = vm_fault_trap(map, va, ftype,
		    VM_FAULT_NORMAL, &sig, &ucode);
		curthread_pflags_restore(pflags_save);
	} else {
		error = vm_fault_trap(map, va, ftype,
		    VM_FAULT_NORMAL, &sig, &ucode);
	}
	if (error == KERN_SUCCESS) {
		__asm __volatile("dbar 0" ::: "memory");
		/*
		 * Page is now mapped with correct permissions in the page
		 * table and PTE_V=1.  Invalidate any stale cached TLB entry
		 * and let HW PTW (or the assembly TLBR handler) refill on
		 * the retry, avoiding the ~500-line manual C-language walk.
		 */
		if (va >= VM_MIN_KERNEL_ADDRESS)
			invtlb_addr(INVTLB_ADDR_GTRUE_OR_ASID, va);
		else
			invtlb(INVTLB_ADDR_GFALSE_AND_ASID,
			    csr_read64(LOONGARCH_CSR_ASID) & CSR_ASID_ASID, va);
		goto done;
	}

	/*
	 * vm_fault_trap failed: sig/ucode are already set for signal delivery.
	 * Fall through to tlb_done which will deliver the signal.
	 */
	printf("PFAULT FAIL: pid=%d comm=%s va=0x%lx ftype=%d error=%d "
	    "sig=%d ucode=%d\n",
	    td->td_proc->p_pid, td->td_proc->p_comm,
	    va, ftype, error, sig, ucode);
	/*
	 * Dump vm_map entries immediately after PFAULT FAIL, before
	 * any signal delivery or register dump that could trigger
	 * nested exceptions. Includes VM_PROT_READ (TLBL load faults)
	 * to catch RTLD linked-list traversal crashes (ld.d r4,r4,4
	 * with r4=0). Removed usermode check to also capture
	 * supervisor-mode copyin/copyout write faults.
	 */
	if (ftype == VM_PROT_READ || ftype == VM_PROT_WRITE ||
	    ftype == VM_PROT_EXECUTE) {
		vm_map_t map;
		vm_map_entry_t entry;
		vm_offset_t vstart, vend;
		vm_offset_t fva;

		fva = (ftype == VM_PROT_EXECUTE) ?
		    frame->tf_era : frame->tf_badvaddr;

		if (td->td_proc == NULL ||
		    td->td_proc->p_vmspace == NULL) {
			printf("  VM-MAP: skipped (no vmspace)\n");
			goto skip_vm_map;
		}

		map = &td->td_proc->p_vmspace->vm_map;
		printf("  VM-MAP: fva=0x%lx type=%s usermode=%d\n",
		    (unsigned long)fva,
		    ftype == VM_PROT_EXECUTE ? "EXEC" :
		    ftype == VM_PROT_WRITE ? "WRITE" : "READ",
		    usermode);
		printf("  VM-MAP: entries near fault addr:\n");

		vm_map_lock_read(map);
		vstart = (fva > 0x100000UL) ?
		    fva - 0x100000UL : 0;
		vend = fva + 0x100000UL;
		if (vm_map_lookup_entry(map, vstart, &entry)) {
			for (; entry != &map->header;
			    entry = vm_map_entry_succ(entry)) {
				if (entry->start >= vend)
					break;
				printf("  VM-MAP: [0x%lx-0x%lx) "
				    "prot=0x%x eflags=0x%x "
				    "wired=%d %s%s\n",
				    (unsigned long)entry->start,
				    (unsigned long)entry->end,
				    entry->protection,
				    entry->eflags,
				    entry->wired_count,
				    (entry->eflags & MAP_ENTRY_COW)
				    ? "COW " : "",
				    (entry->eflags & MAP_ENTRY_IS_SUB_MAP)
				    ? "SUB " : "");
			}
		} else {
			entry = vm_map_entry_succ(&map->header);
			if (entry != &map->header &&
			    entry->start < vend &&
			    entry->end > vstart) {
				for (; entry != &map->header;
				    entry =
				    vm_map_entry_succ(entry)) {
					if (entry->start >= vend)
						break;
					printf("  VM-MAP: [0x%lx-0x%lx)"
					    " prot=0x%x eflags=0x%x"
					    " wired=%d %s%s\n",
					    (unsigned long)entry->start,
					    (unsigned long)entry->end,
					    entry->protection,
					    entry->eflags,
					    entry->wired_count,
					    (entry->eflags &
					    MAP_ENTRY_COW)
					    ? "COW " : "",
					    (entry->eflags &
					    MAP_ENTRY_IS_SUB_MAP)
					    ? "SUB " : "");
				}
			}
		}
		vm_map_unlock_read(map);
	}
skip_vm_map:
	goto tlb_done;

tlb_done:
	if (error != KERN_SUCCESS) {
		if (usermode) {
			/*
			 * Dump user registers for ANY SIGSEGV/SIGBUS user fault.
			 * This is critical for debugging the LoongArch port
			 * where TLS ($tp/r2) initialization may be broken.
			 */
			if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL) {
				uint32_t insn;
				int insn_ok;
				uintptr_t sp;
				int si;

				printf("*** USER FAULT DUMP ***\n");
				printf("  pid=%d, comm=%s, sig=%d, ucode=%d "
				    "trap=%lu\n",
				    td->td_proc->p_pid, td->td_proc->p_comm,
				    sig, ucode,
				    (unsigned long)EXC_CODE(frame->tf_estat));
				printf("  ERA=0x%016lx, BADV=0x%016lx\n",
				    frame->tf_era, evaddr);
				printf("  SP=0x%016lx, RA=0x%016lx, TP=0x%016lx\n",
				    frame->tf_regs[3], frame->tf_regs[1],
				    frame->tf_regs[2]);
				printf("  A0=0x%016lx, A1=0x%016lx\n",
				    frame->tf_regs[4], frame->tf_regs[5]);
				printf("  A2=0x%016lx, A3=0x%016lx\n",
				    frame->tf_regs[6], frame->tf_regs[7]);
				printf("  A4=0x%016lx, A5=0x%016lx\n",
				    frame->tf_regs[8], frame->tf_regs[9]);
				printf("  A6=0x%016lx, A7=0x%016lx\n",
				    frame->tf_regs[10], frame->tf_regs[11]);
				printf("  T0=0x%016lx, T1=0x%016lx "
				    "T2=0x%016lx, T3=0x%016lx\n",
				    frame->tf_regs[12], frame->tf_regs[13],
				    frame->tf_regs[14], frame->tf_regs[15]);
				printf("  T4=0x%016lx, T5=0x%016lx "
				    "T6=0x%016lx, T7=0x%016lx\n",
				    frame->tf_regs[16], frame->tf_regs[17],
				    frame->tf_regs[18], frame->tf_regs[19]);
				printf("  T8=0x%016lx\n", frame->tf_regs[20]);
				for (si = 0; si < 9; si++) {
					printf("  S%u=0x%016lx%c",
					    si, frame->tf_regs[23 + si],
					    ((si & 1) ? '\n' : ' '));
				}
				if ((9 & 1) == 0)
					printf("\n");
				printf("  vm_maxsaddr=%p, vm_stacktop=0x%lx\n",
				    td->td_proc->p_vmspace->vm_maxsaddr,
				    (unsigned long)td->td_proc->p_vmspace->
				    vm_stacktop);

				insn_ok = (fueword32(
				    (const void *)frame->tf_era, &insn) == 0);
				if (insn_ok) {
					uint32_t opc, rd, rj, imm12;
					uint64_t rj_val, eff_addr;

					opc = (insn >> 22) & 0x3ff;
					rj = (insn >> 5) & 0x1f;
					rd = insn & 0x1f;
					imm12 = (insn >> 10) & 0xfff;
					rj_val = frame->tf_regs[rj];
					eff_addr = (uint64_t)((int64_t)rj_val +
					    (int64_t)((int32_t)(imm12 << 20) >> 20));
					printf("  [ERA]=%08x opc=%03x rj=r%u(%016lx)"
					    " rd=r%u imm=%d eff=0x%016lx\n",
					    insn, opc, rj, rj_val, rd,
					    (int)(imm12 << 20) >> 20, eff_addr);
				} else {
					printf("  [ERA] = <unreadable>\n");
				}

				/* Dump 4 words around the faulting instruction */
				{
					int k;
					uintptr_t dump_era =
					    (uintptr_t)((intptr_t)frame->tf_era & ~7);
					printf("  INSNS@ERA: ");
					for (k = 0; k < 4; k++) {
						uint32_t iw;
						if (fueword32(
						    (const void *)(dump_era + k * 4),
						    &iw) == 0)
							printf("%08x ", iw);
						else
							printf("???????? ");
					}
					printf("\n");
				}

				/* Dump stack data around SP */
				sp = (uintptr_t)frame->tf_regs[3];
				if (sp >= VM_MIN_ADDRESS && sp < VM_MAXUSER_ADDRESS) {
					uintptr_t ds = (sp & ~0xfUL);
					int k;
					printf("  STACK@0x%lx: ", (unsigned long)ds);
					for (k = 0; k < 8; k++) {
						uint64_t sw;
						if (fueword64(
						    (const void *)(ds + k * 8),
						    &sw) == 0)
							printf("%016lx ",
							    (unsigned long)sw);
						else
							printf("???????????????? ");
					}
					printf("\n");
				}

				/* Dump data around TP */
				{
					uintptr_t tp = (uintptr_t)frame->tf_regs[2];
					if (tp >= VM_MIN_ADDRESS &&
					    tp < VM_MAXUSER_ADDRESS &&
					    tp >= PAGE_SIZE) {
						uintptr_t dtp;
						int k;
						dtp = (tp & ~0xfUL);
						printf("  TP-DATA@0x%lx: ",
						    (unsigned long)dtp);
						for (k = 0; k < 4; k++) {
							uint64_t tw;
							if (fueword64(
							    (const void *)(dtp + k * 8),
							    &tw) == 0)
								printf("%016lx ",
								    (unsigned long)tw);
							else
								printf("???????????????? ");
						}
						printf("\n");
					}
				}

				/* Walk frame pointer chain for user backtrace */
				{
					uintptr_t fp, saved_ra, saved_fp;
					int fdepth;
					int fvalid, r1, r2;
					int k;

					/* Dump string at a0 (arg0) to identify operation */
					{
						uintptr_t a0addr;
						char a0str[129];
						int a0len;

						a0addr = (uintptr_t)frame->tf_regs[4];
						if (a0addr >= VM_MIN_ADDRESS &&
						    a0addr < VM_MAXUSER_ADDRESS) {
							a0len = copyinstr(
							    (const void *)a0addr,
							    a0str, sizeof(a0str),
							    NULL);
							if (a0len == 0)
								printf("  A0_STR@%#lx: \"%s\"\n",
								    (unsigned long)a0addr,
								    a0str);
							else
								printf("  A0_STR@%#lx: "
								    "copyinstr failed %d\n",
								    (unsigned long)a0addr,
								    a0len);
						}
					}

					fp = (uintptr_t)frame->tf_regs[22];
					printf("  BACKTRACE: fp=0x%016lx "
					    "ra=0x%016lx sp=0x%016lx "
					    "fp-sp=0x%lx\n",
					    (unsigned long)fp,
					    (unsigned long)frame->tf_regs[1],
					    (unsigned long)frame->tf_regs[3],
					    (unsigned long)(fp -
					    frame->tf_regs[3]));

					/* Dump raw bytes around fp to see frame layout */
					if (fp >= VM_MIN_ADDRESS &&
					    fp < VM_MAXUSER_ADDRESS &&
					    fp >= PAGE_SIZE) {
						printf("  FPRAW@fp-16: ");
						for (k = 0; k < 4; k++) {
							uint64_t v;
							if (fueword64(
							    (const void *)(fp - 16 +
							    k * 8), &v) == 0)
								printf("%016lx ",
								    (unsigned long)v);
							else
								printf("???????????????? ");
						}
						printf("\n  FPRAW@fp+0:  ");
						for (k = 0; k < 4; k++) {
							uint64_t v;
							if (fueword64(
							    (const void *)(fp +
							    k * 8), &v) == 0)
								printf("%016lx ",
								    (unsigned long)v);
							else
								printf("???????????????? ");
						}
						printf("\n");
					}

					for (fdepth = 0;
					    fdepth < 16 &&
					    fp >= PAGE_SIZE;
					    fdepth++) {
						fvalid = (fp >= VM_MIN_ADDRESS &&
						    fp < VM_MAXUSER_ADDRESS &&
						    (fp & 7) == 0);
						if (!fvalid)
							break;

						/*
						 * Try fp=strategy: if fp points
						 * to old_sp (top of frame),
						 * saved ra/fp are at fp-8/fp-16.
						 * If fp=sp (bottom), at fp/fp+8.
						 */
						r1 = fueword64(
						    (const void *)(fp - 16),
						    &saved_fp);
						r2 = fueword64(
						    (const void *)(fp - 8),
						    &saved_ra);

						/* If both zero at fp-8/fp-16, try fp/fp+8 */
						if ((r1 != 0 || saved_fp == 0) &&
						    (r2 != 0 || saved_ra == 0)) {
							r1 = fueword64(
							    (const void *)(fp),
							    &saved_fp);
							r2 = fueword64(
							    (const void *)(fp + 8),
							    &saved_ra);
						}

						printf("  FRAME%d: ra=0x%lx "
						    "fp=0x%lx\n",
						    fdepth,
						    (unsigned long)saved_ra,
						    (unsigned long)saved_fp);

						if (r1 != 0 || r2 != 0)
							break;
						if (saved_ra == 0 || saved_fp == 0)
							break;
						fp = saved_fp;
					}
				}

				/*
				 * Stack scan: when frame pointer chain is
				 * broken (rtld doesn't maintain standard fp),
				 * scan the user stack for values that look
				 * like code addresses to reconstruct the
				 * call chain.
				 */
				{
					uintptr_t ssp, swalk, send;
					uintptr_t era_lo, era_hi;
					int scount, sfound, sfound_other;

					ssp = (uintptr_t)frame->tf_regs[3];
					send = (uintptr_t)td->td_proc->
					    p_vmspace->vm_stacktop;

					/*
					 * Define a ±4MB window around ERA to
					 * identify likely ld-elf.so.1 code
					 * addresses. Also covers /bin/sh.
					 */
					era_lo = ((uintptr_t)frame->tf_era &
					    ~0x3fffffUL);
					era_hi = era_lo + 0x800000UL;
					if (era_lo < 0x800000UL)
						era_lo = 0x800000UL;

					printf("  STACKSCAN: sp=0x%lx "
					    "stacktop=0x%lx "
					    "era_window=[0x%lx,0x%lx)\n",
					    (unsigned long)ssp,
					    (unsigned long)send,
					    (unsigned long)era_lo,
					    (unsigned long)era_hi);

					swalk = ((ssp - 16) & ~0x7UL);
					if (swalk < VM_MIN_ADDRESS)
						swalk = VM_MIN_ADDRESS;
					sfound = 0;
					sfound_other = 0;
					scount = 0;

					/*
					 * Pass 1: find code addresses in
					 * ERA window (likely rtld code).
					 */
					while (swalk <= send - 8 &&
					    sfound < 32 && scount < 512) {
						uint64_t sval;

						if (fueword64(
						    (const void *)swalk,
						    &sval) != 0)
							break;

						if (sval >= 0x10000UL &&
						    sval <
						    VM_MAXUSER_ADDRESS &&
						    (sval & 3) == 0) {
							if (sval >= era_lo &&
							    sval < era_hi) {
							printf("  STACK["
							    "+0x%lx"
							    "]=0x%016lx"
							    " (RTLD "
							    "+0x%lx)\n",
							    (unsigned long)
							    (swalk - ssp),
							    (unsigned long)
							    sval,
							    (unsigned long)
							    (sval - era_lo));
							sfound++;
							} else if (
							    sfound_other < 8) {
							printf("  STACK["
							    "+0x%lx"
							    "]=0x%016lx"
							    " (OTHER)\n",
							    (unsigned long)
							    (swalk - ssp),
							    (unsigned long)
							    sval);
							sfound_other++;
							}
						}
						swalk += 8;
						scount++;
					}
					if (sfound == 0 && sfound_other == 0)
					printf("  STACKSCAN: no code "
					    "addresses found\n");
				else
					printf("  STACKSCAN: "
					    "total=%d rtld=%d "
					    "other=%d scanned=%d\n",
					    sfound + sfound_other,
					    sfound, sfound_other,
					    scount);
			}

			/*
			 * GOT audit: scan backward from ERA to find
			 * pcalau12i (opc=0x0D) or pcadd12i (opc=0x0C)
			 * instructions that load a register used as base
			 * in ld.d at ERA.  Then dump the first 8 GOT
			 * entries on that page.
			 *
			 * Also dump all insns from ERA-32 to ERA+12
			 * with disassembly hints for key opcodes.
			 */
			{
				uintptr_t era = (uintptr_t)frame->tf_era;
				uintptr_t p;
				uint32_t insn, era_insn;
				int i, loos;
				uint64_t got_entry;
				int found_got = 0;
				int rj_era;

				/*
				 * Read the actual faulting instruction.
				 */
				if (fueword32((const void *)era,
				    &era_insn) != 0)
					era_insn = 0;
				rj_era = (era_insn >> 5) & 0x1f;

				printf("  GOTDBG: ERA=%#lx insn=%08x "
				    "rj=%d rd=%d\n",
				    (unsigned long)era, era_insn,
				    rj_era, (int)(era_insn & 0x1f));

				/*
				 * Dump 9 instructions before ERA and
				 * 3 after, to capture the full GOT
				 * access sequence (pcalau12i + ld.d
				 * chain + use).
				 */
				printf("  GOTDBG: insn window "
				    "[ERA-36 .. ERA+12]:\n");
				for (i = -9; i <= 3; i++) {
					uintptr_t addr = era + i * 4;
					int rj, rd, imm;

					if ((long)addr < 0)
						continue;
					if (fueword32((const void *)addr,
					    &insn) != 0)
						continue;

					rj = (insn >> 5) & 0x1f;
					rd = insn & 0x1f;
					imm = 0;

					printf("    %cERA%+d: %08x",
					    (i == 0) ? '*' : ' ',
					    i * 4, insn);

					/*
					 * Annotate pcalau12i/pcadd12i.
					 */
					if (((insn >> 25) & 0x7f) == 0x0c ||
					    ((insn >> 25) & 0x7f) == 0x0d) {
						loos = (insn >> 5) & 0xfffff;
						if (loos & 0x80000)
							loos |= ~0xfffff;
						printf("  <pca%s12i "
						    "r%d si20=%d tgt=%#lx>",
						    ((insn >> 25) & 0x7f) == 0x0d
						    ? "lau" : "dd",
						    rd, loos,
						    (unsigned long)
						    ((addr & ~0xfffUL) +
						    ((intptr_t)loos << 12)));
					}
					/*
					 * Annotate ld.d.
					 */
					else if (((insn >> 22) & 0x3ff) ==
					    0x0a2) {
						imm = (insn >> 10) & 0xfff;
						if (imm & 0x800)
							imm |= ~0xfff;
						printf("  <ld.d r%d, r%d, %d>",
						    rd, rj, imm);
					}
					/*
					 * Annotate addi.d.
					 */
					else if (((insn >> 22) & 0x3ff) ==
					    0x0a6 && ((insn >> 15) & 1) == 0) {
						imm = (insn >> 10) & 0xfff;
						if (imm & 0x800)
							imm |= ~0xfff;
						printf("  <addi.d r%d, r%d, %d>",
						    rd, rj, imm);
					}

					printf("\n");
				}

				/*
				 * Scan ERA-48 to ERA for any
				 * pcadd12i/pcalau12i that sets rj_era.
				 */
				for (p = era - 48; p < era; p += 4) {
					if ((long)p < 0)
						continue;
					if (fueword32((const void *)p,
					    &insn) != 0)
						continue;
					if (((insn >> 25) & 0x7f) != 0x0c &&
					    ((insn >> 25) & 0x7f) != 0x0d)
						continue;
					loos = (insn >> 5) & 0xfffff;
					if (loos & 0x80000)
						loos |= ~0xfffff;

					printf("  GOTDBG: found pca%s12i "
					    "rd=%d si20=%d "
					    "target=%#lx at ERA%+d, "
					    "ERA uses rj=%d\n",
					    ((insn >> 25) & 0x7f) == 0x0d
					    ? "lau" : "dd",
					    (int)(insn & 0x1f), loos,
					    (unsigned long)((p & ~0xfffUL) +
					    ((intptr_t)loos << 12)),
					    (int)(p - era),
					    rj_era);
					found_got = 1;

					/*
					 * Dump first 8 GOT entries on
					 * the target page.
					 */
					{
						uintptr_t gpage;
						int g;

						gpage = (p & ~0xfffUL) +
						    ((intptr_t)loos << 12);
						printf("  GOTDBG: GOT@%#lx:",
						    (unsigned long)gpage);
						for (g = 0; g < 8; g++) {
							if (fueword64(
							    (const void *)
							    (gpage + g * 8),
							    &got_entry) != 0) {
								printf(" ??");
								break;
							}
							printf(" %016lx",
							    (unsigned long)
							    got_entry);
						}
						printf("\n");
					}
				}

				if (!found_got)
				printf("  GOTDBG: no pca*12i "
				    "found in [ERA-48..ERA)\n");
		}
	}
	call_trapsignal(td, sig, ucode, (void *)evaddr,
			    EXC_CODE(frame->tf_estat));
		} else {
			if (pcb->pcb_onfault != 0) {
				frame->tf_a[0] = EFAULT;
				frame->tf_era = pcb->pcb_onfault;
				return;
			}
			goto fatal;
		}
	}

done:
	if (usermode)
		userret(td, frame);
	return;

fatal:
	dump_regs(frame);
#ifdef KDB
	if (debugger_on_trap) {
		kdb_why = KDB_WHY_TRAP;
		handled = kdb_trap(EXC_CODE(frame->tf_estat), 0, frame);
		kdb_why = KDB_WHY_UNSET;
		if (handled)
			return;
	}
#endif
	panic("Fatal page fault at %#lx: %#016lx", frame->tf_era, evaddr);
}

void
do_trap_supervisor(struct trapframe *frame)
{
	uint64_t exception;

	/* Ensure we came from supervisor mode, interrupts disabled */
	exception = EXC_CODE(frame->tf_estat);

	if (frame->tf_estat == 0) {
		uint64_t raw_estat = csr_read64(LOONGARCH_CSR_ESTAT);

		/* If there's a pending interrupt (ISR bits set), handle it */
		if (raw_estat & ((1 << IRQ_NMI) - 1)) {
			frame->tf_estat = raw_estat;
			exception = EXC_CODE(frame->tf_estat);
		} else {
			return;
		}
	}

	if (EXC_CODE(frame->tf_estat) == EXCCODE_RSV) {
		/* Interrupt - dispatch through FreeBSD interrupt framework */
		intr_irq_handler(frame, 0);
		return;
	}

#ifdef KDTRACE_HOOKS
	if (dtrace_trap_func != NULL && (*dtrace_trap_func)(frame, exception))
		return;
#endif

	CTR4(KTR_TRAP, "%s: exception=%lu, era=%lx, badvaddr=%lx", __func__,
	    exception, frame->tf_era, frame->tf_badvaddr);

	switch (exception) {
	case EXCCODE_ADE:
#ifdef DDB
		if (kdb_active || debugger_on_trap) {
			kdb_why = KDB_WHY_TRAP;
			if (kdb_trap(exception, 0, frame)) {
				kdb_why = KDB_WHY_UNSET;
				return;
			}
			kdb_why = KDB_WHY_UNSET;
		}
#endif
		dump_regs(frame);
		panic("Memory access exception at 0x%016lx\n", frame->tf_era);
		break;
	case EXCCODE_ALE:
		dump_regs(frame);
		panic("Misaligned address exception at %#016lx: %#016lx\n",
		    frame->tf_era, frame->tf_badvaddr);
		break;
	case EXCCODE_TLBL:
	case EXCCODE_TLBS:
	case EXCCODE_TLBI:
	case EXCCODE_TLBM:
	case EXCCODE_TLBNR:
	case EXCCODE_TLBNX:
	case EXCCODE_TLBPE:
		page_fault_handler(frame, 0);
		break;
	case EXCCODE_BP:
#ifdef KDTRACE_HOOKS
		if (dtrace_invop_jump_addr != NULL &&
		    dtrace_invop_jump_addr(frame) == 0)
				break;
#endif
#ifdef KDB
		kdb_trap(exception, 0, frame);
#else
		dump_regs(frame);
		panic("No debugger in kernel.\n");
#endif
		break;
	case EXCCODE_INE:
		dump_regs(frame);
		panic("Illegal instruction at 0x%016lx\n", frame->tf_era);
		break;
	case EXCCODE_FPDIS:
		/*
		 * FPU/SIMU unexpectedly disabled in kernel mode.
		 * Re-enable and continue.  This should not normally
		 * happen; it is a safety net in case a nested FPDIS
		 * occurs during user-mode FPU trap handling.
		 */
		write_csr_euen(read_csr_euen() | CSR_EUEN_FPEN);
		frame->tf_euen |= CSR_EUEN_FPEN;
		break;
	case EXCCODE_LSXDIS:
		/*
		 * LSX SIMD unexpectedly disabled in kernel mode.
		 * Re-enable FPU + LSX and continue.
		 */
		write_csr_euen(read_csr_euen() |
		    CSR_EUEN_FPEN | CSR_EUEN_LSXEN);
		frame->tf_euen |= CSR_EUEN_FPEN | CSR_EUEN_LSXEN;
		break;
	case EXCCODE_LASXDIS:
		/*
		 * LASX SIMD unexpectedly disabled in kernel mode.
		 * Re-enable FPU + LSX + LASX and continue.
		 */
		write_csr_euen(read_csr_euen() |
		    CSR_EUEN_FPEN | CSR_EUEN_LSXEN | CSR_EUEN_LASXEN);
		frame->tf_euen |= CSR_EUEN_FPEN | CSR_EUEN_LSXEN |
		    CSR_EUEN_LASXEN;
		break;
	case EXCCODE_FPE:
		dump_regs(frame);
		panic("Floating point exception in kernel at 0x%016lx\n",
		    frame->tf_era);
		break;
	default:
		dump_regs(frame);
		panic("Unknown kernel exception %lx trap value %lx\n",
		    exception, frame->tf_badvaddr);
	}
}

void
do_trap_user(struct trapframe *frame)
{
	uint64_t exception;
	struct thread *td;
	struct pcb *pcb;

	td = curthread;
	pcb = td->td_pcb;

	KASSERT(td->td_frame == frame,
	    ("%s: td_frame %p != frame %p", __func__, td->td_frame, frame));

	/* Ensure we came from usermode, interrupts disabled */
	exception = EXC_CODE(frame->tf_estat);

	/* Diagnostic: print spinlock state and exception info */
#if 0
	if (td->td_proc != NULL && (td->td_proc->p_pid <= 1 ||
	    exception >= EXCCODE_TLBL || exception == EXCCODE_RSV)) {
		printf("DEV-DEBUG: do_trap_user ENTRY: pid=%d comm=%s "
		    "exc=%lu era=%#lx spinlock_cnt=%d critnest=%d\n",
		    td->td_proc->p_pid, td->td_proc->p_comm,
		    (unsigned long)exception,
		    (unsigned long)frame->tf_era,
		    td->td_md.md_spinlock_count, td->td_critnest);
	}
#endif

	/* TLBM fast diag disabled to reduce UART noise */
	if (EXC_CODE(frame->tf_estat) == EXCCODE_RSV) {
		/* Interrupt - dispatch through FreeBSD interrupt framework */
		intr_irq_handler(frame, 0);
		return;
	}
	intr_enable();

	CTR4(KTR_TRAP, "%s: exception=%lu, pc=%lx, badvaddr=%lx", __func__,
	    exception, frame->tf_era, frame->tf_badvaddr);

	switch (exception) {
	case EXCCODE_ADE:
		call_trapsignal(td, SIGBUS, BUS_ADRERR, (void *)frame->tf_era,
		    exception);
		userret(td, frame);
		break;
	case EXCCODE_ALE:
		call_trapsignal(td, SIGBUS, BUS_ADRALN, (void *)frame->tf_era,
		    exception);
		userret(td, frame);
		break;
	case EXCCODE_TLBL:
	case EXCCODE_TLBS:
	case EXCCODE_TLBI:
	case EXCCODE_TLBM:
	case EXCCODE_TLBNR:
	case EXCCODE_TLBNX:
	case EXCCODE_TLBPE:
		page_fault_handler(frame, 1);
		break;
	case EXCCODE_SYS:
		frame->tf_era += 4;	/* Next instruction */
		ecall_handler();
		break;
	case EXCCODE_INE:
		if ((pcb->pcb_fpflags & PCB_FP_STARTED) == 0) {
			/*
			 * May be a FPE trap. Enable FPE usage
			 * for this thread and try again.
			 */
			fpe_state_clear();
			pcb->pcb_fpflags |= PCB_FP_STARTED;
			break;
		}
		call_trapsignal(td, SIGILL, ILL_ILLOPC, (void *)frame->tf_era,
		    exception);
		userret(td, frame);
		break;
	case EXCCODE_BP:
		call_trapsignal(td, SIGTRAP, TRAP_BRKPT, (void *)frame->tf_era,
		    exception);
		userret(td, frame);
		break;
	case EXCCODE_FPDIS:
		/*
		 * FPU/SIMD is disabled.  Enable it for this thread
		 * and retry the instruction.  The compiler may generate
		 * LSX instructions for memcpy/string operations, which
		 * trigger FPDIS when EUEN.FPEN is not set.
		 *
		 * fpe_state_clear() uses FPU instructions internally,
		 * so we must enable the FPU in hardware before calling it.
		 */
		if ((pcb->pcb_fpflags & PCB_FP_STARTED) == 0) {
			write_csr_euen(read_csr_euen() | CSR_EUEN_FPEN);
			fpe_state_clear();
			pcb->pcb_fpflags |= PCB_FP_STARTED;
		}
		frame->tf_euen |= CSR_EUEN_FPEN;
		break;
	case EXCCODE_LSXDIS:
		/*
		 * LSX SIMD is disabled.  Newer LoongArch CPUs (LA664)
		 * generate separate exception codes for LSX/LASX.
		 * LSX depends on FPU; enable both EUEN.FPEN and
		 * EUEN.LSXEN, then retry the instruction.
		 *
		 * fpe_state_clear() initializes the FP register file
		 * (lower 64 bits of each vector register).  LSX upper
		 * bits (64-127) will be zero-initialized by the
		 * first vxor.v/vldi instruction in libc if needed.
		 */
		if ((pcb->pcb_fpflags & PCB_FP_STARTED) == 0) {
			write_csr_euen(read_csr_euen() |
			    CSR_EUEN_FPEN | CSR_EUEN_LSXEN);
			fpe_state_clear();
			pcb->pcb_fpflags |= PCB_FP_STARTED;
		}
		frame->tf_euen |= CSR_EUEN_FPEN | CSR_EUEN_LSXEN;
		break;
	case EXCCODE_LASXDIS:
		/*
		 * LASX SIMD is disabled.  LASX depends on both FPU
		 * and LSX; enable all three EUEN bits.
		 */
		if ((pcb->pcb_fpflags & PCB_FP_STARTED) == 0) {
			write_csr_euen(read_csr_euen() |
			    CSR_EUEN_FPEN | CSR_EUEN_LSXEN |
			    CSR_EUEN_LASXEN);
			fpe_state_clear();
			pcb->pcb_fpflags |= PCB_FP_STARTED;
		}
		frame->tf_euen |= CSR_EUEN_FPEN | CSR_EUEN_LSXEN |
		    CSR_EUEN_LASXEN;
		break;
	case EXCCODE_FPE:
		call_trapsignal(td, SIGFPE, FPE_FLTINV, (void *)frame->tf_era,
		    exception);
		userret(td, frame);
		break;
	default:
		/*
		 * Unknown user exception.  Deliver SIGILL instead of
		 * panicking — a broken userspace program should not
		 * be able to crash the kernel.  The exception code
		 * is passed as si_trapno for diagnostic purposes.
		 */
		printf("WARNING: Unknown user exception %#lx at era=%#lx "
		    "pid=%d comm=%s\n",
		    exception, frame->tf_era,
		    td->td_proc->p_pid, td->td_proc->p_comm);
		call_trapsignal(td, SIGILL, ILL_ILLOPC, (void *)frame->tf_era,
		    exception);
		userret(td, frame);
		break;
	}

	/* Diagnostic: print spinlock state before returning to exception.S */
#if 0
	if (td->td_proc != NULL && td->td_proc->p_pid <= 1) {
		printf("DEV-DEBUG: do_trap_user EXIT: pid=%d comm=%s "
		    "exc=%lu spinlock_cnt=%d\n",
		    td->td_proc->p_pid, td->td_proc->p_comm,
		    (unsigned long)exception,
		    td->td_md.md_spinlock_count);
	}
#endif
}
