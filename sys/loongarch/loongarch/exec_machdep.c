/*-
 * Copyright (c) 2014 Andrew Turner
 * Copyright (c) 2015-2017 Ruslan Bukin <br@bsdpad.com>
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
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/elf.h>
#include <sys/elf_common.h>
#include <sys/imgact.h>
#include <sys/imgact_elf.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/signalvar.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/ucontext.h>

#include <machine/cpu.h>
#include <machine/fpe.h>
#include <machine/kdb.h>
#include <machine/pcb.h>
#include <machine/pte.h>
#include <machine/loongarchreg.h>
#include <machine/trap.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

static void get_fpcontext(struct thread *td, mcontext_t *mcp);
static void set_fpcontext(struct thread *td, mcontext_t *mcp);

/* Verify structure sizes match expected values for ABI compatibility */
_Static_assert(sizeof(mcontext_t) == 688, "mcontext_t size incorrect");
_Static_assert(sizeof(ucontext_t) == 760, "ucontext_t size incorrect");
_Static_assert(sizeof(siginfo_t) == 80, "siginfo_t size incorrect");

int
fill_regs(struct thread *td, struct reg *regs)
{
	struct trapframe *frame;

	frame = td->td_frame;
	regs->orig_a0 = frame->tf_regs[4];
	regs->era = frame->tf_era;
	regs->badvaddr = frame->tf_badvaddr;
	regs->crmd = frame->tf_crmd;
	regs->prmd = frame->tf_prmd;
	regs->euen = frame->tf_euen;
	regs->ecfg = frame->tf_ecfg;
	regs->estat = frame->tf_estat;

	memcpy(regs->regs, frame->tf_regs, sizeof(regs->regs));

	return (0);
}

int
set_regs(struct thread *td, struct reg *regs)
{
	struct trapframe *frame;

	frame = td->td_frame;
	frame->tf_regs[4] = regs->orig_a0;
	frame->tf_era = regs->era;
	frame->tf_badvaddr = regs->badvaddr;
	frame->tf_crmd = regs->crmd;
	frame->tf_prmd = regs->prmd;
	frame->tf_euen = regs->euen;
	frame->tf_ecfg = regs->ecfg;
	frame->tf_estat = regs->estat;

	memcpy(frame->tf_regs, regs->regs, sizeof(frame->tf_regs));

	return (0);
}

int
fill_fpregs(struct thread *td, struct fpreg *regs)
{
	struct pcb *pcb;

	pcb = td->td_pcb;

	if ((pcb->pcb_fpflags & PCB_FP_STARTED) != 0) {
		/*
		 * If we have just been running FPE instructions we will
		 * need to save the state to memcpy it below.
		 */
		if (td == curthread)
			fpe_state_save(td);

		memcpy(regs->fp_regs, pcb->pcb_fregs, sizeof(regs->fp_regs));
		regs->fp_fcsr = pcb->uf.pcb_fcsr0;
	} else
		memset(regs, 0, sizeof(*regs));

	return (0);
}

int
set_fpregs(struct thread *td, struct fpreg *regs)
{
	//struct trapframe *frame;
	struct pcb *pcb;

	//frame = td->td_frame;
	pcb = td->td_pcb;

	memcpy(pcb->pcb_fregs, regs->fp_regs, sizeof(regs->fp_regs));
	pcb->uf.pcb_fcsr0 = regs->fp_fcsr;
	pcb->pcb_fpflags |= PCB_FP_STARTED;

	return (0);
}

int
fill_dbregs(struct thread *td, struct dbreg *regs)
{

	panic("fill_dbregs");
}

int
set_dbregs(struct thread *td, struct dbreg *regs)
{

	panic("set_dbregs");
}

#define	TP_OFFSET	16	/* sizeof(struct tcb), must match vm_machdep.c */

