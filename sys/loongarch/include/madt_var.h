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

#ifndef _MACHINE_MADT_VAR_H_
#define _MACHINE_MADT_VAR_H_

#include <sys/types.h>

/*
 * Maximum number of CPUs supported via MADT CORE_PIC entries.
 * Must match the definition in acpi_madt.c.
 */
#define	MAX_CORE_PIC	2048

/*
 * CORE_PIC (Type 17) - CPU Core Interrupt Controller information.
 * Populated by acpi_parse_madt() during early boot.
 */
struct loongarch_madt_core_pic {
	uint32_t	processor_id;
	uint32_t	core_id;
	uint32_t	flags;
};

extern struct loongarch_madt_core_pic loongarch_core_pic[MAX_CORE_PIC];
extern int loongarch_num_core_pic;

/*
 * EIO_PIC (Type 20) - Extended I/O Interrupt Controller information.
 */
struct loongarch_madt_eio_pic {
	uint8_t		cascade;
	uint8_t		node;
	uint64_t	node_map;
};

extern struct loongarch_madt_eio_pic loongarch_eio_pic;
extern int loongarch_num_eio_pic;

/*
 * MSI_PIC (Type 21) - MSI Interrupt Controller information.
 */
struct loongarch_madt_msi_pic {
	uint64_t	msg_address;
	uint32_t	start;
	uint32_t	count;
};

extern struct loongarch_madt_msi_pic loongarch_msi_pic;
extern int loongarch_num_msi_pic;

/*
 * BIO_PIC (Type 22) - Bridge I/O Interrupt Controller (PCH-PIC) information.
 */
struct loongarch_madt_bio_pic {
	uint64_t	address;
	uint16_t	size;
	uint16_t	id;
	uint16_t	gsi_base;
};

extern struct loongarch_madt_bio_pic loongarch_bio_pic;
extern int loongarch_num_bio_pic;

/*
 * Parse MADT table. Called from acpi_machdep_init() after ACPI tables
 * are initialized.
 */
void acpi_parse_madt(void);

#endif /* !_MACHINE_MADT_VAR_H_ */
