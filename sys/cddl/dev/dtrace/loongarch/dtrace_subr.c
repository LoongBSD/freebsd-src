/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * Portions Copyright 2016-2018 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 *
 */
/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/kmem.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/dtrace_impl.h>
#include <sys/dtrace_bsd.h>
#include <cddl/dev/dtrace/dtrace_cddl.h>
#include <machine/vmparam.h>
#include <machine/loongarchreg.h>
#include <machine/clock.h>
#include <machine/frame.h>
#include <machine/trap.h>
#include <vm/pmap.h>

extern dtrace_id_t	dtrace_probeid_error;
extern int (*dtrace_invop_jump_addr)(struct trapframe *);
extern void dtrace_getnanotime(struct timespec *tsp);
extern void dtrace_getnanouptime(struct timespec *tsp);

int dtrace_invop(uintptr_t, struct trapframe *);
void dtrace_invop_init(void);
void dtrace_invop_uninit(void);

typedef struct dtrace_invop_hdlr {
	int (*dtih_func)(uintptr_t, struct trapframe *, uintptr_t);
	struct dtrace_invop_hdlr *dtih_next;
} dtrace_invop_hdlr_t;

dtrace_invop_hdlr_t *dtrace_invop_hdlr;

int
dtrace_invop(uintptr_t addr, struct trapframe *frame)
{
	struct thread *td;
	dtrace_invop_hdlr_t *hdlr;
	int rval;

	rval = 0;
	td = curthread;
	td->t_dtrace_trapframe = frame;
	for (hdlr = dtrace_invop_hdlr; hdlr != NULL; hdlr = hdlr->dtih_next)
		if ((rval = hdlr->dtih_func(addr, frame, 0)) != 0)
			break;
	td->t_dtrace_trapframe = NULL;
	return (rval);
}

void
dtrace_invop_add(int (*func)(uintptr_t, struct trapframe *, uintptr_t))
{
	dtrace_invop_hdlr_t *hdlr;

	hdlr = kmem_alloc(sizeof (dtrace_invop_hdlr_t), KM_SLEEP);
	hdlr->dtih_func = func;
	hdlr->dtih_next = dtrace_invop_hdlr;
	dtrace_invop_hdlr = hdlr;
}

void
dtrace_invop_remove(int (*func)(uintptr_t, struct trapframe *, uintptr_t))
{
	dtrace_invop_hdlr_t *hdlr, *prev;

	hdlr = dtrace_invop_hdlr;
	prev = NULL;

	for (;;) {
		if (hdlr == NULL)
			panic("attempt to remove non-existent invop handler");

		if (hdlr->dtih_func == func)
			break;

		prev = hdlr;
		hdlr = hdlr->dtih_next;
	}

	if (prev == NULL) {
		ASSERT(dtrace_invop_hdlr == hdlr);
		dtrace_invop_hdlr = hdlr->dtih_next;
	} else {
		ASSERT(dtrace_invop_hdlr != hdlr);
		prev->dtih_next = hdlr->dtih_next;
	}

	kmem_free(hdlr, 0);
}

/*ARGSUSED*/
void
dtrace_toxic_ranges(void (*func)(uintptr_t base, uintptr_t limit))
{

	(*func)(0, (uintptr_t)VM_MIN_KERNEL_ADDRESS);
}

/*
 * DTrace needs a high resolution time function which can
 * be called from a probe context and guaranteed not to have
 * instrumented with probes itself.
 *
 * Returns nanoseconds since boot.
 */
uint64_t
dtrace_gethrtime(void)
{
	struct timespec curtime;

	dtrace_getnanouptime(&curtime);

	return (curtime.tv_sec * 1000000000UL + curtime.tv_nsec);

}

uint64_t
dtrace_gethrestime(void)
{
	struct timespec current_time;

	dtrace_getnanotime(&current_time);

	return (current_time.tv_sec * 1000000000UL + current_time.tv_nsec);
}

/* Function to handle DTrace traps during probes. See loongarch/loongarch/trap.c */
int
dtrace_trap(struct trapframe *frame, u_int type)
{
	/*
	 * A trap can occur while DTrace executes a probe. Before
	 * executing the probe, DTrace blocks re-scheduling and sets
	 * a flag in its per-cpu flags to indicate that it doesn't
	 * want to fault. On returning from the probe, the no-fault
	 * flag is cleared and finally re-scheduling is enabled.
	 *
	 * Check if DTrace has enabled 'no-fault' mode:
	 */
	if ((cpu_core[curcpu].cpuc_dtrace_flags & CPU_DTRACE_NOFAULT) != 0) {
		/*
		 * There are only a couple of trap types that are expected.
		 * All the rest will be handled in the usual way.
		 */
		switch (type) {
		case EXCCODE_TLBL:
		case EXCCODE_TLBS:
		case EXCCODE_TLBI:
		case EXCCODE_ADE:
		case EXCCODE_ALE:
			/* Flag a bad address. */
			cpu_core[curcpu].cpuc_dtrace_flags |= CPU_DTRACE_BADADDR;
			cpu_core[curcpu].cpuc_dtrace_illval = frame->tf_badvaddr;

			/*
			 * Offset the instruction pointer to the instruction
			 * following the one causing the fault.
			 */
			frame->tf_era +=
			    dtrace_instr_size((uint8_t *)frame->tf_era);

			return (1);
		default:
			/* Handle all other traps in the usual way. */
			break;
		}
	}

	/* Handle the trap in the usual way. */
	return (0);
}

void
dtrace_probe_error(dtrace_state_t *state, dtrace_epid_t epid, int which,
    int fault, int fltoffs, uintptr_t illval)
{

	dtrace_probe(dtrace_probeid_error, (uint64_t)(uintptr_t)state,
	    (uintptr_t)epid,
	    (uintptr_t)which, (uintptr_t)fault, (uintptr_t)fltoffs);
}

static int
dtrace_invop_start(struct trapframe *frame)
{
	int invop;

	invop = dtrace_invop(frame->tf_era, frame);
	if (invop == 0)
		return (-1);

	/*
	 * LoongArch instruction handling for DTrace.
	 * TODO: Implement proper instruction matching for LoongArch.
	 * Currently just skip the instruction.
	 */
	frame->tf_era += 4;
	return (0);
}

void
dtrace_invop_init(void)
{

	dtrace_invop_jump_addr = dtrace_invop_start;
}

void
dtrace_invop_uninit(void)
{

	dtrace_invop_jump_addr = 0;
}
