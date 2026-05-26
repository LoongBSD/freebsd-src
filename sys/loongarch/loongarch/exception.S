/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
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

#include <machine/asm.h>
#include "assym.inc"

#include <machine/trap.h>
#include <machine/pte.h>
#include <machine/loongarchreg.h>

.altmacro
/*
 * Stack frame layout (from high to low address):
 * +------------------+ <- Initial SP
 * | GP registers     | 32 regs * 8 = 256 bytes (offset 0-255)
 * +------------------+ <- SP after first addi.d (-256)
 * | CSR registers    | 9 regs * 8 = 72 bytes (offset 256-327)
 * +------------------+ <- SP after second addi.d (-328)
 * | FP registers     | 34 regs * 8 = 272 bytes (offset 328-599)
 * +------------------+ <- Final SP (-600)
 *
 * Note: We save in order GP -> CSR -> FP, but each section uses
 * fixed offsets from the final SP position.
 */
.macro save_registers mode

.if \mode == 0	/* We came from userspace. */
	csrwr	$t0, LOONGARCH_CSR_KS1
	csrwr	$t1, LOONGARCH_CSR_KS2

	csrrd	$t0, LOONGARCH_CSR_KS0

	move	$t1, $sp
	move	$sp, $t0	/* switch to kernel sp */

	/* Allocate full trapframe (TF_SIZE includes alignment padding) */
	addi.d	$sp, $sp, -TF_SIZE

	/* save user sp */
	st.d	$t1, $sp, TF_SP
.else
	csrwr	$t0, LOONGARCH_CSR_KS1

	move	$t0, $sp

	/* Allocate full trapframe (TF_SIZE includes alignment padding) */
	addi.d	$sp, $sp, -TF_SIZE

	/* save kernel sp */
	st.d	$t0, $sp, TF_SP
.endif

	/* Save GP registers at offset 0 */
	st.d	$zero, $sp, 0 * RSIZE
	st.d	$ra, $sp, 1 * RSIZE
	st.d	$tp, $sp, 2 * RSIZE
	st.d	$a0, $sp, 4 * RSIZE
	st.d	$a1, $sp, 5 * RSIZE
	st.d	$a2, $sp, 6 * RSIZE
	st.d	$a3, $sp, 7 * RSIZE
	st.d	$a4, $sp, 8 * RSIZE
	st.d	$a5, $sp, 9 * RSIZE
	st.d	$a6, $sp, 10 * RSIZE
	st.d	$a7, $sp, 11 * RSIZE

	csrrd	$t0, LOONGARCH_CSR_KS1
	st.d	$t0, $sp, 12 * RSIZE
.if \mode == 0
	csrrd	$t1, LOONGARCH_CSR_KS2
.endif
	st.d	$t1, $sp, 13 * RSIZE

	st.d	$t2, $sp, 14 * RSIZE
	st.d	$t3, $sp, 15 * RSIZE
	st.d	$t4, $sp, 16 * RSIZE
	st.d	$t5, $sp, 17 * RSIZE
	st.d	$t6, $sp, 18 * RSIZE
	st.d	$t7, $sp, 19 * RSIZE
	st.d	$t8, $sp, 20 * RSIZE
	st.d	$r21, $sp, 21 * RSIZE
	st.d	$fp, $sp, 22 * RSIZE

	/* save static register */
	st.d	$s0, $sp, 23 * RSIZE
	st.d	$s1, $sp, 24 * RSIZE
	st.d	$s2, $sp, 25 * RSIZE
	st.d	$s3, $sp, 26 * RSIZE
	st.d	$s4, $sp, 27 * RSIZE
	st.d	$s5, $sp, 28 * RSIZE
	st.d	$s6, $sp, 29 * RSIZE
	st.d	$s7, $sp, 30 * RSIZE
	st.d	$s8, $sp, 31 * RSIZE

	/* Save CSR registers at offset GP_REG_CONTEXT_SIZE (256) */
	csrrd	$t0, LOONGARCH_CSR_CRMD
	st.d	$t0, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_CRMD * RSIZE)
	csrrd	$t0, LOONGARCH_CSR_PRMD
	st.d	$t0, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_PRMD * RSIZE)
	csrrd	$t0, LOONGARCH_CSR_EUEN
	st.d	$t0, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_EUEN * RSIZE)
	csrrd	$t0, LOONGARCH_CSR_MISC
	st.d	$t0, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_MISC * RSIZE)
	csrrd	$t0, LOONGARCH_CSR_ECFG
	st.d	$t0, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_ECFG * RSIZE)
	csrrd	$t0, LOONGARCH_CSR_ESTAT
	st.d	$t0, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_ESTAT * RSIZE)
	csrrd	$t0, LOONGARCH_CSR_ERA
	st.d	$t0, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_ERA * RSIZE)
	csrrd	$t0, LOONGARCH_CSR_BADV
	st.d	$t0, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_BADV * RSIZE)
	csrrd	$t0, LOONGARCH_CSR_BADI
	st.d	$t0, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_BADI * RSIZE)

	csrrd	$t0, LOONGARCH_CSR_KS1
	st.d	$t0, $sp, 12 * RSIZE