void
exec_setregs(struct thread *td, struct image_params *imgp, uintptr_t stack)
{
	struct trapframe *tf;
	struct pcb *pcb;
	vm_offset_t tls_addr;
	vm_size_t tls_size;
	vm_map_t map;
	int error;

	tf = td->td_frame;
	pcb = td->td_pcb;

	printf("DEV-DEBUG: exec_setregs BEGIN: pid=%d comm=%s "
	    "spinlock_cnt=%d critnest=%d\n",
	    td->td_proc->p_pid, td->td_proc->p_comm,
	    td->td_md.md_spinlock_count, td->td_critnest);

	memset(tf, 0, sizeof(struct trapframe));

	tf->tf_regs[3] = STACKALIGN(stack);
	tf->tf_regs[4] = stack;
	tf->tf_regs[1] = imgp->entry_addr;
	tf->tf_era = imgp->entry_addr;

	printf("DEV-DEBUG: exec_setregs: entry=%#lx stack=%#lx\n",
	    imgp->entry_addr, stack);
	if (imgp->entry_addr != 0) {
		uint32_t insn[8];
		if (copyin((const void *)imgp->entry_addr, insn, sizeof(insn)) == 0) {
			printf("DEV-DEBUG: insns@entry: %08x %08x %08x %08x "
			    "%08x %08x %08x %08x\n",
			    insn[0], insn[1], insn[2], insn[3],
			    insn[4], insn[5], insn[6], insn[7]);
		}
	}

	/*
	 * DEBUG: Dump full register state and AUX vector.
	 * The AUX dump is done unconditionally from the user stack
	 * (does NOT depend on imgp->interpreter_name which may be
	 * freed before exec_setregs is called).
	 */
	printf("DEV-DEBUG: exec_setregs full: pid=%d comm=%s "
	    "entry=%#lx stack=%#lx tp=%#lx\n",
	    td->td_proc->p_pid, td->td_proc->p_comm,
	    (unsigned long)imgp->entry_addr, (unsigned long)stack,
	    (unsigned long)tf->tf_tp);
	printf("DEV-DEBUG:   interp_name=%s\n",
	    imgp->interpreter_name != NULL ? imgp->interpreter_name : "(null)");
	printf("DEV-DEBUG:   ra=%#lx sp=%#lx a0=%#lx\n",
	    (unsigned long)tf->tf_regs[1], (unsigned long)tf->tf_regs[3],
	    (unsigned long)tf->tf_regs[4]);
	printf("DEV-DEBUG:   crmd=%#lx prmd=%#lx euen=%#lx ecfg=%#lx\n",
	    (unsigned long)tf->tf_crmd, (unsigned long)tf->tf_prmd,
	    (unsigned long)tf->tf_euen, (unsigned long)tf->tf_ecfg);

	/* Unconditional AUX vector dump from user stack */
	{
		uintptr_t walk;
		int64_t argc;
		uintptr_t argv_base;
		int nenv;

		walk = (uintptr_t)stack;
		if (copyin((const void *)walk, &argc, sizeof(argc)) == 0 &&
		    argc > 0 && argc < 1024) {
			argv_base = walk + sizeof(int64_t);

			walk = argv_base + (argc + 1) * sizeof(uintptr_t);
			nenv = 0;
			while (nenv < 1024) {
				uintptr_t ptr;
				if (copyin((const void *)walk, &ptr,
				    sizeof(ptr)) != 0)
					break;
				if (ptr == 0)
					break;
				walk += sizeof(uintptr_t);
				nenv++;
			}
			walk += sizeof(uintptr_t); /* skip NULL terminator */

			printf("DEV-DEBUG:   AUX: argc=%ld nenv=%d "
			    "auxv@stack+%#lx\n",
			    (long)argc, nenv,
			    (unsigned long)(walk - STACKALIGN(stack)));
			{
				Elf_Auxinfo aux[2];
				int aux_idx = 0;

				while (aux_idx < 64) {
					if (copyin((const void *)walk, aux,
					    sizeof(aux)) != 0) {
						printf("DEV-DEBUG:   AUX: "
						    "copyin failed at idx %d\n",
						    aux_idx);
						break;
					}
					if (aux[0].a_type == AT_NULL)
						break;

					printf("DEV-DEBUG:   AUX[%d]: "
					    "type=%lu val=%#lx (0x%lx)\n",
					    aux_idx,
					    (unsigned long)aux[0].a_type,
					    (unsigned long)aux[0].a_un.a_val,
					    (unsigned long)aux[0].a_un.a_val);
					walk += sizeof(Elf_Auxinfo);
					aux_idx++;
				}
				printf("DEV-DEBUG:   AUX: total=%d entries\n",
				    aux_idx);
			}
		} else {
			printf("DEV-DEBUG:   AUX: copyin argc FAILED or "
			    "argc=%ld out of range (max=1024) at %#lx\n",
			    (long)argc, (unsigned long)walk);
		}
	}

	/*
	 * Allocate a zero-filled TLS region for the initial thread.
	 *
	 * On LoongArch64, the TP ($r2) register is used for Thread Local
	 * Storage (TLS) access via TP-relative addressing.  If TP is zero
	 * when userland first executes, any TLS access that happens before
	 * _init_tls() is called (e.g. from jemalloc's __thread variables
	 * or errno) will compute an invalid address that wraps around to
	 * the top of the address space, loading garbage into registers and
	 * causing subsequent memory corruption and crashes.
	 *
	 * We must allocate enough space to cover the binary's entire TLS
	 * segment (PT_TLS p_memsz), not just one page.  The C runtime
	 * startup code accesses TLS variables before _init_tls() gets a
	 * chance to reallocate, so the initial region must be large enough.
	 * When _init_tls() runs later it will allocate a proper TLS block
	 * and update TP via _tcb_set().
	 */
	map = &td->td_proc->p_vmspace->vm_map;
	tls_addr = 0;
	tls_size = PAGE_SIZE;
	if (imgp->image_header != NULL) {
		const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)imgp->image_header;
		if (ehdr->e_ident[EI_CLASS] == ELFCLASS64 &&
		    ehdr->e_phoff != 0 &&
		    ehdr->e_phnum != 0) {
			const Elf64_Phdr *phdr;
			Elf64_Half i;

			phdr = (const Elf64_Phdr *)(imgp->image_header +
			    ehdr->e_phoff);
			for (i = 0; i < ehdr->e_phnum; i++) {
				if (phdr[i].p_type == PT_TLS) {
					tls_size = roundup2(phdr[i].p_memsz +
					    TP_OFFSET, PAGE_SIZE);
					break;
				}
			}
		}
	}

	printf("DEV-DEBUG: exec_setregs BEFORE vm_map_find: pid=%d "
	    "tls_size=%#lx spinlock_cnt=%d\n",
	    td->td_proc->p_pid, (unsigned long)tls_size,
	    td->td_md.md_spinlock_count);

	error = vm_map_find(map, NULL, 0, &tls_addr, tls_size,
	    VM_MAXUSER_ADDRESS, VMFS_ANY_SPACE,
	    VM_PROT_READ | VM_PROT_WRITE,
	    VM_PROT_READ | VM_PROT_WRITE, 0);

	printf("DEV-DEBUG: exec_setregs AFTER vm_map_find: pid=%d "
	    "error=%d tls_addr=%#lx spinlock_cnt=%d\n",
	    td->td_proc->p_pid, error, (unsigned long)tls_addr,
	    td->td_md.md_spinlock_count);
	if (error == KERN_SUCCESS)
		tf->tf_tp = tls_addr + TP_OFFSET;
	else
		printf("WARNING: exec_setregs: failed to allocate TLS region "
		    "(%lu bytes, error=%d), TP will be 0 for pid=%d\n",
		    (unsigned long)tls_size, error, td->td_proc->p_pid);

	tf->tf_crmd = CSR_CRMD_PG | CSR_CRMD_IE | PLV_USER;
	tf->tf_prmd = CSR_PRMD_PIE | (PLV_USER << CSR_PRMD_PPLV_SHIFT);
	tf->tf_euen = 0;
	/*
	 * Set ECFG to enable local interrupts.  Without this, all local
	 * interrupts remain disabled (e.g. after fork() from cpu_fork
	 * which must set tf_ecfg to a valid mask; reading the current CSR
	 * at this point would return the stale user value, not the kernel's).
	 * HWI1 (bit 3) for EIOINTC cascade, TI (bit 11) for timer,
	 * IPI (bit 12) for inter-processor interrupt.
	 */
	tf->tf_ecfg = ECFGF_TIMER | ECFGF_IPI | ECFGF(3);

	printf("DEV-DEBUG: exec_setregs DONE: pid=%d ecfg=%#lx "
	    "pc=%#lx sp=%#lx spinlock_cnt=%d critnest=%d\n",
	    td->td_proc->p_pid,
	    (unsigned long)tf->tf_ecfg,
	    (unsigned long)tf->tf_era,
	    (unsigned long)tf->tf_regs[3],
	    td->td_md.md_spinlock_count,
	    td->td_critnest);

	pcb->pcb_fpflags &= ~PCB_FP_STARTED;
}

