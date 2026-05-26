/*
 * SPDX-License-Identifier: CDDL 1.0
 *
 * Copyright 2023 Christos Margiolis <christos@FreeBSD.org>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 */

#include <sys/types.h>
#include <sys/dtrace.h>

/*
 * LoongArch instructions are always 4 bytes long.
 * See LoongArch Reference Manual, Section 2.2 Instruction Formats.
 */
#define	LOONGARCH_INSN_SIZE	4

int
dtrace_instr_size(uint8_t *instr)
{

	return (LOONGARCH_INSN_SIZE);
}
