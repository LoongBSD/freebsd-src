/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
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

/*
 * Loongson CPU Interrupt Controller driver
 * Supports both FDT and ACPI bindings.
 */

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/types.h>
#include <sys/cpu.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/cpuset.h>
#include <machine/intr.h>
#include <machine/smp.h>
#include <machine/loongarchreg.h>

#ifdef FDT
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>
#endif

#include "pic_if.h"
#include <machine/eiointc_var.h>

struct loongarch_timer_softc;
extern struct loongarch_timer_softc *loongarch_timer_sc;
extern void loongarch_timer_intr(void *arg);

#define	CPUINTC_MAX_IRQS	256

/*
 * EIOINTC register offsets for extended I/O interrupt routing.
 * These are used for mapping CPU interrupts to EIOINTC nodes.
 */
#define	EIOINTC_REG_NODEMAP	0x14a0
#define	EIOINTC_REG_IPMAP	0x14c0
#define	EIOINTC_REG_ENABLE	0x1600
#define	EIOINTC_REG_BOUNCE	0x1680
#define	EIOINTC_REG_ISR		0x1800
#define	EIOINTC_REG_ROUTE	0x1c00

#define	VEC_REG_COUNT		4
#define	VEC_COUNT_PER_REG	64
#define	VEC_COUNT		(VEC_REG_COUNT * VEC_COUNT_PER_REG)
#define	VEC_REG_IDX(irq_id)	((irq_id) / VEC_COUNT_PER_REG)
#define	VEC_REG_BIT(irq_id)	((irq_id) % VEC_COUNT_PER_REG)
#define	EIOINTC_ALL_ENABLE	0xffffffff

/*
 * Maximum number of EIOINTC nodes.
 * Each node can handle interrupts for CORES_PER_EIO_NODE CPUs.
 */
#define	MAX_EIO_NODES		(MAXCPU / CORES_PER_EIO_NODE)

struct cpuintc_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
	/* EIOINTC node and vector count for interrupt routing */
	uint32_t		node;
	uint32_t		vec_count;
};

struct cpuintc_softc {
	device_t		dev;
	device_t		parent;
	struct resource		*intc_res;
	struct cpuintc_irqsrc	isrcs[CPUINTC_MAX_IRQS];
#ifdef SMP
	struct intr_irqsrc	ipi_isrcs[INTR_IPI_COUNT];
#endif
};

static int	cpuintc_probe(device_t dev);
static int	cpuintc_attach(device_t dev);

static void	cpuintc_disable_intr(device_t dev, struct intr_irqsrc *isrc);
static void	cpuintc_enable_intr(device_t dev, struct intr_irqsrc *isrc);
static int	cpuintc_map_intr(device_t dev, struct intr_map_data *data,
		    struct intr_irqsrc **isrcp);
static void	cpuintc_pre_ithread(device_t dev, struct intr_irqsrc *isrc);
static void	cpuintc_post_ithread(device_t dev, struct intr_irqsrc *isrc);
static void	cpuintc_post_filter(device_t dev, struct intr_irqsrc *isrc);
static int	cpuintc_setup_intr(device_t dev, struct intr_irqsrc *isrc,
		    struct resource *res, struct intr_map_data *data);
static int	cpuintc_bind_intr(device_t dev, struct intr_irqsrc *isrc);

static int	cpuintc_intr(void *arg);

#ifdef SMP
static void	cpuintc_init_secondary(device_t dev, uint32_t rootnum);
static void	cpuintc_ipi_send(device_t dev, struct intr_irqsrc *isrc,
		    cpuset_t cpus, u_int ipi);
static int	cpuintc_ipi_setup(device_t dev, u_int ipi,
		    struct intr_irqsrc **isrcp);
#endif

static device_method_t cpuintc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cpuintc_probe),
	DEVMETHOD(device_attach,	cpuintc_attach),

	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,	cpuintc_disable_intr),
	DEVMETHOD(pic_enable_intr,	cpuintc_enable_intr),
	DEVMETHOD(pic_map_intr,	cpuintc_map_intr),
	DEVMETHOD(pic_pre_ithread,	cpuintc_pre_ithread),
	DEVMETHOD(pic_post_ithread,	cpuintc_post_ithread),
	DEVMETHOD(pic_post_filter,	cpuintc_post_filter),
	DEVMETHOD(pic_setup_intr,	cpuintc_setup_intr),
	DEVMETHOD(pic_bind_intr,	cpuintc_bind_intr),
