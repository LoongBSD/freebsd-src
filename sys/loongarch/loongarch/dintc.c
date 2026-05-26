/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Xiaoqiang Zhao <zxq_yx_007@163.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 */

/*
 * LoongArch DINTC (Direct Message-Signaled Interrupt Controller) driver.
 *
 * DINTC is a message-based interrupt controller that receives MSI writes
 * and dispatches them directly to CPU cores via the DMSI mechanism.
 * It sits between PCH-MSI and the CPU interrupt controller.
 *
 * Interrupt hierarchy (with DMSI enabled):
 *   PCI Devices -> PCH-MSI -> DINTC -> CPUINTC
 *
 * Without DMSI:
 *   PCI Devices -> PCH-MSI -> EIOINTC -> CPUINTC
 *
 * DINTC is an optional feature (IOCSRF_DMSI, bit 15) that must be
 * enabled via the IOCSR feature register at boot time.
 *
 * MSI address format for DINTC:
 *   bits [39:28] = 0x2FF (fixed pattern)
 *   bits [19:12] = CPU number
 *   bits [11:4]  = IRQ number
 *   bits [3:0]   = reserved
 */

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/resource.h>
#include <machine/dintc_var.h>

#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>
#include "acpi_bus_if.h"
#endif
#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include "ofw_bus_if.h"
#endif

#include "pic_if.h"
#include "msi_if.h"

/*
 * DINTC does not have traditional MMIO registers for interrupt control.
 * It receives MSI writes directly. The MMIO region is write-only
 * for triggering interrupts from software.
 */

static struct resource_spec dintc_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	RESOURCE_SPEC_END
};

static int
dintc_probe(device_t dev)
{
#ifdef FDT
	if (ofw_bus_status_okay(dev) &&
	    ofw_bus_is_compatible(dev, "loongarch,dintc-1.0")) {
		device_set_desc(dev, "Loongson DINTC Controller");
		return (BUS_PROBE_DEFAULT);
	}
#endif
	return (ENXIO);
}

static int
dintc_attach(device_t dev)
{
	struct dintc_softc *sc;
	const char *name;
	u_int irq;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	error = bus_alloc_resources(dev, dintc_spec, &sc->mem_res);
	if (error != 0) {
		device_printf(dev, "Cannot allocate memory resource\n");
		return (ENXIO);
	}

	/* Register interrupt sources */
	sc->nr_vecs = DINTC_MAX_IRQS;
	name = device_get_nameunit(dev);
	for (irq = 0; irq < sc->nr_vecs; irq++) {
		sc->isrcs[irq].irq = irq;
		error = intr_isrc_register(&sc->isrcs[irq].isrc, dev,
		    0, "%s,%u", name, irq);
		if (error != 0) {
			device_printf(dev,
			    "Cannot register irq %u: %d\n", irq, error);
			goto cleanup;
		}
	}

	return (0);

cleanup:
	bus_release_resources(dev, dintc_spec, &sc->mem_res);
	return (ENXIO);
}

/*
 * PIC methods — DINTC is primarily a write-only MSI target,
 * not a traditional PIC with enable/disable/mask/unmask.
 */
static void
dintc_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	/* DINTC interrupts are controlled by the MSI address/data, not registers */
}

static void
dintc_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	/* DINTC interrupts are controlled by the MSI address/data, not registers */
}

static int
dintc_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct dintc_softc *sc;
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

	if (irq >= DINTC_MAX_IRQS)
		return (EINVAL);

	sc = device_get_softc(dev);
	*isrcp = &sc->isrcs[irq].isrc;

	return (0);
}

static void
dintc_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	/* Nothing to do */
}

static void
dintc_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	/* Nothing to do */
}

static void
dintc_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	/* Nothing to do */
}

static int
dintc_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	/* Nothing special to setup */
	return (0);
}

