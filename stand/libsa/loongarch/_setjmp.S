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

#include <machine/asm.h>

/*
 * C library -- _setjmp, _longjmp
 *
 *	_longjmp(a,v)
 * will generate a "return(v)" from the last call to
 *	_setjmp(a)
 * by restoring registers from the environment 'a'.
 * The previous signal state is NOT restored.
 *
 * jmp_buf layout for LoongArch64:
 *   0: sp
 *   1: ra
 *   2-11: s0-s8
 *   12: magic value (not used in _setjmp/_longjmp)
 */

ENTRY(_setjmp)
	/* Store the stack pointer */
	st.d	$sp, $a0, 0
	addi.d	$a0, $a0, 8

	/* Store the general purpose registers and ra */
	st.d	$s0, $a0, (0 * 8)
	st.d	$s1, $a0, (1 * 8)
	st.d	$s2, $a0, (2 * 8)
	st.d	$s3, $a0, (3 * 8)
	st.d	$s4, $a0, (4 * 8)
	st.d	$s5, $a0, (5 * 8)
	st.d	$s6, $a0, (6 * 8)
	st.d	$s7, $a0, (7 * 8)
	st.d	$s8, $a0, (8 * 8)
	st.d	$ra, $a0, (9 * 8)

	/* Return value */
	li.d	$a0, 0
	jr	$ra
END(_setjmp)

ENTRY(_longjmp)
	/* Restore the stack pointer */
	ld.d	$sp, $a0, 0
	addi.d	$a0, $a0, 8

	/* Restore the general purpose registers and ra */
	ld.d	$s0, $a0, (0 * 8)
	ld.d	$s1, $a0, (1 * 8)
	ld.d	$s2, $a0, (2 * 8)
	ld.d	$s3, $a0, (3 * 8)
	ld.d	$s4, $a0, (4 * 8)
	ld.d	$s5, $a0, (5 * 8)
	ld.d	$s6, $a0, (6 * 8)
	ld.d	$s7, $a0, (7 * 8)
	ld.d	$s8, $a0, (8 * 8)
	ld.d	$ra, $a0, (9 * 8)

	/* Load the return value */
	ori.d	$a0, $a1, 0
	jr	$ra
END(_longjmp)
