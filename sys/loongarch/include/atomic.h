/*-
 * Copyright (c) 2015-2024 Ruslan Bukin <br@bsdpad.com>
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

#ifndef	_MACHINE_ATOMIC_H_
#define	_MACHINE_ATOMIC_H_

#include <sys/atomic_common.h>

/*
 * LoongArch dbar instruction hint encoding.
 * Refer to LoongArch Reference Manual for dbar instruction semantics.
 *
 * The hint operand is a 5-bit immediate:
 *Bit4: Ordering (0: completion, 1: ordering)
 *Bit3: Previous read barrier (0: yes, 1: no)
 *Bit2: Previous write barrier (0: yes, 1: no)
 *Bit1: Succeeding read barrier (0: yes, 1: no)
 *Bit0: Succeeding write barrier (0: yes, 1: no)
 */
#define	dbar(hint)	__asm __volatile("dbar %0" : : "I"(hint) : "memory")

#define	mb()	dbar(0x00)	/* Full completion barrier */
#define	rmb()	dbar(0x05)	/* Read completion barrier */
#define	wmb()	dbar(0x0a)	/* Write completion barrier */
#define	fence()	mb()

#define	iob()	mb()		/* I/O barrier */
#define	wbflush()	mb()	/* Write buffer flush */

/* Memory ordering barriers */
#define	__smp_mb()	dbar(0x10)	/* Full ordering barrier */
#define	__smp_rmb()	dbar(0x15)	/* Read ordering barrier */
#define	__smp_wmb()	dbar(0x1a)	/* Write ordering barrier */

/* Acquire/Release barriers for atomic operations */
#define	ldacq_mb()	dbar(0x14)	/* Acquire barrier */
#define	strel_mb()	dbar(0x12)	/* Release barrier */

static __inline int atomic_cmpset_8(__volatile uint8_t *, uint8_t, uint8_t);
static __inline int atomic_fcmpset_8(__volatile uint8_t *, uint8_t *, uint8_t);
static __inline int atomic_cmpset_16(__volatile uint16_t *, uint16_t, uint16_t);
static __inline int atomic_fcmpset_16(__volatile uint16_t *, uint16_t *,
    uint16_t);

#define	ATOMIC_ACQ_REL(NAME, WIDTH)					\
static __inline  void							\
atomic_##NAME##_acq_##WIDTH(__volatile uint##WIDTH##_t *p, uint##WIDTH##_t v)\
{									\
	atomic_##NAME##_##WIDTH(p, v);					\
	ldacq_mb(); 							\
}									\
									\
static __inline  void							\
atomic_##NAME##_rel_##WIDTH(__volatile uint##WIDTH##_t *p, uint##WIDTH##_t v)\
{									\
	strel_mb();							\
	atomic_##NAME##_##WIDTH(p, v);					\
}

#define	ATOMIC_CMPSET_ACQ_REL(WIDTH)					\
static __inline  int							\
atomic_cmpset_acq_##WIDTH(__volatile uint##WIDTH##_t *p,		\
    uint##WIDTH##_t cmpval, uint##WIDTH##_t newval)			\
{									\
	int retval;							\
									\
	retval = atomic_cmpset_##WIDTH(p, cmpval, newval);		\
	ldacq_mb();							\
	return (retval);						\
}									\
									\
