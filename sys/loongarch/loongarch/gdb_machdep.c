/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2021 Mitchell Horne <mhorne@FreeBSD.org>
 * Copyright (c) 2024 Xiaoqiang Zhao <zxq_yx_007@163.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/signal.h>

#include <machine/frame.h>
#include <machine/gdb_machdep.h>
#include <machine/pcb.h>
#include <machine/loongarchreg.h>

#include <gdb/gdb.h>

void *
gdb_cpu_getreg(int regnum, size_t *regsz)
{
	*regsz = gdb_cpu_regsz(regnum);

	if (kdb_thread == curthread) {
		switch (regnum) {
		case GDB_REG_ZERO:	static register_t zero = 0;
					return (&zero);
		case GDB_REG_RA:	return (&kdb_frame->tf_ra);
		case GDB_REG_TP:	return (&kdb_frame->tf_tp);
		case GDB_REG_SP:	return (&kdb_frame->tf_sp);
		case GDB_REG_FP:	return (&kdb_frame->tf_s[0]);
		case GDB_REG_PC:	return (&kdb_frame->tf_era);
		case GDB_REG_CRMD:	return (&kdb_frame->tf_crmd);
		case GDB_REG_PRMD:	return (&kdb_frame->tf_prmd);
		case GDB_REG_ESTAT:	return (&kdb_frame->tf_estat);
		default:
			/* a0-a7: regnum 4-11 */
			if (regnum >= GDB_REG_A0 && regnum < GDB_REG_A0 + 8)
				return (&kdb_frame->tf_a[regnum - GDB_REG_A0]);
			/* t0-t8: regnum 12-20 */
			if (regnum >= GDB_REG_T0 && regnum < GDB_REG_T0 + 9)
				return (&kdb_frame->tf_t[regnum - GDB_REG_T0]);
			/* s0-s8: regnum 23-31 */
			if (regnum >= GDB_REG_S0 && regnum < GDB_REG_S0 + 9)
				return (&kdb_frame->tf_s[regnum - GDB_REG_S0]);
			break;
		}
	}
	switch (regnum) {
	case GDB_REG_ZERO:	static register_t zero = 0;
				return (&zero);
	case GDB_REG_RA:	return (&kdb_thrctx->pcb_ra);
	case GDB_REG_TP:	return (&kdb_thrctx->pcb_tp);
	case GDB_REG_SP:	return (&kdb_thrctx->pcb_sp);
	case GDB_REG_FP:	return (&kdb_thrctx->pcb_s[0]);
	case GDB_REG_PC:	return (&kdb_thrctx->pcb_ra);
	default:
		/* s0-s8: regnum 23-31 */
		if (regnum >= GDB_REG_S0 && regnum < GDB_REG_S0 + 9)
			return (&kdb_thrctx->pcb_s[regnum - GDB_REG_S0]);
		break;
	}

	return (NULL);
}

void
gdb_cpu_setreg(int regnum, void *val)
{
	register_t regval = *(register_t *)val;

	/* For curthread, keep the pcb and trapframe in sync. */
	if (kdb_thread == curthread) {
		switch (regnum) {
		case GDB_REG_ZERO:	/* $r0 is hardwired to zero, ignore writes */
					break;
		case GDB_REG_PC:	kdb_frame->tf_era = regval; break;
		case GDB_REG_RA:	kdb_frame->tf_ra = regval; break;
		case GDB_REG_SP:	kdb_frame->tf_sp = regval; break;
		case GDB_REG_TP:	kdb_frame->tf_tp = regval; break;
		case GDB_REG_FP:	kdb_frame->tf_s[0] = regval; break;
		case GDB_REG_CRMD:	kdb_frame->tf_crmd = regval; break;
		case GDB_REG_PRMD:	kdb_frame->tf_prmd = regval; break;
		case GDB_REG_ESTAT:	kdb_frame->tf_estat = regval; break;
		default:
			/* a0-a7: regnum 4-11 */
			if (regnum >= GDB_REG_A0 && regnum < GDB_REG_A0 + 8)
				kdb_frame->tf_a[regnum - GDB_REG_A0] = regval;
			/* t0-t8: regnum 12-20 */
			if (regnum >= GDB_REG_T0 && regnum < GDB_REG_T0 + 9)
				kdb_frame->tf_t[regnum - GDB_REG_T0] = regval;
			/* s0-s8: regnum 23-31 */
			if (regnum >= GDB_REG_S0 && regnum < GDB_REG_S0 + 9)
				kdb_frame->tf_s[regnum - GDB_REG_S0] = regval;
			break;
		}
	}
	switch (regnum) {
	case GDB_REG_ZERO:	/* $r0 is hardwired to zero, ignore writes */
				break;
	case GDB_REG_PC:	kdb_thrctx->pcb_ra = regval; break;
	case GDB_REG_RA:	kdb_thrctx->pcb_ra = regval; break;
	case GDB_REG_SP:	kdb_thrctx->pcb_sp = regval; break;
	case GDB_REG_TP:	kdb_thrctx->pcb_tp = regval; break;
	case GDB_REG_FP:	kdb_thrctx->pcb_s[0] = regval; break;
	default:
		/* s0-s8: regnum 23-31 */
		if (regnum >= GDB_REG_S0 && regnum < GDB_REG_S0 + 9)
			kdb_thrctx->pcb_s[regnum - GDB_REG_S0] = regval;
		break;
	}
}

int
gdb_cpu_signal(int type, int code)
{
	switch (type) {
	case EXCCODE_BP:
	case EXCCODE_WP:
		return (SIGTRAP);
	case EXCCODE_SYS:
		return (SIGSYS);
	case EXCCODE_INE:
	case EXCCODE_IPE:
		return (SIGILL);
	case EXCCODE_TLBL:
	case EXCCODE_TLBS:
	case EXCCODE_ADE:
	case EXCCODE_ALE:
		return (SIGSEGV);
	case EXCCODE_FPE:
		return (SIGFPE);
	default:
		return (SIGEMT);
	}
}
