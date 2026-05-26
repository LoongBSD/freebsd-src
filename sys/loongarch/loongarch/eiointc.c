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

/*
 * Loongson EIOINTC (Extended I/O Interrupt Controller) driver.
 * Common code + ACPI attach. FDT attach is in eiointc_fdt.c.
 */

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitset.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/types.h>
#include <sys/_cpuset.h>

#include <machine/intr.h>
#include <machine/loongarchreg.h>
#include <machine/eiointc_var.h>
#include <machine/madt_var.h>

#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>
#endif

#include "pic_if.h"

struct resource_spec eiointc_spec[] = {
	{ SYS_RES_IRQ,		0,	RF_ACTIVE | RF_SHAREABLE },
	RESOURCE_SPEC_END
};

struct eiointc_softc *eiointc_priv[MAX_IO_PICS];
int nr_pics;

pic_disable_intr_t eiointc_disable_intr;
pic_enable_intr_t eiointc_enable_intr;
pic_map_intr_t eiointc_map_intr;
pic_pre_ithread_t eiointc_pre_ithread;
pic_post_ithread_t eiointc_post_ithread;
pic_post_filter_t eiointc_post_filter;
pic_setup_intr_t eiointc_setup_intr;
pic_bind_intr_t eiointc_bind_intr;

static int
cpu_to_eio_node(int cpu)
{

	return (cpu / CORES_PER_EIO_NODE);
}

static int
eiointc_index(int node)
{
	int i;

	for (i = 0; i < nr_pics; i++) {
		if (BIT_ISSET(1, node, &eiointc_priv[i]->node_map))
			return (i);
	}

	return (-1);
}

static void
eiointc_enable(void)
{
	uint64_t misc;

	misc = iocsr_read64(LOONGARCH_IOCSR_MISC_FUNC);
	misc |= IOCSR_MISC_FUNC_EXT_IOI_EN;
	iocsr_write64(misc, LOONGARCH_IOCSR_MISC_FUNC);
}

static int
eiointc_router_init(int cpu)
{
	int i, bit;
	uint32_t data;
	uint32_t node;
	int index;

	printf("DEBUG: eiointc_router_init: cpu=%d\n", cpu);

	node = cpu_to_eio_node(cpu);
	index = eiointc_index(node);
	printf("DEBUG: eiointc_router_init: node=%u, index=%d\n", node, index);

	if (index < 0)
		return (-1);

	if ((cpu % CORES_PER_EIO_NODE) == 0) {
		eiointc_enable();

		for (i = 0; i < eiointc_priv[0]->vec_count / 32; i++) {
			/*
			 * NODEMAP register maps interrupt groups to EIOINTC nodes.
			 * Each register handles 2 groups:
			 *   bits [7:0]:   bitmask for group 2i
			 *   bits [23:16]: bitmask for group 2i+1
			 * Node N is represented by bit N (1 << N).
			 * All groups must map to the local node (index 0).
			 */
			data = ((1 << 16) | 1);
			iocsr_write32(data, EIOINTC_REG_NODEMAP + i * 4);
		}
		/* Verify NODEMAP */
		{
			uint32_t nm0 = iocsr_read32(EIOINTC_REG_NODEMAP);
			uint32_t nm2 = iocsr_read32(EIOINTC_REG_NODEMAP + 2 * 4);
			printf("DEBUG: eiointc_router_init: NODEMAP[0]=0x%08x NODEMAP[2]=0x%08x\n",
			    nm0, nm2);
		}

		for (i = 0; i < eiointc_priv[0]->vec_count / 32 / 4; i++) {
			bit = _BIT(1 + index);
			data = bit | (bit << 8) | (bit << 16) | (bit << 24);
			iocsr_write32(data, EIOINTC_REG_IPMAP + i * 4);
		}

		for (i = 0; i < eiointc_priv[0]->vec_count / 4; i++) {
			if (index == 0)
				bit = _BIT(0);
			else
				bit = (eiointc_priv[index]->node << 4) | 1;

			data = bit | (bit << 8) | (bit << 16) | (bit << 24);
			iocsr_write32(data, EIOINTC_REG_ROUTE + i * 4);
		}

		for (i = 0; i < eiointc_priv[0]->vec_count / 32; i++) {
			data = 0xffffffff;
			iocsr_write32(data, EIOINTC_REG_ENABLE + i * 4);
			iocsr_write32(data, EIOINTC_REG_BOUNCE + i * 4);
		}
		printf("DEBUG: eiointc_router_init: all vectors enabled\n");
	}

	/* Debug: read ECFG to verify HWI1 (bit 3) is enabled for EIOINTC cascade */
	{
		uint32_t ecfg = csr_read32(LOONGARCH_CSR_ECFG);
		uint32_t estat = csr_read32(LOONGARCH_CSR_ESTAT);
		printf("DEBUG: eiointc_router_init: ECFG=0x%08x (HWI1 bit3=%s, TI bit11=%s, IPI bit12=%s)\n",
		    ecfg,
		    (ecfg & (1 << IRQ_HWI1)) ? "ON" : "OFF",
		    (ecfg & (1 << IRQ_TI)) ? "ON" : "OFF",
		    (ecfg & (1 << IRQ_IPI)) ? "ON" : "OFF");
		printf("DEBUG: eiointc_router_init: ESTAT=0x%08x (HWI1 pending=%s)\n",
		    estat,
		    (estat & (1 << IRQ_HWI1)) ? "YES" : "no");
	}

	return (0);
}