/*
 * Called from fork_trampoline in swtch.S before ertn.
 * Diagnostic: print spinlock state before entering user space.
 * a0 = trapframe pointer, a1 = td pointer.
 */
void fork_trampoline_diag(struct trapframe *tf, struct thread *td);

void
fork_trampoline_diag(struct trapframe *tf, struct thread *td)
{
	printf("DEV-DEBUG: fork_trampoline PRE-ERTN: pid=%d comm=%s "
	    "era=%#lx sp=%#lx tp=%#lx spinlock_cnt=%d "
	    "crmd=%#lx prmd=%#lx\n",
	    td->td_proc->p_pid, td->td_proc->p_comm,
	    (unsigned long)tf->tf_era,
	    (unsigned long)tf->tf_regs[3],
	    (unsigned long)tf->tf_tp,
	    td->td_md.md_spinlock_count,
	    (unsigned long)tf->tf_crmd,
	    (unsigned long)tf->tf_prmd);
}

/* Sanity check these are the same size, they will be memcpy'd to and from */
CTASSERT(sizeof(((struct trapframe *)0)->tf_regs) ==
    sizeof(((struct gpregs *)0)->gp_regs));
CTASSERT(sizeof(((struct trapframe *)0)->tf_regs) ==
    sizeof(((struct reg *)0)->regs));