#ifdef SMP
	DEVMETHOD(pic_init_secondary,	cpuintc_init_secondary),
	DEVMETHOD(pic_ipi_send,		cpuintc_ipi_send),
	DEVMETHOD(pic_ipi_setup,	cpuintc_ipi_setup),
#endif

	DEVMETHOD_END
};

static driver_t cpuintc_driver = {
	"cpuintc",
	cpuintc_methods,
	sizeof(struct cpuintc_softc),
};

/* FDT version attaches to ofwbus */
#ifdef FDT
EARLY_DRIVER_MODULE(cpuintc, ofwbus, cpuintc_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_FIRST);
#endif

/* ACPI version attaches to root and nexus */
#ifdef DEV_ACPI
EARLY_DRIVER_MODULE(cpuintc, root, cpuintc_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_FIRST);
EARLY_DRIVER_MODULE(cpuintc, nexus, cpuintc_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_FIRST);
#endif

static int
cpuintc_probe(device_t dev)
{
#ifdef DEV_ACPI
	ACPI_HANDLE h;
#endif

#ifdef FDT
	/* Try FDT binding first */
	if (ofw_bus_status_okay(dev)) {
		if (ofw_bus_is_compatible(dev, "loongson,cpu-interrupt-controller")) {
			device_set_desc(dev, "Loongson CPU Interrupt Controller");
			return (BUS_PROBE_DEFAULT);
		}
	}
#endif

#ifdef DEV_ACPI
	/* Try ACPI binding */
	if (acpi_disabled("acpi"))
		return (ENXIO);

	h = acpi_get_handle(dev);
	if (h != NULL) {
		/* ACPI-enumerated device */
		device_set_desc(dev, "Loongson CPU Interrupt Controller (ACPI)");
		return (BUS_PROBE_DEFAULT);
	}

	/*
	 * When attached as a nexus child (not ACPI-enumerated),
	 * still probe successfully if we are the CPU interrupt controller.
	 */
	if (device_get_unit(dev) == 0 &&
	    strcmp(device_get_name(dev), "cpuintc") == 0) {
		device_set_desc(dev, "Loongson CPU Interrupt Controller (ACPI)");
		return (BUS_PROBE_DEFAULT);
	}
#endif
	return (ENXIO);
}