.if \mode == 0
	csrrd	$t1, LOONGARCH_CSR_KS2
	st.d	$t1, $sp, 13 * RSIZE
.else
	st.d	$t1, $sp, 13 * RSIZE
.endif

	/* Save FP registers at offset GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE (328) */
	csrrd	$t0, LOONGARCH_CSR_EUEN
	andi	$t0, $t0, CSR_EUEN_FPEN
	beqz	$t0, PushRegDone_\@

	fst.d  $fa0, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 0 * RSIZE)
	fst.d  $fa1, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 1 * RSIZE)
	fst.d  $fa2, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 2 * RSIZE)
	fst.d  $fa3, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 3 * RSIZE)
	fst.d  $fa4, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 4 * RSIZE)
	fst.d  $fa5, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 5 * RSIZE)
	fst.d  $fa6, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 6 * RSIZE)
	fst.d  $fa7, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 7 * RSIZE)
	fst.d  $ft0, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 8 * RSIZE)
	fst.d  $ft1, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 9 * RSIZE)
	fst.d  $ft2, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 10 * RSIZE)
	fst.d  $ft3, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 11 * RSIZE)
	fst.d  $ft4, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 12 * RSIZE)
	fst.d  $ft5, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 13 * RSIZE)
	fst.d  $ft6, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 14 * RSIZE)
	fst.d  $ft7, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 15 * RSIZE)
	fst.d  $ft8, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 16 * RSIZE)
	fst.d  $ft9, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 17 * RSIZE)
	fst.d  $ft10, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 18 * RSIZE)
	fst.d  $ft11, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 19 * RSIZE)
	fst.d  $ft12, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 20 * RSIZE)
	fst.d  $ft13, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 21 * RSIZE)
	fst.d  $ft14, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 22 * RSIZE)
	fst.d  $ft15, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 23 * RSIZE)
	fst.d  $fs0, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 24 * RSIZE)
	fst.d  $fs1, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 25 * RSIZE)
	fst.d  $fs2, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 26 * RSIZE)
	fst.d  $fs3, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 27 * RSIZE)
	fst.d  $fs4, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 28 * RSIZE)
	fst.d  $fs5, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 29 * RSIZE)
	fst.d  $fs6, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 30 * RSIZE)
	fst.d  $fs7, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 31 * RSIZE)

	movfcsr2gr  $t3, $fcsr0
	st.d        $t3, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 32 * RSIZE)

	// Push the fcc0-fcc7 registers.
	movcf2gr    $t3, $fcc0
	or          $t2, $t3, $zero
	movcf2gr    $t3, $fcc1
	bstrins.d   $t2, $t3, 0xf, 0x8
	movcf2gr    $t3, $fcc2
	bstrins.d   $t2, $t3, 0x17, 0x10
	movcf2gr    $t3, $fcc3
	bstrins.d   $t2, $t3, 0x1f, 0x18
	movcf2gr    $t3, $fcc4
	bstrins.d   $t2, $t3, 0x27, 0x20
	movcf2gr    $t3, $fcc5
	bstrins.d   $t2, $t3, 0x2f, 0x28
	movcf2gr    $t3, $fcc6
	bstrins.d   $t2, $t3, 0x37, 0x30
	movcf2gr    $t3, $fcc7
	bstrins.d   $t2, $t3, 0x3f, 0x38
	st.d        $t2, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 33 * RSIZE)