int
get_mcontext(struct thread *td, mcontext_t *mcp, int clear_ret)
{
	struct trapframe *tf = td->td_frame;

	memcpy(mcp->mc_gpregs.gp_regs, tf->tf_regs, sizeof(mcp->mc_gpregs.gp_regs));

	if (clear_ret & GET_MC_CLEAR_RET) {
		mcp->mc_gpregs.gp_regs[4] = 0;
		mcp->mc_gpregs.gp_regs[12] = 0; /* clear syscall error */
	}

	mcp->mc_gpregs.gp_orig_a0 = tf->tf_regs[4];
	mcp->mc_gpregs.gp_era = tf->tf_era;
	mcp->mc_gpregs.gp_badvaddr = tf->tf_badvaddr;
	mcp->mc_gpregs.gp_crmd = tf->tf_crmd;
	mcp->mc_gpregs.gp_prmd = tf->tf_prmd;
	mcp->mc_gpregs.gp_euen = tf->tf_euen;
	mcp->mc_gpregs.gp_ecfg = tf->tf_ecfg;
	mcp->mc_gpregs.gp_estat = tf->tf_estat;
	get_fpcontext(td, mcp);

	return (0);
}

int
set_mcontext(struct thread *td, mcontext_t *mcp)
{
	struct trapframe *tf;

	tf = td->td_frame;

	/*
	 * Validate CRMD and PRMD fields.
	 *
	 * Ignore writes to the PLV field as we always run in user mode.
	 * Permit changes to the IE (interrupt enable) bits.
	 */
	if (((mcp->mc_gpregs.gp_crmd ^ tf->tf_crmd) & CSR_CRMD_PLV) != 0)
		return (EINVAL);
	if (((mcp->mc_gpregs.gp_prmd ^ tf->tf_prmd) & CSR_PRMD_PPLV) != 0)
		return (EINVAL);
	if (mcp->mc_gpregs.gp_regs[2] >= VM_MAXUSER_ADDRESS)
		return (EINVAL);
	if (mcp->mc_gpregs.gp_era >= VM_MAXUSER_ADDRESS)
		return (EINVAL);
	if (mcp->mc_gpregs.gp_regs[3] >= VM_MAXUSER_ADDRESS)
		return (EINVAL);

	memcpy(tf->tf_regs, mcp->mc_gpregs.gp_regs, sizeof(tf->tf_regs));

	tf->tf_era = mcp->mc_gpregs.gp_era;
	tf->tf_badvaddr = mcp->mc_gpregs.gp_badvaddr;
	tf->tf_crmd = mcp->mc_gpregs.gp_crmd;
	tf->tf_prmd = mcp->mc_gpregs.gp_prmd;
	tf->tf_euen = mcp->mc_gpregs.gp_euen;
	tf->tf_ecfg = mcp->mc_gpregs.gp_ecfg;
	tf->tf_estat = mcp->mc_gpregs.gp_estat;
	set_fpcontext(td, mcp);

	return (0);
}

