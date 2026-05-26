/*-
 * Copyright (c) 2001 Mitsuru IWASAKI
 * Copyright (c) 2015 The FreeBSD Foundation
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
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
 * ACPI machine-dependent layer for LoongArch
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/machdep.h>
#include <machine/madt_var.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/actables.h>

#include <dev/acpica/acpivar.h>

extern struct bus_space memmap_bus;

/*
 * acpi_machdep_init
 *
 * Initialize machine-dependent ACPI support.
 *
 * dev: ACPI device
 *
 * RETURN: Status (0 on success)
 */
int
acpi_machdep_init(device_t dev)
{

	/*
	 * Parse MADT table now that ACPI tables are initialized.
	 * This must be done before interrupt controller drivers attach.
	 */
	acpi_parse_madt();

	return (0);
}

/*
 * acpi_machdep_quirks
 *
 * Get machine-dependent ACPI quirks.
 *
 * quirks: Pointer to store quirk flags
 *
 * RETURN: Status (0 on success)
 */
int
acpi_machdep_quirks(int *quirks)
{

	/* No quirks needed for LoongArch */
	*quirks = 0;

	return (0);
}

/*
 * Map an ACPI table by signature.
 *
 * pa: Physical address of the table
 * sig: Expected table signature (e.g., "FACP", "DSDT")
 *
 * RETURN: Virtual address of the mapped table, or NULL on failure
 */
static void *
map_table(vm_paddr_t pa, const char *sig)
{
	ACPI_TABLE_HEADER *header;
	vm_size_t length;
	void *table;

	/* Map the table header to get the length */
	header = pmap_mapbios(pa, sizeof(ACPI_TABLE_HEADER));
	if (header == NULL)
		return (NULL);

	/* Verify the signature */
	if (strncmp(header->Signature, sig, ACPI_NAMESEG_SIZE) != 0) {
		pmap_unmapbios(header, sizeof(ACPI_TABLE_HEADER));
		return (NULL);
	}

	length = header->Length;
	pmap_unmapbios(header, sizeof(ACPI_TABLE_HEADER));

	/* Map the entire table */
	table = pmap_mapbios(pa, length);
	if (table == NULL)
		return (NULL);

	/* Verify checksum if not already done by ACPICA */
	if (ACPI_FAILURE(AcpiUtChecksum(table, length))) {
		if (bootverbose)
			printf("ACPI: Failed checksum for table %s\n", sig);
#if (ACPI_CHECKSUM_ABORT)
		pmap_unmapbios(table, length);
		return (NULL);
#endif
	}

	return (table);
}

/*
 * Check if a table at the given address matches the signature.
 *
 * address: Physical address to check
 * sig: Expected table signature
 *
 * RETURN: 1 if matches, 0 otherwise
 */
static int
probe_table(vm_paddr_t address, const char *sig)
{
	ACPI_TABLE_HEADER *table;

	table = pmap_mapbios(address, sizeof(ACPI_TABLE_HEADER));
	if (table == NULL)
		return (0);

	if (strncmp(table->Signature, sig, ACPI_NAMESEG_SIZE) != 0) {
		pmap_unmapbios(table, sizeof(ACPI_TABLE_HEADER));
		return (0);
	}

	pmap_unmapbios(table, sizeof(ACPI_TABLE_HEADER));
	return (1);
}

/*
 * acpi_unmap_table
 *
 * Unmap a previously mapped ACPI table.
 *
 * table: Virtual address returned by acpi_map_table()
 */
void
acpi_unmap_table(void *table)
{
	ACPI_TABLE_HEADER *header;

	header = (ACPI_TABLE_HEADER *)table;
	pmap_unmapbios(table, header->Length);
}

/*
 * acpi_map_table
 *
 * Map an ACPI table by physical address and signature.
 *
 * pa: Physical address of the table
 * sig: Expected table signature
 *
 * RETURN: Virtual address of the mapped table, or NULL on failure
 */
void *
acpi_map_table(vm_paddr_t pa, const char *sig)
{

	return (map_table(pa, sig));
}