PushRegDone_\@:

.if \mode == 0
	csrrd	$r21, PERCPU_BASE_KS
.endif

.endm

.macro load_registers mode

	/* Check if FP state needs to be restored */
	ld.d	$t1, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_EUEN * RSIZE)
	csrrd	$t0, LOONGARCH_CSR_EUEN
	andi	$t0, $t0, CSR_EUEN_FPEN
	andi	$t1, $t1, CSR_EUEN_FPEN
	beq	$t0, $t1, PopRegs_\@

	/*
	 * FP state mismatch between current and saved context.
	 *
	 * t0 = current FPEN, t1 = saved FPEN
	 *
	 * Case: t0=1, t1=0 -> Current FP enabled, saved FP disabled.
	 *   We need to clear FP state before disabling FP.
	 *   FP is already enabled so fpe_state_clear is safe.
	 *
	 * Case: t0=0, t1=1 -> Current FP disabled, saved FP enabled.
	 *   We need to enable FP before restoring FP registers.
	 *   Skip fpe_state_clear since we will restore FP regs anyway.
	 */
	beqz	$t1, ClearFP_\@	/* t1=0: saved FP disabled, clear it */
	/* t1=1: saved FP enabled, enable FP now for restore */
	csrrd	$t2, LOONGARCH_CSR_EUEN
	ori	$t2, $t2, CSR_EUEN_FPEN
	csrwr	$t2, LOONGARCH_CSR_EUEN
	b	PopRegs_\@

ClearFP_\@:
	/* t1=0, t0=1: clear FP state and disable FP */
	bl	_C_LABEL(fpe_state_clear)

PopRegs_\@:

	/*
	 * Restore PRMD, EUEN, ECFG, and ERA from trapframe.
	 * CRMD is NOT restored here; the ertn instruction
	 * atomically sets CRMD.PLV from PRMD.PPLV and
	 * CRMD.IE from PRMD.PIE.
	 */
	ld.d    $t0, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_PRMD * RSIZE)
	csrwr   $t0, LOONGARCH_CSR_PRMD
	ld.d    $t0, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_EUEN * RSIZE)
	csrwr   $t0, LOONGARCH_CSR_EUEN
	ld.d    $t0, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_ECFG * RSIZE)
	csrwr   $t0, LOONGARCH_CSR_ECFG
	ld.d    $t0, $sp, (GP_REG_CONTEXT_SIZE + LOONGARCH_CSR_ERA * RSIZE)
	csrwr   $t0, LOONGARCH_CSR_ERA

	beqz    $t1, PopGP_\@                      /* If the FPE not set, only pop the GP registers. */

	/* Restore FP registers from offset GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE (328) */
	fld.d  $fa0, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 0 * RSIZE)
	fld.d  $fa1, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 1 * RSIZE)
	fld.d  $fa2, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 2 * RSIZE)
	fld.d  $fa3, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 3 * RSIZE)
	fld.d  $fa4, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 4 * RSIZE)
	fld.d  $fa5, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 5 * RSIZE)
	fld.d  $fa6, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 6 * RSIZE)
	fld.d  $fa7, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 7 * RSIZE)
	fld.d  $ft0, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 8 * RSIZE)
	fld.d  $ft1, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 9 * RSIZE)
	fld.d  $ft2, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 10 * RSIZE)
	fld.d  $ft3, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 11 * RSIZE)
	fld.d  $ft4, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 12 * RSIZE)
	fld.d  $ft5, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 13 * RSIZE)
	fld.d  $ft6, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 14 * RSIZE)
	fld.d  $ft7, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 15 * RSIZE)
	fld.d  $ft8, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 16 * RSIZE)
	fld.d  $ft9, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 17 * RSIZE)
	fld.d  $ft10, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 18 * RSIZE)
	fld.d  $ft11, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 19 * RSIZE)
	fld.d  $ft12, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 20 * RSIZE)
	fld.d  $ft13, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 21 * RSIZE)
	fld.d  $ft14, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 22 * RSIZE)
	fld.d  $ft15, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 23 * RSIZE)
	fld.d  $fs0, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 24 * RSIZE)
	fld.d  $fs1, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 25 * RSIZE)
	fld.d  $fs2, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 26 * RSIZE)
	fld.d  $fs3, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 27 * RSIZE)
	fld.d  $fs4, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 28 * RSIZE)
	fld.d  $fs5, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 29 * RSIZE)
	fld.d  $fs6, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 30 * RSIZE)
	fld.d  $fs7, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 31 * RSIZE)

	ld.d        $t0, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 32 * RSIZE)
	movgr2fcsr  $fcsr0, $t0	/* pop fcsr0 register */

	/* Pop the fcc0-fcc7 registers. */
	ld.d        $t0, $sp, (GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE + 33 * RSIZE)
	bstrpick.d  $t1, $t0, 7, 0
	movgr2cf    $fcc0, $t1
	bstrpick.d  $t1, $t0, 15, 8
	movgr2cf    $fcc1, $t1
	bstrpick.d  $t1, $t0, 23, 16
	movgr2cf    $fcc2, $t1
	bstrpick.d  $t1, $t0, 31, 24
	movgr2cf    $fcc3, $t1
	bstrpick.d  $t1, $t0, 39, 32
	movgr2cf    $fcc4, $t1
	bstrpick.d  $t1, $t0, 47, 40
	movgr2cf    $fcc5, $t1
	bstrpick.d  $t1, $t0, 55, 48
	movgr2cf    $fcc6, $t1
	bstrpick.d  $t1, $t0, 63, 56
	movgr2cf    $fcc7, $t1