int
eiointc_init(struct eiointc_softc *sc)
{
	uint64_t node_map;

	node_map = -1ULL;

	if (node_map & (1ULL << cpu_to_eio_node(0)))
		BIT_SET(1, cpu_to_eio_node(0), &sc->node_map);

	eiointc_priv[nr_pics++] = sc;
	eiointc_router_init(0);

	return (0);
}

/* PIC methods */
void
eiointc_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct eiointc_irqsrc *eisrc;
	uint32_t data;
	int reg_idx;
	uint32_t bit;

	eisrc = (struct eiointc_irqsrc *)isrc;
	reg_idx = ENA_REG_IDX(eisrc->irq);
	bit = ENA_REG_BIT(eisrc->irq);
	data = iocsr_read32(EIOINTC_REG_ENABLE + reg_idx * 4);
	data &= ~(1 << bit);
	iocsr_write32(data, EIOINTC_REG_ENABLE + reg_idx * 4);
}

void
eiointc_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct eiointc_irqsrc *eisrc;
	uint32_t data;
	int reg_idx;
	uint32_t bit;

	eisrc = (struct eiointc_irqsrc *)isrc;
	reg_idx = ENA_REG_IDX(eisrc->irq);
	bit = ENA_REG_BIT(eisrc->irq);
	printf("DEBUG: eiointc_enable_intr: irq=%u, reg_idx=%d, bit=%d\n",
	    eisrc->irq, reg_idx, bit);
	data = iocsr_read32(EIOINTC_REG_ENABLE + reg_idx * 4);
	data |= (1 << bit);
	iocsr_write32(data, EIOINTC_REG_ENABLE + reg_idx * 4);
}

int
eiointc_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct eiointc_softc *sc;
	u_int irq;

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

	if (irq >= EIO_INTC_MAX_IRQS)
		return (EINVAL);

	sc = device_get_softc(dev);
	*isrcp = &sc->isrcs[irq].isrc;

	return (0);
}

void
eiointc_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{

}

void
eiointc_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct eiointc_irqsrc *eisrc;
	uint64_t pending;

	eisrc = (struct eiointc_irqsrc *)isrc;
	pending = (1ULL << eisrc->irq);
	iocsr_write64(pending, EIOINTC_REG_ISR + (eisrc->irq / 64) * 8);
}

