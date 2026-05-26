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
 
#ifndef _MACHINE_EIOINTC_VAR_H_
#define _MACHINE_EIOINTC_VAR_H_

#include <machine/intr.h>

#define	EIO_INTC_MAX_IRQS	256

struct intr_pic;

#define	EIOINTC_REG_NODEMAP	0x14a0
#define	EIOINTC_REG_IPMAP	0x14c0
#define	EIOINTC_REG_ENABLE	0x1600
#define	EIOINTC_REG_BOUNCE	0x1680
#define	EIOINTC_REG_ISR		0x1800
#define	EIOINTC_REG_ROUTE	0x1c00

#define	VEC_REG_COUNT		4
#define	VEC_COUNT_PER_REG	64
#define	VEC_COUNT		(VEC_REG_COUNT * VEC_COUNT_PER_REG)
#define	VEC_REG_IDX(irq_id)	((irq_id) / VEC_COUNT_PER_REG)
#define	VEC_REG_BIT(irq_id)	((irq_id) % VEC_COUNT_PER_REG)

#define	VEC_COUNT_PER_ENA_REG	32
#define	ENA_REG_IDX(irq_id)	((irq_id) / VEC_COUNT_PER_ENA_REG)
#define	ENA_REG_BIT(irq_id)	((irq_id) % VEC_COUNT_PER_ENA_REG)

struct eiointc_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
};

struct eiointc_softc {
	device_t		dev;
	device_t		parent;
	struct resource		*intc_res;
	void			*intrhand;
	struct intr_pic		*pic;		/* PIC handle for intr_child_irq_handler */
	cpuset_t		node_map;
	uint32_t		node;
	uint32_t		vec_count;
	struct eiointc_irqsrc	isrcs[EIO_INTC_MAX_IRQS];
#ifdef FDT
	struct intr_map_data_fdt *parent_map_data;
#endif
};

extern struct resource_spec eiointc_spec[];
extern struct eiointc_softc *eiointc_priv[];
extern int nr_pics;

int	eiointc_init(struct eiointc_softc *sc);
int	eiointc_pic_intr(void *arg);
int	eiointc_child_intr(void *arg, uintptr_t irq);
struct eiointc_softc *eiointc_get_softc(void);
int	eiointc_map_intr(device_t dev, struct intr_map_data *data,
	    struct intr_irqsrc **isrcp);
void	eiointc_disable_intr(device_t dev, struct intr_irqsrc *isrc);
void	eiointc_enable_intr(device_t dev, struct intr_irqsrc *isrc);
void	eiointc_pre_ithread(device_t dev, struct intr_irqsrc *isrc);
void	eiointc_post_ithread(device_t dev, struct intr_irqsrc *isrc);
void	eiointc_post_filter(device_t dev, struct intr_irqsrc *isrc);
int	eiointc_setup_intr(device_t dev, struct intr_irqsrc *isrc,
	    struct resource *res, struct intr_map_data *data);
int	eiointc_bind_intr(device_t dev, struct intr_irqsrc *isrc);

#endif /* _MACHINE_EIOINTC_VAR_H_ */
