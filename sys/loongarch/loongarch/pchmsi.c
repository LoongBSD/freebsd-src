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
 * Loongson PCH MSI Controller driver.
 * This driver handles MSI (Message Signaled Interrupts) for PCIe devices.
 */

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/intr.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/madt_var.h>

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
#include "msi_if.h"

#define	PCHMSI_MAX_IRQS	256

/*
 * QEMU virt machine constants.
 * These match the ACPI MADT MSI_PIC table values (start=64, count=192)
 * and are consistent with the Linux loongarch64 kernel reference
 * implementation.
 *
 * The MSI data value written by PCI devices maps directly to EIOINTC
 * IRQ numbers in the range [64, 255] as declared in the ACPI MADT table.
 */
#define	VIRT_PCHPIC_IRQ_NUM	32	/* PCH-PIC uses IRQ 0-31 */
#define	VIRT_PCHMSI_IRQ_BASE	64	/* PCH-MSI starts at IRQ 64 in EIOINTC (MADT) */
#define	VIRT_PCHMSI_IRQ_COUNT	192	/* Total MSI IRQs: 64-255 (MADT) */
#define	VIRT_PCHMSI_ADDR_LOW	0x2FF00000UL
#define	VIRT_PCHMSI_SIZE	0x8

/* MSI registers */
#define	MSI_IRQS		0x00
#define	MSI_MASK		0x04

struct pchmsi_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;	/* Local index (0, 1, 2...) */
	u_int			vec;	/* EIOINTC IRQ number (e.g., 64, 65, 66...) */
	u_int			global_irq;	/* Global IRQ resource ID from intr_isrc_register */
	bool			allocated;	/* Track allocation state */
};

/* MSI domain information */
struct pchmsi_domain_info {
	struct pchmsi_softc	*sc;
	u_int				base_irq;
	u_int				num_irqs;
	vm_paddr_t			msg_addr;
};

struct pchmsi_softc {
	device_t		dev;
	device_t		parent;
	struct resource	*mem_res;
	u_int			msi_base_vec;
	u_int			msi_num_vecs;
	struct pchmsi_irqsrc	isrcs[PCHMSI_MAX_IRQS];
	struct intr_map_data_fdt *parent_map_data;
};

static struct resource_spec msi_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	RESOURCE_SPEC_END
};

#ifdef FDT
static int
pchmsi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "loongson,pch-msi-1.0"))
		return (ENXIO);

	device_set_desc(dev, "Loongson PCH MSI Controller");

	return (BUS_PROBE_DEFAULT);
}

static int
pchmsi_attach(device_t dev)
{
	struct pchmsi_softc *sc;
	phandle_t node, xref, intr_parent;
	struct pchmsi_irqsrc *isrcs;
	u_int irq, vec;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(dev);

	/* Get MSI parameters from device tree */
	error = OF_getencprop(node, "loongson,msi-base-vec",
	    &sc->msi_base_vec, sizeof(sc->msi_base_vec));
	if (error <= 0) {
		device_printf(dev, "Cannot get loongson,msi-base-vec\n");
		return (ENXIO);
	}

	error = OF_getencprop(node, "loongson,msi-num-vecs",
	    &sc->msi_num_vecs, sizeof(sc->msi_num_vecs));
	if (error <= 0) {
		device_printf(dev, "Cannot get loongson,msi-num-vecs\n");
		return (ENXIO);
	}

	/* Find parent interrupt controller */
	if ((intr_parent = ofw_bus_find_iparent(node)) == 0) {
		device_printf(dev,
		    "Cannot find our parent interrupt controller\n");
		return (ENXIO);
	}

	/* Allocate memory resource */
	error = bus_alloc_resources(dev, msi_spec, &sc->mem_res);
	if (error != 0) {
		device_printf(dev, "Cannot allocate memory resource\n");
		return (ENXIO);
	}

	/* Initialize isrc array; register on-demand during alloc_msix */
	isrcs = sc->isrcs;
	for (irq = 0; irq < sc->msi_num_vecs; irq++) {
		vec = sc->msi_base_vec + irq;
		/*
		 * In FDT mode, msi_base_vec from DT is the EIOINTC IRQ base.
		 * So isrc irq should be msi_base_vec + irq.
		 * For MSI data (vec), we use the same value since FDT doesn't
		 * have a separate GSI concept.
		 */
		isrcs[irq].irq = irq;  /* Local index */
		isrcs[irq].vec = vec;  /* EIOINTC IRQ number */
		isrcs[irq].global_irq = 0;  /* Will be set when allocated */
		isrcs[irq].allocated = false;
	}

	device_printf(dev, "Initialized %u MSI interrupt sources\n",
	    sc->msi_num_vecs);

	/* Register as interrupt controller */
	xref = OF_xref_from_node(node);
	if (intr_pic_register(dev, xref) == NULL) {
		device_printf(dev, "Cannot register MSI PIC\n");
		error = ENXIO;
		goto cleanup;
	}

	/* Register xref for device lookup */
	OF_device_register_xref(xref, dev);

	/* Register as child of parent EIOINTC for interrupt dispatch */
	if (intr_parent != 0) {
		device_t parent_dev;
		parent_dev = OF_device_get_xref(intr_parent);
		if (parent_dev != NULL) {
			error = intr_pic_add_handler(parent_dev, pic,
			    pchmsi_child_intr, sc,
			    sc->msi_base_vec, sc->msi_num_vecs);
			if (error != 0) {
				device_printf(dev,
				    "Cannot register as child of parent PIC\n");
				goto cleanup;
			}
		}
	}

	return (0);

cleanup:
	bus_release_resources(dev, msi_spec, &sc->mem_res);
	return (error);
}
#endif /* FDT */

