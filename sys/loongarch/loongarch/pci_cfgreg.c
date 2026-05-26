/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * Copyright (c) 2024 Xiaoqiang Zhao <zxq_yx_007@163.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 * All rights reserved.
 *
 * This software was developed by Semihalf under
 * the sponsorship of the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
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
 * PCI configuration space access routines for LoongArch
 *
 * Uses ECAM (Enhanced Configuration Access Mechanism) via memory-mapped I/O.
 * The ECAM base address is read from the ACPI MCFG table at boot time.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/pci_cfgreg.h>
#include <machine/loongarchreg.h>
#include <machine/vmparam.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/actables.h>

#include <dev/acpica/acpivar.h>

/*
 * ECAM configuration.
 * For QEMU LoongArch VM, the default ECAM base is 0x20000000 (physical).
 * This maps to DMAP virtual address via PHYS_TO_DMAP macro.
 *
 * QEMU LoongArch uses a non-standard ECAM layout with slot_shift=15:
 * - Slot 0: offset 0x0000
 * - Slot 1: offset 0x8000
 * - Slot 2: offset 0x10000
 *
 * The actual value is read from ACPI MCFG table at boot time.
 */
static uintptr_t pci_ecam_base = 0;
static int pci_ecam_bus_shift = 20;	/* 1MB per bus for standard ECAM */

/*
 * Default ECAM base for QEMU virt machine.
 * Physical address 0x20000000, mapped via DMAP to virtual address.
 */
#define PCI_ECAM_DEFAULT_PADDR	0x20000000UL

/*
 * Parse MCFG table to get ECAM base address.
 * Called early during boot, before PCI configuration space access.
 */
static int
pci_cfgreg_parse_mcfg(void *dummy __unused)
{
	ACPI_TABLE_MCFG *mcfg;
	ACPI_MCFG_ALLOCATION *alloc;
	vm_paddr_t mcfg_pa;

	mcfg_pa = acpi_find_table(ACPI_SIG_MCFG);
	if (mcfg_pa == 0) {
		printf("PCI: MCFG table not found, using default ECAM base 0x%jx\n",
		    (uintmax_t)PCI_ECAM_DEFAULT_PADDR);
		return (ENXIO);
	}

	mcfg = acpi_map_table(mcfg_pa, ACPI_SIG_MCFG);
	if (mcfg == NULL) {
		printf("PCI: Failed to map MCFG table\n");
		return (ENXIO);
	}

	if (mcfg->Header.Length < sizeof(ACPI_TABLE_MCFG) + sizeof(ACPI_MCFG_ALLOCATION)) {
		printf("PCI: MCFG table too small\n");
		acpi_unmap_table(mcfg);
		return (ENXIO);
	}

	/* Get the first allocation entry */
	alloc = (ACPI_MCFG_ALLOCATION *)(mcfg + 1);

	printf("PCI: MCFG found: ECAM base=0x%jx, segment=%u, start_bus=%u, end_bus=%u\n",
	    (uintmax_t)alloc->Address, alloc->PciSegment,
	    alloc->StartBusNumber, alloc->EndBusNumber);

	/* Use the MCFG address for ECAM base */
	pci_ecam_base = (uintptr_t)PHYS_TO_DMAP(alloc->Address);

	acpi_unmap_table(mcfg);
	return (0);
}
SYSINIT(pci_cfgreg_parse_mcfg, SI_SUB_DRIVERS, SI_ORDER_FIRST, pci_cfgreg_parse_mcfg, NULL);

/*
 * Compute ECAM address for a given bus/slot/func/reg.
 *
 * QEMU LoongArch ECAM uses slot_shift=15 (32KB per slot):
 * ECAM address = base + (bus << 20) + (slot << 15) + (func << 12) + reg
 */
static __inline uintptr_t
pci_ecam_addr(int bus, int slot, int func, int reg)
{

	return ((uintptr_t)pci_ecam_base +
	    ((uintptr_t)bus << pci_ecam_bus_shift) +
	    ((uintptr_t)slot << 15) +
	    ((uintptr_t)func << 12) +
	    (reg & 0xff));
}

/*
 * Initialize access to configuration space.
 *
 * Returns 1 on success, 0 on failure.
 */
int
pci_cfgregopen(void)
{
	vm_paddr_t paddr;

	if (pci_ecam_base == 0) {
		paddr = PCI_ECAM_DEFAULT_PADDR;
		pci_ecam_base = (uintptr_t)PHYS_TO_DMAP(paddr);
		if (bootverbose)
			printf("PCI: ECAM base initialized to %p (phys: 0x%jx)\n",
			    (void *)pci_ecam_base, (uintmax_t)paddr);
	}

	return (pci_ecam_base != 0 ? 1 : 0);
}

/*
 * Read from PCI configuration space.
 *
 * bus:    PCI bus number
 * slot:   PCI slot number
 * func:   PCI function number
 * reg:    Configuration register offset
 * width:  Access width in bytes (1, 2, 4)
 * domain: PCI domain number
 */
uint32_t
pci_cfgregread(int domain, int bus, int slot, int func, int reg, int width)
{
	uintptr_t addr;
	uint32_t val;

	if (pci_ecam_base == 0)
		pci_cfgregopen();
	if (pci_ecam_base == 0)
		return (0xFFFFFFFF);

	addr = pci_ecam_addr(bus, slot, func, reg);

	switch (width) {
	case 1:
		val = *(volatile uint8_t *)addr;
		break;
	case 2:
		val = *(volatile uint16_t *)addr;
		break;
	case 4:
		val = *(volatile uint32_t *)addr;
		break;
	default:
		val = 0xFFFFFFFF;
		break;
	}

	/* DEBUG: Log MSI-X capability register reads */
	if (bus == 0 && slot == 3 && func == 0 &&
	    (reg == 0x50 || reg == 0x52 || reg == 0x54 || reg == 0x58)) {
		printf("DEBUG: pci_cfgregread: bus=%d, slot=%d, func=%d, reg=0x%x, width=%d, val=0x%x\n",
		    bus, slot, func, reg, width, val);
	}

	return (val);
}

/*
 * Write to PCI configuration space.
 *
 * bus:    PCI bus number
 * slot:   PCI slot number
 * func:   PCI function number
 * reg:    Configuration register offset
 * width:  Access width in bytes (1, 2, 4)
 * val:    Value to write
 * domain: PCI domain number
 */
void
pci_cfgregwrite(int domain, int bus, int slot, int func, int reg, int width,
    uint32_t val)
{
	uintptr_t addr;

	if (pci_ecam_base == 0)
		pci_cfgregopen();
	if (pci_ecam_base == 0)
		return;

	addr = pci_ecam_addr(bus, slot, func, reg);

	/* DEBUG: Log MSI-X control register writes */
	if (bus == 0 && slot == 3 && func == 0 &&
	    (reg == 0x50 || reg == 0x52 || reg == 0x54 || reg == 0x58)) {
		printf("DEBUG: pci_cfgregwrite: bus=%d, slot=%d, func=%d, reg=0x%x, width=%d, val=0x%x\n",
		    bus, slot, func, reg, width, val);
	}

	switch (width) {
	case 1:
		*(volatile uint8_t *)addr = (uint8_t)val;
		break;
	case 2:
		*(volatile uint16_t *)addr = (uint16_t)val;
		break;
	case 4:
		*(volatile uint32_t *)addr = val;
		break;
	default:
		break;
	}
}
