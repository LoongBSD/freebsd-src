/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
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
 * Loongson LS7A RTC driver. The LS7A RTC uses a Time-of-Year (TOY) counter
 * that stores time in seconds since the epoch.
 * Supports both FDT and ACPI bindings.
 */

#include "opt_acpi.h"

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/types.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "clock_if.h"

#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

static struct ofw_compat_data compat_data[] = {
	{ "loongson,ls7a-rtc",		1 },
	{ "loongson,ls2k1000-rtc",	1 },
	{ "loongson,ls2k2000-rtc",	1 },
	{ "loongson,ls2k0500-rtc",	1 },
	{ "loongson,ls1b-rtc",		1 },
	{ "loongson,ls1c-rtc",		1 },
	{ NULL,				0 }
};
#endif

#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>
#endif

/* LS7A RTC registers */
#define	LS7A_RTC_TOY_WRITE0	0x00	/* Write seconds [31:0] */
#define	LS7A_RTC_TOY_WRITE1	0x04	/* Write seconds [63:32] */
#define	LS7A_RTC_TOY_READ0	0x08	/* Read seconds [31:0] */
#define	LS7A_RTC_TOY_READ1	0x0c	/* Read seconds [63:32] */

struct ls7a_rtc_softc {
	struct resource	*res;
	int		rid;
	struct mtx	mtx;
};

static int	ls7a_rtc_probe(device_t dev);
static int	ls7a_rtc_attach(device_t dev);
static int	ls7a_rtc_detach(device_t dev);

static int	ls7a_rtc_gettime(device_t dev, struct timespec *ts);
static int	ls7a_rtc_settime(device_t dev, struct timespec *ts);

static device_method_t ls7a_rtc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ls7a_rtc_probe),
	DEVMETHOD(device_attach,	ls7a_rtc_attach),
	DEVMETHOD(device_detach,	ls7a_rtc_detach),

	/* Clock interface */
	DEVMETHOD(clock_gettime,	ls7a_rtc_gettime),
	DEVMETHOD(clock_settime,	ls7a_rtc_settime),

	DEVMETHOD_END,
};

static driver_t ls7a_rtc_driver = {
	"ls7a_rtc",
	ls7a_rtc_methods,
	sizeof(struct ls7a_rtc_softc),
};

#ifdef FDT
DRIVER_MODULE(ls7a_rtc, simplebus, ls7a_rtc_driver, 0, 0);
#endif
#ifdef DEV_ACPI
DRIVER_MODULE(ls7a_rtc, acpi, ls7a_rtc_driver, 0, 0);
#endif
MODULE_VERSION(ls7a_rtc, 1);

static int
ls7a_rtc_probe(device_t dev)
{
#ifdef DEV_ACPI
	ACPI_HANDLE h;
#endif

#ifdef FDT
	/* Try FDT binding first */
	if (ofw_bus_status_okay(dev)) {
		if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
			device_set_desc(dev, "Loongson LS7A RTC");
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

	/* Check for LS7A RTC HID */
	if (ACPI_FAILURE(acpi_MatchHid(h, "LOON0004")) &&
	    ACPI_FAILURE(acpi_MatchHid(h, "LOON0005")))
		return (ENXIO);

	/*
	 * Only match devices with memory resources.
	 * This prevents matching interrupt-only devices.
	 */
	if (bus_get_resource_count(dev, SYS_RES_MEMORY, 0) == 0)
		return (ENXIO);

	device_set_desc(dev, "Loongson LS7A RTC (ACPI)");
	return (BUS_PROBE_DEFAULT);
#else
	return (ENXIO);
#endif
}

static int
ls7a_rtc_attach(device_t dev)
{
	struct ls7a_rtc_softc *sc;

	sc = device_get_softc(dev);

	sc->rid = 0;
	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->rid,
	    RF_ACTIVE);
	if (sc->res == NULL) {
		device_printf(dev, "could not allocate resource\n");
		return (ENXIO);
	}

	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	/*
	 * Register as a system realtime clock with 1 second resolution.
	 */
	clock_register_flags(dev, 1000000, CLOCKF_SETTIME_NO_ADJ);
	clock_schedule(dev, 1);

	return (0);
}

static int
ls7a_rtc_detach(device_t dev)
{
	struct ls7a_rtc_softc *sc;

	sc = device_get_softc(dev);

	clock_unregister(dev);
	mtx_destroy(&sc->mtx);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->rid, sc->res);

	return (0);
}

static int
ls7a_rtc_gettime(device_t dev, struct timespec *ts)
{
	struct ls7a_rtc_softc *sc;
	uint32_t low, high;
	uint64_t sec;

	sc = device_get_softc(dev);

	/*
	 * Reading TOY_READ1 after TOY_READ0 gives us atomicity,
	 * as TOY_READ1 captures the high 32 bits corresponding
	 * to the last TOY_READ0 read.
	 */
	mtx_lock(&sc->mtx);
	low = bus_read_4(sc->res, LS7A_RTC_TOY_READ0);
	high = bus_read_4(sc->res, LS7A_RTC_TOY_READ1);
	mtx_unlock(&sc->mtx);

	sec = ((uint64_t)high << 32) | low;
	ts->tv_sec = sec;
	ts->tv_nsec = 0;

	return (0);
}

static int
ls7a_rtc_settime(device_t dev, struct timespec *ts)
{
	struct ls7a_rtc_softc *sc;
	uint64_t sec;

	sc = device_get_softc(dev);

	/*
	 * We request a timespec with no resolution-adjustment.  That also
	 * disables utc adjustment, so apply that ourselves.
	 */
	ts->tv_sec -= utc_offset();
	sec = (uint64_t)ts->tv_sec;

	mtx_lock(&sc->mtx);
	bus_write_4(sc->res, LS7A_RTC_TOY_WRITE0, sec & 0xffffffff);
	bus_write_4(sc->res, LS7A_RTC_TOY_WRITE1, (sec >> 32) & 0xffffffff);
	mtx_unlock(&sc->mtx);

	return (0);
}