/*
 * Shared MSI methods - used by both FDT and ACPI drivers
 */
static int
pchmsi_child_intr(void *arg, uintptr_t irq)
{
	struct pchmsi_softc *sc;
	struct pchmsi_irqsrc *msi_isrc;
	struct intr_irqsrc *isrc;
	int i;

	sc = (struct pchmsi_softc *)arg;

	if (irq < sc->msi_base_vec ||
	    irq >= sc->msi_base_vec + sc->msi_num_vecs) {
		return (FILTER_STRAY);
	}

	/*
	 * Find the isrc that matches this EIOINTC IRQ number.
	 * We need to search by vec (EIOINTC IRQ) because the isrcs array
	 * index (local index) doesn't correspond to the global IRQ resource ID
	 * used by bus_setup_intr.
	 */
	for (i = 0; i < sc->msi_num_vecs; i++) {
		msi_isrc = &sc->isrcs[i];
		if (msi_isrc->vec == (u_int)irq) {
			isrc = &msi_isrc->isrc;
			break;
		}
	}
	if (i >= sc->msi_num_vecs)
		return (FILTER_STRAY);

	if (isrc->isrc_event != NULL) {
		intr_isrc_dispatch(isrc, curthread->td_intr_frame);
		return (FILTER_HANDLED);
	}

	return (FILTER_STRAY);
}

static int
pchmsi_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct pchmsi_softc *sc;
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

	sc = device_get_softc(dev);

	if (irq >= sc->msi_num_vecs)
		return (EINVAL);

	*isrcp = &sc->isrcs[irq].isrc;

	return (0);
}