PopGP_\@:
.if \mode == 0
	addi.d	$t0, $sp, TF_SIZE
	csrwr	$t0, LOONGARCH_CSR_KS0
	csrwr	$r21, PERCPU_BASE_KS
.endif
	/* Restore GP registers from offset 0 */
	ld.d    $ra, $sp, 1 * RSIZE
	ld.d    $tp, $sp, 2 * RSIZE
	ld.d    $a0, $sp, 4 * RSIZE
	ld.d    $a1, $sp, 5 * RSIZE
	ld.d    $a2, $sp, 6 * RSIZE
	ld.d    $a3, $sp, 7 * RSIZE
	ld.d    $a4, $sp, 8 * RSIZE
	ld.d    $a5, $sp, 9 * RSIZE
	ld.d    $a6, $sp, 10 * RSIZE
	ld.d    $a7, $sp, 11 * RSIZE
	ld.d    $t0, $sp, 12 * RSIZE
	ld.d    $t1, $sp, 13 * RSIZE
	ld.d    $t2, $sp, 14 * RSIZE
	ld.d    $t3, $sp, 15 * RSIZE
	ld.d    $t4, $sp, 16 * RSIZE
	ld.d    $t5, $sp, 17 * RSIZE
	ld.d    $t6, $sp, 18 * RSIZE
	ld.d    $t7, $sp, 19 * RSIZE
	ld.d    $t8, $sp, 20 * RSIZE
	ld.d    $r21, $sp, 21 * RSIZE
	ld.d    $fp, $sp, 22 * RSIZE
	ld.d    $s0, $sp, 23 * RSIZE
	ld.d    $s1, $sp, 24 * RSIZE
	ld.d    $s2, $sp, 25 * RSIZE
	ld.d    $s3, $sp, 26 * RSIZE
	ld.d    $s4, $sp, 27 * RSIZE
	ld.d    $s5, $sp, 28 * RSIZE
	ld.d    $s6, $sp, 29 * RSIZE
	ld.d    $s7, $sp, 30 * RSIZE
	ld.d    $s8, $sp, 31 * RSIZE
	ld.d    $sp, $sp, TF_SP
.endm

	.align 12
ENTRY(cpu_exception_handler)
	csrwr 	$t0, LOONGARCH_CSR_KS1
	csrrd 	$t0, LOONGARCH_CSR_PRMD
	andi 	$t0, $t0, 0x3
	beqz 	$t0, cpu_exception_handler_supervisor
	/* User mode detected */
	csrrd 	$t0, LOONGARCH_CSR_KS1
	b 	cpu_exception_handler_user
