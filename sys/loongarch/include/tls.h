/*-
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
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

#ifndef _MACHINE_TLS_H_
#define	_MACHINE_TLS_H_

#include <sys/_null.h>
#include <sys/_tls_variant_i.h>
#include <sys/types.h>

/*
 * TLS (Thread-Local Storage) definitions for LoongArch.
 * Based on LoongArch ELF psABI specification (lapcs.adoc, laelf.adoc).
 *
 * LoongArch uses TLS Variant I (TP-relative), where the thread pointer ($tp)
 * points to the end of the TCB structure.
 */

/* TLS DTV (Dynamic Thread Vector) offset from TP */
#define	TLS_DTV_OFFSET		0x800

/* TCB (Thread Control Block) alignment requirement */
#define	TLS_TCB_ALIGN		16

/* Offset from TP to the actual TLS data */
#define	TLS_TP_OFFSET		0

/*
 * Size of the TCB structure.
 * This must match the size of struct tcb defined in <sys/_tls_variant_i.h>.
 * The TCB contains:
 * - self pointer (8 bytes)
 * - dtv pointer (8 bytes)
 * - padding and architecture-specific fields
 */
#define	TLS_TCB_SIZE		sizeof(struct tcb)

/*
 * Set the thread pointer to point to the given TCB.
 * After this call, $tp = tcb + TLS_TCB_SIZE
 */
static __inline void
_tcb_set(struct tcb *tcb)
{

	__asm __volatile("addi.d $tp, %0, %1"
	    :: "r" (tcb), "I" (TLS_TCB_SIZE));
}

/*
 * Get the current TCB pointer from the thread pointer.
 * Returns: tcb = $tp - TLS_TCB_SIZE
 */
static __inline struct tcb *
_tcb_get(void)
{
	struct tcb *tcb;

	__asm __volatile("addi.d %0, $tp, %1"
	    : "=r" (tcb) : "I" (-TLS_TCB_SIZE));
	return (tcb);
}

/*
 * Initialize TCB fields.
 * This is called during thread creation to set up the TCB self-reference
 * and DTV (Dynamic Thread Vector) pointer.
 */
static __inline void
_tcb_init(struct tcb *tcb)
{

	tcb->tcb_dtv = NULL;
	tcb->tcb_thread = NULL;
}

#endif /* !_MACHINE_TLS_H_ */