void
eiointc_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct eiointc_irqsrc *eisrc;
	uint64_t pending;

	eisrc = (struct eiointc_irqsrc *)isrc;
	pending = (1ULL << eisrc->irq);
	iocsr_write64(pending, EIOINTC_REG_ISR + (eisrc->irq / 64) * 8);
}

int
eiointc_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{

	return (0);
}

int
eiointc_bind_intr(device_t dev, struct intr_irqsrc *isrc)
{

	return (0);
}

/* Interrupt handlers */
int
eiointc_child_intr(void *arg, uintptr_t irq)
{
	struct eiointc_softc *sc;
	struct trapframe *tf;
	uint64_t pending;
	int i, bit;
	int eio_irq;
	int error;
	bool found;

	sc = (struct eiointc_softc *)arg;
	tf = curthread->td_intr_frame;

	found = false;
	for (i = 0; i < sc->vec_count / VEC_COUNT_PER_REG; i++) {
		pending = iocsr_read64(EIOINTC_REG_ISR + (i << 3));
		if (pending == 0)
			continue;
		found = true;
		while (pending != 0) {
			bit = ffsl(pending) - 1;
			pending &= ~_BIT(bit);
			eio_irq = i * VEC_COUNT_PER_REG + bit;
			/*
			 * First try child PIC dispatch (for MSI/MSI-X or
			 * other child interrupt controllers).  If the child
			 * PIC's filter has already handled and masked the
			 * interrupt (FILTER_HANDLED or FILTER_SCHEDULE_THREAD),
			 * we must not re-dispatch.
			 *
			 * Only fall back to direct isrc dispatch for truly
			 * unhandled (FILTER_STRAY) interrupts.
			 */
			error = intr_child_irq_handler(sc->pic, eio_irq);
			if (error == FILTER_HANDLED ||
			    error == FILTER_SCHEDULE_THREAD) {
				/*
				 * Child PIC handled the interrupt.  Clear
				 * the ISR bit now, because the framework's
				 * post_filter/post_ithread are called on the
				 * child PIC, not on us (EIOINTC).  Without
				 * this, the ISR bit remains set forever,
				 * causing an infinite cascade interrupt storm.
				 */
				iocsr_write64(_BIT(eio_irq),
				    EIOINTC_REG_ISR + (i << 3));
				continue;
			}
			if (eio_irq < EIO_INTC_MAX_IRQS) {
				struct intr_irqsrc *isrc = &sc->isrcs[eio_irq].isrc;
				intr_isrc_dispatch(isrc, tf);
			}
		}
	}

	return (found ? FILTER_HANDLED : FILTER_STRAY);
}

/* Debug function to check ISR registers periodically */
static void
eiointc_debug_check_isr(void *arg)
{
	struct eiointc_softc *sc;
	uint64_t pending;
	int i;

	sc = (struct eiointc_softc *)arg;

	for (i = 0; i < sc->vec_count / VEC_COUNT_PER_REG; i++) {
		pending = iocsr_read64(EIOINTC_REG_ISR + (i << 3));
		if (pending != 0) {
			/* IRQ 71 is bit 7 in ISR[1] (i=1) */
			const char *vblk71 = (i == 1 && (pending & (1ULL << 7))) ? "SET" : "not set";
			printf("DEBUG: eiointc_check_isr: pending=0x%lx in reg %d, vblk IRQ71=%s\n",
			    (u_long)pending, i, vblk71);
		}
	}
}

/*
 * Return the eiointc softc for the first (and typically only) EIOINTC.
 * This is used by cpuintc to dispatch cascade interrupts.
 */
struct eiointc_softc *
eiointc_get_softc(void)
{

	if (nr_pics > 0 && eiointc_priv[0] != NULL)
		return (eiointc_priv[0]);
	return (NULL);
}