static void
get_fpcontext(struct thread *td, mcontext_t *mcp)
{
	struct pcb *curpcb;

	critical_enter();

	curpcb = curthread->td_pcb;

	KASSERT(td->td_pcb == curpcb, ("Invalid fpe pcb"));

	if ((curpcb->pcb_fpflags & PCB_FP_STARTED) != 0) {
		/*
		 * If we have just been running FPE instructions we will
		 * need to save the state to memcpy it below.
		 */
		fpe_state_save(td);

		KASSERT((curpcb->pcb_fpflags & ~PCB_FP_USERMASK) == 0,
		    ("Non-userspace FPE flags set in get_fpcontext"));
		memcpy(mcp->mc_fpregs.fp_regs, curpcb->pcb_fregs,
		    sizeof(mcp->mc_fpregs.fp_regs));
		mcp->mc_fpregs.fp_fcsr = curpcb->uf.pcb_fcsr0;
		mcp->mc_fpregs.fp_flags = curpcb->pcb_fpflags;
		mcp->mc_flags |= _MC_FP_VALID;
	}

	critical_exit();
}

static void
set_fpcontext(struct thread *td, mcontext_t *mcp)
{
	struct pcb *curpcb;

	critical_enter();

	if ((mcp->mc_flags & _MC_FP_VALID) != 0) {
		curpcb = curthread->td_pcb;
		/* FPE usage is enabled, override registers. */
		memcpy(curpcb->pcb_fregs, mcp->mc_fpregs.fp_regs,
		    sizeof(mcp->mc_fpregs.fp_regs));
		curpcb->uf.pcb_fcsr0 = mcp->mc_fpregs.fp_fcsr;
		curpcb->pcb_fpflags = mcp->mc_fpregs.fp_flags & PCB_FP_USERMASK;
		/* Enable FPU for user mode */
		td->td_frame->tf_euen |= CSR_EUEN_FPEN;
	}

	critical_exit();
}

int
sys_sigreturn(struct thread *td, struct sigreturn_args *uap)
{
	ucontext_t uc;
	int error;

	if (copyin(uap->sigcntxp, &uc, sizeof(uc)))
		return (EFAULT);

	error = set_mcontext(td, &uc.uc_mcontext);
	if (error != 0)
		return (error);

	/* Restore signal mask. */
	kern_sigprocmask(td, SIG_SETMASK, &uc.uc_sigmask, NULL, 0);

	return (EJUSTRETURN);
}

