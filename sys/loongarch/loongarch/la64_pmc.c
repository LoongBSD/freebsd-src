/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2018 Kyle Evans <kevans@FreeBSD.org>
 * Copyright (c) 2020 Jessica Clarke <jrtc27@FreeBSD.org>
 * Copyright (c) 2024 Xiaoqiang Zhao <zxq_yx_007@163.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
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
 * LoongArch64 Power Management Controller (PMC) syscon driver.
 * Used as a generic interface for various Loongson SoC power management
 * controllers and system controllers.
 * Supports both FDT and ACPI bindings.
 */

#include "opt_acpi.h"

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <machine/bus.h>

#include <dev/syscon/syscon.h>
#include <dev/syscon/syscon_generic.h>

#ifdef FDT
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

static struct ofw_compat_data compat_data[] = {
	/* Loongson SoC power management controller */
	{"loongson,ls2k0500-pmc",	1},
	{"loongson,ls2k1000-pmc",	1},
	{"loongson,ls2k2000-pmc",	1},
	/* Generic syscon fallback */
	{"syscon",			1},
	{NULL,				0}
};
#endif

#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>
#endif

static int		la64_pmc_probe(device_t dev);

static device_method_t la64_pmc_methods[] = {
	DEVMETHOD(device_probe, la64_pmc_probe),

	DEVMETHOD_END
};

DEFINE_CLASS_1(la64_pmc, la64_pmc_driver, la64_pmc_methods,
    sizeof(struct syscon_generic_softc), syscon_generic_driver);

/* la64_pmc needs to attach prior to syscon_power */
#ifdef FDT
EARLY_DRIVER_MODULE(la64_pmc, simplebus, la64_pmc_driver, 0, 0,
    BUS_PASS_SCHEDULER + BUS_PASS_ORDER_LAST);
#endif
#ifdef DEV_ACPI
EARLY_DRIVER_MODULE(la64_pmc, acpi, la64_pmc_driver, 0, 0,
    BUS_PASS_SCHEDULER + BUS_PASS_ORDER_LAST);
#endif
MODULE_VERSION(la64_pmc, 1);

static int
la64_pmc_probe(device_t dev)
{
#ifdef DEV_ACPI
	ACPI_HANDLE h;
#endif

#ifdef FDT
	/* Try FDT binding first if not on ACPI bus */
	if (ofw_bus_status_okay(dev)) {
		if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
			device_set_desc(dev, "LoongArch64 PMC syscon");
			return (BUS_PROBE_DEFAULT);
		}
	}
#endif

#ifdef DEV_ACPI
	/* Try ACPI binding */
	if (acpi_disabled("acpi"))
		return (ENXIO);

	h = acpi_get_handle(dev);
	if (h == NULL)
		return (ENXIO);

	/* Check for Loongson PMC HID */
	if (ACPI_FAILURE(acpi_MatchHid(h, "LOON0001")) &&
	    ACPI_FAILURE(acpi_MatchHid(h, "LOON0002")) &&
	    ACPI_FAILURE(acpi_MatchHid(h, "LOON0003")))
		return (ENXIO);

	/*
	 * Only match devices with memory resources.
	 * This prevents matching interrupt controllers, GPIOs, etc.
	 */
	if (bus_get_resource_count(dev, SYS_RES_MEMORY, 0) == 0)
		return (ENXIO);

	/*
	 * Check that this is not a PCI host bridge.
	 * PCI host bridges have different HID (PNP0A08/PNP0A03).
	 */
	if (ACPI_SUCCESS(acpi_MatchHid(h, "PNP0A08")) ||
	    ACPI_SUCCESS(acpi_MatchHid(h, "PNP0A03")))
		return (ENXIO);

	device_set_desc(dev, "LoongArch64 PMC syscon (ACPI)");
	return (BUS_PROBE_DEFAULT);
#else
	return (ENXIO);
#endif
}
