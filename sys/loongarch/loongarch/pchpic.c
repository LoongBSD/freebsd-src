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
 * Loongson PCH-PIC (Platform Controller Hub - Programmable Interrupt
 * Controller) driver.
 */

#include "opt_acpi.h"
#include "opt_platform.h"

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/types.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/madt_var.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/vmparam.h>

#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>
#endif

#include "pic_if.h"

#define	PCHPIC_MAX_IRQS	256
#define	PCHPIC_ROUTE_MAX	64	/* Max IRQs with route entries (0x100-0x13f) */

/*
 * PCH-PIC register offsets (aligned with QEMU/Linux definitions).
 * Reference: QEMU include/hw/intc/loongarch_pic_common.h
 *            Linux drivers/irqchip/irq-loongson-pch-pic.c
 *
 * The PCH-PIC uses memory-mapped I/O for interrupt control.
 * All registers are 64-bit (8-byte aligned) unless noted.
 */
#define	PCHPIC_INT_ID		0x00	/* Interrupt ID register (RO) */
#define	PCHPIC_INT_MASK	0x20	/* Interrupt mask register (RW) */
					/* 1=masked(disabled), 0=enabled */
#define	PCHPIC_HTMSI_EN	0x40	/* HT/MSI enable register */
#define	PCHPIC_INT_EDGE	0x60	/* Edge/Level trigger select (RW) */
					/* 1=edge, 0=level */
#define	PCHPIC_INT_CLEAR	0x80	/* Interrupt clear register (WO) */
#define	PCHPIC_AUTO_CTRL0	0xc0	/* Auto bounce control 0 */
#define	PCHPIC_AUTO_CTRL1	0xe0	/* Auto bounce control 1 */
#define	PCHPIC_ROUTE_ENTRY	0x100	/* Per-IRQ routing (byte access) */
#define	PCHPIC_ROUTE_ENTRY_END	0x13f
#define	PCHPIC_HTMSI_VEC	0x200	/* Per-IRQ HT vector (byte access) */
#define	PCHPIC_HTMSI_VEC_END	0x23f
#define	PCHPIC_INT_REQUEST	0x380	/* Interrupt Request Register (IRR) */
#define	PCHPIC_INT_STATUS	0x3a0	/* Interrupt Service Register (ISR) */
#define	PCHPIC_INT_POL		0x3e0	/* Polarity register (RW) */
					/* 0=high, 1=low */

struct pchpic_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
};

struct pchpic_softc {
	device_t		dev;
	struct resource		*intc_res;
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	vm_size_t		bsh_size;
	void			*mmio_base;
	u_int			nirqs;		/* Actual number of IRQs */
	struct pchpic_irqsrc	isrcs[PCHPIC_MAX_IRQS];
};

/*
 * Helper macros for register access.
 * PCH-PIC registers are 64-bit wide.
 * When mmio_base is set (direct MMIO mapping), use 64-bit direct access.
 * Otherwise use bus_space.
 */
#define	PIC_WRITE(sc, reg, val)	do { \
	if ((sc)->mmio_base != NULL) \
		*(volatile uint64_t *)((uint8_t *)((sc)->mmio_base) + (reg)) = (val); \
	else \
		bus_space_write_8((sc)->bst, (sc)->bsh, reg, val); \
} while (0)
#define	PIC_READ(sc, reg)	((sc)->mmio_base != NULL ? \
	*(volatile uint64_t *)((uint8_t *)((sc)->mmio_base) + (reg)) : \
	bus_space_read_8((sc)->bst, (sc)->bsh, reg))

/* Byte-wide register access for route entries */
#define	PIC_WRITE_BYTE(sc, reg, val)	do { \
	if ((sc)->mmio_base != NULL) \
		*(volatile uint8_t *)((uint8_t *)((sc)->mmio_base) + (reg)) = (val); \
	else \
		bus_space_write_1((sc)->bst, (sc)->bsh, reg, val); \
} while (0)

static struct resource_spec pchpic_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	RESOURCE_SPEC_END
};

/*
 * PCH-PIC child interrupt filter.
 * This is called by the parent controller (eiointc) to dispatch interrupts.
 * The 'irq' parameter is the interrupt number in the parent controller.
 */