static void
pchmsi_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct pchmsi_softc *sc;
	struct pchmsi_irqsrc *msi_isrc;

	sc = device_get_softc(dev);
	msi_isrc = (struct pchmsi_irqsrc *)isrc;

	printf("DEBUG: pchmsi_enable_intr: ENTER, irq=%u, vec=%u, mem_res=%p\n",
	    msi_isrc->irq, msi_isrc->vec, sc->mem_res);

	/*
	 * On QEMU, the PCH-MSI MMIO region is only 8 bytes and any write
	 * triggers an MSI message (not a mask register write).
	 * However, we still need to enable the interrupt in the parent
	 * interrupt controller (EIOINTC).
	 */
	if (sc->mem_res == NULL) {
		printf("DEBUG: pchmsi_enable_intr: NO mem_res for irq=%u\n",
		    msi_isrc->irq);
		return;
	}

	/*
	 * In QEMU (VM_GUEST_KVM) mode, skip writing MSI_MASK register
	 * to avoid triggering spurious MSI messages.
	 * However, we MUST enable the interrupt in the parent EIOINTC.
	 */
	if (vm_guest == VM_GUEST_KVM) {
		printf("DEBUG: pchmsi_enable_intr: QEMU mode, enabling EIOINTC irq=%u (vec=%u)\n",
		    msi_isrc->vec, msi_isrc->vec);
		/*
		 * Enable the interrupt in the parent EIOINTC.
		 * The PCH-MSI's GPIO outputs are connected to EIOINTC inputs.
		 *
		 * IMPORTANT: msi_isrc->vec is the EIOINTC IRQ number (e.g., 64, 65, 66...),
		 * so we need to get the corresponding EIOINTC isrc and enable it there.
		 */
		if (sc->parent != NULL) {
			struct intr_irqsrc *eio_isrc;
#ifdef DEV_ACPI
			struct intr_map_data_acpi map_data = {0};

			/* Map the EIOINTC IRQ number to its isrc */
			map_data.hdr.type = INTR_MAP_DATA_ACPI;
			map_data.irq = msi_isrc->vec;

			if (PIC_MAP_INTR(sc->parent, (struct intr_map_data *)&map_data, &eio_isrc) == 0) {
				PIC_ENABLE_INTR(sc->parent, eio_isrc);
			} else {
				printf("DEBUG: pchmsi_enable_intr: Failed to map EIOINTC irq=%u\n",
				    msi_isrc->vec);
			}
#else
			printf("DEBUG: pchmsi_enable_intr: ACPI not available, cannot enable EIOINTC irq=%u\n",
			    msi_isrc->vec);
#endif
		}
		return;
	}

	/* Real hardware: write to MSI_MASK register */
	uint32_t mask;
	mask = bus_read_4(sc->mem_res, MSI_MASK);
	mask |= (1 << (msi_isrc->irq % 32));
	bus_write_4(sc->mem_res, MSI_MASK, mask);
	printf("DEBUG: pchmsi_enable_intr: wrote mask=0x%x\n", mask);
}

static void
pchmsi_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct pchmsi_softc *sc;
	struct pchmsi_irqsrc *msi_isrc;

	sc = device_get_softc(dev);
	msi_isrc = (struct pchmsi_irqsrc *)isrc;

	/* Same as enable: skip on QEMU to avoid triggering spurious MSI */
	if (sc->mem_res == NULL)
		return;

	if (vm_guest == VM_GUEST_KVM)
		return;

	uint32_t mask;
	mask = bus_read_4(sc->mem_res, MSI_MASK);
	mask &= ~(1 << (msi_isrc->irq % 32));
	bus_write_4(sc->mem_res, MSI_MASK, mask);
}

static void
pchmsi_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	pchmsi_disable_intr(dev, isrc);
}

static void
pchmsi_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
}

static void
pchmsi_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	/* Nothing to do */
}

static int
pchmsi_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	/* Nothing special to setup */
	return (0);
}

static int
pchmsi_bind_intr(device_t dev, struct intr_irqsrc *isrc)
{
	return (0);
}

/*
 * MSI domain allocation and management functions
 */

static int
pchmsi_alloc_msi_domain(device_t dev, device_t child, int count,
    int maxcount, device_t *pic, struct intr_irqsrc **srcs)
{
	struct pchmsi_softc *sc;
	int i, irq, error;

	sc = device_get_softc(dev);

	device_printf(dev, "msi_alloc_msi_domain: child=%s, count=%d, maxcount=%d\n",
	    device_get_nameunit(child), count, maxcount);

	/* Simple allocation: find consecutive free IRQs */
	for (irq = 0; irq <= sc->msi_num_vecs - count; irq++) {
		for (i = 0; i < count; i++) {
			if (sc->isrcs[irq + i].allocated)
				break;
		}
		if (i == count)
			goto found;
	}
	device_printf(dev, "msi_alloc_msi_domain: failed to find free IRQs\n");
	return (ENXIO);

found:
	*pic = dev;
	for (i = 0; i < count; i++) {
		sc->isrcs[irq + i].allocated = true;
		/* Register the isrc now to get a global IRQ number */
		error = intr_isrc_register(&sc->isrcs[irq + i].isrc, dev, 0,
		    "%s,%u", device_get_nameunit(dev), irq + i);
		if (error != 0) {
			device_printf(dev,
			    "msi_alloc_msi_domain: failed to register isrc: %d\n",
			    error);
			/* Rollback */
			while (i > 0) {
				i--;
				sc->isrcs[irq + i].allocated = false;
			}
			return (ENXIO);
		}
		srcs[i] = &sc->isrcs[irq + i].isrc;
		device_printf(dev,
		    "msi_alloc_msi_domain: child=%s allocated irq=%u, vec=%u (IRQ range [%u-%u])\n",
		    device_get_nameunit(child),
		    sc->isrcs[irq + i].irq, sc->isrcs[irq + i].vec,
		    sc->msi_base_vec, sc->msi_base_vec + sc->msi_num_vecs - 1);
	}

	return (0);
}