static int
dintc_bind_intr(device_t dev, struct intr_irqsrc *isrc)
{
	/* DINTC binding is handled by the CPU interrupt controller */
	return (0);
}

/*
 * MSI methods — DINTC provides MSI doorbell addresses
 */
static msi_alloc_msi_t dintc_alloc_msi;
static msi_alloc_msix_t dintc_alloc_msix;
static msi_release_msi_t dintc_release_msi;
static msi_release_msix_t dintc_release_msix;
static msi_map_msi_t dintc_map_msi;

static int
dintc_alloc_msi(device_t dev, device_t child, int count,
    int maxcount, device_t *pic, struct intr_irqsrc **srcs)
{
	struct dintc_softc *sc;
	int i, irq;

	sc = device_get_softc(dev);

	/* Find consecutive free IRQs */
	for (irq = 0; irq <= sc->nr_vecs - count; irq++) {
		for (i = 0; i < count; i++) {
			if (sc->isrcs[irq + i].isrc.isrc_handlers != 0)
				break;
		}
		if (i == count)
			goto found;
	}
	return (ENXIO);

found:
	*pic = dev;
	for (i = 0; i < count; i++)
		srcs[i] = &sc->isrcs[irq + i].isrc;

	return (0);
}

static int
dintc_alloc_msix(device_t dev, device_t child,
    device_t *pic, struct intr_irqsrc **src)
{
	struct dintc_softc *sc;
	int irq;

	sc = device_get_softc(dev);

	for (irq = 0; irq < sc->nr_vecs; irq++) {
		if (sc->isrcs[irq].isrc.isrc_handlers == 0) {
			*pic = dev;
			*src = &sc->isrcs[irq].isrc;
			return (0);
		}
	}
	return (ENXIO);
}

static int
dintc_release_msi(device_t dev, device_t child,
    int count, struct intr_irqsrc **srcs)
{
	return (0);
}

static int
dintc_release_msix(device_t dev, device_t child,
    struct intr_irqsrc *src)
{
	return (0);
}

static int
dintc_map_msi(device_t dev, device_t child,
    struct intr_irqsrc *src, uint64_t *addr, uint32_t *data)
{
	struct dintc_irqsrc *dintc_isrc;

	dintc_isrc = (struct dintc_irqsrc *)src;

	/*
	 * DINTC MSI address format:
	 *   bits [39:28] = 0x2FF (fixed)
	 *   bits [19:12] = CPU number (0 for now)
	 *   bits [11:4]  = IRQ number
	 *   bits [3:0]   = reserved
	 *
	 * The write address encodes both CPU and IRQ number.
	 * The write data value is not used by DINTC (it's ignored).
	 */
	*addr = DINTC_MSI_ADDR_FIXED | (dintc_isrc->irq << 4);
	*data = 0;

	return (0);
}

static device_method_t dintc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		dintc_probe),
	DEVMETHOD(device_attach,	dintc_attach),

	/* PIC interface */
	DEVMETHOD(pic_disable_intr,	dintc_disable_intr),
	DEVMETHOD(pic_enable_intr,	dintc_enable_intr),
	DEVMETHOD(pic_map_intr,		dintc_map_intr),
	DEVMETHOD(pic_pre_ithread,	dintc_pre_ithread),
	DEVMETHOD(pic_post_ithread,	dintc_post_ithread),
	DEVMETHOD(pic_post_filter,	dintc_post_filter),
	DEVMETHOD(pic_setup_intr,	dintc_setup_intr),
	DEVMETHOD(pic_bind_intr,	dintc_bind_intr),

	/* MSI interface */
	DEVMETHOD(msi_alloc_msi,	dintc_alloc_msi),
	DEVMETHOD(msi_release_msi,	dintc_release_msi),
	DEVMETHOD(msi_alloc_msix,	dintc_alloc_msix),
	DEVMETHOD(msi_release_msix,	dintc_release_msix),
	DEVMETHOD(msi_map_msi,		dintc_map_msi),

	DEVMETHOD_END
};