END(cpu_exception_handler)

ENTRY(cpu_exception_handler_supervisor)
	csrrd	$t0, LOONGARCH_CSR_KS1
	save_registers 1
	/*
	 * Save the trapframe pointer for passing to do_trap_supervisor.
	 * $sp currently points to the trapframe.
	 */
	move		$s0, $sp		/* Save trapframe pointer in $s0 */
	/*
	 * Create a pseudo stack frame for GDB unwinding across exception boundary.
	 * GDB expects $fp to point to:
	 *   [$fp]     = old frame pointer (0 for bottom)
	 *   [$fp + 8] = return address (ERA for interrupted function)
	 *
	 * We allocate 16 bytes on stack, save [0, ERA], and set $fp to point there.
	 * This pseudo-frame is only used for GDB unwinding during do_trap_supervisor.
	 */
	addi.d		$sp, $sp, -16
	st.d		$zero, $sp, 0			/* old $fp = 0 (stack bottom) */
	csrrd		$t0, LOONGARCH_CSR_ERA
	st.d		$t0, $sp, 8			/* return address = ERA */
	move		$fp, $sp
	move		$a0, $s0		/* Pass trapframe pointer, not pseudo-frame */
	bl	_C_LABEL(do_trap_supervisor)
	addi.d		$sp, $sp, 16			/* deallocate pseudo-frame */
	load_registers 1
	ertn
END(cpu_exception_handler_supervisor)

/*
 * handle_tlb_modify_fast - Fast path for TLBM (exc=4) from user mode.
 *
 * Hardware PTW (QEMU's pte_present checks PTE_W=bit7) has filled the
 * TLB entry with D=0 (not dirty).  We need to set PTE_D (bit 1) in
 * the page table and update the TLB entry.
 *
 * Page table walk via lddir-equivalent using bstrpick/alsl.d through
 * DMW1 window (0x9000_0000_0000_0000+phys).  Handles both 4KB and 2MB
 * huge pages.
 *
 * Entry state:
 *   KS1 = user t0
 *   KS2 = user t1
 *   TLBRSAVE = user ra
 *
 * Diagnostic struct tlbm_fast_diag (C-visible):
 *   +0:  hit count (successful ertn from fast path)
 *   +8:  fail count (routed to tlbm_fail -> do_trap_user)
 *   +16: last fail reason (1=L0,2=L1,3=L2,5=!SW_WIRED,6=!V)
 *   +24: last BADV
 *   +32: last PGDL
 *   +40: last PTE value (at failure point)
 */
