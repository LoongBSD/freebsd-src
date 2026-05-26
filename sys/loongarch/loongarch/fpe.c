/*-
 * Copyright (c) 2024 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2024 Xiaoqiang Zhao <zxq_yx_007@163.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 * All rights reserved.
 *
 * This software was developed by the University of Cambridge Computer
 * Laboratory (Department of Computer Science and Technology) under Innovate
 * UK project 105694, "Digital Security by Design (DSbD) Technology Platform
 * Prototype".
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include <vm/uma.h>

#include <machine/fpe.h>
#include <machine/fpu.h>
#include <machine/pcb.h>
#include <machine/frame.h>
#include <machine/reg.h>
#include <machine/loongarchreg.h>

MALLOC_DEFINE(M_FPUKERN_CTX, "fpu_ctx", "FPU kernel context");

static uma_zone_t fpu_save_area_zone;
static struct fpreg *fpu_initialstate;

void
fpe_enable(void)
{

	write_csr_euen(read_csr_euen() | CSR_EUEN_FPEN);
}

void
fpe_disable(void)
{

	write_csr_euen(read_csr_euen() & ~CSR_EUEN_FPEN);
}

void
fpe_store(struct fpreg *regs)
{
	uint64_t fcsr;

	__asm __volatile(
	    "fst.d	$fa0, %1, 0	\n"
	    "fst.d	$fa1, %1, 8	\n"
	    "fst.d	$fa2, %1, 16	\n"
	    "fst.d	$fa3, %1, 24	\n"
	    "fst.d	$fa4, %1, 32	\n"
	    "fst.d	$fa5, %1, 40	\n"
	    "fst.d	$fa6, %1, 48	\n"
	    "fst.d	$fa7, %1, 56	\n"
	    "fst.d	$ft0, %1, 64	\n"
	    "fst.d	$ft1, %1, 72	\n"
	    "fst.d	$ft2, %1, 80	\n"
	    "fst.d	$ft3, %1, 88	\n"
	    "fst.d	$ft4, %1, 96	\n"
	    "fst.d	$ft5, %1, 104	\n"
	    "fst.d	$ft6, %1, 112	\n"
	    "fst.d	$ft7, %1, 120	\n"
	    "fst.d	$ft8, %1, 128	\n"
	    "fst.d	$ft9, %1, 136	\n"
	    "fst.d	$ft10, %1, 144	\n"
	    "fst.d	$ft11, %1, 152	\n"
	    "fst.d	$ft12, %1, 160	\n"
	    "fst.d	$ft13, %1, 168	\n"
	    "fst.d	$ft14, %1, 176	\n"
	    "fst.d	$ft15, %1, 184	\n"
	    "fst.d	$fs0, %1, 192	\n"
	    "fst.d	$fs1, %1, 200	\n"
	    "fst.d	$fs2, %1, 208	\n"
	    "fst.d	$fs3, %1, 216	\n"
	    "fst.d	$fs4, %1, 224	\n"
	    "fst.d	$fs5, %1, 232	\n"
	    "fst.d	$fs6, %1, 240	\n"
	    "fst.d	$fs7, %1, 248	\n"
	    "movfcsr2gr %0, $fcsr0	\n"
	    : "=&r"(fcsr)
	    : "r"(regs->fp_regs)
	    : "memory");

	regs->fp_regs[32] = fcsr;
}

void
fpe_restore(struct fpreg *regs)
{
	uint64_t fcsr;

	fcsr = regs->fp_regs[32];

	__asm __volatile(
	    "movgr2fcsr $fcsr0, %0	\n"
	    "fld.d	$fa0, %1, 0	\n"
	    "fld.d	$fa1, %1, 8	\n"
	    "fld.d	$fa2, %1, 16	\n"
	    "fld.d	$fa3, %1, 24	\n"
	    "fld.d	$fa4, %1, 32	\n"
	    "fld.d	$fa5, %1, 40	\n"
	    "fld.d	$fa6, %1, 48	\n"
	    "fld.d	$fa7, %1, 56	\n"
	    "fld.d	$ft0, %1, 64	\n"
	    "fld.d	$ft1, %1, 72	\n"
	    "fld.d	$ft2, %1, 80	\n"
	    "fld.d	$ft3, %1, 88	\n"
	    "fld.d	$ft4, %1, 96	\n"
	    "fld.d	$ft5, %1, 104	\n"
	    "fld.d	$ft6, %1, 112	\n"
	    "fld.d	$ft7, %1, 120	\n"
	    "fld.d	$ft8, %1, 128	\n"
	    "fld.d	$ft9, %1, 136	\n"
	    "fld.d	$ft10, %1, 144	\n"
	    "fld.d	$ft11, %1, 152	\n"
	    "fld.d	$ft12, %1, 160	\n"
	    "fld.d	$ft13, %1, 168	\n"
	    "fld.d	$ft14, %1, 176	\n"
	    "fld.d	$ft15, %1, 184	\n"
	    "fld.d	$fs0, %1, 192	\n"
	    "fld.d	$fs1, %1, 200	\n"
	    "fld.d	$fs2, %1, 208	\n"
	    "fld.d	$fs3, %1, 216	\n"
	    "fld.d	$fs4, %1, 224	\n"
	    "fld.d	$fs5, %1, 232	\n"
	    "fld.d	$fs6, %1, 240	\n"
	    "fld.d	$fs7, %1, 248	\n"
	    :
	    : "r"(fcsr), "r"(regs->fp_regs)
	    : "memory");
}

struct fpreg *
fpu_save_area_alloc(void)
{

	return (uma_zalloc(fpu_save_area_zone, M_WAITOK));
}

void
fpu_save_area_free(struct fpreg *fsa)
{

	uma_zfree(fpu_save_area_zone, fsa);
}

void
fpu_save_area_reset(struct fpreg *fsa)
{

	memcpy(fsa, fpu_initialstate, sizeof(*fsa));
}

static void
fpe_init(const void *dummy __unused)
{

	fpu_save_area_zone = uma_zcreate("FPE save area",
	    sizeof(struct fpreg), NULL, NULL, NULL, NULL,
	    _Alignof(struct fpreg) - 1, 0);
	fpu_initialstate = uma_zalloc(fpu_save_area_zone, M_WAITOK | M_ZERO);

	fpe_enable();
	fpe_store(fpu_initialstate);
	fpe_disable();

	bzero(fpu_initialstate->fp_regs, sizeof(fpu_initialstate->fp_regs));
}

SYSINIT(fpe, SI_SUB_CPU, SI_ORDER_ANY, fpe_init, NULL);

struct fpu_kern_ctx *
fpu_kern_alloc_ctx(u_int flags)
{

	return (malloc(sizeof(struct fpu_kern_ctx), M_FPUKERN_CTX,
	    ((flags & FPU_KERN_NOWAIT) != 0 ? M_NOWAIT : M_WAITOK) | M_ZERO));
}

void
fpu_kern_free_ctx(struct fpu_kern_ctx *ctx)
{

	KASSERT((ctx->flags & FPU_KERN_CTX_INUSE) == 0,
	    ("free'ing inuse ctx"));
	free(ctx, M_FPUKERN_CTX);
}

void
fpu_kern_enter(struct thread *td, struct fpu_kern_ctx *ctx, u_int flags)
{
	struct pcb *pcb;

	pcb = td->td_pcb;
	KASSERT((flags & FPU_KERN_NOCTX) != 0 || ctx != NULL,
	    ("ctx is required when !FPU_KERN_NOCTX"));

	if ((flags & FPU_KERN_NOCTX) != 0) {
		critical_enter();
		if (td->td_frame != NULL &&
		    (td->td_frame->tf_euen & CSR_EUEN_FPEN) != 0) {
			fpe_state_save(td);
			td->td_frame->tf_euen &= ~CSR_EUEN_FPEN;
		}
		fpe_enable();
		pcb->pcb_fpflags |= PCB_FP_KERN | PCB_FP_NOSAVE |
		    PCB_FP_STARTED;
		return;
	}

	if ((flags & FPU_KERN_KTHR) != 0 && is_fpu_kern_thread(0)) {
		ctx->flags = FPU_KERN_CTX_DUMMY | FPU_KERN_CTX_INUSE;
		return;
	}

	if (td->td_frame != NULL &&
	    (td->td_frame->tf_euen & CSR_EUEN_FPEN) != 0) {
		fpe_enable();
		fpe_store(&ctx->state);
	} else {
		memset(&ctx->state, 0, sizeof(ctx->state));
	}
	ctx->flags = FPU_KERN_CTX_INUSE;
	pcb->pcb_fpflags |= PCB_FP_KERN | PCB_FP_STARTED;
}

int
fpu_kern_leave(struct thread *td, struct fpu_kern_ctx *ctx)
{
	struct pcb *pcb;

	pcb = td->td_pcb;

	if ((pcb->pcb_fpflags & PCB_FP_NOSAVE) != 0) {
		KASSERT(ctx == NULL,
		    ("non-null ctx after FPU_KERN_NOCTX"));
		fpe_disable();
		pcb->pcb_fpflags &= ~(PCB_FP_NOSAVE | PCB_FP_STARTED |
		    PCB_FP_KERN);
		critical_exit();
		return (0);
	}

	KASSERT((ctx->flags & FPU_KERN_CTX_INUSE) != 0,
	    ("FPU context not inuse"));
	ctx->flags &= ~FPU_KERN_CTX_INUSE;

	if (is_fpu_kern_thread(0) &&
	    (ctx->flags & FPU_KERN_CTX_DUMMY) != 0)
		return (0);

	fpe_restore(&ctx->state);
	if (td->td_frame != NULL)
		td->td_frame->tf_euen |= CSR_EUEN_FPEN;
	pcb->pcb_fpflags &= ~(PCB_FP_STARTED | PCB_FP_KERN);

	return (0);
}

int
fpu_kern_thread(u_int flags __unused)
{
	struct pcb *pcb;

	pcb = curthread->td_pcb;
	KASSERT((curthread->td_pflags & TDP_KTHREAD) != 0,
	    ("Only kthread may use fpu_kern_thread"));
	KASSERT((pcb->pcb_fpflags & PCB_FP_KERN) == 0,
	    ("Thread already setup for the FPU"));
	pcb->pcb_fpflags |= PCB_FP_KERN;

	return (0);
}

int
is_fpu_kern_thread(u_int flags __unused)
{

	if ((curthread->td_pflags & TDP_KTHREAD) == 0)
		return (0);
	return ((curthread->td_pcb->pcb_fpflags & PCB_FP_KERN) != 0);
}