int
eiointc_pic_intr(void *arg)
{
	struct eiointc_softc *sc;
	struct trapframe *tf;
	uint64_t pending;
	int i, bit;
	int irq;
	int error;
	bool found;

	sc = (struct eiointc_softc *)arg;
	tf = curthread->td_intr_frame;

	found = false;
	for (i = 0; i < sc->vec_count / VEC_COUNT_PER_REG; i++) {
		pending = iocsr_read64(EIOINTC_REG_ISR + (i << 3));
		if (pending == 0)
			continue;
		found = true;
		while (pending != 0) {
			bit = ffsl(pending) - 1;
			pending &= ~_BIT(bit);
			irq = i * VEC_COUNT_PER_REG + bit;
			/*
			 * First try child PIC dispatch (for MSI/MSI-X or
			 * other child interrupt controllers).  If the child
			 * PIC's filter has already handled and masked the
			 * interrupt (FILTER_HANDLED or FILTER_SCHEDULE_THREAD),
			 * we must not re-dispatch.
			 *
			 * Only fall back to direct isrc dispatch for truly
			 * unhandled (FILTER_STRAY) interrupts.
			 */
			error = intr_child_irq_handler(NULL, irq);
			if (error == FILTER_HANDLED ||
			    error == FILTER_SCHEDULE_THREAD) {
				/*
				 * Child PIC handled the interrupt.  Clear
				 * the ISR bit now, because the framework's
				 * post_filter/post_ithread are called on the
				 * child PIC, not on us (EIOINTC).  Without
				 * this, the ISR bit remains set forever,
				 * causing an infinite cascade interrupt storm.
				 */
				iocsr_write64(_BIT(irq),
				    EIOINTC_REG_ISR + (i << 3));
				continue;
			}
			if (irq < EIO_INTC_MAX_IRQS) {
				struct intr_irqsrc *isrc = &sc->isrcs[irq].isrc;
				intr_isrc_dispatch(isrc, tf);
			}
		}
	}

	return (found ? FILTER_HANDLED : FILTER_STRAY);
}