/*
 * acpi_find_table
 *
 * Find an ACPI table by signature and return its physical address.
 *
 * sig: Table signature to find (e.g., "FACP", "DSDT")
 *
 * RETURN: Physical address of the table, or 0 if not found
 */
vm_paddr_t
acpi_find_table(const char *sig)
{
	ACPI_PHYSICAL_ADDRESS rsdp_ptr;
	ACPI_TABLE_RSDP *rsdp;
	ACPI_TABLE_XSDT *xsdt;
	ACPI_TABLE_HEADER *table;
	vm_paddr_t addr;
	int i, count;

	/* Check if ACPI is disabled */
	if (resource_disabled("acpi", 0))
		return (0);

	/* Get the RSDP pointer */
	rsdp_ptr = AcpiOsGetRootPointer();
	if (rsdp_ptr == 0)
		return (0);

	/* Map the RSDP */
	rsdp = pmap_mapbios(rsdp_ptr, sizeof(ACPI_TABLE_RSDP));
	if (rsdp == NULL) {
		printf("ACPI: Failed to map RSDP\n");
		return (0);
	}

	addr = 0;

	/* Check for XSDT (ACPI 2.0+) */
	if (rsdp->Revision >= 2 && rsdp->XsdtPhysicalAddress != 0) {
		/*
		 * AcpiOsGetRootPointer only verifies the checksum for
		 * the version 1.0 portion of the RSDP. Version 2.0 has
		 * an additional checksum that we verify first.
		 */
		if (AcpiUtChecksum((UINT8 *)rsdp, ACPI_RSDP_XCHECKSUM_LENGTH)) {
			printf("ACPI: RSDP failed extended checksum\n");
			pmap_unmapbios(rsdp, sizeof(ACPI_TABLE_RSDP));
			return (0);
		}

		/* Map the XSDT */
		xsdt = map_table(rsdp->XsdtPhysicalAddress, ACPI_SIG_XSDT);
		if (xsdt == NULL) {
			printf("ACPI: Failed to map XSDT\n");
			pmap_unmapbios(rsdp, sizeof(ACPI_TABLE_RSDP));
			return (0);
		}

		/* Search for the table in XSDT */
		count = (xsdt->Header.Length - sizeof(ACPI_TABLE_HEADER)) /
		    sizeof(UINT64);
		for (i = 0; i < count; i++) {
			if (probe_table(xsdt->TableOffsetEntry[i], sig)) {
				addr = xsdt->TableOffsetEntry[i];
				break;
			}
		}

		acpi_unmap_table(xsdt);
	} else {
		/*
		 * ACPI 1.0 uses RSDT instead of XSDT.
		 * LoongArch systems should use ACPI 2.0+, but handle this
		 * for completeness.
		 */
		if (bootverbose)
			printf("ACPI: Using RSDT (ACPI 1.0 compatibility)\n");
		/* RSDT support can be added if needed */
	}

	pmap_unmapbios(rsdp, sizeof(ACPI_TABLE_RSDP));

	if (addr == 0)
		return (0);

	/* Verify that we can map the full table */
	table = map_table(addr, sig);
	if (table == NULL)
		return (0);
	acpi_unmap_table(table);

	return (addr);
}

/*
 * acpi_map_addr
 *
 * Map a Generic Address Space (GAS) address to a bus space handle.
 *
 * addr: Generic Address Structure
 * tag: Bus space tag (output)
 * handle: Bus space handle (output)
 * size: Size of the region
 *
 * RETURN: Status (0 on success, ENXIO if not mappable)
 */
int
acpi_map_addr(struct acpi_generic_address *addr, bus_space_tag_t *tag,
    bus_space_handle_t *handle, bus_size_t size)
{
	bus_addr_t phys;

	/*
	 * Check the address space ID.
	 * 0 = System Memory, 1 = System I/O
	 * LoongArch only supports memory-mapped I/O.
	 */
	if (addr->SpaceId != ACPI_ADR_SPACE_SYSTEM_MEMORY)
		return (ENXIO);

	phys = (bus_addr_t)addr->Address;
	*tag = &memmap_bus;

	return (bus_space_map(*tag, phys, size, 0, handle));
}