static int
pchpic_child_intr(void *arg, uintptr_t irq)
{
	struct pchpic_softc *sc;
	struct trapframe *tf;
	uint64_t status;
	u_int bit;

	sc = (struct pchpic_softc *)arg;
	tf = curthread->td_intr_frame;

	/* Read interrupt status to find which PCH interrupt is pending */
	status = PIC_READ(sc, PCHPIC_INT_STATUS);

	if (status == 0)
		return (FILTER_STRAY);

	/* Process all pending interrupts */
	while (status != 0) {
		bit = ffsl(status) - 1;
		status &= ~(1UL << bit);

		/* Clear the edge-triggered interrupt */
		PIC_WRITE(sc, PCHPIC_INT_CLEAR, (1ULL << bit));

		/* Dispatch to the interrupt source handler */
		if (bit < PCHPIC_MAX_IRQS) {
			intr_isrc_dispatch(&sc->isrcs[bit].isrc, tf);
		}
	}

	return (FILTER_HANDLED);
}

/*
 * Shared PIC interface functions.
 * These are used by both FDT and ACPI drivers.
 */
static void
pchpic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct pchpic_softc *sc;
	struct pchpic_irqsrc *src;
	uint64_t val;

	sc = device_get_softc(dev);
	src = (struct pchpic_irqsrc *)isrc;

	/*
	 * Disable the interrupt by setting the corresponding bit
	 * in the interrupt mask register (1=masked, 0=enabled).
	 */
	val = PIC_READ(sc, PCHPIC_INT_MASK);
	val |= (1ULL << src->irq);
	PIC_WRITE(sc, PCHPIC_INT_MASK, val);
}

static void
pchpic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct pchpic_softc *sc;
	struct pchpic_irqsrc *src;
	uint64_t val;

	sc = device_get_softc(dev);
	src = (struct pchpic_irqsrc *)isrc;

	/*
	 * Enable the interrupt by clearing the corresponding bit
	 * in the interrupt mask register (1=masked, 0=enabled).
	 */
	val = PIC_READ(sc, PCHPIC_INT_MASK);
	val &= ~(1ULL << src->irq);
	PIC_WRITE(sc, PCHPIC_INT_MASK, val);
}

static int
pchpic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct pchpic_softc *sc;
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
		u_int gsi_base;

		daa = (struct intr_map_data_acpi *)data;
		/*
		 * ACPI provides an absolute GSI number.
		 * Convert to PCH-PIC internal IRQ by subtracting gsi_base.
		 * For ACPI mode, get gsi_base from MADT BIO_PIC entry.
		 * For FDT mode, gsi_base is 0 (IRQ is already relative).
		 */
		if (loongarch_num_bio_pic > 0)
			gsi_base = loongarch_bio_pic.gsi_base;
		else
			gsi_base = LOONGSON_PCH_IRQ_BASE;
		if (daa->irq < gsi_base)
			return (EINVAL);
		irq = daa->irq - gsi_base;
		break;
	}
#endif
	default:
		return (EINVAL);
	}

	if (irq >= PCHPIC_MAX_IRQS)
		return (EINVAL);

	sc = device_get_softc(dev);
	*isrcp = &sc->isrcs[irq].isrc;

	return (0);
}

static void
pchpic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	/*
	 * Pre-ithread handling:
	 * Called before running the interrupt thread.
	 * Can be used to mask the interrupt or perform any
	 * necessary preparation before the handler runs.
	 */
}

static void
pchpic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct pchpic_softc *sc;
	struct pchpic_irqsrc *src;

	sc = device_get_softc(dev);
	src = (struct pchpic_irqsrc *)isrc;

	/*
	 * Post-ithread handling:
	 * Clear the interrupt after the thread has completed.
	 * This allows the interrupt controller to accept new interrupts.
	 */
	PIC_WRITE(sc, PCHPIC_INT_CLEAR, (1ULL << src->irq));
}

static void
pchpic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct pchpic_softc *sc;
	struct pchpic_irqsrc *src;

	sc = device_get_softc(dev);
	src = (struct pchpic_irqsrc *)isrc;

	/*
	 * Post-filter handling:
	 * Clear the interrupt after a filter handler.
	 * Similar to post_ithread but for filter handlers.
	 */
	PIC_WRITE(sc, PCHPIC_INT_CLEAR, (1ULL << src->irq));
}