void
sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask)
{
	struct sigframe *fp, frame;
	struct sysentvec *sysent;
	struct trapframe *tf;
	struct sigacts *psp;
	struct thread *td;
	struct proc *p;
	int onstack;
	int sig;

	td = curthread;
	p = td->td_proc;
	PROC_LOCK_ASSERT(p, MA_OWNED);

	sig = ksi->ksi_signo;
	psp = p->p_sigacts;
	mtx_assert(&psp->ps_mtx, MA_OWNED);

	tf = td->td_frame;
	onstack = sigonstack(tf->tf_regs[3]);

	CTR4(KTR_SIG, "sendsig: td=%p (%s) catcher=%p sig=%d", td, p->p_comm,
	    catcher, sig);

	/* Allocate and validate space for the signal handler context. */
	if ((td->td_pflags & TDP_ALTSTACK) != 0 && !onstack &&
	    SIGISMEMBER(psp->ps_sigonstack, sig)) {
		fp = (struct sigframe *)((uintptr_t)td->td_sigstk.ss_sp +
		    td->td_sigstk.ss_size);
	} else {
		fp = (struct sigframe *)td->td_frame->tf_regs[3];
	}

	/* Make room, keeping the stack aligned */
	fp--;
	fp = (struct sigframe *)STACKALIGN(fp);

	/* Fill in the frame to copy out */
	bzero(&frame, sizeof(frame));
	get_mcontext(td, &frame.sf_uc.uc_mcontext, 0);
	frame.sf_si = ksi->ksi_info;
	frame.sf_uc.uc_sigmask = *mask;
	frame.sf_uc.uc_stack = td->td_sigstk;
	frame.sf_uc.uc_stack.ss_flags = (td->td_pflags & TDP_ALTSTACK) != 0 ?
	    (onstack ? SS_ONSTACK : 0) : SS_DISABLE;
	mtx_unlock(&psp->ps_mtx);
	PROC_UNLOCK(td->td_proc);

	/* Copy the sigframe out to the user's stack. */
	if (copyout(&frame, fp, sizeof(*fp)) != 0) {
		/* Process has trashed its stack. Kill it. */
		CTR2(KTR_SIG, "sendsig: sigexit td=%p fp=%p", td, fp);
		PROC_LOCK(p);
		sigexit(td, SIGILL);
	}

	tf->tf_regs[4] = sig;
	tf->tf_regs[5] = (register_t)&fp->sf_si;
	tf->tf_regs[6] = (register_t)&fp->sf_uc;

	/*
	 * Set up the register arguments for the signal handler.
	 * According to LoongArch psABI:
	 * - a0 (r4): signal number
	 * - a1 (r5): pointer to siginfo
	 * - a2 (r6): pointer to ucontext
	 * - era: signal handler address (catcher)
	 * - ra (r1): sigreturn trampoline address
	 * - sp (r3): signal frame pointer
	 */
	sysent = p->p_sysent;
	if (PROC_HAS_SHP(p)) {
		tf->tf_ra = (register_t)PROC_SIGCODE(p);
		if (tf->tf_ra >= VM_MAXUSER_ADDRESS) {
			printf("sendsig: PROC_SIGCODE=%lx invalid, killing pid %d\n",
			    tf->tf_ra, p->p_pid);
			PROC_LOCK(p);
			sigexit(td, SIGILL);
		}
	} else
		tf->tf_ra = (register_t)(PROC_PS_STRINGS(p) -
		    *(sysent->sv_szsigcode));

	tf->tf_sp = (register_t)fp;
	tf->tf_era = (register_t)catcher;

	CTR3(KTR_SIG, "sendsig: return td=%p pc=%#x sp=%#x", td, tf->tf_regs[1],
	    tf->tf_regs[3]);

	PROC_LOCK(p);
	mtx_lock(&psp->ps_mtx);
}