ENTRY(handle_tlb_modify_fast)
	csrrd	$t0, LOONGARCH_CSR_BADV	/* t0 = faulting VA */
	csrrd	$t1, LOONGARCH_CSR_PGDL	/* t1 = L0 physical base */

	/* Save BADV and PGDL for diagnostic on failure */
	la.pcrel $ra, tlbm_fast_diag
	st.d	$t0, $ra, 24			/* diag.badv */
	st.d	$t1, $ra, 32			/* diag.pgdl */

	/* Walk L0: index = VA[47:39] */
	bstrpick.d $ra, $t0, 47, 39		/* ra = L0 index */
	alsl.d	  $t1, $t1, $ra, 3		/* t1 = PGDL + index*8 */
	lu52i.d	  $t1, $t1, 9			/* si12=9 -> DMW1 VA (0x9000_0000_0000_0000) */
	ld.d	  $t1, $t1, 0			/* t1 = L1 PTE */
	beqz	  $t1, tlbm_fail_l0

	/* Walk L1: index = VA[38:30] */
	bstrpick.d $ra, $t0, 38, 30		/* ra = L1 index */
	bstrins.d  $t1, $zero, 11, 0		/* clear flags -> phys L1 base */
	alsl.d	  $t1, $t1, $ra, 3
	lu52i.d	  $t1, $t1, 9
	ld.d	  $t1, $t1, 0			/* t1 = L2 PTE */
	beqz	  $t1, tlbm_fail_l1

	/* Walk L2: index = VA[29:21] */
	bstrpick.d $ra, $t0, 29, 21		/* ra = L2 index */
	bstrins.d  $t1, $zero, 11, 0
	alsl.d	  $t1, $t1, $ra, 3
	lu52i.d	  $t1, $t1, 9
	la.pcrel  $ra, tlbm_fast_diag
	st.d	  $t1, $ra, 64		/* save L2 DMW VA for huge writeback */
	ld.d	  $t1, $t1, 0			/* t1 = L2 entry */
	beqz	  $t1, tlbm_fail_l2
	/* Check for huge page (PTE_HUGE = bit 6) */
	bstrpick.d $ra, $t1, 6, 6
	bnez	  $ra, handle_2mb_tlb_modify

	/* Walk L3: index = VA[20:12] -> PTE address (4KB page) */
	bstrpick.d $ra, $t0, 20, 12		/* ra = L3 index */
	bstrins.d  $t1, $zero, 11, 0
	alsl.d	  $t1, $t1, $ra, 3		/* t1 = phys PTE addr */
	lu52i.d	  $t1, $t1, 9			/* t1 = DMW1 VA of PTE */

	/* Load PTE and check writability using software bit.
	 * PTE_W (bit 7) is always set for QEMU PTW compatibility;
	 * PTE_SW_WIRED (bit 8) is the real write-permission flag.
	 * Checking PTE_SW_WIRED ensures COW pages are not silently
	 * made writable by the fast path. */
	ld.d	  $t0, $t1, 0			/* t0 = PTE value */
	andi	  $ra, $t0, PTE_SW_WIRED	/* check software write permission */
	beqz	  $ra, tlbm_fail_notwritable
	/* Verify PTE is valid */
	andi	  $ra, $t0, PTE_V
	beqz	  $ra, tlbm_fail_notvalid

	/* Set PTE_D (bit 1) and write back */
	ori	  $t0, $t0, PTE_D
	st.d	  $t0, $t1, 0

	/* Set up TLBEHI for tlbsrch: VPN | ASID */
	csrrd	  $ra, LOONGARCH_CSR_BADV
	srli.d	  $ra, $ra, 13
	slli.d	  $ra, $ra, 13		/* VPN = VA & ~0x1FFF */
	csrrd	  $t0, LOONGARCH_CSR_ASID
	andi	  $t0, $t0, CSR_ASID_ASID
	or	  $ra, $ra, $t0
	csrwr	  $ra, LOONGARCH_CSR_TLBEHI

	/* Update TLB: read PTE pair, write to TLBELO */
	tlbsrch
	bstrins.d  $t1, $zero, 3, 0		/* align to 16-byte PTE pair */
	ld.d	  $t0, $t1, 0			/* Even PTE (TLBELO0) */
	ld.d	  $ra, $t1, 8			/* Odd PTE (TLBELO1) */
	csrwr	  $t0, LOONGARCH_CSR_TLBELO0
	csrwr	  $ra, LOONGARCH_CSR_TLBELO1
	tlbwr

	/* Increment hit counter */
	la.pcrel  $ra, tlbm_fast_diag
	ld.d	  $t0, $ra, 0
	addi.d	  $t0, $t0, 1
	st.d	  $t0, $ra, 0

	/* Restore user registers and return */
	csrrd	$t0, LOONGARCH_CSR_KS1		/* user t0 */
	csrrd	$t1, LOONGARCH_CSR_KS2		/* user t1 */
	csrrd	$ra, LOONGARCH_CSR_TLBRSAVE	/* user ra */
	ertn

/*
 * 2MB huge page TLBM handler.
 * L2 entry IS the huge page PTE.  Must handle differently:
 *   - PTE is at L2 (not L3), writeback to L2 DMW VA saved in diag+64
 *   - TLBIDX.PS = 0x3 (2MB)
 *   - TLBELO0/1 = modified L2 PTE (paired 2MB slots)
 *   - TLBEHI = VPN(2MB-aligned) | ASID
 */
