/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2024 Shanwei Yu <mpysw@vip.163.com>
 * Copyright (c) 2024 Xiaoqiang Zhao <zxq_yx_007@163.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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

#ifndef _MACHINE_FRAME_H_
#define	_MACHINE_FRAME_H_

#ifndef LOCORE

#include <sys/signal.h>
#include <sys/ucontext.h>

/*
 * NOTE: keep this structure in sync with exception.S save/restore code.
 *
 * Structure layout must match exception.S save/restore code:
 * - GP registers: 32 regs * 8 bytes = 256 bytes (offset 0-255)
 * - CSR registers: 9 regs * 8 bytes = 72 bytes (offset 256-327)
 *   CSR offsets use LOONGARCH_CSR_* register numbers:
 *     CRMD=0, PRMD=1, EUEN=2, MISC=3, ECFG=4, ESTAT=5, ERA=6, BADV=7, BADI=8
 * - FP registers: 34 regs * 8 bytes = 272 bytes (offset 328-599)
 *
 * Based on LoongArch ELF psABI specification (lapcs.adoc).
 */
struct trapframe {
	/* General registers (offsets 0-255) */
	union {
		uint64_t tf_regs[32];
		struct {
			uint64_t tf_r0;
			uint64_t tf_ra;
			uint64_t tf_tp;
			uint64_t tf_sp;
			uint64_t tf_a[8];
			uint64_t tf_t[9];
			uint64_t tf_r21;
			uint64_t tf_fp;
			uint64_t tf_s[9];
		};
	};

	/* Special CSR registers (offsets 256-327) */
	uint64_t tf_crmd;	/* offset 256: LOONGARCH_CSR_CRMD = 0x0 */
	uint64_t tf_prmd;	/* offset 264: LOONGARCH_CSR_PRMD = 0x1 */
	uint64_t tf_euen;	/* offset 272: LOONGARCH_CSR_EUEN = 0x2 */
	uint64_t tf_misc;	/* offset 280: LOONGARCH_CSR_MISC = 0x3 */
	uint64_t tf_ecfg;	/* offset 288: LOONGARCH_CSR_ECFG = 0x4 */
	uint64_t tf_estat;	/* offset 296: LOONGARCH_CSR_ESTAT = 0x5 */
	uint64_t tf_era;	/* offset 304: LOONGARCH_CSR_ERA = 0x6 */
	uint64_t tf_badvaddr;	/* offset 312: LOONGARCH_CSR_BADV = 0x7 */
	uint64_t tf_badi;	/* offset 320: LOONGARCH_CSR_BADI = 0x8 */

	/* Floating point registers (offsets 328-599) */
	union {
		uint64_t tf_fregs[34];
		struct {
			uint64_t tf_fa[8];
			uint64_t tf_ft[16];
			uint64_t tf_fs[8];
			uint64_t tf_fcsr0;
		};
	};
};

/* Trapframe field offsets */
#define	TF_R0		0
#define	TF_RA		1
#define	TF_TP		2
#define	TF_SP		3
#define	TF_A0		4
#define	TF_A1		5
#define	TF_A2		6
#define	TF_A3		7
#define	TF_A4		8
#define	TF_A5		9
#define	TF_A6		10
#define	TF_A7		11
#define	TF_T0		12
#define	TF_T1		13
#define	TF_T2		14
#define	TF_T3		15
#define	TF_T4		16
#define	TF_T5		17
#define	TF_T6		18
#define	TF_T7		19
#define	TF_T8		20
#define	TF_R21		21
#define	TF_FP		22
#define	TF_S0		23
#define	TF_S1		24
#define	TF_S2		25
#define	TF_S3		26
#define	TF_S4		27
#define	TF_S5		28
#define	TF_S6		29
#define	TF_S7		30
#define	TF_S8		31

#define	TF_CRMD		32
#define	TF_PRMD		33
#define	TF_EUEN		34
#define	TF_MISC		35
#define	TF_ECFG		36
#define	TF_ESTAT	37
#define	TF_ERA		38
#define	TF_BADVADDR	39
#define	TF_BADI		40

#define	TF_FA0		41
#define	TF_FCSR0	74

#define	TF_SIZE		(roundup2(sizeof(struct trapframe), STACKALIGNBYTES + 1))

/*
 * Signal frame. Pushed onto user stack before calling sigcode.
 */
struct sigframe {
	siginfo_t	sf_si;	/* actual saved siginfo */
	ucontext_t	sf_uc;	/* actual saved ucontext */
};

#endif /* !LOCORE */

/* Definitions for syscalls */
#define	NARGREG		8				/* 8 args in regs */

#endif /* !_MACHINE_FRAME_H_ */
