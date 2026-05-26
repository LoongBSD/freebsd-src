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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/limits.h>
#include <sys/proc.h>
#include <sys/sf_buf.h>
#include <sys/signal.h>
#include <sys/unistd.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/uma.h>
#include <vm/uma_int.h>

#include <machine/loongarchreg.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/pcb.h>
#include <machine/frame.h>

#define	TP_OFFSET	16	/* sizeof(struct tcb) */

void
cpu_fork(struct thread *td1, struct proc *p2, struct thread *td2, int flags)
{
	struct pcb *pcb2;
	struct trapframe *tf;

	if ((flags & RFPROC) == 0)
		return;

	pcb2 = td2->td_pcb;
	tf = td2->td_frame;
	KASSERT(pcb2 != NULL, ("cpu_fork: td_pcb not initialized"));
	KASSERT(tf != NULL, ("cpu_fork: td_frame not initialized"));

	bcopy(td1->td_pcb, pcb2, sizeof(*pcb2));
	bcopy(td1->td_frame, tf, sizeof(*tf));

	tf->tf_t[0] = 0;
	tf->tf_a[0] = 0;
	tf->tf_a[1] = 0;

	tf->tf_crmd = CSR_CRMD_PG | CSR_CRMD_IE | PLV_USER;
	tf->tf_prmd = CSR_PRMD_PIE | (PLV_USER << CSR_PRMD_PPLV_SHIFT);
	tf->tf_euen = 0;
	tf->tf_ecfg = ECFGF_TIMER | ECFGF_IPI | ECFGF(3);

	pcb2->pcb_s[0] = (uintptr_t)fork_return;
	pcb2->pcb_s[1] = (uintptr_t)td2;
	pcb2->pcb_ra = (uintptr_t)fork_trampoline;
	pcb2->pcb_sp = (uintptr_t)tf;

	td2->td_md.md_spinlock_count = 1;
	td2->td_md.md_saved_crmd_ie = CSR_CRMD_IE;
	td2->td_critnest = 1;
}

typedef void (*cpu_reset_func_t)(void);
static cpu_reset_func_t cpu_reset_func = NULL;

void
cpu_reset(void)
{

	if (cpu_reset_func != NULL) {
		cpu_reset_func();
		printf("cpu_reset: platform reset function returned, trying fallback\n");
	}

	printf("cpu_reset: No reset mechanism available, halting CPU\n");

	intr_disable();
	while(1)
		__asm __volatile("idle 0" ::: "memory");
}

void
cpu_set_reset_func(cpu_reset_func_t func)
{

	cpu_reset_func = func;
}

void
cpu_set_syscall_retval(struct thread *td, int error)
{
	struct trapframe *frame;

	frame = td->td_frame;

	if (__predict_true(error == 0)) {
		frame->tf_a[0] = td->td_retval[0];
		frame->tf_a[1] = td->td_retval[1];
		frame->tf_t[0] = 0;
		return;
	}

	switch (error) {
	case ERESTART:
		frame->tf_era -= 4;
		break;
	case EJUSTRETURN:
		break;
	default:
		frame->tf_a[0] = error;
		frame->tf_t[0] = 1;
		break;
	}
}

void
cpu_copy_thread(struct thread *td, struct thread *td0)
{

	bcopy(td0->td_frame, td->td_frame, sizeof(struct trapframe));
	bcopy(td0->td_pcb, td->td_pcb, sizeof(struct pcb));

	td->td_pcb->pcb_s[0] = (uintptr_t)fork_return;
	td->td_pcb->pcb_s[1] = (uintptr_t)td;
	td->td_pcb->pcb_ra = (uintptr_t)fork_trampoline;
	td->td_pcb->pcb_sp = (uintptr_t)td->td_frame;

	td->td_md.md_spinlock_count = 1;
	td->td_md.md_saved_crmd_ie = CSR_CRMD_IE;
	td->td_critnest = 1;
}

int
cpu_set_upcall(struct thread *td, void (*entry)(void *), void *arg,
    stack_t *stack)
{
	struct trapframe *tf;

	tf = td->td_frame;

	tf->tf_sp = STACKALIGN((uintptr_t)stack->ss_sp + stack->ss_size);
	tf->tf_era = (register_t)entry;
	tf->tf_a[0] = (register_t)arg;

	return (0);
}

int
cpu_set_user_tls(struct thread *td, void *tls_base, int flags __unused)
{

	if ((uintptr_t)tls_base >= VM_MAXUSER_ADDRESS)
		return (EINVAL);

	td->td_frame->tf_tp = (register_t)tls_base + TP_OFFSET;

	return (0);
}

void
cpu_thread_exit(struct thread *td)
{
}

void
cpu_thread_alloc(struct thread *td)
{

	KASSERT(td != NULL, ("cpu_thread_alloc: td is NULL"));
	KASSERT(td->td_kstack != 0, ("cpu_thread_alloc: td_kstack is NULL"));

	td->td_pcb = (struct pcb *)(td->td_kstack +
	    td->td_kstack_pages * PAGE_SIZE) - 1;
	td->td_frame = (struct trapframe *)STACKALIGN(
	    (caddr_t)td->td_pcb - 8 - sizeof(struct trapframe));
}

void
cpu_thread_free(struct thread *td)
{
}

void
cpu_thread_clean(struct thread *td)
{
}

void
cpu_fork_kthread_handler(struct thread *td, void (*func)(void *), void *arg)
{
	struct pcb *pcb;

	pcb = td->td_pcb;
	KASSERT(pcb != NULL, ("cpu_fork_kthread_handler: td_pcb not initialized"));
	KASSERT(td->td_frame != NULL,
	    ("cpu_fork_kthread_handler: td_frame not initialized"));

	pcb->pcb_s[0] = (uintptr_t)func;
	pcb->pcb_s[1] = (uintptr_t)arg;
	pcb->pcb_ra = (uintptr_t)fork_trampoline;
	pcb->pcb_sp = (uintptr_t)td->td_frame;

	td->td_frame->tf_era = (uintptr_t)fork_trampoline;

	pcb->pcb_euen = 0;

	__asm __volatile("csrrd %0, %1" : "=r"(td->td_frame->tf_crmd) : "i"(LOONGARCH_CSR_CRMD));
	__asm __volatile("csrrd %0, %1" : "=r"(td->td_frame->tf_prmd) : "i"(LOONGARCH_CSR_PRMD));

	__asm __volatile("move %0, $r21" : "=r"(td->td_frame->tf_regs[21]));

	pcb->pcb_fpflags = 0;
	td->td_frame->tf_euen = 0;
	pcb->pcb_euen = 0;
}

void
cpu_update_pcb(struct thread *td)
{
}

void
cpu_exit(struct thread *td)
{
}

bool
cpu_exec_vmspace_reuse(struct proc *p __unused, vm_map_t map __unused)
{

	return (true);
}

int
cpu_procctl(struct thread *td __unused, int idtype __unused, id_t id __unused,
    int com __unused, void *data __unused)
{

	return (EINVAL);
}

void
cpu_sync_core(void)
{
	flush_icache();
}