static driver_t dintc_driver = {
	"dintc",
	dintc_methods,
	sizeof(struct dintc_softc),
};

#ifdef FDT
EARLY_DRIVER_MODULE(dintc, ofwbus, dintc_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_EARLY);
#endif

#ifdef DEV_ACPI
static int
dintc_acpi_probe(device_t dev)
{
	ACPI_HANDLE h;

	if (acpi_disabled("acpi"))
		return (ENXIO);

	h = acpi_get_handle(dev);
	if (h != NULL) {
		device_set_desc(dev, "Loongson Direct Message-Signaled Interrupt Controller");
		return (BUS_PROBE_DEFAULT);
	}

	if (device_get_unit(dev) == 0 &&
	    strcmp(device_get_name(dev), "dintc") == 0) {
		device_set_desc(dev, "Loongson Direct Message-Signaled Interrupt Controller 2");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
dintc_acpi_attach(device_t dev)
{
	struct dintc_softc *sc;
	const char *name;
	u_int irq;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/*
	 * Try to allocate memory resource from pre-configured resource list.
	 * For nexus children in ACPI mode, there is no pre-configured resource,
	 * so we fall back to manually setting a known MMIO address.
	 */
	error = bus_alloc_resources(dev, dintc_spec, &sc->mem_res);
	if (error != 0) {
		bus_set_resource(dev, SYS_RES_MEMORY, 0,
		    0x2FE00000, 0x100000);
		error = bus_alloc_resources(dev, dintc_spec, &sc->mem_res);
		if (error != 0) {
			device_printf(dev, "Cannot allocate memory resource\n");
			return (ENXIO);
		}
	}

	/* Register interrupt sources */
	sc->nr_vecs = DINTC_MAX_IRQS;
	name = device_get_nameunit(dev);
	for (irq = 0; irq < sc->nr_vecs; irq++) {
		sc->isrcs[irq].irq = irq;
		error = intr_isrc_register(&sc->isrcs[irq].isrc, dev,
		    0, "%s,%u", name, irq);
		if (error != 0) {
			device_printf(dev,
			    "Cannot register irq %u: %d\n", irq, error);
			goto cleanup;
		}
	}

	return (0);

cleanup:
	bus_release_resources(dev, dintc_spec, &sc->mem_res);
	return (ENXIO);
}

static device_method_t dintc_acpi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		dintc_acpi_probe),
	DEVMETHOD(device_attach,	dintc_acpi_attach),

	/* PIC interface */
	DEVMETHOD(pic_disable_intr,	dintc_disable_intr),
	DEVMETHOD(pic_enable_intr,	dintc_enable_intr),
	DEVMETHOD(pic_map_intr,		dintc_map_intr),
	DEVMETHOD(pic_pre_ithread,	dintc_pre_ithread),
	DEVMETHOD(pic_post_ithread,	dintc_post_ithread),
	DEVMETHOD(pic_post_filter,	dintc_post_filter),
	DEVMETHOD(pic_setup_intr,	dintc_setup_intr),
	DEVMETHOD(pic_bind_intr,	dintc_bind_intr),

	/* MSI interface */
	DEVMETHOD(msi_alloc_msi,	dintc_alloc_msi),
	DEVMETHOD(msi_release_msi,	dintc_release_msi),
	DEVMETHOD(msi_alloc_msix,	dintc_alloc_msix),
	DEVMETHOD(msi_release_msix,	dintc_release_msix),
	DEVMETHOD(msi_map_msi,		dintc_map_msi),

	DEVMETHOD_END
};

DEFINE_CLASS_1(dintc, dintc_acpi_driver, dintc_acpi_methods,
    sizeof(struct dintc_softc), dintc_driver);

EARLY_DRIVER_MODULE(dintc, nexus, dintc_acpi_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_EARLY);
#endif /* DEV_ACPI */