static int
pchmsi_alloc_msix_domain(device_t dev, device_t child,
    device_t *pic, struct intr_irqsrc **src)
{
	struct pchmsi_softc *sc;
	int irq;
	int error;

	sc = device_get_softc(dev);

	device_printf(dev, "msi_alloc_msix_domain: child=%s looking for free IRQ\n",
	    device_get_nameunit(child));

	/* Find a single free IRQ */
	for (irq = 0; irq < sc->msi_num_vecs; irq++) {
		if (!sc->isrcs[irq].allocated) {
			sc->isrcs[irq].allocated = true;
			/* Register the isrc now to get a global IRQ number */
			error = intr_isrc_register(&sc->isrcs[irq].isrc, dev, 0,
			    "%s,%u", device_get_nameunit(dev), irq);
			if (error != 0) {
				device_printf(dev,
				    "msi_alloc_msix_domain: failed to register isrc: %d\n",
				    error);
				sc->isrcs[irq].allocated = false;
				return (ENXIO);
			}
			*pic = dev;
			*src = &sc->isrcs[irq].isrc;
			device_printf(dev,
			    "msi_alloc_msix_domain: child=%s allocated irq=%u, vec=%u (EIOINTC IRQ range [%u-%u])\n",
			    device_get_nameunit(child),
			    sc->isrcs[irq].irq, sc->isrcs[irq].vec,
			    sc->msi_base_vec, sc->msi_base_vec + sc->msi_num_vecs - 1);
			return (0);
		}
	}
	device_printf(dev, "msi_alloc_msix_domain: failed to find free IRQ\n");
	return (ENXIO);
}

static int
pchmsi_release_msi_domain(device_t dev, device_t child,
    int count, struct intr_irqsrc **srcs)
{
	struct pchmsi_irqsrc *msi_isrc;
	int i;

	for (i = 0; i < count; i++) {
		msi_isrc = (struct pchmsi_irqsrc *)srcs[i];
		msi_isrc->allocated = false;
	}
	return (0);
}

static int
pchmsi_map_msi_domain(device_t dev, device_t child,
    struct intr_irqsrc *src, uint64_t *addr, uint32_t *data)
{
	struct pchmsi_softc *sc;
	struct pchmsi_irqsrc *msi_isrc;
	struct pchmsi_irqsrc *isrcs;
	u_int i;
	int irq_num;
	char irq_num_status[64];

	sc = device_get_softc(dev);
	msi_isrc = (struct pchmsi_irqsrc *)src;

	*addr = rman_get_start(sc->mem_res);
	*data = msi_isrc->vec;

	irq_num = (*data & 0xff) - sc->msi_base_vec;
	if (irq_num >= 0 && (u_int)irq_num < sc->msi_num_vecs)
		snprintf(irq_num_status, sizeof(irq_num_status), "VALID irq_num=%d", irq_num);
	else
		snprintf(irq_num_status, sizeof(irq_num_status), "INVALID irq_num=%d", irq_num);

	device_printf(dev, "pchmsi_map_msi_domain: child=%s MSI addr=0x%jx data=0x%x (%u) %s\n",
	    device_get_nameunit(child),
	    (uintmax_t)*addr, *data, *data, irq_num_status);
	device_printf(dev, "pchmsi_map_msi_domain: isrc_info: idx=%u vec=%u allocated=%s\n",
	    msi_isrc->irq, msi_isrc->vec,
	    msi_isrc->allocated ? "YES" : "NO");

	isrcs = sc->isrcs;
	device_printf(dev, "pchmsi_map_msi_domain: First 16 isrcs:\n");
	for (i = 0; i < 16; i++) {
		device_printf(dev, "  [%2u]: irq=%2u vec=%3u alloc=%s global=%u\n",
		    i, isrcs[i].irq, isrcs[i].vec,
		    isrcs[i].allocated ? "YES" : "NO",
		    isrcs[i].global_irq);
	}

	return (0);
}

static int
pchmsi_release_msix_domain(device_t dev, device_t child,
    struct intr_irqsrc *src)
{
	struct pchmsi_irqsrc *msi_isrc;