handle_2mb_tlb_modify:
	/* t1 = L2 PTE (huge page), t0 = BADV, diag+64 = L2 DMW VA */
	andi	  $ra, $t1, PTE_SW_WIRED
	beqz	  $ra, tlbm_fail_notwritable_huge
	andi	  $ra, $t1, PTE_V
	beqz	  $ra, tlbm_fail_notvalid

	/* Set PTE_D and write back to L2 entry */
	ori	  $t1, $t1, PTE_D
	la.pcrel  $ra, tlbm_fast_diag
	ld.d	  $ra, $ra, 64		/* ra = L2 DMW VA (saved earlier) */
	st.d	  $t1, $ra, 0

	/* Set TLBIDX.PS = 0x3 (2MB) */
	li.d	  $ra, 0x03000000		/* PS=3 << CSR_TLBIDX_PS_SHIFT(24) */
	csrwr	  $ra, LOONGARCH_CSR_TLBIDX

	/* TLBEHI = VPN(2MB-aligned) | ASID */
	srli.d	  $ra, $t0, 21
	slli.d	  $ra, $ra, 21			/* VPN aligned to 2MB */
	csrrd	  $t0, LOONGARCH_CSR_ASID
	andi	  $t0, $t0, CSR_ASID_ASID
	or	  $ra, $ra, $t0
	csrwr	  $ra, LOONGARCH_CSR_TLBEHI

	/* Set TLBELO0/1 from modified L2 PTE (paired 2MB entry) */
	csrwr	  $t1, LOONGARCH_CSR_TLBELO0
	csrwr	  $t1, LOONGARCH_CSR_TLBELO1
	tlbwr

	/* Increment hit counter */
	la.pcrel  $ra, tlbm_fast_diag
	ld.d	  $t0, $ra, 0
	addi.d	  $t0, $t0, 1
	st.d	  $t0, $ra, 0

	/* Restore user registers and return */
	csrrd	$t0, LOONGARCH_CSR_KS1		/* user t0 */
	csrrd	$t1, LOONGARCH_CSR_KS2		/* user t1 */
	csrrd	$ra, LOONGARCH_CSR_TLBRSAVE	/* user ra */
	ertn

/*
 * Fail paths with detailed reason codes.
 * Reason: 1=L0 zero, 2=L1 zero, 3=L2 zero, 5=!PTE_SW_WIRED, 6=!PTE_V.
 * Code 4 (PTE_HUGE) is obsolete — huge pages are now handled above.
 */
tlbm_fail_l0:
	li.d	$ra, 1
	b	tlbm_fail_record
tlbm_fail_l1:
	li.d	$ra, 2
	b	tlbm_fail_record
tlbm_fail_l2:
	li.d	$ra, 3
	b	tlbm_fail_record
tlbm_fail_notwritable:
	li.d	$ra, 5
	la.pcrel $t1, tlbm_fast_diag
	st.d	$t0, $t1, 40			/* save PTE value */
	b	tlbm_fail_record
tlbm_fail_notwritable_huge:
	/* $t1 = L2 PTE, save to diag using $t0 as temp (t0=BADV, not needed here) */
	la.pcrel $t0, tlbm_fast_diag
	st.d	$t1, $t0, 40			/* save L2 PTE */
	li.d	$ra, 5
	b	tlbm_fail_record
tlbm_fail_notvalid:
	li.d	$ra, 6
	la.pcrel $t1, tlbm_fast_diag
	st.d	$t0, $t1, 40			/* save PTE value */
	b	tlbm_fail_record

tlbm_fail_record:
	la.pcrel $t1, tlbm_fast_diag
	st.d	$ra, $t1, 16			/* diag.reason = $ra */
	ld.d	$ra, $t1, 8			/* ra = old fail count */
	addi.d	$ra, $ra, 1
	st.d	$ra, $t1, 8			/* diag.fail++ */
	/* fall through to tlbm_fail */

tlbm_fail:
	/*
	 * Fast path failed.  Restore user registers and fall through
	 * to the normal save_registers -> do_trap_user flow.
	 */
	csrrd	$t0, LOONGARCH_CSR_KS1		/* user t0 */
	csrrd	$t1, LOONGARCH_CSR_KS2		/* user t1 */
	csrrd	$ra, LOONGARCH_CSR_TLBRSAVE	/* user ra */
	save_registers 0
	b	user_trap_continue
