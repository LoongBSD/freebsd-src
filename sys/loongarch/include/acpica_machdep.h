/*-
 * Copyright (c) 2002 Mitsuru IWASAKI
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

/******************************************************************************
 *
 * Name: acpica_machdep.h - LoongArch-specific defines, etc.
 *
 *****************************************************************************/

#ifndef __LOONGARCH_ACPICA_MACHDEP_H__
#define	__LOONGARCH_ACPICA_MACHDEP_H__

#ifdef _KERNEL

#include <machine/_bus.h>

/*
 * LoongArch uses the ACPI Reduced Hardware Model.
 * This simplifies the ACPI implementation by removing legacy hardware support.
 */
#define	ACPI_REDUCED_HARDWARE	1

/*
 * Section 5.2.10.1: global lock acquire/release functions
 * These are required for ACPI Global Lock support.
 */
int	acpi_acquire_global_lock(volatile uint32_t *);
int	acpi_release_global_lock(volatile uint32_t *);

/*
 * ACPI table mapping functions
 */
void	*acpi_map_table(vm_paddr_t pa, const char *sig);
void	acpi_unmap_table(void *table);
vm_paddr_t acpi_find_table(const char *sig);

/*
 * Generic Address Space (GAS) mapping
 */
struct acpi_generic_address;

int	acpi_map_addr(struct acpi_generic_address *, bus_space_tag_t *,
    bus_space_handle_t *, bus_size_t);

/*
 * Machine-dependent ACPI initialization
 */
int	acpi_machdep_init(device_t dev);
int	acpi_machdep_quirks(int *quirks);

/*
 * NMI handler for APEI (Advanced Platform Error Interface)
 */
extern int (*apei_nmi)(void);

#endif /* _KERNEL */

#endif /* __LOONGARCH_ACPICA_MACHDEP_H__ */