static int
cpuintc_intr(void *arg)
{
	struct cpuintc_softc *sc;
	struct trapframe *tf;
	int active_irq;
	int val;
	int rc;

	sc = (struct cpuintc_softc *)arg;
	tf = curthread->td_intr_frame;

	/* If estat is 0, there's no actual interrupt pending */
	if (tf->tf_estat == 0)
		return (FILTER_HANDLED);

	/* Extract IRQ number from estat */
	val = tf->tf_estat & ((1 << IRQ_NMI) - 1);

	/* Process all pending interrupts */
	while (val != 0) {
		/* Find the highest priority pending interrupt */
		active_irq = -1;
		for (int i = IRQ_IPI; i >= 0; i--) {
			if ((1 << i) & val) {
				active_irq = i;
				break;
			}
		}

		if (active_irq < 0)
			break;

		/* Clear this bit from val so we don't process it again */
		val &= ~(1 << active_irq);

		/* Acknowledge the interrupt source */
		int handled_by_child = 0;
		switch (active_irq) {
		case IRQ_TI:
			/*
			 * Timer interrupt - clear TINTCLR first to avoid interrupt
			 * storm, then dispatch directly via the timer softc callback.
			 * This is necessary because the timer handler is registered
			 * via the legacy intr_machdep.c mechanism, not through the
			 * cpuintc PIC driver's intr_isrc_dispatch path.
			 */
			csr_write32(1, LOONGARCH_CSR_TINTCLR);
			if (loongarch_timer_sc != NULL)
				loongarch_timer_intr(loongarch_timer_sc);
			handled_by_child = 1;
			break;
		case IRQ_HWI0: case IRQ_HWI1: case IRQ_HWI2: case IRQ_HWI3:
		case IRQ_HWI4: case IRQ_HWI5: case IRQ_HWI6: case IRQ_HWI7:
			/*
			 * EIOINTC cascade interrupt.
			 * Don't clear the EIOINTC ISR here - let eiointc_child_intr
			 * handle the clearing and dispatching.
			 * Just dispatch to eiointc to handle the actual interrupts.
			 */
			{
				struct eiointc_softc *eiointc_sc;

				eiointc_sc = eiointc_get_softc();
				if (eiointc_sc != NULL) {
					eiointc_child_intr(eiointc_sc, active_irq);
					handled_by_child = 1;
				}
			}
			break;
#ifdef SMP
		case IRQ_IPI:
			{
				uint32_t ipi_reg, bit;
				u_int ipi;

				ipi_reg = iocsr_read32(
				    LOONGARCH_IOCSR_IPI_STATUS);
				if (ipi_reg != 0) {
					iocsr_write32(ipi_reg,
					    LOONGARCH_IOCSR_IPI_CLEAR);

					/* Process all pending IPIs */
					while ((bit = ffs(ipi_reg)) != 0) {
						ipi = bit - 1;
						ipi_reg &= ~(1u << ipi);
						intr_ipi_dispatch(ipi);
					}
				}
				handled_by_child = 1;
			}
			break;
#endif
		default:
			break;
		}

		/* Dispatch to registered interrupt sources (skip for HWI, handled by eiointc) */
		if (!handled_by_child && active_irq < CPUINTC_MAX_IRQS) {
			rc = intr_isrc_dispatch(&sc->isrcs[active_irq].isrc, tf);
			if (rc != 0 && active_irq != IRQ_TI)
				printf("cpuintc: stray irq %d estat=0x%lx rc=%d\n",
				    active_irq, (unsigned long)tf->tf_estat, rc);
		}
	}

	return (FILTER_HANDLED);
}

static int
cpuintc_attach(device_t dev)
{
	struct cpuintc_softc *sc;
	struct cpuintc_irqsrc *isrcs;
	const char *name;
	uintptr_t xref;
	u_int irq;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;

#ifdef FDT
	if (ofw_bus_status_okay(dev)) {
		phandle_t node;
		node = ofw_bus_get_node(dev);
		xref = OF_xref_from_node(node);
	} else
#endif
	{
		xref = ACPI_INTR_XREF;
	}

	/* Register the interrupt sources. */
	isrcs = sc->isrcs;
	name = device_get_nameunit(sc->dev);
	for (irq = 0; irq < CPUINTC_MAX_IRQS; irq++) {
		isrcs[irq].irq = irq;
		error = intr_isrc_register(&isrcs[irq].isrc, sc->dev,
		    0, "%s,%u", name, irq);
		if (error != 0) {
			device_printf(dev, "could not register irq %u: %d\n",
			    irq, error);
			return (error);
		}
	}

	/* Register ourself as an interrupt controller. */
	if (intr_pic_register(dev, xref) == NULL) {
		device_printf(dev, "cannot register PIC\n");
		return (ENXIO);
	}

#ifdef FDT
	if (ofw_bus_status_okay(dev)) {
		phandle_t node;
		node = ofw_bus_get_node(dev);
		OF_device_register_xref(OF_xref_from_node(node), dev);
	}
#endif

	/* Claim our root controller role. */
	if (intr_pic_claim_root(dev, xref, cpuintc_intr, sc, 0) != 0) {
		device_printf(dev, "could not set PIC as root\n");
		intr_pic_deregister(dev, xref);
		return (ENXIO);
	}

#ifdef SMP
	error = intr_ipi_pic_register(dev, 0);
	if (error != 0) {
		device_printf(dev, "could not register for IPIs\n");
		return (error);
	}
#endif

	return (0);
}

