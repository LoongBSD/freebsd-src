/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
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
 * QEMU Firmware Configuration (fw_cfg) Device Driver
 *
 * This driver provides access to QEMU's fw_cfg interface, which allows
 * the guest to retrieve configuration information from QEMU.
 *
 * Reference: QEMU docs/specs/fw_cfg.txt
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/types.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

/* fw_cfg MMIO registers */
#define	FW_CFG_SELECTOR		0x00	/* Selector register (16-bit) */
#define	FW_CFG_DATA		0x02	/* Data register (8-bit in original, 32-bit in MMIO) */
#define	FW_CFG_DMA_ADDR_LOW	0x08	/* DMA address low 32 bits */
#define	FW_CFG_DMA_ADDR_HIGH	0x0c	/* DMA address high 32 bits */

/* fw_cfg selector keys */
#define	FW_CFG_SIGNATURE	0x0000
#define	FW_CFG_ID		0x0001
#define	FW_CFG_UUID		0x0002
#define	FW_CFG_RAM_SIZE		0x0003
#define	FW_CFG_NOGRAPHIC	0x0004
#define	FW_CFG_NB_CPUS		0x0005
#define	FW_CFG_MACHINE_ID	0x0006
#define	FW_CFG_KERNEL_ADDR	0x0007
#define	FW_CFG_KERNEL_SIZE	0x0008
#define	FW_CFG_KERNEL_CMDLINE	0x0009
#define	FW_CFG_INITRD_ADDR	0x000a
#define	FW_CFG_INITRD_SIZE	0x000b
#define	FW_CFG_BOOT_DEVICE	0x000c
#define	FW_CFG_NUMA		0x000d
#define	FW_CFG_BOOT_MENU	0x000e
#define	FW_CFG_MAX_CPUS		0x000f
#define	FW_CFG_KERNEL_ENTRY	0x0010
#define	FW_CFG_KERNEL_DATA	0x0011
#define	FW_CFG_INITRD_DATA	0x0012
#define	FW_CFG_CMDLINE_ADDR	0x0013
#define	FW_CFG_CMDLINE_SIZE	0x0014
#define	FW_CFG_CMDLINE_DATA	0x0015
#define	FW_CFG_SETUP_ADDR	0x0016
#define	FW_CFG_SETUP_SIZE	0x0017
#define	FW_CFG_SETUP_DATA	0x0018

/* File directory entry */
#define	FW_CFG_FILE_DIR		0x0019

/* Architecture-specific keys start at 0x8000 */
#define	FW_CFG_ARCH_LOCAL	0x8000

/* fw_cfg signature "QEMU" */
#define	FW_CFG_SIGNATURE_QEMU	0x554D4551	/* "QEMU" in little-endian */

struct fw_cfg_softc {
	device_t		dev;
	struct resource	*mem_res;
	struct mtx		mtx;
};

static struct resource_spec fw_cfg_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	RESOURCE_SPEC_END
};

static int
fw_cfg_probe(device_t dev)
{
	struct resource *mem_res;
	uint32_t sig;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "qemu,fw-cfg-mmio"))
		return (ENXIO);

	/*
	 * Verify signature early in probe phase to avoid attaching
	 * to non-QEMU firmware. This prevents crashes during cleanup
	 * when the device is not actually QEMU fw_cfg.
	 */
	mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, 0, RF_ACTIVE);
	if (mem_res == NULL)
		return (ENXIO);

	sig = bus_read_4(mem_res, FW_CFG_DATA);
	bus_release_resource(dev, SYS_RES_MEMORY, 0, mem_res);

	if (sig != FW_CFG_SIGNATURE_QEMU)
		return (ENXIO);

	device_set_desc(dev, "QEMU Firmware Configuration");

	return (BUS_PROBE_DEFAULT);
}

static void
fw_cfg_select(struct fw_cfg_softc *sc, uint16_t selector)
{

	bus_write_2(sc->mem_res, FW_CFG_SELECTOR, selector);
}

static uint8_t
fw_cfg_read_1(struct fw_cfg_softc *sc)
{

	return (bus_read_1(sc->mem_res, FW_CFG_DATA));
}

static uint16_t
fw_cfg_read_2(struct fw_cfg_softc *sc)
{

	return (bus_read_2(sc->mem_res, FW_CFG_DATA));
}

static uint32_t
fw_cfg_read_4(struct fw_cfg_softc *sc)
{

	return (bus_read_4(sc->mem_res, FW_CFG_DATA));
}

static void
fw_cfg_read_buf(struct fw_cfg_softc *sc, void *buf, size_t len)
{
	uint8_t *p = buf;
	size_t i;

	for (i = 0; i < len; i++)
		p[i] = fw_cfg_read_1(sc);
}

static int
fw_cfg_attach(device_t dev)
{
	struct fw_cfg_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	error = bus_alloc_resources(dev, fw_cfg_spec, &sc->mem_res);
	if (error != 0) {
		device_printf(dev, "Cannot allocate memory resource\n");
		return (ENXIO);
	}

	mtx_init(&sc->mtx, "fw_cfg", NULL, MTX_DEF);

	device_printf(dev, "QEMU fw_cfg device initialized\n");

	return (0);
}

static int
fw_cfg_detach(device_t dev)
{
	struct fw_cfg_softc *sc;

	sc = device_get_softc(dev);

	mtx_destroy(&sc->mtx);
	bus_release_resources(dev, fw_cfg_spec, &sc->mem_res);

	return (0);
}

static device_method_t fw_cfg_methods[] = {
	DEVMETHOD(device_probe,		fw_cfg_probe),
	DEVMETHOD(device_attach,	fw_cfg_attach),
	DEVMETHOD(device_detach,	fw_cfg_detach),

	DEVMETHOD_END
};

static driver_t fw_cfg_driver = {
	"fw_cfg",
	fw_cfg_methods,
	sizeof(struct fw_cfg_softc)
};

DRIVER_MODULE(fw_cfg, simplebus, fw_cfg_driver, 0, 0);
MODULE_VERSION(fw_cfg, 1);
