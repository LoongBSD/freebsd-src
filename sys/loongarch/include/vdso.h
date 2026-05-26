/*-
 * Copyright 2012 Konstantin Belousov <kib@FreeBSD.ORG>.
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
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_VDSO_H_
#define	_MACHINE_VDSO_H_

/*
 * vDSO (virtual Dynamic Shared Object) definitions for LoongArch.
 *
 * The vDSO provides user-space access to certain kernel services without
 * the overhead of a system call. On LoongArch, this includes:
 * - High-resolution time access via rdtime.d instruction
 * - gettimeofday()
 * - clock_gettime()
 *
 * Based on LoongArch ELF psABI specification (lapcs.adoc).
 */

/*
 * Architecture-specific timehands data.
 * This structure is mapped to user space and contains timekeeping data.
 */
#define	VDSO_TIMEHANDS_MD			\
	uint32_t	th_res[8];		/* Reserved for future use */

/*
 * vDSO timehands algorithm identifier.
 * VDSO_TH_ALGO_1: Use rdtime.d instruction for time access.
 *
 * The LoongArch rdtime.d instruction reads the 64-bit timer value.
 */
#define	VDSO_TH_ALGO_LOONGARCH_RDTIME	VDSO_TH_ALGO_1

/*
 * vDSO function prototypes (for kernel internal use).
 * These functions are implemented in sys/loongarch/loongarch/vdso.c
 */
#ifdef _KERNEL

/*
 * Read the current counter value using rdtime.d instruction.
 * Returns: 64-bit timer value.
 */
static __inline uint64_t
vdso_gettc(void)
{
	uint64_t val;

	__asm __volatile("rdtime.d %0, $zero" : "=r" (val));
	return (val);
}

#endif /* _KERNEL */

#endif /* !_MACHINE_VDSO_H_ */