static void
cpuintc_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct cpuintc_irqsrc *cpuisrc;
	u_int irq;

	cpuisrc = (struct cpuintc_irqsrc *)isrc;
	irq = cpuisrc->irq;

	/*
	 * Disable the CPU local interrupt by clearing the corresponding
	 * bit in the ESTAT (Exception Status) enable mask.
	 * CPU interrupts 0-7 are handled by the CPU interrupt controller.
	 */
	if (irq < 8) {
		uint32_t ecfg;

		ecfg = csr_read32(LOONGARCH_CSR_ECFG);
		ecfg &= ~(1 << irq);
		csr_write32(ecfg, LOONGARCH_CSR_ECFG);
	}
}

static void
cpuintc_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct cpuintc_irqsrc *cpuisrc;
	u_int irq;

	cpuisrc = (struct cpuintc_irqsrc *)isrc;
	irq = cpuisrc->irq;

	/*
	 * Enable the CPU local interrupt by setting the corresponding
	 * bit in the ECFG (Exception Configuration) register.
	 * CPU interrupts 0-7 are handled by the CPU interrupt controller.
	 */
	if (irq < 8) {
		uint32_t ecfg;

		ecfg = csr_read32(LOONGARCH_CSR_ECFG);
		ecfg |= (1 << irq);
		csr_write32(ecfg, LOONGARCH_CSR_ECFG);
	}
}

static int
cpuintc_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct cpuintc_softc *sc;
	u_int irq;

	sc = device_get_softc(dev);

	switch (data->type) {
#ifdef FDT
	case INTR_MAP_DATA_FDT: {
		struct intr_map_data_fdt *daf;
		daf = (struct intr_map_data_fdt *)data;
		if (daf->ncells < 1)
			return (EINVAL);
		irq = daf->cells[0];
		break;
	}
#endif
#ifdef DEV_ACPI
	case INTR_MAP_DATA_ACPI: {
		struct intr_map_data_acpi *daa;
		daa = (struct intr_map_data_acpi *)data;
		irq = daa->irq;
		break;
	}
#endif
	default:
		return (EINVAL);
	}

	if (irq >= CPUINTC_MAX_IRQS)
		return (EINVAL);

	*isrcp = &sc->isrcs[irq].isrc;
	return (0);
}

static void
cpuintc_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	/* Pre-ithread handling */
}

static void
cpuintc_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	/* Post-ithread handling */
}

static void
cpuintc_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	/* Post-filter handling */
}

static int
cpuintc_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	return (0);
}

static int
cpuintc_bind_intr(device_t dev, struct intr_irqsrc *isrc)
{
	return (0);
}

#ifdef SMP
static void
cpuintc_init_secondary(device_t dev, uint32_t rootnum)
{
	struct cpuintc_softc *sc;
	struct intr_irqsrc *isrc;
	u_int cpu, irq;

	sc = device_get_softc(dev);
	cpu = PCPU_GET(cpuid);

	/* Enable interrupts that were initialized on this CPU */
	for (irq = 0; irq < CPUINTC_MAX_IRQS; irq++) {
		isrc = &sc->isrcs[irq].isrc;
		if (intr_isrc_init_on_cpu(isrc, cpu))
			cpuintc_enable_intr(dev, isrc);
	}
}

static void
cpuintc_ipi_send(device_t dev, struct intr_irqsrc *isrc, cpuset_t cpus,
    u_int ipi)
{
	struct pcpu *pcpu_data;
	u_int cpu;

	CPU_FOREACH(cpu) {
		if (!CPU_ISSET(cpu, &cpus))
			continue;

		pcpu_data = cpuid_to_pcpu[cpu];
		if (pcpu_data == NULL)
			continue;

		iocsr_write32(
		    ((u_int)pcpu_data->pc_hart << IOCSR_IPI_SEND_CPU_SHIFT) |
		    (1U << ipi) | IOCSR_IPI_SEND_BLOCKING,
		    LOONGARCH_IOCSR_IPI_SEND);
	}
}

static int
cpuintc_ipi_setup(device_t dev, u_int ipi, struct intr_irqsrc **isrcp)
{
	struct cpuintc_softc *sc;

	sc = device_get_softc(dev);

	if (ipi >= INTR_IPI_COUNT)
		return (ENOSPC);

	*isrcp = &sc->ipi_isrcs[ipi];
	return (0);
}
#endif
