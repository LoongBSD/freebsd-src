/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Shanwei Yu <mpysw@vip.163.com>
 * Copyright (c) 2024 Xiaoqiang Zhao <zxq_yx_007@163.com>
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
 *    distribution and/or other materials provided with the distribution.
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
 
#ifndef _MACHINE_DINTC_VAR_H_
#define _MACHINE_DINTC_VAR_H_

#include <machine/intr.h>

struct intr_pic;

#define	DINTC_MAX_IRQS		256
#define	DINTC_MAX_CPUS		256

/*
 * DINTC (Direct Message-Signaled Interrupt Controller) registers.
 * DINTC is a message-based interrupt controller that receives
 * MSI writes and dispatches them directly to CPU cores via
 * the DMSI mechanism.
 *
 * MSI address format:
 *   bits [39:28] = 0x2FF (fixed pattern)
 *   bits [19:12] = CPU number
 *   bits [11:4]  = IRQ number
 *   bits [3:0]   = reserved
 */
#define	DINTC_MSI_ADDR_FIXED	0x2FF00000UL

struct dintc_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
};

struct dintc_softc {
	device_t		dev;
	struct resource		*mem_res;
	uint32_t		nr_vecs;
	struct dintc_irqsrc	isrcs[DINTC_MAX_IRQS];
};

#endif /* _MACHINE_DINTC_VAR_H_ */
