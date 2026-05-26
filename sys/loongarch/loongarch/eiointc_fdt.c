/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 xiaoqiang zhao <zxq_yx_007@163.com>
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

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <machine/intr.h>
#include <machine/eiointc_var.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "pic_if.h"

static int
eiointc_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "loongson,ls2k2000-eiointc"))
		return (ENXIO);

	device_set_desc(dev, "Loongson EIOINTC Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
eiointc_fdt_attach(device_t dev)
{
	struct eiointc_softc *sc;
	struct eiointc_irqsrc *isrcs;
	phandle_t node, xref, intr_parent;
	const char *name;
	device_t parent_dev;
	int error;
	u_int irq;
	u_int registered_irqs;

	sc = device_get_softc(dev);
	error = bus_alloc_resources(dev, eiointc_spec, &sc->intc_res);
	if (error != 0) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->dev = dev;
	node = ofw_bus_get_node(dev);

	if ((intr_parent = ofw_bus_find_iparent(node)) == 0) {
		device_printf(dev, "Cannot find parent interrupt controller\n");
		goto cleanup;
	}

	isrcs = sc->isrcs;
	name = device_get_nameunit(sc->dev);
	registered_irqs = 0;
	for (irq = 0; irq < EIO_INTC_MAX_IRQS; irq++) {
		isrcs[irq].irq = irq;
		error = intr_isrc_register(&isrcs[irq].isrc, sc->dev,
		    0, "%s,%u", name, irq);
		if (error != 0) {
			device_printf(dev, "could not register irq %u: %d\n",
			    irq, error);
			goto cleanup_irq;
		}
		registered_irqs++;
	}

	xref = OF_xref_from_node(node);
	sc->pic = intr_pic_register(dev, xref);
	if (sc->pic == NULL) {
		device_printf(dev, "Cannot register EIOINTC\n");
		goto cleanup;
	}

	sc->vec_count = VEC_COUNT;
	sc->node = 0;
	error = eiointc_init(sc);
	if (error < 0) {
		device_printf(dev, "eiointc_init failed\n");
		intr_pic_deregister(dev, xref);
		goto cleanup;
	}

	if (intr_parent != 0) {
		parent_dev = OF_device_from_xref(intr_parent);
		if (parent_dev != NULL) {
			error = intr_pic_add_handler(parent_dev,
			    sc->pic,
			    eiointc_child_intr, sc,
			    LOONGSON_CPU_IRQ_BASE,
			    LOONGSON_PCH_IRQ_BASE - LOONGSON_CPU_IRQ_BASE);
			if (error != 0)
				device_printf(dev,
				    "Failed to add handler to parent PIC: %d\n",
				    error);
		}
	}

	error = bus_setup_intr(dev, sc->intc_res, INTR_TYPE_CLK | INTR_MPSAFE,
	    eiointc_pic_intr, NULL, sc, &sc->intrhand);
	if (error != 0) {
		device_printf(dev, "could not setup irq handler: %d\n", error);
		intr_pic_deregister(dev, xref);
		goto cleanup;
	}

	OF_device_register_xref(xref, dev);
	return (0);

cleanup_irq:
	for (irq = 0; irq < registered_irqs; irq++)
		intr_isrc_deregister(&isrcs[irq].isrc);
cleanup:
	bus_release_resources(dev, eiointc_spec, &sc->intc_res);
	return (ENXIO);
}

static device_method_t eiointc_fdt_methods[] = {
	DEVMETHOD(device_probe,		eiointc_fdt_probe),
	DEVMETHOD(device_attach,	eiointc_fdt_attach),
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

static driver_t eiointc_fdt_driver = {
	"eiointc",
	eiointc_fdt_methods,
	sizeof(struct eiointc_softc),
};

EARLY_DRIVER_MODULE(eiointc, ofwbus, eiointc_fdt_driver, 0, 0,
    BUS_PASS_INTERRUPT);