	msi_isrc = (struct pchmsi_irqsrc *)src;
	msi_isrc->allocated = false;
	return (0);
}





/*
 * FDT driver definition
 */
#ifdef FDT
static device_method_t pchmsi_fdt_methods[] = {
	DEVMETHOD(device_probe,		pchmsi_probe),
	DEVMETHOD(device_attach,		pchmsi_attach),

	DEVMETHOD(pic_enable_intr,	pchmsi_enable_intr),
	DEVMETHOD(pic_disable_intr,	pchmsi_disable_intr),
	DEVMETHOD(pic_map_intr,		pchmsi_map_intr),
	DEVMETHOD(pic_pre_ithread,	pchmsi_pre_ithread),
	DEVMETHOD(pic_post_ithread,	pchmsi_post_ithread),
	DEVMETHOD(pic_post_filter,	pchmsi_post_filter),
	DEVMETHOD(pic_setup_intr,	pchmsi_setup_intr),
	DEVMETHOD(pic_bind_intr,	pchmsi_bind_intr),

	/* MSI/MSI-X */
	DEVMETHOD(msi_alloc_msi,	pchmsi_alloc_msi_domain),
	DEVMETHOD(msi_release_msi,	pchmsi_release_msi_domain),
	DEVMETHOD(msi_alloc_msix,	pchmsi_alloc_msix_domain),
	DEVMETHOD(msi_release_msix,	pchmsi_release_msix_domain),
	DEVMETHOD(msi_map_msi,		pchmsi_map_msi_domain),

	DEVMETHOD_END
};

static driver_t pchmsi_fdt_driver = {
	"pchmsi",
	pchmsi_fdt_methods,
	sizeof(struct pchmsi_softc)
};

