/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * Copyright (c) 2024 Shanwei Yu <mpysw@vip.163.com>
 * Copyright (c) 2024 Xiaoqiang Zhao <zxq_yx_007@163.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 * All rights reserved.
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
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

/*
 * I/O space access macros for LoongArch
 * 
 * LoongArch uses memory-mapped I/O like ARM64, not port I/O like x86.
 */

#ifndef _MACHINE_IODEV_H_
#define	_MACHINE_IODEV_H_

#include <sys/types.h>
#include <sys/cdefs.h>
#include <machine/bus.h>

/*
 * LoongArch uses memory-mapped I/O.
 * These macros provide volatile access to I/O memory regions.
 * We use bus_space operations for proper memory barriers.
 */

static __inline uint8_t
iodev_read_1(vm_offset_t addr)
{
	volatile uint8_t *p = (volatile uint8_t *)addr;
	return (*p);
}

static __inline uint16_t
iodev_read_2(vm_offset_t addr)
{
	volatile uint16_t *p = (volatile uint16_t *)addr;
	return (*p);
}

static __inline uint32_t
iodev_read_4(vm_offset_t addr)
{
	volatile uint32_t *p = (volatile uint32_t *)addr;
	return (*p);
}

static __inline void
iodev_write_1(vm_offset_t addr, uint8_t val)
{
	volatile uint8_t *p = (volatile uint8_t *)addr;
	*p = val;
}

static __inline void
iodev_write_2(vm_offset_t addr, uint16_t val)
{
	volatile uint16_t *p = (volatile uint16_t *)addr;
	*p = val;
}

static __inline void
iodev_write_4(vm_offset_t addr, uint32_t val)
{
	volatile uint32_t *p = (volatile uint32_t *)addr;
	*p = val;
}

#endif /* _MACHINE_IODEV_H_ */