static int
pchpic_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct pchpic_softc *sc;
	struct pchpic_irqsrc *src;
	uint64_t val;

	sc = device_get_softc(dev);
	src = (struct pchpic_irqsrc *)isrc;

	/*
	 * Setup interrupt:
	 * Configure the interrupt trigger mode and routing.
	 * This is called when a device driver requests an interrupt.
	 *
	 * Per QEMU/Linux definitions:
	 * - PCHPIC_INT_EDGE (0x60): 1=edge, 0=level trigger
	 * - PCHPIC_ROUTE_ENTRY (0x100): per-IRQ routing (byte per IRQ)
	 *   Route value 0 = INT0 (EIOINTC), which is the default.
	 */

	/* Set interrupt as level-triggered by default (clear edge bit) */
	val = PIC_READ(sc, PCHPIC_INT_EDGE);
	val &= ~(1ULL << src->irq);
	PIC_WRITE(sc, PCHPIC_INT_EDGE, val);

	/* Route interrupt to INT0 (EIOINTC) by default */
	/* Route entries are byte-wide at offset 0x100 + irq */
	/* Only write route if IRQ is within the route register range */
	if (src->irq < PCHPIC_ROUTE_MAX)
		PIC_WRITE_BYTE(sc, PCHPIC_ROUTE_ENTRY + src->irq, 0);

	return (0);
}

static int
pchpic_bind_intr(device_t dev, struct intr_irqsrc *isrc)
{
	/*
	 * Bind interrupt to a specific CPU:
	 * The interrupt routing is handled by the parent interrupt
	 * controller. For now, we just return success.
	 */
	return (0);
}

#ifdef FDT
static int
pchpic_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "loongson,pch-pic-1.0"))
		return (ENXIO);

	device_set_desc(dev, "Loongson PCH-PIC Controller");

	return (BUS_PROBE_DEFAULT);
}

static int
pchpic_fdt_attach(device_t dev)
{
	struct pchpic_softc *sc;
	struct pchpic_irqsrc *isrcs;
	phandle_t node, xref, intr_parent;
	device_t parent_dev;
	const char *name;
	u_int irq;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(dev);

	/* Allocate memory resource */
	error = bus_alloc_resources(dev, pchpic_spec, &sc->intc_res);
	if (error != 0) {
		/*
		 * If bus_alloc_resources fails, the parent bus may not have
		 * set up the resource from FDT.  Set a default MMIO address
		 * and retry.
		 */
		bus_set_resource(dev, SYS_RES_MEMORY, 0,
		    0x10000000, 0x400);
		error = bus_alloc_resources(dev, pchpic_spec, &sc->intc_res);
		if (error != 0) {
			device_printf(dev, "Cannot allocate memory resource\n");
			return (ENXIO);
		}
	}
	sc->bst = rman_get_bustag(sc->intc_res);
	sc->bsh = rman_get_bushandle(sc->intc_res);

	if ((intr_parent = ofw_bus_find_iparent(node)) == 0) {
		device_printf(dev,
		    "Cannot find parent interrupt controller\n");
		bus_release_resources(dev, pchpic_spec, &sc->intc_res);
		return (ENXIO);
	}

	/* Register interrupt sources */
	isrcs = sc->isrcs;
	name = device_get_nameunit(dev);
	for (irq = 0; irq < PCHPIC_MAX_IRQS; irq++) {
		isrcs[irq].irq = irq;
		error = intr_isrc_register(&isrcs[irq].isrc, dev,
		    0, "%s,%u", name, irq);
		if (error != 0) {
			device_printf(dev, "could not register irq %u: %d\n",
			    irq, error);
			/* Continue anyway, some IRQs might fail */
		}
	}

	/* Register ourself as an interrupt controller */
	xref = OF_xref_from_node(node);
	if (intr_pic_register(dev, xref) == NULL) {
		device_printf(dev, "Cannot register PIC\n");
		bus_release_resources(dev, pchpic_spec, &sc->intc_res);
		return (ENXIO);
	}

	/*
	 * Register ourself as a child of the parent interrupt controller.
	 * PCH-PIC handles IRQs from LOONGSON_PCH_IRQ_BASE (64).
	 */
	parent_dev = OF_device_from_xref(intr_parent);
	if (parent_dev != NULL) {
		error = intr_pic_add_handler(parent_dev,
		    intr_pic_register(dev, xref), pchpic_child_intr, sc,
		    LOONGSON_PCH_IRQ_BASE, PCHPIC_MAX_IRQS);
		if (error != 0) {
			device_printf(dev, "Failed to add handler to parent PIC: %d\n", error);
			/* Continue anyway, we might still work */
		}
	} else {
		device_printf(dev, "Cannot find parent device for xref %d\n", intr_parent);
	}

	/* Register ourself so device can find us */
	OF_device_register_xref(xref, dev);

	return (0);
}

