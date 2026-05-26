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
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/madt_var.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/actables.h>

#include <dev/acpica/acpivar.h>

/*
 * Global storage for MADT-parsed information.
 * These are populated during early boot by acpi_parse_madt()
 * and consumed by interrupt controller drivers in later stages.
 *
 * The extern declarations are in <machine/madt_var.h>.
 */

/* CORE_PIC (Type 17) - CPU Core Interrupt Controller */
struct loongarch_madt_core_pic loongarch_core_pic[MAX_CORE_PIC];
int loongarch_num_core_pic = 0;

/* EIO_PIC (Type 20) - Extended I/O Interrupt Controller */
struct loongarch_madt_eio_pic loongarch_eio_pic;
int loongarch_num_eio_pic = 0;

/* MSI_PIC (Type 21) - MSI Interrupt Controller */
struct loongarch_madt_msi_pic loongarch_msi_pic;
int loongarch_num_msi_pic = 0;

/* BIO_PIC (Type 22) - Bridge I/O Interrupt Controller (PCH-PIC) */
struct loongarch_madt_bio_pic loongarch_bio_pic;
int loongarch_num_bio_pic = 0;

/*
 * MADT subtable type counters for diagnostics.
 */
static int madt_count_core_pic;
static int madt_count_lio_pic;
static int madt_count_ht_pic;
static int madt_count_eio_pic;
static int madt_count_msi_pic;
static int madt_count_bio_pic;
static int madt_count_lpc_pic;
static int madt_count_generic_translator;
static int madt_count_unknown;

/*
 * Callback for walking MADT subtables.
 * Records each subtable type and extracts data for LoongArch-specific types.
 */
static void
acpi_madt_subtable_handler(ACPI_SUBTABLE_HEADER *entry, void *arg __unused)
{

	switch (entry->Type) {
	case ACPI_MADT_TYPE_CORE_PIC:
		madt_count_core_pic++;
		if (madt_count_core_pic <= MAX_CORE_PIC) {
			ACPI_MADT_CORE_PIC *cp;
			cp = (ACPI_MADT_CORE_PIC *)entry;
			loongarch_core_pic[loongarch_num_core_pic].processor_id =
			    cp->ProcessorId;
			loongarch_core_pic[loongarch_num_core_pic].core_id =
			    cp->CoreId;
			loongarch_core_pic[loongarch_num_core_pic].flags =
			    cp->Flags;
			loongarch_num_core_pic++;
		}
		break;
	case ACPI_MADT_TYPE_LIO_PIC:
		madt_count_lio_pic++;
		break;
	case ACPI_MADT_TYPE_HT_PIC:
		madt_count_ht_pic++;
		break;
	case ACPI_MADT_TYPE_EIO_PIC:
		madt_count_eio_pic++;
		if (loongarch_num_eio_pic == 0) {
			ACPI_MADT_EIO_PIC *ep;
			ep = (ACPI_MADT_EIO_PIC *)entry;
			loongarch_eio_pic.cascade = ep->Cascade;
			loongarch_eio_pic.node = ep->Node;
			loongarch_eio_pic.node_map = ep->NodeMap;
			loongarch_num_eio_pic++;
		}
		break;
	case ACPI_MADT_TYPE_MSI_PIC:
		madt_count_msi_pic++;
		if (loongarch_num_msi_pic == 0) {
			ACPI_MADT_MSI_PIC *mp;
			mp = (ACPI_MADT_MSI_PIC *)entry;
			loongarch_msi_pic.msg_address = mp->MsgAddress;
			loongarch_msi_pic.start = mp->Start;
			loongarch_msi_pic.count = mp->Count;
			loongarch_num_msi_pic++;
		}
		break;
	case ACPI_MADT_TYPE_BIO_PIC:
		madt_count_bio_pic++;
		if (loongarch_num_bio_pic == 0) {
			ACPI_MADT_BIO_PIC *bp;
			bp = (ACPI_MADT_BIO_PIC *)entry;
			loongarch_bio_pic.address = bp->Address;
			loongarch_bio_pic.size = bp->Size;
			loongarch_bio_pic.id = bp->Id;
			loongarch_bio_pic.gsi_base = bp->GsiBase;
			loongarch_num_bio_pic++;
		}
		break;
	case ACPI_MADT_TYPE_LPC_PIC:
		madt_count_lpc_pic++;
		break;
	case ACPI_MADT_TYPE_GENERIC_TRANSLATOR:
		madt_count_generic_translator++;
		break;
	default:
		madt_count_unknown++;
		break;
	}
}

static int madt_parsed = 0;