#ifdef DEV_ACPI
/* ACPI attach path */
static int
eiointc_acpi_probe(device_t dev)
{
	ACPI_HANDLE h;

	if (acpi_disabled("acpi"))
		return (ENXIO);

	/* Check for ACPI-enumerated device */
	h = acpi_get_handle(dev);
	if (h != NULL) {
		device_set_desc(dev, "Loongson Extended I/O Interrupt Controller");
		return (BUS_PROBE_DEFAULT);
	}

	/* Non-ACPI enumerated (nexus child) - check name */
	if (device_get_unit(dev) == 0 &&
	    strcmp(device_get_name(dev), "eiointc") == 0) {
		device_set_desc(dev, "Loongson Extended I/O Interrupt Controller 2");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
eiointc_acpi_attach(device_t dev)
{
	struct eiointc_softc *sc;
	struct eiointc_irqsrc *isrcs;
	struct intr_pic *pic;
	device_t cpuintc_dev;
	const char *name;
	uintptr_t xref;
	int error;
	u_int irq;
	u_int registered_irqs;

	sc = device_get_softc(dev);
	sc->dev = dev;
	xref = ACPI_EIO_XREF;

	isrcs = sc->isrcs;
	name = device_get_nameunit(sc->dev);
	registered_irqs = 0;
	for (irq = 0; irq < EIO_INTC_MAX_IRQS; irq++) {
		isrcs[irq].irq = irq;
		error = intr_isrc_register(&isrcs[irq].isrc, sc->dev,
		    0, "%s,%u", name, irq);
		if (error != 0) {
			device_printf(dev,
			    "could not register irq %u: %d\n", irq, error);
			goto cleanup_irq;
		}
		registered_irqs++;
	}

	pic = intr_pic_register(dev, xref);
	if (pic == NULL) {
		device_printf(dev, "Cannot register EIOINTC\n");
		goto cleanup_irq;
	}
	sc->pic = pic;

	sc->vec_count = VEC_COUNT;
	sc->node = 0;
	error = eiointc_init(sc);
	if (error < 0) {
		device_printf(dev, "eiointc_init failed\n");
		intr_pic_deregister(dev, xref);
		goto cleanup_irq;
	}

	cpuintc_dev = devclass_get_device(devclass_find("cpuintc"), 0);
	if (cpuintc_dev != NULL) {
		/*
		 * Use MADT EIO_PIC cascade IRQ if available.
		 * cascade=3 means EIOINTC is connected to CPUINTC IRQ 3.
		 */
		u_int cascade_irq;
		if (loongarch_num_eio_pic > 0)
			cascade_irq = loongarch_eio_pic.cascade;
		else
			cascade_irq = LOONGSON_CPU_IRQ_BASE + 3;

		device_printf(dev, "Registering as child of cpuintc at cascade IRQ %u\n",
		    cascade_irq);

		error = intr_pic_add_handler(cpuintc_dev, pic,
		    eiointc_child_intr, sc,
		    cascade_irq,
		    EIO_INTC_MAX_IRQS);
		if (error != 0)
			device_printf(dev,
			    "Failed to add handler to cpuintc: %d\n",
			    error);

		/*
		 * Enable the cascade interrupt in the CPU's ECFG register.
		 * The EIOINTC is connected to CPU HWI1 (cascade=3, i.e. IRQ_HWI1).
		 * Without this, the CPU will not respond to EIOINTC interrupts
		 * even though they are pending in ESTAT.
		 */
		{
			uint32_t ecfg, estat;

			ecfg = csr_read32(LOONGARCH_CSR_ECFG);
			printf("DEBUG: eiointc_attach: ECFG before enable=0x%08x (HWI1=%s)\n",
			    ecfg, (ecfg & (1 << IRQ_HWI1)) ? "ON" : "OFF");
			ecfg |= (1 << IRQ_HWI1);
			csr_write32(ecfg, LOONGARCH_CSR_ECFG);
			ecfg = csr_read32(LOONGARCH_CSR_ECFG);
			printf("DEBUG: eiointc_attach: ECFG after enable=0x%08x (HWI1=%s)\n",
			    ecfg, (ecfg & (1 << IRQ_HWI1)) ? "ON" : "OFF");

			/* Also check ESTAT for pending interrupts */
			estat = csr_read32(LOONGARCH_CSR_ESTAT);
			printf("DEBUG: eiointc_attach: ESTAT=0x%08x (HWI1 pending=%s)\n",
			    estat, (estat & (1 << IRQ_HWI1)) ? "YES" : "no");
		}
	}

	return (0);

cleanup_irq:
	for (irq = 0; irq < registered_irqs; irq++)
		intr_isrc_deregister(&isrcs[irq].isrc);
	return (ENXIO);
}

static device_method_t eiointc_acpi_methods[] = {
	DEVMETHOD(device_probe,		eiointc_acpi_probe),
	DEVMETHOD(device_attach,	eiointc_acpi_attach),
	DEVMETHOD(pic_disable_intr,	eiointc_disable_intr),
	DEVMETHOD(pic_enable_intr,	eiointc_enable_intr),
	DEVMETHOD(pic_map_intr,	eiointc_map_intr),
	DEVMETHOD(pic_pre_ithread,	eiointc_pre_ithread),
	DEVMETHOD(pic_post_ithread,	eiointc_post_ithread),
	DEVMETHOD(pic_post_filter,	eiointc_post_filter),
	DEVMETHOD(pic_setup_intr,	eiointc_setup_intr),
	DEVMETHOD(pic_bind_intr,	eiointc_bind_intr),
	DEVMETHOD_END
};

static driver_t eiointc_acpi_driver = {
	"eiointc",
	eiointc_acpi_methods,
	sizeof(struct eiointc_softc),
};

EARLY_DRIVER_MODULE(eiointc, nexus, eiointc_acpi_driver, 0, 0,
    BUS_PASS_INTERRUPT);
#endif /* DEV_ACPI */
