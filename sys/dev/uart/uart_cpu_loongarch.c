/*-
 * Copyright (c) 2016 The FreeBSD Foundation
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under sponsorship from
 * the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * LoongArch-specific UART support.
 *
 * When FDT is enabled, uart_cpu_fdt.c provides the core symbols.
 * When FDT is disabled, this file provides them with ACPI support.
 */

#include "opt_acpi.h"
#include "opt_platform.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu.h>

#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/actables.h>
#include <dev/uart/uart_cpu_acpi.h>
#endif

#ifndef FDT
/*
 * UART console routines for LoongArch (non-FDT mode).
 *
 * LoongArch uses memory-mapped I/O for UART, so uart_bus_space_io is NULL.
 * uart_bus_space_mem is initialized to &memmap_bus for early console use.
 */
extern struct bus_space memmap_bus;
bus_space_tag_t uart_bus_space_io;
bus_space_tag_t uart_bus_space_mem = &memmap_bus;

/*
 * QEMU LoongArch virt machine default UART address.
 * Used as fallback when ACPI SPCR is not available.
 */
#define	LOONGARCH_DEFAULT_UART_BASE	0x1fe001e0UL

int
uart_cpu_eqres(struct uart_bas *b1, struct uart_bas *b2)
{

	if (pmap_kextract(b1->bsh) == 0)
		return (0);
	if (pmap_kextract(b2->bsh) == 0)
		return (0);
	return ((pmap_kextract(b1->bsh) == pmap_kextract(b2->bsh)) ? 1 : 0);
}

int
uart_cpu_getdev(int devtype, struct uart_devinfo *di)
{
	struct uart_class *class;
	int err;

	/* Initialize hwmtx to NULL to prevent uart_lock from using garbage value */
	di->hwmtx = NULL;

	/* Allow overriding using the environment. */
	class = &uart_ns8250_class;
	err = uart_getenv(devtype, di, class);
	if (err == 0)
		return (0);

#ifdef DEV_ACPI
	/* Check if SPCR can tell us what console to use. */
	err = uart_cpu_acpi_setup(devtype, di);
	if (err == 0) {
		/*
		 * Workaround for QEMU LoongArch virt machine:
		 * The SPCR table may report incorrect regshft value
		 * (BitWidth=32 leads to regshft=2, but actual 16550
		 * UART has 1-byte register spacing).
		 * Force regshft to 0 for standard 16550 UART.
		 */
		di->bas.regshft = 0;
		return (0);
	}
#endif

	/*
	 * Fallback: use the default QEMU LoongArch virt UART address.
	 * This is needed when ACPI SPCR table is not present or doesn't
	 * contain a recognized UART type.
	 */
	if (devtype == UART_DEV_CONSOLE) {
		di->bas.chan = 0;
		di->bas.regshft = 0;
		di->bas.regiowidth = 1;
		di->bas.rclk = 0;
		di->baudrate = 115200;
		di->ops = uart_getops(class);
		di->databits = 8;
		di->stopbits = 1;
		di->parity = UART_PARITY_NONE;
		di->bas.bst = &memmap_bus;

		err = bus_space_map(di->bas.bst, LOONGARCH_DEFAULT_UART_BASE,
		    uart_getrange(class), 0, &di->bas.bsh);
		if (err == 0)
			return (0);
	}

	return (ENXIO);
}
#endif /* !FDT */
