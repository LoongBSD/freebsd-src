/*-
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/module.h>
#include <sys/interrupt.h>

#include <machine/bus.h>
#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/frame.h>
#include <machine/intr.h>

/* External timer functions */
extern struct loongarch_timer_softc *loongarch_timer_sc;
extern void loongarch_timer_intr(void *arg);

struct intc_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
};

struct intc_irqsrc isrcs[INTC_NIRQS];

static void
loongarch_mask_irq(void *source)
{
	int irq;
	uint32_t value;

	irq = (int)(uintptr_t)source;

	if (irq < 0 || irq >= INTC_NIRQS)
		return;

	value = csr_read32(LOONGARCH_CSR_ECFG);

	switch (irq) {
	case IRQ_SWI0:
	case IRQ_SWI1:
	case IRQ_HWI0:
	case IRQ_HWI1:
	case IRQ_HWI2:
	case IRQ_HWI3:
	case IRQ_HWI4:
	case IRQ_HWI5:
	case IRQ_HWI6:
	case IRQ_HWI7:
	case IRQ_PCOV:
	case IRQ_TI:
	case IRQ_IPI:
		value &= ~(1 << irq);
		csr_write32(value, LOONGARCH_CSR_ECFG);
		break;

	default:
		panic("Unknown irq %d\n", irq);
	}
}

static void
loongarch_unmask_irq(void *source)
{
	int irq;
	uint32_t value;

	irq = (int)(uintptr_t)source;

	if (irq < 0 || irq >= INTC_NIRQS)
		return;

	value = csr_read32(LOONGARCH_CSR_ECFG);

	switch (irq) {
	case IRQ_SWI0:
	case IRQ_SWI1:
	case IRQ_HWI0:
	case IRQ_HWI1:
	case IRQ_HWI2:
	case IRQ_HWI3:
	case IRQ_HWI4:
	case IRQ_HWI5:
	case IRQ_HWI6:
	case IRQ_HWI7:
	case IRQ_PCOV:
	case IRQ_TI:
	case IRQ_IPI:
		value |= (1 << irq);
		csr_write32(value, LOONGARCH_CSR_ECFG);
		break;

	default:
		panic("Unknown irq %d\n", irq);
	}
}

int
loongarch_setup_intr(const char *name, driver_filter_t *filt,
    void (*handler)(void*), void *arg, int irq, int flags, void **cookiep)
{
	struct intr_irqsrc *isrc;
	int error;

	if (irq < 0 || irq >= INTC_NIRQS)
		panic("%s: unknown intr %d", __func__, irq);

	isrc = &isrcs[irq].isrc;
	if (isrc->isrc_event == NULL) {
		/* Pass IRQ number as source, not isrc pointer */
		error = intr_event_create(&isrc->isrc_event, (void *)(uintptr_t)irq, 0, irq,
		    loongarch_mask_irq, loongarch_unmask_irq, NULL, NULL, "int%d", irq);
		if (error)
			return (error);
		loongarch_unmask_irq((void*)(uintptr_t)irq);
	} else {
	}

	error = intr_event_add_handler(isrc->isrc_event, name,
	    filt, handler, arg, intr_priority(flags), flags, cookiep);
	if (error) {
		return (error);
	}

	return (0);
}

int
loongarch_teardown_intr(void *ih)
{

	/* TODO */

	return (0);
}

/* extract irq num */
static inline int get_irq(uint32_t estat)
{
	int ret = IRQ_IPI;
	int val = estat & ((1 << IRQ_NMI) - 1);

	while ((((1 << ret) & val) == 0) && ret > 0)
		ret--;
	
	return ret;
}

void
loongarch_cpu_intr(struct trapframe *frame)
{
	int active_irq;

	/* If estat is 0, there's no actual interrupt pending - just return */
	if (frame->tf_estat == 0) {
		/* Spurious interrupt - return without calling handler */
		return;
	}

	active_irq = get_irq(frame->tf_estat);

	switch (active_irq) {
	case IRQ_SWI0:
	case IRQ_SWI1:
		/* Software interrupts - acknowledge and continue */
		break;
	case IRQ_TI:
		/*
		 * Timer interrupt - always clear the interrupt source first
		 * to avoid interrupt storm when timer softc is not yet
		 * initialized (loongarch_timer_sc == NULL).
		 */
		csr_write32(1, LOONGARCH_CSR_TINTCLR);
		if (loongarch_timer_sc != NULL) {
			loongarch_timer_intr(loongarch_timer_sc);
		}
		break;
	case IRQ_HWI0:
	case IRQ_HWI1:
	case IRQ_HWI2:
	case IRQ_HWI3:
	case IRQ_HWI4:
	case IRQ_HWI5:
	case IRQ_HWI6:
	case IRQ_HWI7:
		/*
		 * External hardware interrupts routed through EIOINTC.
		 * During early boot, EIOINTC driver may not be initialized yet.
		 * Read and clear EIOINTC ISR to prevent interrupt storm.
		 * The actual interrupt dispatch will happen once the EIOINTC
		 * driver is attached and registered as a child controller.
		 */
		{
			int i;
			uint64_t pending;
			/* EIOINTC ISR base address via IOCSR */
			for (i = 0; i < 4; i++) {
				pending = iocsr_read64(0x1800 + (i << 3));
				if (pending != 0)
					iocsr_write64(pending, 0x1800 + (i << 3));
			}
		}
		break;
	case IRQ_PCOV:
		/* Performance counter overflow */
		break;
	case IRQ_IPI:
		/* IPI - should not happen in UP mode */
		break;
	default:
		break;
	}
}

/* Interrupt machdep initialization routine. */
static void
intc_init(void *dummy __unused)
{
	int error;
	int i;

	for (i = 0; i < INTC_NIRQS; i++) {
		isrcs[i].irq = i;
		error = intr_isrc_register(&isrcs[i].isrc, NULL,
		    0, "intc,%u", i);
		if (error != 0)
			printf("Can't register interrupt %d\n", i);
	}
}

SYSINIT(intc_init, SI_SUB_INTR, SI_ORDER_MIDDLE, intc_init, NULL);
