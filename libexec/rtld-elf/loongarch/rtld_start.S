/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2024 Shanwei Yu <mpysw@vip.163.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * This software was developed by the University of Cambridge Computer
 * Laboratory as part of the CTSRD Project, with support from the UK Higher
 * Education Innovation Fund (HEIF).
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
 * func_ptr_type
 * _rtld(Elf_Addr *sp, func_ptr_type *exit_proc, Obj_Entry **objp)
 */

ENTRY(.rtld_start)
	move	$s0, $a0		/* Put ps_strings in a callee-saved register */
	move	$s1, $sp		/* And the stack pointer */

	/*
	 * Self-relocation: process R_LARCH_RELATIVE relocations
	 * before entering C code.  The LoongArch LLD linker may
	 * leave GOT entries as zero instead of pre-filling them
	 * with addend values, so we must initialize the GOT
	 * before any C code that accesses global variables.
	 *
	 * We use la.pcrel to get the runtime _DYNAMIC address
	 * (PC-relative, no GOT access).  For relocbase we walk
	 * the auxiliary vector on the stack to find AT_BASE,
	 * which is the load base of ld-elf.so.1 set by the kernel.
	 */
	la.pcrel	$a0, _DYNAMIC	/* a0 = runtime _DYNAMIC */

	/*
	 * Walk the stack to find AT_BASE in the auxiliary vector.
	 * Stack layout: argc | argv[0..argc] | NULL | envp[0..N] | NULL | auxv
	 * Each auxv entry is 16 bytes: a_type (8) + a_val (8).
	 * AT_BASE = 7, AT_NULL = 0.
	 */
	ld.d	$t0, $s0, 0		/* t0 = argc */
	addi.d	$t0, $t0, 2		/* skip argc + NULL after argv[] */
	slli.d	$t0, $t0, 3		/* * 8 bytes per pointer */
	add.d	$t0, $s0, $t0		/* t0 = &envp[0] */

1:	ld.d	$t1, $t0, 0		/* envp[i] */
	addi.d	$t0, $t0, 8
	bnez	$t1, 1b			/* loop until NULL envp */

	/* Now $t0 points to the start of auxv */
	move	$a1, $zero		/* a1 = relocbase, default to 0 */

2:	ld.d	$t1, $t0, 0		/* a_type */
	beqz	$t1, 4f			/* AT_NULL -> end of auxv, use default */
	addi.d	$t2, $zero, 7		/* AT_BASE = 7 */
	beq	$t1, $t2, 3f		/* found AT_BASE */
	addi.d	$t0, $t0, 16		/* skip to next auxv entry */
	b	2b

3:	ld.d	$a1, $t0, 8		/* a1 = AT_BASE = relocbase */
4:
	bl	_rtld_relocate_nonplt_self

	addi.d	$sp, $sp, -16	/* Make room for obj_main & exit proc */

	move	$a0, $s0		/* Restore original sp for _rtld() */
	move	$a1, $sp		/* exit_proc */
	addi.d	$a2, $a1, 8	/* obj_main */
	la.pcrel	$ra, _rtld
	jirl	$ra, $ra, 0	/* Call the loader */
	move	$t0, $a0		/* Backup the entry point */

	ld.d	$a2, $sp, 0	/* Load cleanup */
	ld.d	$a1, $sp, 8	/* Load obj_main */
	move	$a0, $s0		/* Restore ps_strings */
	move	$sp, $s1		/* Restore the stack pointer */
	jr	$t0		/* Jump to the entry point */
END(.rtld_start)

/*
 * t0 = obj pointer
 * t1 = reloc offset
 */
ENTRY(_rtld_bind_start)
	/* Save the arguments and ra */
	/* We require 17 dwords, but the stack must be aligned to 16-bytes */
	addi.d	$sp, $sp, -(8 * 18)
	st.d	$a0, $sp, (8 * 0)
	st.d	$a1, $sp, (8 * 1)
	st.d	$a2, $sp, (8 * 2)
	st.d	$a3, $sp, (8 * 3)
	st.d	$a4, $sp, (8 * 4)
	st.d	$a5, $sp, (8 * 5)
	st.d	$a6, $sp, (8 * 6)
	st.d	$a7, $sp, (8 * 7)
	st.d	$ra, $sp, (8 * 8)

	/* Reloc offset is 3x of the .got.plt offset */
	slli.d	$a1, $t1, 1	/* Mult items by 2 */
	add.d	$a1, $a1, $t1	/* Plus item */

	/* Load obj */
	move	$a0, $t0

	/* Call into rtld */
	la.pcrel	$ra, _rtld_bind
	jirl	$ra, $ra, 0

	/* Backup the address to branch to */
	move	$t0, $a0

	/* Restore the arguments and ra */
	ld.d	$a0, $sp, (8 * 0)
	ld.d	$a1, $sp, (8 * 1)
	ld.d	$a2, $sp, (8 * 2)
	ld.d	$a3, $sp, (8 * 3)
	ld.d	$a4, $sp, (8 * 4)
	ld.d	$a5, $sp, (8 * 5)
	ld.d	$a6, $sp, (8 * 6)
	ld.d	$a7, $sp, (8 * 7)
	ld.d	$ra, $sp, (8 * 8)

	addi.d	$sp, $sp, (8 * 18)

	/* Call into the correct function */
	jr	$t0
END(_rtld_bind_start)