EARLY_DRIVER_MODULE(pchmsi, ofwbus, pchmsi_fdt_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
#endif /* FDT */

/*
 * ACPI driver definition
 */
#ifdef DEV_ACPI
static int
pchmsi_acpi_probe(device_t dev)
{
	ACPI_HANDLE h;

	if (acpi_disabled("acpi"))
		return (ENXIO);

	h = acpi_get_handle(dev);
	if (h != NULL) {
		device_set_desc(dev, "Loongson PCH MSI Controller (ACPI)");
		return (BUS_PROBE_DEFAULT);
	}

	if (device_get_unit(dev) == 0 &&
	    strcmp(device_get_name(dev), "pchmsi") == 0) {
		device_set_desc(dev, "Loongson PCH MSI Controller 2");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
pchmsi_acpi_attach(device_t dev)
{
	struct pchmsi_softc *sc;
	struct pchmsi_irqsrc *isrcs;
	uintptr_t xref;
	int error;
	u_int irq;
	vm_paddr_t msi_addr;
	device_t eiointc;
	struct intr_pic *pic;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/*
	 * Find the parent interrupt controller (EIOINTC).
	 * Look for the eiointc device by name.
	 */
	eiointc = devclass_get_device(devclass_find("eiointc"), 0);
	if (eiointc == NULL) {
		device_printf(dev, "Cannot find parent EIOINTC\n");
		return (ENXIO);
	}
	sc->parent = eiointc;
	device_printf(dev, "Found parent EIOINTC: %s\n",
	    device_get_nameunit(eiointc));

	/* Use MADT MSI_PIC information for MMIO address */
	if (loongarch_num_msi_pic > 0) {
		msi_addr = loongarch_msi_pic.msg_address;
		sc->msi_num_vecs = loongarch_msi_pic.count;
		sc->msi_base_vec = loongarch_msi_pic.start;
		device_printf(dev, "ACPI attach: MSI addr=0x%jx, vec_base=%u, count=%u\n",
		    (uintmax_t)msi_addr, sc->msi_base_vec, sc->msi_num_vecs);
	} else {
		msi_addr = VIRT_PCHMSI_ADDR_LOW;
		sc->msi_base_vec = VIRT_PCHMSI_IRQ_BASE;
		sc->msi_num_vecs = VIRT_PCHMSI_IRQ_COUNT;
		device_printf(dev, "ACPI attach: using fallback addr=0x%jx, vec_base=%u, count=%u\n",
		    (uintmax_t)msi_addr, sc->msi_base_vec, sc->msi_num_vecs);
	}

	/*
	 * Try to allocate memory resource from pre-configured resource list.
	 * For nexus children in ACPI mode, there is no pre-configured resource,
	 * so we fall back to manually setting the MMIO address from MADT.
	 */
	error = bus_alloc_resources(dev, msi_spec, &sc->mem_res);
	if (error != 0) {
		bus_set_resource(dev, SYS_RES_MEMORY, 0, msi_addr, 0x8);
		error = bus_alloc_resources(dev, msi_spec, &sc->mem_res);
		if (error != 0) {
			device_printf(dev, "Cannot allocate memory resource\n");
			return (ENXIO);
		}
	}

	/* Verify resource was allocated */
	if (sc->mem_res == NULL) {
		device_printf(dev, "Resource allocation returned NULL\n");
		return (ENXIO);
	}

	/*
	 * Initialize the isrc array. The isrcs will be registered
	 * when they are actually allocated via alloc_msix/alloc_msi.
	 *
	 * The vec values correspond to EIOINTC IRQ numbers in the range
	 * declared by the ACPI MADT MSI_PIC table (start=64, count=192).
	 * This matches the Linux loongarch64 kernel reference implementation.
	 */
	isrcs = sc->isrcs;
	for (irq = 0; irq < sc->msi_num_vecs; irq++) {
		isrcs[irq].irq = irq;  /* Local index 0, 1, 2... */
		isrcs[irq].vec = sc->msi_base_vec + irq;  /* EIOINTC IRQ */
		isrcs[irq].global_irq = 0;
		isrcs[irq].allocated = false;
	}

	device_printf(dev, "Initialized %u MSI interrupt sources (vec range %u-%u)\n",
	    sc->msi_num_vecs, sc->msi_base_vec,
	    sc->msi_base_vec + sc->msi_num_vecs - 1);

	xref = ACPI_MSI_XREF;
	pic = intr_pic_register(dev, xref);
	if (pic == NULL) {
		device_printf(dev, "Cannot register MSI PIC\n");
		error = ENXIO;
		goto cleanup;
	}
	intr_msi_register(dev, xref);

	/* Register as child of parent EIOINTC for interrupt dispatch */
	if (sc->parent != NULL) {
		error = intr_pic_add_handler(sc->parent, pic,
		    pchmsi_child_intr, sc,
		    sc->msi_base_vec, sc->msi_num_vecs);
		if (error != 0) {
			device_printf(dev,
			    "Cannot register as child of EIOINTC: %d\n", error);
			goto cleanup;
		}
		device_printf(dev, "Successfully registered as child of EIOINTC (IRQ %u-%u)\n",
		    sc->msi_base_vec, sc->msi_base_vec + sc->msi_num_vecs - 1);
	}

	return (0);

cleanup:
	bus_release_resources(dev, msi_spec, &sc->mem_res);
	return (error);
}

static device_method_t pchmsi_acpi_methods[] = {
	DEVMETHOD(device_probe,		pchmsi_acpi_probe),
	DEVMETHOD(device_attach,		pchmsi_acpi_attach),

	DEVMETHOD(pic_enable_intr,	pchmsi_enable_intr),
	DEVMETHOD(pic_disable_intr,	pchmsi_disable_intr),
	DEVMETHOD(pic_map_intr,		pchmsi_map_intr),
	DEVMETHOD(pic_pre_ithread,	pchmsi_pre_ithread),
	DEVMETHOD(pic_post_ithread,	pchmsi_post_ithread),
	DEVMETHOD(pic_post_filter,	pchmsi_post_filter),
	DEVMETHOD(pic_setup_intr,	pchmsi_setup_intr),
	DEVMETHOD(pic_bind_intr,	pchmsi_bind_intr),

	/* MSI/MSI-X */
	DEVMETHOD(msi_alloc_msi,	pchmsi_alloc_msi_domain),
	DEVMETHOD(msi_release_msi,	pchmsi_release_msi_domain),
	DEVMETHOD(msi_alloc_msix,	pchmsi_alloc_msix_domain),
	DEVMETHOD(msi_release_msix,	pchmsi_release_msix_domain),
	DEVMETHOD(msi_map_msi,		pchmsi_map_msi_domain),

	DEVMETHOD_END
};

static driver_t pchmsi_acpi_driver = {
	"pchmsi",
	pchmsi_acpi_methods,
	sizeof(struct pchmsi_softc)
};

EARLY_DRIVER_MODULE(pchmsi, nexus, pchmsi_acpi_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
#endif /* DEV_ACPI */