END(handle_tlb_modify_fast)

ENTRY(cpu_exception_handler_user)
	/*
	 * When hardware PTW is enabled, TLBM is handled entirely by
	 * pmap_fault() in the C handler.  The hardware already walked
	 * the page table and filled the TLB with D=0.  We just need to
	 * set PTE_D and invalidate the TLB entry — no need for a fast
	 * path that re-walks the page table.
	 *
	 * Save registers and let the C handler do the right thing.
	 */
	csrwr	$t0, LOONGARCH_CSR_KS1		/* KS1 = user t0 */
	csrwr	$t1, LOONGARCH_CSR_KS2		/* KS2 = user t1 */

	/* DIAG: count every user exception entry */
	la.pcrel $t0, tlbm_fast_diag
	ld.d	$t1, $t0, 48			/* user_entry_count */
	addi.d	$t1, $t1, 1
	st.d	$t1, $t0, 48

	/* Always fall through to full handler — let pmap_fault handle TLBM */
	csrrd	$t1, LOONGARCH_CSR_KS2		/* restore user t1 */
	csrrd	$t0, LOONGARCH_CSR_KS1		/* restore user t0 */
	save_registers 0
user_trap_continue:
	/*
	 * Save the trapframe pointer for passing to do_trap_user.
	 * $sp currently points to the trapframe.
	 */
	move		$s0, $sp		/* Save trapframe pointer in $s0 */
	/*
	 * Create a pseudo stack frame for GDB unwinding across exception boundary.
	 * See the comment in cpu_exception_handler_supervisor for details.
	 */
	addi.d		$sp, $sp, -16
	st.d		$zero, $sp, 0			/* old $fp = 0 (stack bottom) */
	csrrd		$t0, LOONGARCH_CSR_ERA
	st.d		$t0, $sp, 8			/* return address = ERA */
	move		$fp, $sp
	move		$a0, $s0		/* Pass trapframe pointer, not pseudo-frame */
	bl	_C_LABEL(do_trap_user)
	addi.d		$sp, $sp, 16			/* deallocate pseudo-frame */

	/*
	 * Check for ASTs before returning to userspace.
	 * Equivalent to ARM64/RISC-V do_ast.
	 * $sp points to the trapframe at this point.
	 * $r21 contains the PCPU pointer (restored in save_registers).
	 */
1:
	ld.d	$t0, $r21, PC_CURTHREAD		/* t0 = curthread */
	ld.w	$t1, $t0, TD_AST			/* t1 = td->td_ast */
	beqz	$t1, 2f				/* No AST pending, skip */
	move	$a0, $sp				/* a0 = trapframe pointer */
	bl	_C_LABEL(ast)				/* Process AST */
	/* Re-check for newly scheduled ASTs */
	b	1b
2:
	load_registers 0
	ertn
END(cpu_exception_handler_user)

/*
 * TLBM fast path diagnostic counters (C-visible).
 * tlbm_fast_diag:
 *   [ 0] hit:  successful ertn from fast path (4KB + 2MB)
 *   [ 8] fail: routed to tlbm_fail -> do_trap_user
 *   [16] last_reason: 1=L0,2=L1,3=L2 zero, 5=!SW_WIRED, 6=!V
 *   [24] last_badv: BADV at time of failure
 *   [32] last_pgdl: PGDL at time of failure
 *   [40] last_pte: PTE value at failure point
 *   [48] user_entry_count: times cpu_exception_handler_user entered
 *   [56] tlb_cnt: total TLBM exceptions counted at entry
 *   [64] saved_l2_dmw: L2 DMW VA for huge page writeback
 */
	.pushsection .data
	.align	3
	.globl	tlbm_fast_diag
tlbm_fast_diag:
	.dword	0	/* hit */
	.dword	0	/* fail */
	.dword	0	/* last_reason */
	.dword	0	/* last_badv */
	.dword	0	/* last_pgdl */
	.dword	0	/* last_pte */
	.dword	0	/* user_entry_count */
	.dword	0	/* tlb_cnt: TLBM counted at entry check */
	.dword	0	/* saved_l2_dmw: L2 DMW VA for huge writeback */
	.popsection