static __inline  int							\
atomic_cmpset_rel_##WIDTH(__volatile uint##WIDTH##_t *p,		\
    uint##WIDTH##_t cmpval, uint##WIDTH##_t newval)			\
{									\
	strel_mb();							\
	return (atomic_cmpset_##WIDTH(p, cmpval, newval));		\
}

#define	ATOMIC_FCMPSET_ACQ_REL(WIDTH)					\
static __inline  int							\
atomic_fcmpset_acq_##WIDTH(__volatile uint##WIDTH##_t *p,		\
    uint##WIDTH##_t *cmpval, uint##WIDTH##_t newval)			\
{									\
	int retval;							\
									\
	retval = atomic_fcmpset_##WIDTH(p, cmpval, newval);		\
	ldacq_mb();							\
	return (retval);						\
}									\
									\
static __inline  int							\
atomic_fcmpset_rel_##WIDTH(__volatile uint##WIDTH##_t *p,		\
    uint##WIDTH##_t *cmpval, uint##WIDTH##_t newval)			\
{									\
	strel_mb();							\
	return (atomic_fcmpset_##WIDTH(p, cmpval, newval));		\
}

ATOMIC_CMPSET_ACQ_REL(8);
ATOMIC_FCMPSET_ACQ_REL(8);
ATOMIC_CMPSET_ACQ_REL(16);
ATOMIC_FCMPSET_ACQ_REL(16);

#define	atomic_cmpset_char		atomic_cmpset_8
#define	atomic_cmpset_acq_char		atomic_cmpset_acq_8
#define	atomic_cmpset_rel_char		atomic_cmpset_rel_8
#define	atomic_fcmpset_char		atomic_fcmpset_8
#define	atomic_fcmpset_acq_char		atomic_fcmpset_acq_8
#define	atomic_fcmpset_rel_char		atomic_fcmpset_rel_8

#define	atomic_cmpset_short		atomic_cmpset_16
#define	atomic_cmpset_acq_short		atomic_cmpset_acq_16
#define	atomic_cmpset_rel_short		atomic_cmpset_rel_16
#define	atomic_fcmpset_short		atomic_fcmpset_16
#define	atomic_fcmpset_acq_short	atomic_fcmpset_acq_16
#define	atomic_fcmpset_rel_short	atomic_fcmpset_rel_16

static __inline void
atomic_add_32(volatile uint32_t *p, uint32_t val)
{

	__asm __volatile("amadd.w $zero, %1, %0"
			: "+ZB" (*p)
			: "r" (val)
			: "memory");
}

static __inline void
atomic_subtract_32(volatile uint32_t *p, uint32_t val)
{

	__asm __volatile("amadd.w $zero, %1, %0"
			: "+ZB" (*p)
			: "r" (-val)
			: "memory");
}

static __inline void
atomic_set_32(volatile uint32_t *p, uint32_t val)
{

	__asm __volatile("amor.w $zero, %1, %0"
			: "+ZB" (*p)
			: "r" (val)
			: "memory");
}

static __inline void
atomic_clear_32(volatile uint32_t *p, uint32_t val)
{

	__asm __volatile("amand.w $zero, %1, %0"
			: "+ZB" (*p)
			: "r" (~val)
			: "memory");
}

static __inline int
atomic_cmpset_32(volatile uint32_t *p, uint32_t cmpval, uint32_t newval)
{
	uint32_t tmp;
	int res;

	res = 0;

	__asm __volatile(
		"0:"
			"li.w   %1, 0\n"
			"ll.w %0, %2\n"
			"bne  %0, %z3, 1f\n"
			"move %1, %z4\n"
			"sc.w %1, %2\n"
			"beqz %1, 0b\n"
		"1:"
			"dbar 0x700\n"
			: "=&r" (tmp), "=&r" (res), "+ZB" (*p)
			: "rJ" ((long)(int32_t)cmpval), "rJ" (newval)
			: "memory");

	return (res);
}

static __inline int
atomic_fcmpset_32(volatile uint32_t *p, uint32_t *cmpval, uint32_t newval)
{
	uint32_t tmp;
	int res;

	res = 0;

	__asm __volatile(
		"0:"
			"li.w   %1, 0\n"
			"ll.w %0, %2\n"
			"bne  %0, %z4, 1f\n"
			"move %1, %z5\n"
			"sc.w %1, %2\n"
			"beqz %1, 0b\n"
			"b 2f\n"
		"1:"
			"st.w  %0, %3\n"
		"2:"
			"dbar 0x700\n"
			: "=&r" (tmp), "=&r" (res), "+ZB" (*p), "+ZB" (*cmpval)
			: "rJ" ((long)(int32_t)*cmpval), "rJ" (newval)
			: "memory");

	return (res);
}

static __inline uint32_t
atomic_fetchadd_32(volatile uint32_t *p, uint32_t val)
{
	uint32_t ret;

	__asm __volatile("amadd.w %0, %2, %1"
			: "=&r" (ret), "+ZB" (*p)
			: "r" (val)
			: "memory");

	return (ret);
}

static __inline uint32_t
atomic_readandclear_32(volatile uint32_t *p)
{
	uint32_t ret;
	uint32_t val;

	val = 0;

	__asm __volatile("amswap.w %0, %2, %1"
			: "=&r"(ret), "+ZB" (*p)
			: "r" (val)
			: "memory");

	return (ret);
}

static __inline int
atomic_testandclear_32(volatile uint32_t *p, u_int val)
{
	uint32_t mask, old;

	mask = 1u << (val & 31);
	__asm __volatile("amand.w %0, %2, %1"
			: "=&r" (old), "+ZB" (*p)
			: "r" (~mask)
			: "memory");

	return ((old & mask) != 0);
}

static __inline int
atomic_testandset_32(volatile uint32_t *p, u_int val)
{
	uint32_t mask, old;

	mask = 1u << (val & 31);
	__asm __volatile("amor.w %0, %2, %1"
			: "=&r" (old), "+ZB" (*p)
			: "r" (mask)
			: "memory");

	return ((old & mask) != 0);
}

#define	atomic_add_int		atomic_add_32
#define	atomic_clear_int	atomic_clear_32
#define	atomic_cmpset_int	atomic_cmpset_32
#define	atomic_fcmpset_int	atomic_fcmpset_32
#define	atomic_fetchadd_int	atomic_fetchadd_32
#define	atomic_readandclear_int	atomic_readandclear_32
#define	atomic_set_int		atomic_set_32
#define	atomic_subtract_int	atomic_subtract_32
#define	atomic_testandclear_int	atomic_testandclear_32
#define	atomic_testandset_int	atomic_testandset_32

ATOMIC_ACQ_REL(set, 32)
ATOMIC_ACQ_REL(clear, 32)
ATOMIC_ACQ_REL(add, 32)
ATOMIC_ACQ_REL(subtract, 32)

ATOMIC_CMPSET_ACQ_REL(32);
ATOMIC_FCMPSET_ACQ_REL(32);

static __inline uint32_t
atomic_load_acq_32(const volatile uint32_t *p)
{
	uint32_t ret;

	ret = *p;

	ldacq_mb();

	return (ret);
}

static __inline void
atomic_store_rel_32(volatile uint32_t *p, uint32_t val)
{

	strel_mb();

	*p = val;
}

#define	atomic_add_acq_int	atomic_add_acq_32
#define	atomic_clear_acq_int	atomic_clear_acq_32
#define	atomic_cmpset_acq_int	atomic_cmpset_acq_32
#define	atomic_fcmpset_acq_int	atomic_fcmpset_acq_32
#define	atomic_load_acq_int	atomic_load_acq_32
#define	atomic_set_acq_int	atomic_set_acq_32
#define	atomic_subtract_acq_int	atomic_subtract_acq_32

#define	atomic_add_rel_int	atomic_add_rel_32
#define	atomic_clear_rel_int	atomic_clear_rel_32
#define	atomic_cmpset_rel_int	atomic_cmpset_rel_32
#define	atomic_fcmpset_rel_int	atomic_fcmpset_rel_32
#define	atomic_set_rel_int	atomic_set_rel_32
#define	atomic_subtract_rel_int	atomic_subtract_rel_32
#define	atomic_store_rel_int	atomic_store_rel_32

static __inline void
atomic_add_64(volatile uint64_t *p, uint64_t val)
{

	__asm __volatile("amadd.d $zero, %1, %0"
			: "+ZB" (*p)
			: "r" (val)
			: "memory");
}

static __inline void
atomic_subtract_64(volatile uint64_t *p, uint64_t val)
{

	__asm __volatile("amadd.d $zero, %1, %0"
			: "+ZB" (*p)
			: "r" (-val)
			: "memory");
}

static __inline void
atomic_set_64(volatile uint64_t *p, uint64_t val)
{

	__asm __volatile("amor.d $zero, %1, %0"
			: "+ZB" (*p)
			: "r" (val)
			: "memory");
}

static __inline void
atomic_clear_64(volatile uint64_t *p, uint64_t val)
{

	__asm __volatile("amand.d $zero, %1, %0"
			: "+ZB" (*p)
			: "r" (~val)
			: "memory");
}

static __inline int
atomic_cmpset_64(volatile uint64_t *p, uint64_t cmpval, uint64_t newval)
{
	uint64_t tmp;
	int res;

	res = 0;

	__asm __volatile(
		"0:"
			"li.w   %1, 0\n"
			"ll.d %0, %2\n"
			"bne  %0, %z3, 1f\n"
			"move %1, %z4\n"
			"sc.d %1, %2\n"
			"beqz %1, 0b\n"
		"1:"
			"dbar 0x700\n"
			: "=&r" (tmp), "=&r" (res), "+ZB" (*p)
			: "rJ" (cmpval), "rJ" (newval)
			: "memory");

	return (res);
}

static __inline int
atomic_fcmpset_64(volatile uint64_t *p, uint64_t *cmpval, uint64_t newval)
{
	uint64_t tmp;
	int res;

	res = 0;

	__asm __volatile(
		"0:"
			"li.w   %1, 0\n"
			"ll.d %0, %2\n"
			"bne  %0, %z4, 1f\n"
			"move %1, %z5\n"
			"sc.d %1, %2\n"
			"beqz %1, 0b\n"
			"b 2f\n"
		"1:"
			"st.d  %0, %3\n"
		"2:"
			"dbar 0x700\n"
			: "=&r" (tmp), "=&r" (res), "+ZB" (*p), "+ZB" (*cmpval)
			: "rJ" (*cmpval), "rJ" (newval)
			: "memory");

	return (res);
}

static __inline uint64_t
atomic_fetchadd_64(volatile uint64_t *p, uint64_t val)
{
	uint64_t ret;

	__asm __volatile("amadd.d %0, %2, %1"
			: "=&r" (ret), "+ZB" (*p)
			: "r" (val)
			: "memory");

	return (ret);
}

static __inline uint64_t
atomic_readandclear_64(volatile uint64_t *p)
{
	uint64_t ret;
	uint64_t val;

	val = 0;

	__asm __volatile("amswap.d %0, %2, %1"
			: "=&r"(ret), "+ZB" (*p)
			: "r" (val)
			: "memory");

	return (ret);
}

static __inline int
atomic_testandclear_64(volatile uint64_t *p, u_int val)
{
	uint64_t mask, old;

	mask = 1ul << (val & 63);
	__asm __volatile("amand.d %0, %2, %1"
			: "=&r" (old), "+ZB" (*p)
			: "r" (~mask)
			: "memory");

	return ((old & mask) != 0);
}

static __inline int
atomic_testandset_64(volatile uint64_t *p, u_int val)
{
	uint64_t mask, old;

	mask = 1ul << (val & 63);
	__asm __volatile("amor.d %0, %2, %1"
			: "=&r" (old), "+ZB" (*p)
			: "r" (mask)
			: "memory");

	return ((old & mask) != 0);
}

static __inline uint32_t
atomic_swap_32(volatile uint32_t *p, uint32_t val)
{
	uint32_t old;

	__asm __volatile("amswap.w %0, %2, %1"
			: "=&r"(old), "+ZB" (*p)
			: "r" (val)
			: "memory");

	return (old);
}

static __inline uint64_t
atomic_swap_64(volatile uint64_t *p, uint64_t val)
{
	uint64_t old;

	__asm __volatile("amswap.d %0, %2, %1"
			: "=&r"(old), "+ZB" (*p)
			: "r" (val)
			: "memory");

	return (old);
}

#define	atomic_swap_int			atomic_swap_32

#define	atomic_add_long			atomic_add_64
#define	atomic_clear_long		atomic_clear_64
#define	atomic_cmpset_long		atomic_cmpset_64
#define	atomic_fcmpset_long		atomic_fcmpset_64
#define	atomic_fetchadd_long		atomic_fetchadd_64
#define	atomic_readandclear_long	atomic_readandclear_64
#define	atomic_set_long			atomic_set_64
#define	atomic_subtract_long		atomic_subtract_64
#define	atomic_swap_long		atomic_swap_64
#define	atomic_testandclear_long	atomic_testandclear_64
#define	atomic_testandset_long		atomic_testandset_64

#define	atomic_add_ptr			atomic_add_64
#define	atomic_clear_ptr		atomic_clear_64
#define	atomic_cmpset_ptr		atomic_cmpset_64
#define	atomic_fcmpset_ptr		atomic_fcmpset_64
#define	atomic_fetchadd_ptr		atomic_fetchadd_64
#define	atomic_readandclear_ptr		atomic_readandclear_64
#define	atomic_set_ptr			atomic_set_64
#define	atomic_subtract_ptr		atomic_subtract_64
#define	atomic_swap_ptr			atomic_swap_64
#define	atomic_testandclear_ptr		atomic_testandclear_64
#define	atomic_testandset_ptr		atomic_testandset_64

ATOMIC_ACQ_REL(set, 64)
ATOMIC_ACQ_REL(clear, 64)
ATOMIC_ACQ_REL(add, 64)
ATOMIC_ACQ_REL(subtract, 64)

ATOMIC_CMPSET_ACQ_REL(64);
ATOMIC_FCMPSET_ACQ_REL(64);

static __inline uint64_t
atomic_load_acq_64(const volatile uint64_t *p)
{
	uint64_t ret;

	ret = *p;

	ldacq_mb();

	return (ret);
}

static __inline void
atomic_store_rel_64(volatile uint64_t *p, uint64_t val)
{

	strel_mb();

	*p = val;
}

#define	atomic_add_acq_long		atomic_add_acq_64
#define	atomic_clear_acq_long		atomic_clear_acq_64
#define	atomic_cmpset_acq_long		atomic_cmpset_acq_64
#define	atomic_fcmpset_acq_long		atomic_fcmpset_acq_64
#define	atomic_load_acq_long		atomic_load_acq_64
#define	atomic_set_acq_long		atomic_set_acq_64
#define	atomic_subtract_acq_long	atomic_subtract_acq_64

#define	atomic_add_acq_ptr		atomic_add_acq_64
#define	atomic_clear_acq_ptr		atomic_clear_acq_64
#define	atomic_cmpset_acq_ptr		atomic_cmpset_acq_64
#define	atomic_fcmpset_acq_ptr		atomic_fcmpset_acq_64
#define	atomic_load_acq_ptr		atomic_load_acq_64
#define	atomic_set_acq_ptr		atomic_set_acq_64
#define	atomic_subtract_acq_ptr		atomic_subtract_acq_64

#undef ATOMIC_ACQ_REL

static __inline void
atomic_thread_fence_acq(void)
{

	ldacq_mb();
}

static __inline void
atomic_thread_fence_rel(void)
{

	strel_mb();
}

static __inline void
atomic_thread_fence_acq_rel(void)
{

	__smp_mb();
}

static __inline void
atomic_thread_fence_seq_cst(void)
{

	mb();
}

#define	atomic_add_rel_long		atomic_add_rel_64
#define	atomic_clear_rel_long		atomic_clear_rel_64
#define	atomic_cmpset_rel_long		atomic_cmpset_rel_64
#define	atomic_fcmpset_rel_long		atomic_fcmpset_rel_64
#define	atomic_set_rel_long		atomic_set_rel_64
#define	atomic_subtract_rel_long	atomic_subtract_rel_64
#define	atomic_store_rel_long		atomic_store_rel_64

#define	atomic_add_rel_ptr		atomic_add_rel_64
#define	atomic_clear_rel_ptr		atomic_clear_rel_64
#define	atomic_cmpset_rel_ptr		atomic_cmpset_rel_64
#define	atomic_fcmpset_rel_ptr		atomic_fcmpset_rel_64
#define	atomic_set_rel_ptr		atomic_set_rel_64
#define	atomic_subtract_rel_ptr		atomic_subtract_rel_64
#define	atomic_store_rel_ptr		atomic_store_rel_64

#include <sys/_atomic_subword.h>

#endif /* _MACHINE_ATOMIC_H_ */
