/*-
 * Copyright (c) 2015-2016 Ruslan Bukin <br@bsdpad.com>
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

#ifndef	_MACHINE_PCB_H_
#define	_MACHINE_PCB_H_

#ifndef LOCORE

struct trapframe;

/*
 * PCB (Process Control Block) structure for LoongArch.
 *
 * This structure stores the saved context of a thread when it is not running.
 * It includes:
 * - General-purpose registers (GPRs)
 * - Control and status registers (CSRs)
 * - Floating-point registers (FPRs) and FCSR
 */
struct pcb {
	/*
	 * Frequently accessed fields must be placed at the beginning
	 * to ensure they are within the immediate range of ld.d/st.d [-2048, 2047].
	 * This is critical for assembly code in copyinout.S and swtch.S.
	 */

	/* Copyinout fault handler - MUST be at small offset! */
	vm_offset_t	pcb_onfault;

	/* PCB flags */
	uint64_t	pcb_a0;
	uint64_t	pcb_fpflags;
#define	PCB_FP_STARTED		0x0001	/* FPU context has been used */
#define	PCB_FP_USERMASK		0x0001	/* FPU used by user mode */
#define	PCB_FP_KERN		0x0002	/* FPU used by kernel mode */
#define	PCB_FP_NOSAVE		0x0004	/* Don't save FPU on ctx switch */

	/* General-purpose registers (32 x 8 bytes = 256 bytes) */
	union {
		uint64_t	pcb_regs[32];
		struct {
			uint64_t r0;
			uint64_t ra;
			uint64_t tp;
			uint64_t sp;
			uint64_t a[8];
			uint64_t t[9];
			uint64_t r21;
			uint64_t fp;
			uint64_t s[9];
		} u;
	};
#define pcb_a u.a
#define pcb_t u.t
#define pcb_s u.s
#define pcb_ra u.ra
#define pcb_sp u.sp
#define pcb_tp u.tp
#define pcb_fp u.fp

	/* Control and status registers (7 x 8 bytes = 56 bytes) */
	uint64_t	pcb_crmd;
	uint64_t	pcb_prmd;
	uint64_t	pcb_euen;
	uint64_t	pcb_misc;
	uint64_t	pcb_ecfg;
	uint64_t	pcb_estat;
	uint64_t	pcb_era;
	uint64_t	pcb_badvaddr;

	/* Floating-point registers (34 x 8 bytes = 272 bytes) */
	union {
		uint64_t	pcb_fregs[34];
		struct {
			uint64_t fa[8];
			uint64_t ft[16];
			uint64_t fs[8];
			uint64_t pcb_fcsr0;
		} uf;
	};
};

/*
 * PCB total size: approximately 1,104 bytes
 */
#define	PCB_SIZE	(sizeof(struct pcb))

#ifdef _KERNEL
void	makectx(struct trapframe *tf, struct pcb *pcb);
int	savectx(struct pcb *pcb) __returns_twice;
#endif

#endif /* !LOCORE */

#endif /* !_MACHINE_PCB_H_ */
