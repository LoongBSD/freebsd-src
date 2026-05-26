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

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/ttycom.h>

#include <machine/intr.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/actables.h>

#include <dev/acpica/acpivar.h>

/*
 * SPCR (Serial Port Console Redirection Table) parsed information.
 */
struct spcr_info {
	uint8_t		interface_type;
	uint64_t	base_addr;
	uint32_t	interrupt;
	uint32_t	baud_rate;
	uint8_t		parity;
	uint8_t		stop_bits;
};

static struct spcr_info spcr_data;
static int spcr_parsed = 0;

/*
 * Baud rate encoding (from ACPI spec):
 * 0 = 9600
 * 1 = 19200
 * 2 = 57600
 * 3 = 115200
 * 6 = 1200
 * 7 = 115200 (same as 3, but commonly used)
 */
static uint32_t
spcr_baud_rate(uint8_t rate)
{

	switch (rate) {
	case 0: return (9600);
	case 1: return (19200);
	case 2: return (57600);
	case 3: return (115200);
	case 6: return (1200);
	case 7: return (115200);
	default: return (0);
	}
}

/*
 * Parse SPCR table and store information for console setup.
 */
static int
acpi_parse_spcr(void *dummy __unused)
{
	ACPI_TABLE_SPCR *spcr;
	vm_paddr_t spcr_pa;

	spcr_pa = acpi_find_table(ACPI_SIG_SPCR);
	if (spcr_pa == 0) {
		printf("ACPI: SPCR table not found\n");
		return (ENXIO);
	}

	spcr = acpi_map_table(spcr_pa, ACPI_SIG_SPCR);
	if (spcr == NULL) {
		printf("ACPI: Failed to map SPCR table\n");
		return (ENXIO);
	}

	printf("ACPI: SPCR found at 0x%jx\n", (uintmax_t)spcr_pa);
	printf("ACPI: SPCR interface_type=%u, interrupt=%u, baud_rate=%u\n",
	    spcr->InterfaceType, spcr->Interrupt, spcr->BaudRate);
	printf("ACPI: SPCR base_addr: space_id=%u, bit_width=%u, address=0x%jx\n",
	    spcr->SerialPort.SpaceId,
	    spcr->SerialPort.BitWidth,
	    (uintmax_t)spcr->SerialPort.Address);

	/* Store parsed information */
	spcr_data.interface_type = spcr->InterfaceType;
	spcr_data.base_addr = spcr->SerialPort.Address;
	spcr_data.interrupt = spcr->Interrupt;
	spcr_data.baud_rate = spcr_baud_rate(spcr->BaudRate);
	spcr_data.parity = spcr->Parity;
	spcr_data.stop_bits = spcr->StopBits;
	spcr_parsed = 1;

	acpi_unmap_table(spcr);
	return (0);
}

/*
 * Parse SPCR after ACPI tables are initialized.
 */
SYSINIT(acpi_parse_spcr, SI_SUB_DRIVERS, SI_ORDER_SECOND, acpi_parse_spcr, NULL);

/*
 * Get SPCR information for console setup.
 */
static int
acpi_get_spcr_info(uint64_t *base_addr, uint32_t *interrupt,
    uint32_t *baud_rate)
{

	if (!spcr_parsed)
		return (ENXIO);

	if (base_addr != NULL)
		*base_addr = spcr_data.base_addr;
	if (interrupt != NULL)
		*interrupt = spcr_data.interrupt;
	if (baud_rate != NULL)
		*baud_rate = spcr_data.baud_rate;

	return (0);
}
