/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * Portions Copyright 2016 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#define _ASM
#define _LOCORE

#include <sys/cpuvar_defs.h>
#include <sys/dtrace.h>

#include <machine/loongarchreg.h>
#include <machine/asm.h>

#include "assym.inc"

/*
 void dtrace_membar_producer(void)
 */
ENTRY(dtrace_membar_producer)
	dbar	0
	jr	ra
END(dtrace_membar_producer)

/*
 void dtrace_membar_consumer(void)
 */
ENTRY(dtrace_membar_consumer)
	dbar	0
	jr	ra
END(dtrace_membar_consumer)

/*
 dtrace_icookie_t dtrace_interrupt_disable(void)
 */
ENTRY(dtrace_interrupt_disable)
	csrrd	a0, LOONGARCH_CSR_CRMD
	andi	a0, a0, CSR_CRMD_IE
	csrrci	zero, LOONGARCH_CSR_CRMD, CSR_CRMD_IE
	jr	ra
END(dtrace_interrupt_disable)

/*
 void dtrace_interrupt_enable(dtrace_icookie_t cookie)
 */
ENTRY(dtrace_interrupt_enable)
	csrrs	zero, LOONGARCH_CSR_CRMD, a0
	jr	ra
END(dtrace_interrupt_enable)

/*
 uint8_t
 dtrace_fuword8_nocheck(void *addr)
 */
ENTRY(dtrace_fuword8_nocheck)
	ld.bu	a0, a0, 0
	jr	ra
END(dtrace_fuword8_nocheck)

/*
 uint16_t
 dtrace_fuword16_nocheck(void *addr)
 */
ENTRY(dtrace_fuword16_nocheck)
	ld.hu	a0, a0, 0
	jr	ra
END(dtrace_fuword16_nocheck)

/*
 uint32_t
 dtrace_fuword32_nocheck(void *addr)
 */
ENTRY(dtrace_fuword32_nocheck)
	ld.wu	a0, a0, 0
	jr	ra
END(dtrace_fuword32_nocheck)

/*
 uint64_t
 dtrace_fuword64_nocheck(void *addr)
 */
ENTRY(dtrace_fuword64_nocheck)
	ld.d	a0, a0, 0
	jr	ra
END(dtrace_fuword64_nocheck)

/*
 void
 dtrace_copy(uintptr_t uaddr, uintptr_t kaddr, size_t size)
 */
ENTRY(dtrace_copy)
	beqz	a2, 2f		/* If len == 0 then skip loop */
1:
	ld.bu	t0, a0, 0	/* Load from uaddr */
	addi.d	a0, a0, 1
	st.b	t0, a1, 0	/* Store in kaddr */
	addi.d	a1, a1, 1
	addi.d	a2, a2, -1	/* len-- */
	bnez	a2, 1b
2:
	jr	ra
END(dtrace_copy)

/*
 void
 dtrace_copystr(uintptr_t uaddr, uintptr_t kaddr, size_t size,
     volatile uint16_t *flags)
 XXX: Check for flags?
 */
ENTRY(dtrace_copystr)
	beqz	a2, 3f		/* If len == 0 then skip loop */
1:
	ld.bu	t0, a0, 0	/* Load from uaddr */
	addi.d	a0, a0, 1
	st.b	t0, a1, 0	/* Store in kaddr */
	addi.d	a1, a1, 1
	beqz	t0, 2f		/* If == 0 then break */
	addi.d	a2, a2, -1	/* len-- */
	bnez	a2, 1b
2:
3:
	jr	ra
END(dtrace_copystr)

/*
 uintptr_t
 dtrace_caller(int aframes)
 */
ENTRY(dtrace_caller)
	li.d	a0, -1
	jr	ra
END(dtrace_caller)

/*
 uint32_t
 dtrace_cas32(uint32_t *target, uint32_t cmp, uint32_t new)
 */
ENTRY(dtrace_cas32)
1:
	ll.w	t0, a0, 0	/* Load target */
	bne	t0, a1, 2f	/* *target != cmp ? return */
	sc.w	t1, a2, a0	/* Store new to target */
	beqz	t1, 1b		/* Try again if store not succeed */
2:
	move	a0, t0		/* Return the value loaded from target */
	jr	ra
END(dtrace_cas32)

/*
 void *
 dtrace_casptr(volatile void *target, volatile void *cmp, volatile void *new)
 */
ENTRY(dtrace_casptr)
1:
	ll.d	t0, a0, 0	/* Load target */
	bne	t0, a1, 2f	/* *target != cmp ? return */
	sc.d	t1, a2, a0	/* Store new to target */
	beqz	t1, 1b		/* Try again if store not succeed */
2:
	move	a0, t0		/* Return the value loaded from target */
	jr	ra
END(dtrace_casptr)