static device_method_t pchpic_fdt_methods[] = {
	DEVMETHOD(device_probe,	pchpic_fdt_probe),
	DEVMETHOD(device_attach,	pchpic_fdt_attach),

	DEVMETHOD(pic_disable_intr,	pchpic_disable_intr),
	DEVMETHOD(pic_enable_intr,	pchpic_enable_intr),
	DEVMETHOD(pic_map_intr,		pchpic_map_intr),
	DEVMETHOD(pic_pre_ithread,	pchpic_pre_ithread),
	DEVMETHOD(pic_post_ithread,	pchpic_post_ithread),
	DEVMETHOD(pic_post_filter,	pchpic_post_filter),
	DEVMETHOD(pic_setup_intr,	pchpic_setup_intr),
	DEVMETHOD(pic_bind_intr,	pchpic_bind_intr),

	DEVMETHOD_END
};

static driver_t pchpic_fdt_driver = {
	"pchpic",
	pchpic_fdt_methods,
	sizeof(struct pchpic_softc),
};

EARLY_DRIVER_MODULE(pchpic, ofwbus, pchpic_fdt_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
#endif /* FDT */

#ifdef DEV_ACPI
static int
pchpic_acpi_probe(device_t dev)
{
	ACPI_HANDLE h;

	if (acpi_disabled("acpi"))
		return (ENXIO);

	h = acpi_get_handle(dev);
	if (h != NULL) {
		device_set_desc(dev, "Loongson PCH-PIC Controller (ACPI)");
		return (BUS_PROBE_DEFAULT);
	}

	if (device_get_unit(dev) == 0 &&
	    strcmp(device_get_name(dev), "pchpic") == 0) {
		device_set_desc(dev, "Loongson PCH-PIC Controller 2");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
pchpic_acpi_attach(device_t dev)
{
	struct pchpic_softc *sc;
	struct pchpic_irqsrc *isrcs;
	struct intr_pic *pic;
	device_t eiointc_dev;
	const char *name;
	uintptr_t xref;
	int error;
	u_int irq;
	vm_paddr_t pchpic_addr;
	vm_size_t pchpic_size;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->bsh = 0;
	sc->bst = NULL;
	sc->intc_res = NULL;
	sc->mmio_base = NULL;

	/* Use MADT BIO_PIC information for MMIO address */
	if (loongarch_num_bio_pic > 0) {
		pchpic_addr = loongarch_bio_pic.address;
		pchpic_size = loongarch_bio_pic.size;
		device_printf(dev,
		    "ACPI attach: using MADT BIO_PIC: addr=0x%jx, size=0x%jx, gsi_base=%u\n",
		    (uintmax_t)pchpic_addr, (uintmax_t)pchpic_size,
		    loongarch_bio_pic.gsi_base);
	} else {
		pchpic_addr = 0x10000000;
		pchpic_size = 0x400;
		device_printf(dev,
		    "ACPI attach: using fallback addr=0x%jx\n",
		    (uintmax_t)pchpic_addr);
	}

	/*
	 * Tell the parent bus (nexus) about our MMIO resource before
	 * trying to allocate it.  Without this, nexus_alloc_resource()
	 * cannot find the resource in the device's resource list and
	 * returns NULL → ENXIO.
	 */
	bus_set_resource(dev, SYS_RES_MEMORY, 0, pchpic_addr, pchpic_size);
	error = bus_alloc_resources(dev, pchpic_spec, &sc->intc_res);
	if (error != 0) {
		device_printf(dev, "First bus_alloc_resources failed: %d, using pmap_mapdev_attr\n", error);
		sc->bst = NULL;
		sc->mmio_base = pmap_mapdev_attr(pchpic_addr, pchpic_size, VM_MEMATTR_UNCACHEABLE);
		sc->bsh_size = pchpic_size;
		if (sc->mmio_base == NULL) {
			device_printf(dev, "Cannot map MMIO region via pmap_mapdev_attr\n");
			return (ENXIO);
		}
		device_printf(dev, "MMIO mapped via pmap_mapdev_attr: pa=0x%jx, va=%p\n",
		    (uintmax_t)pchpic_addr, sc->mmio_base);
	} else {
		sc->bst = rman_get_bustag(sc->intc_res);
		sc->bsh = rman_get_bushandle(sc->intc_res);
		device_printf(dev, "Resource allocated: rman_start=0x%jx, rman_end=0x%jx\n",
		    (uintmax_t)rman_get_start(sc->intc_res),
		    (uintmax_t)rman_get_end(sc->intc_res));
	}

	isrcs = sc->isrcs;
	name = device_get_nameunit(dev);
	for (irq = 0; irq < PCHPIC_MAX_IRQS; irq++) {
		isrcs[irq].irq = irq;
		error = intr_isrc_register(&isrcs[irq].isrc, dev,
		    0, "%s,%u", name, irq);
		if (error != 0) {
			if (error == E2BIG)
				break;
			device_printf(dev, "could not register irq %u: %d\n",
			    irq, error);
		}
	}

	/*
	 * PCH-PIC uses a unique xref to avoid conflict with other interrupt
	 * controllers. The xref value ACPI_PCH_PIC_XREF is distinct from:
	 *   - ACPI_INTR_XREF (1): used by cpuintc
	 *   - ACPI_MSI_XREF (2): used by pchmsi
	 *   - ACPI_EIO_XREF (4): used by eiointc
	 */
	xref = ACPI_PCH_PIC_XREF;
	pic = intr_pic_register(dev, xref);
	if (pic == NULL) {
		device_printf(dev, "Cannot register PIC\n");
		if (sc->intc_res != NULL)
			bus_release_resources(dev, pchpic_spec, &sc->intc_res);
		return (ENXIO);
	}

	/*
	 * Register ourself as a child of eiointc.
	 * PCH-PIC handles IRQs from EIOINTC IRQ 0 (32 IRQs: 0-31).
	 * PCH-MSI handles IRQs from EIOINTC IRQ 32 (224 IRQs: 32-255).
	 *
	 * The PCH-PIC interrupts are routed through EIOINTC. When an interrupt
	 * occurs, EIOINTC receives it and dispatches to PCH-PIC's child_intr
	 * handler.
	 *
	 * IRQ layout in EIOINTC (QEMU virt):
	 * - IRQ 0-31:  PCH-PIC (legacy devices)
	 * - IRQ 32-255: PCH-MSI (PCIe MSI/MSI-X)
	 */
	eiointc_dev = devclass_get_device(devclass_find("eiointc"), 0);
	if (eiointc_dev != NULL) {
		/*
		 * PCH-PIC uses EIOINTC IRQ 0-31 (32 IRQs).
		 * Use 0 as the base and 32 as the count to match QEMU's design.
		 */
		const u_int pchpic_eioirq_base = 0;
		const u_int pchpic_irq_count = 32;

		device_printf(dev, "Registering as child of eiointc, EIOINTC IRQ base=%u, count=%u\n",
		    pchpic_eioirq_base, pchpic_irq_count);

		error = intr_pic_add_handler(eiointc_dev, pic,
		    pchpic_child_intr, sc,
		    pchpic_eioirq_base, pchpic_irq_count);
		if (error != 0)
			device_printf(dev,
			    "Failed to add handler to eiointc: %d\n", error);
		else
			device_printf(dev, "Successfully registered with eiointc\n");
	} else {
		device_printf(dev, "WARNING: eiointc not found, PCH-PIC not registered\n");
	}

	return (0);
}

static device_method_t pchpic_acpi_methods[] = {
	DEVMETHOD(device_probe,		pchpic_acpi_probe),
	DEVMETHOD(device_attach,	pchpic_acpi_attach),

	DEVMETHOD(pic_disable_intr,	pchpic_disable_intr),
	DEVMETHOD(pic_enable_intr,	pchpic_enable_intr),
	DEVMETHOD(pic_map_intr,		pchpic_map_intr),
	DEVMETHOD(pic_pre_ithread,	pchpic_pre_ithread),
	DEVMETHOD(pic_post_ithread,	pchpic_post_ithread),
	DEVMETHOD(pic_post_filter,	pchpic_post_filter),
	DEVMETHOD(pic_setup_intr,	pchpic_setup_intr),
	DEVMETHOD(pic_bind_intr,	pchpic_bind_intr),

	DEVMETHOD_END
};

static driver_t pchpic_acpi_driver = {
	"pchpic",
	pchpic_acpi_methods,
	sizeof(struct pchpic_softc),
};

EARLY_DRIVER_MODULE(pchpic, nexus, pchpic_acpi_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
#endif /* DEV_ACPI */