/*
 * Parse the MADT (Multiple APIC Description Table) and extract
 * LoongArch-specific interrupt controller information.
 *
 * This is called from acpi_machdep_init() or nexus_acpi_attach()
 * after ACPI tables are initialized.
 * The parsed data is stored in global variables for use by
 * interrupt controller drivers (cpuintc, eiointc, pch_pic, pch_msi).
 */
void
acpi_parse_madt(void)
{
	ACPI_TABLE_MADT *madt;
	vm_paddr_t madt_pa;

	/* Idempotency guard: only parse once */
	if (madt_parsed)
		return;
	madt_parsed = 1;

	printf("ACPI: MADT parser starting (acpi_madt.c compiled OK)\n");

	printf("ACPI: Calling acpi_find_table...\n");
	madt_pa = acpi_find_table(ACPI_SIG_MADT);
	printf("ACPI: acpi_find_table returned 0x%jx\n", (uintmax_t)madt_pa);
	if (madt_pa == 0) {
		printf("ACPI: MADT not found\n");
		return;
	}

	printf("ACPI: Calling acpi_map_table...\n");
	madt = acpi_map_table(madt_pa, ACPI_SIG_MADT);
	printf("ACPI: acpi_map_table returned %p\n", (void *)madt);
	if (madt == NULL) {
		printf("ACPI: Failed to map MADT\n");
		return;
	}

	if (bootverbose)
		printf("ACPI: MADT found at 0x%jx, length %u, local APIC 0x%x\n",
		    (uintmax_t)madt_pa, madt->Header.Length, madt->Address);

	/* Walk all subtables and collect information. */
	printf("ACPI: Walking MADT subtables...\n");
	acpi_walk_subtables(madt + 1, (char *)madt + madt->Header.Length,
	    acpi_madt_subtable_handler, NULL);
	printf("ACPI: MADT subtable walk complete\n");

	acpi_unmap_table(madt);

	/* Always print summary for debugging */
	printf("ACPI: MADT subtable summary:\n");
	printf("  CORE_PIC (Type 17):        %d entries\n",
	    madt_count_core_pic);
	printf("  LIO_PIC  (Type 18):        %d entries\n",
	    madt_count_lio_pic);
	printf("  HT_PIC   (Type 19):        %d entries\n",
	    madt_count_ht_pic);
	printf("  EIO_PIC  (Type 20):        %d entries\n",
	    madt_count_eio_pic);
	printf("  MSI_PIC  (Type 21):        %d entries\n",
	    madt_count_msi_pic);
	printf("  BIO_PIC  (Type 22):        %d entries\n",
	    madt_count_bio_pic);
	printf("  LPC_PIC  (Type 23):        %d entries\n",
	    madt_count_lpc_pic);
	printf("  GENERIC_TRANSLATOR:        %d entries\n",
	    madt_count_generic_translator);
	printf("  Unknown:                   %d entries\n",
	    madt_count_unknown);

	/* Print CORE_PIC details. */
	if (loongarch_num_core_pic > 0) {
		int i;
		printf("ACPI: MADT CORE_PIC entries:\n");
		for (i = 0; i < loongarch_num_core_pic; i++) {
			printf("  CPU %d: ProcessorId=%u, CoreId=%u, Flags=0x%x (%s)\n",
			    i,
			    loongarch_core_pic[i].processor_id,
			    loongarch_core_pic[i].core_id,
			    loongarch_core_pic[i].flags,
			    (loongarch_core_pic[i].flags & ACPI_MADT_ENABLED) ?
			    "enabled" : "disabled");
		}
	}

	/* Print EIO_PIC details. */
	if (loongarch_num_eio_pic > 0) {
		printf("ACPI: MADT EIO_PIC: cascade=%u, node=%u, node_map=0x%jx\n",
		    loongarch_eio_pic.cascade,
		    loongarch_eio_pic.node,
		    (uintmax_t)loongarch_eio_pic.node_map);
	}

	/* Print MSI_PIC details. */
	if (loongarch_num_msi_pic > 0) {
		printf("ACPI: MADT MSI_PIC: address=0x%jx, start=%u, count=%u\n",
		    (uintmax_t)loongarch_msi_pic.msg_address,
		    loongarch_msi_pic.start,
		    loongarch_msi_pic.count);
	}

	/* Print BIO_PIC details. */
	if (loongarch_num_bio_pic > 0) {
		printf("ACPI: MADT BIO_PIC: address=0x%jx, size=0x%x, id=%u, gsi_base=%u\n",
		    (uintmax_t)loongarch_bio_pic.address,
		    loongarch_bio_pic.size,
		    loongarch_bio_pic.id,
		    loongarch_bio_pic.gsi_base);
	}
}
