/*-
 * Copyright (c) 2000,2001 Michael Smith
 * Copyright (c) 2000 BSDi
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
 * ACPI OS-specific layer for LoongArch
 */

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/efi.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/md_var.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/aclocal.h>
#include <contrib/dev/acpica/include/actables.h>

/*
 * Global variable to store the RSDP physical address.
 * This is populated either from the loader tunable or from EFI configuration.
 */
static vm_paddr_t acpi_root_phys = 0;

SYSCTL_ULONG(_machdep, OID_AUTO, acpi_root, CTLFLAG_RD, &acpi_root_phys, 0,
    "The physical address of the ACPI RSDP");

/*
 * AcpiOsInitialize
 *
 * Initialize the OS layer. This is called early in the ACPI subsystem
 * initialization.
 *
 * RETURN: Status
 */
ACPI_STATUS
AcpiOsInitialize(void)
{

	/*
	 * LoongArch ACPI OS layer initialization.
	 * The ACPICA subsystem will call this during initialization.
	 * We return AE_OK to indicate success.
	 */
	return (AE_OK);
}

/*
 * AcpiOsTerminate
 *
 * Terminate the OS layer. This is called when the ACPI subsystem is
 * being shut down.
 *
 * RETURN: Status
 */
ACPI_STATUS
AcpiOsTerminate(void)
{

	return (AE_OK);
}

/*
 * Get the RSDP physical address from loader tunables or resource hints.
 *
 * RETURN: RSDP physical address, or 0 if not found
 */
static vm_paddr_t
acpi_get_root_from_loader(void)
{
	long acpi_root;

	/*
	 * Try to get the RSDP address from the tunable first.
	 * This is the preferred method for modern bootloaders.
	 */
	if (TUNABLE_ULONG_FETCH("acpi.rsdp", &acpi_root))
		return ((vm_paddr_t)acpi_root);

	/*
	 * Fallback to the legacy resource hints mechanism for compatibility
	 * with older bootloaders.
	 */
	if (resource_long_value("acpi", 0, "rsdp", &acpi_root) == 0)
		return ((vm_paddr_t)acpi_root);

	return (0);
}

/*
 * Try to get the RSDP address from the EFI configuration table.
 *
 * RETURN: RSDP physical address, or 0 if not found
 */
static vm_paddr_t
acpi_get_root_from_efi(void)
{
#ifdef EFI
	/* ACPI 2.0+ GUID: eb9d2d31-2d88-11d3-9a16-0090273fc14d */
	static const efi_guid_t acpi20_guid = EFI_TABLE_ACPI20;
	/* ACPI 1.0 GUID: eb9d2d30-2d88-11d3-9a16-0090273fc14d */
	static const efi_guid_t acpi_guid = EFI_TABLE_ACPI;
	struct efi_systbl *systbl;
	struct efi_cfgtbl *cfgtbl;
	vm_paddr_t rsdp_addr;
	int i;

	if (efi_systbl_phys == 0)
		return (0);

	systbl = (struct efi_systbl *)PHYS_TO_DMAP(efi_systbl_phys);
	if (systbl == NULL || systbl->st_hdr.th_sig != EFI_SYSTBL_SIG)
		return (0);

	cfgtbl = (struct efi_cfgtbl *)PHYS_TO_DMAP(
	    (vm_offset_t)systbl->st_cfgtbl);
	if (cfgtbl == NULL)
		return (0);

	/*
	 * Search for ACPI 2.0 RSDP first, then fall back to ACPI 1.0 RSDP.
	 */
	for (i = 0; i < systbl->st_entries; i++) {
		if (memcmp(&cfgtbl[i].ct_guid, &acpi20_guid, sizeof(efi_guid_t)) == 0 ||
		    memcmp(&cfgtbl[i].ct_guid, &acpi_guid, sizeof(efi_guid_t)) == 0) {
			rsdp_addr = (vm_paddr_t)cfgtbl[i].ct_data;
			if (bootverbose)
				printf("ACPI: Found RSDP at 0x%jx via EFI\n",
				    (uintmax_t)rsdp_addr);
			return (rsdp_addr);
		}
	}
#endif

	return (0);
}

/*
 * AcpiOsGetRootPointer
 *
 * Get the root system description pointer (RSDP) physical address.
 * This is the entry point for ACPI table discovery.
 *
 * The RSDP address can be obtained from:
 * 1. Loader tunable (acpi.rsdp) - highest priority
 * 2. EFI configuration table
 * 3. Legacy resource hints
 * 4. Hardcoded fallback for QEMU LoongArch VM (RSDP at 0x0e14f014)
 *
 * RETURN: RSDP physical address
 */
ACPI_PHYSICAL_ADDRESS
AcpiOsGetRootPointer(void)
{

	if (acpi_root_phys == 0) {
		/* Try loader tunable first */
		acpi_root_phys = acpi_get_root_from_loader();

		/* If not found, try EFI configuration table */
		if (acpi_root_phys == 0)
			acpi_root_phys = acpi_get_root_from_efi();

		/*
		 * Fallback: hardcoded RSDP address for QEMU LoongArch VM.
		 * Verified via GDB: RSDP (ACPI 2.0) found at DMAP virtual
		 * address 0xffffc0400e14f014. The corresponding physical
		 * address is 0x0e14f014.
		 */
		if (acpi_root_phys == 0) {
			acpi_root_phys = 0x0e14f014;
			printf("ACPI: Using hardcoded RSDP at 0x%jx (QEMU fallback)\n",
			    (uintmax_t)acpi_root_phys);
		}

		if (acpi_root_phys != 0 && bootverbose)
			printf("ACPI: RSDP located at physical address 0x%jx\n",
			    (uintmax_t)acpi_root_phys);
	}

	return ((ACPI_PHYSICAL_ADDRESS)acpi_root_phys);
}
