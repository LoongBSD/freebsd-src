/*-
 * Copyright (c) 2016-2018 Ruslan Bukin <br@bsdpad.com>
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

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <ddb/ddb.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>

/*
 * LoongArch instruction encoding bits and masks.
 */
#define	OPCODE_SHIFT	26
#define	OPCODE_MASK	(0x3f << OPCODE_SHIFT)

#define	RD_SHIFT	0
#define	RD_MASK		(0x1f << RD_SHIFT)
#define	RJ_SHIFT	5
#define	RJ_MASK		(0x1f << RJ_SHIFT)
#define	RK_SHIFT	10
#define	RK_MASK		(0x1f << RK_SHIFT)
#define	RA_SHIFT	15
#define	RA_MASK		(0x1f << RA_SHIFT)
#define	FD_SHIFT	0
#define	FD_MASK		(0x1f << FD_SHIFT)
#define	FJ_SHIFT	5
#define	FJ_MASK		(0x1f << FJ_SHIFT)
#define	FK_SHIFT	10
#define	FK_MASK		(0x1f << FK_SHIFT)
#define	FA_SHIFT	15
#define	FA_MASK		(0x1f << FA_SHIFT)

#define	IMM5_SHIFT	10
#define	IMM5_MASK	(0x1f << IMM5_SHIFT)
#define	IMM6_SHIFT	10
#define	IMM6_MASK	(0x3f << IMM6_SHIFT)
#define	IMM12_SHIFT	10
#define	IMM12_MASK	(0xfff << IMM12_SHIFT)
#define	IMM14_SHIFT	10
#define	IMM14_MASK	(0x3fff << IMM14_SHIFT)
#define	IMM16_SHIFT	10
#define	IMM16_MASK	(0xffff << IMM16_SHIFT)
#define	IMM20_SHIFT	5
#define	IMM20_MASK	(0xfffff << IMM20_SHIFT)
#define	IMM21_SHIFT	0
#define	IMM21_MASK	(0x1fffff << IMM21_SHIFT)
#define	IMM25_SHIFT	0
#define	IMM25_MASK	(0x1ffffff << IMM25_SHIFT)
#define	IMM26_SHIFT	0
#define	IMM26_MASK	(0x3ffffff << IMM26_SHIFT)

#define	OPCODE_1RI20	0x0a	/* lu12i.w, lu32i.d */
#define	OPCODE_1RI21	0x02	/* b, bl */
#define	OPCODE_2RI12	0x0a	/* addi.w, addi.d, lu52i.d, etc */
#define	OPCODE_2RI14	0x03	/* ll.w, sc.w, ll.d, sc.d */
#define	OPCODE_2RI16	0x05	/* jirl, beq, bne, blt, bge, bltu, bgeu */
#define	OPCODE_3R	0x00	/* add.w, add.d, sub.w, sub.d, etc */
#define	OPCODE_4R	0x00	/* fadd.s, fadd.d, etc (with func) */
#define	OPCODE_2R	0x00	/* csrrd, csrwr, csrxchg */

/* General purpose register names */
static const char * const gpr_name[32] __unused = {
	"$r0",	"$r1",	"$r2",	"$r3",	"$r4",	"$r5",	"$r6",	"$r7",
	"$r8",	"$r9",	"$r10",	"$r11",	"$r12",	"$r13",	"$r14",	"$r15",
	"$r16",	"$r17",	"$r18",	"$r19",	"$r20",	"$r21",	"$r22",	"$r23",
	"$r24",	"$r25",	"$r26",	"$r27",	"$r28",	"$r29",	"$r30",	"$r31"
};

/* Aliases for general purpose registers */
static const char *gpr_alias[32] = {
	"$zero",	"$ra",	"$tp",	"$sp",
	"$a0",	"$a1",	"$a2",	"$a3",	"$a4",	"$a5",	"$a6",	"$a7",
	"$t0",	"$t1",	"$t2",	"$t3",	"$t4",	"$t5",	"$t6",	"$t7",	"$t8",
	"$u0",	"$fp",	"$s0",	"$s1",	"$s2",	"$s3",	"$s4",	"$s5",	"$s6",	"$s7",	"$s8"
};

/* Floating point register names */
static const char * const fpr_name[32] __unused = {
	"$f0",	"$f1",	"$f2",	"$f3",	"$f4",	"$f5",	"$f6",	"$f7",
	"$f8",	"$f9",	"$f10",	"$f11",	"$f12",	"$f13",	"$f14",	"$f15",
	"$f16",	"$f17",	"$f18",	"$f19",	"$f20",	"$f21",	"$f22",	"$f23",
	"$f24",	"$f25",	"$f26",	"$f27",	"$f28",	"$f29",	"$f30",	"$f31"
};

/* Aliases for floating point registers */
static const char *fpr_alias[32] = {
	"$fa0",	"$fa1",	"$fa2",	"$fa3",	"$fa4",	"$fa5",	"$fa6",	"$fa7",
	"$ft0",	"$ft1",	"$ft2",	"$ft3",	"$ft4",	"$ft5",	"$ft6",	"$ft7",
	"$ft8",	"$ft9",	"$ft10",	"$ft11",	"$ft12",	"$ft13",	"$ft14",	"$ft15",
	"$fs0",	"$fs1",	"$fs2",	"$fs3",	"$fs4",	"$fs5",	"$fs6",	"$fs7"
};

/* Use aliases for better readability */
#define	GPR_NAME(r)	gpr_alias[(r)]
#define	FPR_NAME(f)	fpr_alias[(f)]

struct loongarch_op {
	const char *name;
	uint32_t opcode;
	uint32_t mask;
	const char *fmt;
};

/*
 * Instruction format characters:
 * d - destination register (rd)
 * j - source register 1 (rj)
 * k - source register 2 (rk)
 * a - source register 3 (ra)
 * D - floating point destination (fd)
 * J - floating point source 1 (fj)
 * K - floating point source 2 (fk)
 * A - floating point source 3 (fa)
 * i - immediate value (12-bit signed)
 * I - immediate value (14-bit signed)
 * b - branch offset
 * c - CSR register
 */

/* 3R-type instructions (opcode = 0x00) */
static const struct loongarch_op op_3r[] = {
	/* Integer arithmetic */
	{ "add.w",	0x00100000, 0x003f8000, "djk" },
	{ "add.d",	0x00108000, 0x003f8000, "djk" },
	{ "sub.w",	0x00110000, 0x003f8000, "djk" },
	{ "sub.d",	0x00118000, 0x003f8000, "djk" },
	{ "slt",	0x00120000, 0x003f8000, "djk" },
	{ "sltu",	0x00128000, 0x003f8000, "djk" },
	{ "slti",	0x00130000, 0x003f8000, "djk" },
	{ "sltui",	0x00138000, 0x003f8000, "djk" },
	{ "and",	0x00140000, 0x003f8000, "djk" },
	{ "or",		0x00148000, 0x003f8000, "djk" },
	{ "xor",	0x00150000, 0x003f8000, "djk" },
	{ "nor",	0x00158000, 0x003f8000, "djk" },
	{ "andn",	0x00160000, 0x003f8000, "djk" },
	{ "orn",	0x00168000, 0x003f8000, "djk" },
	/* Shift operations */
	{ "sll.w",	0x00180000, 0x003f8000, "djk" },
	{ "srl.w",	0x00188000, 0x003f8000, "djk" },
	{ "sra.w",	0x00190000, 0x003f8000, "djk" },
	{ "sll.d",	0x00198000, 0x003f8000, "djk" },
	{ "srl.d",	0x001a0000, 0x003f8000, "djk" },
	{ "sra.d",	0x001a8000, 0x003f8000, "djk" },
	/* Bit operations */
	{ "rotr.w",	0x001b0000, 0x003f8000, "djk" },
	{ "rotr.d",	0x001b8000, 0x003f8000, "djk" },
	/* Multiply/divide */
	{ "mul.w",	0x001c0000, 0x003f8000, "djk" },
	{ "mulh.w",	0x001c8000, 0x003f8000, "djk" },
	{ "mulh.wu",	0x001d0000, 0x003f8000, "djk" },
	{ "mul.d",	0x001d8000, 0x003f8000, "djk" },
	{ "mulh.d",	0x001e0000, 0x003f8000, "djk" },
	{ "mulh.du",	0x001e8000, 0x003f8000, "djk" },
	{ "mulw.d.w",	0x001f0000, 0x003f8000, "djk" },
	{ "mulw.d.wu",	0x001f8000, 0x003f8000, "djk" },
	{ "div.w",	0x00200000, 0x003f8000, "djk" },
	{ "mod.w",	0x00208000, 0x003f8000, "djk" },
	{ "div.wu",	0x00210000, 0x003f8000, "djk" },
	{ "mod.wu",	0x00218000, 0x003f8000, "djk" },
	{ "div.d",	0x00220000, 0x003f8000, "djk" },
	{ "mod.d",	0x00228000, 0x003f8000, "djk" },
	{ "div.du",	0x00230000, 0x003f8000, "djk" },
	{ "mod.du",	0x00238000, 0x003f8000, "djk" },
	/* Breakpoint */
	{ "break",	0x002a0000, 0x003ffc00, "i" },
	{ "dblock",	0x002a8000, 0x003f8000, "" },
	/* CRC */
	{ "crc.w.b.w",	0x002b0000, 0x003f8000, "djk" },
	{ "crc.w.h.w",	0x002b8000, 0x003f8000, "djk" },
	{ "crc.w.w.w",	0x002c0000, 0x003f8000, "djk" },
	{ "crc.w.d.w",	0x002c8000, 0x003f8000, "djk" },
	{ "crcc.w.b.w",	0x002d0000, 0x003f8000, "djk" },
	{ "crcc.w.h.w",	0x002d8000, 0x003f8000, "djk" },
	{ "crcc.w.w.w",	0x002e0000, 0x003f8000, "djk" },
	{ "crcc.w.d.w",	0x002e8000, 0x003f8000, "djk" },
	/* System call */
	{ "syscall",	0x002b0000, 0x003ffc00, "i" },
	/* NULL terminator */
	{ NULL, 0, 0, NULL }
};

/* 2RI12-type instructions (opcode = 0x0a) */
/* Note: These instructions have 12-bit immediate at bits 10-21 */
static const struct loongarch_op op_2ri12[] = {
	{ "addi.w",	0x02800000, 0xffc00000, "dji" },
	{ "addi.d",	0x02840000, 0xffc40000, "dji" },
	{ "lu52i.d",	0x02880000, 0xffc40000, "dji" },
	{ "andi",	0x028c0000, 0xffc40000, "dji" },
	{ "ori",	0x02900000, 0xffc40000, "dji" },
	{ "xori",	0x02940000, 0xffc40000, "dji" },
	{ "lu12i.w",	0x0a000000, 0xffc00000, "di" },
	{ "lu32i.d",	0x0b000000, 0xffc00000, "di" },
	{ "pcaddi",	0x0c000000, 0xffc00000, "db" },
	{ "pcaddu12i",	0x0c400000, 0xffc00000, "db" },
	{ "pcaddu18i",	0x0c800000, 0xffc00000, "db" },
	{ "pcalau12i",	0x0cc00000, 0xffc00000, "db" },
	{ "ll.w",	0x20000000, 0xff000000, "dji" },
	{ "sc.w",	0x21000000, 0xff000000, "dji" },
	{ "ll.d",	0x22000000, 0xff000000, "dji" },
	{ "sc.d",	0x23000000, 0xff000000, "dji" },
	/* NULL terminator */
	{ NULL, 0, 0, NULL }
};

/* 2RI16-type instructions (opcode = 0x05) */
static const struct loongarch_op op_2ri16[] = {
	{ "jirl",	0x14000000, 0xfc000000, "djb" },
	{ "beq",	0x14400000, 0xfc000000, "jkb" },
	{ "bne",	0x14800000, 0xfc000000, "jkb" },
	{ "blt",	0x14c00000, 0xfc000000, "jkb" },
	{ "bge",	0x15000000, 0xfc000000, "jkb" },
	{ "bltu",	0x15400000, 0xfc000000, "jkb" },
	{ "bgeu",	0x15800000, 0xfc000000, "jkb" },
	/* NULL terminator */
	{ NULL, 0, 0, NULL }
};

/* 1RI21-type instructions (opcode = 0x02) */
static const struct loongarch_op op_1ri21[] = {
	{ "beqz",	0x08000000, 0xfc1f0000, "jb" },
	{ "bnez",	0x08200000, 0xfc1f0000, "jb" },
	{ "b",		0x10000000, 0xfc000000, "b" },
	{ "bl",		0x14000000, 0xfc000000, "b" },
	/* NULL terminator */
	{ NULL, 0, 0, NULL }
};

/* Load/Store instructions */
static const struct loongarch_op op_load_store[] = {
	{ "ld.b",	0x28000000, 0xff000000, "dji" },
	{ "ld.h",	0x29000000, 0xff000000, "dji" },
	{ "ld.w",	0x2a000000, 0xff000000, "dji" },
	{ "ld.d",	0x2b000000, 0xff000000, "dji" },
	{ "ld.bu",	0x2c000000, 0xff000000, "dji" },
	{ "ld.hu",	0x2d000000, 0xff000000, "dji" },
	{ "ld.wu",	0x2e000000, 0xff000000, "dji" },
	{ "st.b",	0x30000000, 0xff000000, "dji" },
	{ "st.h",	0x31000000, 0xff000000, "dji" },
	{ "st.w",	0x32000000, 0xff000000, "dji" },
	{ "st.d",	0x33000000, 0xff000000, "dji" },
	{ "fld.s",	0x34000000, 0xff000000, "Dji" },
	{ "fst.s",	0x35000000, 0xff000000, "Dji" },
	{ "fld.d",	0x36000000, 0xff000000, "Dji" },
	{ "fst.d",	0x37000000, 0xff000000, "Dji" },
	/* NULL terminator */
	{ NULL, 0, 0, NULL }
};

/* CSR instructions */
static const struct loongarch_op op_csr[] = {
	{ "csrrd",	0x04000000, 0xfff80000, "dc" },
	{ "csrwr",	0x04040000, 0xfff80000, "jc" },
	{ "csrxchg",	0x04080000, 0xfff80000, "jkc" },
	/* NULL terminator */
	{ NULL, 0, 0, NULL }
};

/* IOCSR instructions */
static const struct loongarch_op op_iocsr[] = {
	{ "iocsrrd.b",	0x04200000, 0xffff8000, "dj" },
	{ "iocsrrd.h",	0x04208000, 0xffff8000, "dj" },
	{ "iocsrrd.w",	0x04210000, 0xffff8000, "dj" },
	{ "iocsrrd.d",	0x04218000, 0xffff8000, "dj" },
	{ "iocsrwr.b",	0x04220000, 0xffff8000, "dj" },
	{ "iocsrwr.h",	0x04228000, 0xffff8000, "dj" },
	{ "iocsrwr.w",	0x04230000, 0xffff8000, "dj" },
	{ "iocsrwr.d",	0x04238000, 0xffff8000, "dj" },
	/* NULL terminator */
	{ NULL, 0, 0, NULL }
};

/* TLB instructions */
static const struct loongarch_op op_tlb[] = {
	{ "tlbsrch",	0x04240000, 0xffffffff, "" },
	{ "tlbrd",	0x04248000, 0xffffffff, "" },
	{ "tlbwr",	0x04250000, 0xffffffff, "" },
	{ "tlbfill",	0x04258000, 0xffffffff, "" },
	{ "tlbclr",	0x04260000, 0xffffffff, "" },
	{ "tlbflush",	0x04268000, 0xffffffff, "" },
	{ "invtlb",	0x04270000, 0xfff80000, "jki" },
	/* NULL terminator */
	{ NULL, 0, 0, NULL }
};

/* Cache instructions */
static const struct loongarch_op op_cache[] = {
	{ "cacop",	0x06000000, 0xff000000, "iji" },
	{ "lddir",	0x06400000, 0xffe00000, "dji" },
	{ "ldpte",	0x06600000, 0xffe00000, "ji" },
	{ "ertn",	0x06800000, 0xffffffff, "" },
	{ "idle",	0x06808000, 0xffffffff, "" },
	{ "dbar",	0x06810000, 0xfff80000, "i" },
	{ "ibar",	0x06818000, 0xfff80000, "i" },
	{ "extop",	0x06820000, 0xffffffff, "" },
	/* NULL terminator */
	{ NULL, 0, 0, NULL }
};

/* Floating point instructions */
static const struct loongarch_op op_fp[] = {
	{ "fadd.s",	0x01000000, 0x003f8000, "DJK" },
	{ "fadd.d",	0x01008000, 0x003f8000, "DJK" },
	{ "fsub.s",	0x01010000, 0x003f8000, "DJK" },
	{ "fsub.d",	0x01018000, 0x003f8000, "DJK" },
	{ "fmul.s",	0x01020000, 0x003f8000, "DJK" },
	{ "fmul.d",	0x01028000, 0x003f8000, "DJK" },
	{ "fdiv.s",	0x01030000, 0x003f8000, "DJK" },
	{ "fdiv.d",	0x01038000, 0x003f8000, "DJK" },
	{ "fmax.s",	0x01040000, 0x003f8000, "DJK" },
	{ "fmax.d",	0x01048000, 0x003f8000, "DJK" },
	{ "fmin.s",	0x01050000, 0x003f8000, "DJK" },
	{ "fmin.d",	0x01058000, 0x003f8000, "DJK" },
	{ "fmaxa.s",	0x01060000, 0x003f8000, "DJK" },
	{ "fmaxa.d",	0x01068000, 0x003f8000, "DJK" },
	{ "fmina.s",	0x01070000, 0x003f8000, "DJK" },
	{ "fmina.d",	0x01078000, 0x003f8000, "DJK" },
	{ "fscaleb.s",	0x01080000, 0x003f8000, "DJK" },
	{ "fscaleb.d",	0x01088000, 0x003f8000, "DJK" },
	{ "fcopysign.s", 0x01090000, 0x003f8000, "DJK" },
	{ "fcopysign.d", 0x01098000, 0x003f8000, "DJK" },
	{ "fabs.s",	0x010a0000, 0x003f8000, "DJ" },
	{ "fabs.d",	0x010a8000, 0x003f8000, "DJ" },
	{ "fneg.s",	0x010b0000, 0x003f8000, "DJ" },
	{ "fneg.d",	0x010b8000, 0x003f8000, "DJ" },
	{ "flogb.s",	0x010c0000, 0x003f8000, "DJ" },
	{ "flogb.d",	0x010c8000, 0x003f8000, "DJ" },
	{ "fclass.s",	0x010d0000, 0x003f8000, "DJ" },
	{ "fclass.d",	0x010d8000, 0x003f8000, "DJ" },
	{ "fsqrt.s",	0x010e0000, 0x003f8000, "DJ" },
	{ "fsqrt.d",	0x010e8000, 0x003f8000, "DJ" },
	{ "frecip.s",	0x010f0000, 0x003f8000, "DJ" },
	{ "frecip.d",	0x010f8000, 0x003f8000, "DJ" },
	{ "frsqrt.s",	0x01100000, 0x003f8000, "DJ" },
	{ "frsqrt.d",	0x01108000, 0x003f8000, "DJ" },
	{ "fmov.s",	0x01120000, 0x003f8000, "DJ" },
	{ "fmov.d",	0x01128000, 0x003f8000, "DJ" },
	{ "movgr2fr.w",	0x01130000, 0x003f8000, "Dj" },
	{ "movgr2fr.d",	0x01138000, 0x003f8000, "Dj" },
	{ "movfr2gr.s",	0x01140000, 0x003f8000, "dJ" },
	{ "movfr2gr.d",	0x01148000, 0x003f8000, "dJ" },
	{ "movgr2fcsr",	0x01150000, 0x003f8000, "cj" },
	{ "movfcsr2gr",	0x01158000, 0x003f8000, "dc" },
	{ "movfr2cf",	0x01160000, 0x003f8000, "cJ" },
	{ "movcf2fr",	0x01168000, 0x003f8000, "Dc" },
	{ "movgr2cf",	0x01170000, 0x003f8000, "cj" },
	{ "movcf2gr",	0x01178000, 0x003f8000, "dc" },
	{ "fcvt.s.d",	0x01190000, 0x003f8000, "DJ" },
	{ "fcvt.d.s",	0x01198000, 0x003f8000, "DJ" },
	{ "ftintrm.w.s", 0x011a0000, 0x003f8000, "dJ" },
	{ "ftintrm.w.d", 0x011a8000, 0x003f8000, "dJ" },
	{ "ftintrm.l.s", 0x011b0000, 0x003f8000, "dJ" },
	{ "ftintrm.l.d", 0x011b8000, 0x003f8000, "dJ" },
	{ "ftintrp.w.s", 0x011c0000, 0x003f8000, "dJ" },
	{ "ftintrp.w.d", 0x011c8000, 0x003f8000, "dJ" },
	{ "ftintrp.l.s", 0x011d0000, 0x003f8000, "dJ" },
	{ "ftintrp.l.d", 0x011d8000, 0x003f8000, "dJ" },
	{ "ftintrz.w.s", 0x011e0000, 0x003f8000, "dJ" },
	{ "ftintrz.w.d", 0x011e8000, 0x003f8000, "dJ" },
	{ "ftintrz.l.s", 0x011f0000, 0x003f8000, "dJ" },
	{ "ftintrz.l.d", 0x011f8000, 0x003f8000, "dJ" },
	{ "ftint.w.s",	0x01200000, 0x003f8000, "dJ" },
	{ "ftint.w.d",	0x01208000, 0x003f8000, "dJ" },
	{ "ftint.l.s",	0x01210000, 0x003f8000, "dJ" },
	{ "ftint.l.d",	0x01218000, 0x003f8000, "dJ" },
	{ "ffint.s.w",	0x01220000, 0x003f8000, "Dj" },
	{ "ffint.s.l",	0x01228000, 0x003f8000, "Dj" },
	{ "ffint.d.w",	0x01230000, 0x003f8000, "Dj" },
	{ "ffint.d.l",	0x01238000, 0x003f8000, "Dj" },
	{ "frint.s",	0x01240000, 0x003f8000, "DJ" },
	{ "frint.d",	0x01248000, 0x003f8000, "DJ" },
	{ "fcmp.s",	0x01280000, 0x003f8000, "jJc" },
	{ "fcmp.d",	0x01288000, 0x003f8000, "jJc" },
	{ "fsel",	0x012c0000, 0x003f8000, "DJKc" },
	/* NULL terminator */
	{ NULL, 0, 0, NULL }
};

/* Special instructions */
static const struct loongarch_op op_special[] = {
	{ "nop",	0x03400000, 0xffffffff, "" },
	{ "ret",	0x14002000, 0xffffffff, "" },	/* jirl $r0, $r1, 0 */
	{ NULL, 0, 0, NULL }
};

static int
sign_extend(uint32_t val, int bits)
{

	if (val & (1 << (bits - 1)))
		return (val | (~0u << bits));
	return (val);
}

static void
print_imm(int imm, int bits)
{

	db_printf("%d", sign_extend(imm, bits));
}

static void
print_branch(vm_offset_t loc, int offset)
{

	db_printf("0x%lx", loc + (offset << 2));
}

static void
print_csr(uint32_t csr)
{
	const char *name;
	static const struct {
		uint32_t num;
		const char *name;
	} csr_names[] = {
		{ 0x0, "crmd" },
		{ 0x1, "prmd" },
		{ 0x2, "euen" },
		{ 0x3, "misc" },
		{ 0x4, "ecfg" },
		{ 0x5, "estat" },
		{ 0x6, "era" },
		{ 0x7, "badv" },
		{ 0x8, "badi" },
		{ 0xc, "eentry" },
		{ 0x10, "tlbidx" },
		{ 0x11, "tlbehi" },
		{ 0x12, "tlbelo0" },
		{ 0x13, "tlbelo1" },
		{ 0x15, "gtlbc" },
		{ 0x16, "trgp" },
		{ 0x17, "asid" },
		{ 0x18, "pgdl" },
		{ 0x19, "pgdh" },
		{ 0x1a, "pgd" },
		{ 0x1b, "gpgdl" },
		{ 0x1c, "gpgdh" },
		{ 0x1d, "gpgd" },
		{ 0x1e, "gpid" },
		{ 0x1f, "gtoa" },
		{ 0x20, "tlbrentry" },
		{ 0x21, "tlbrbadv" },
		{ 0x22, "tlbrera" },
		{ 0x23, "tlbrsave" },
		{ 0x24, "tlbrelo0" },
		{ 0x25, "tlbrelo1" },
		{ 0x26, "tlbrehi" },
		{ 0x27, "tlbrprmd" },
		{ 0x30, "dmw0" },
		{ 0x31, "dmw1" },
		{ 0x32, "dmw2" },
		{ 0x33, "dmw3" },
		{ 0x40, "tval" },
		{ 0x41, "ticlr" },
		{ 0x80, "llbctl" },
		{ 0x81, "gllbctl" },
		{ 0x88, "impctl1" },
		{ 0x89, "impctl2" },
		{ 0x90, "tlbrefill" },
		{ 0x91, "gtlbrefill" },
		{ 0x92, "htlbrefill" },
		{ 0x93, "ftlbrefill" },
		{ 0xa0, "pvctl" },
		{ 0xa1, "vpid" },
		{ 0xa2, "gstat" },
		{ 0xa3, "gintc" },
		{ 0, NULL }
	};
	int i;

	name = NULL;
	for (i = 0; csr_names[i].name != NULL; i++) {
		if (csr_names[i].num == csr) {
			name = csr_names[i].name;
			break;
		}
	}

	if (name != NULL)
		db_printf("%s", name);
	else
		db_printf("0x%x", csr);
}

static int
print_op(const struct loongarch_op *ops, uint32_t insn, vm_offset_t loc)
{
	const struct loongarch_op *op;
	const char *fmt;
	uint32_t rd, rj, rk, ra;
	uint32_t fd, fj, fk, fa;
	uint32_t imm;
	int need_comma;

	for (op = ops; op->name != NULL; op++) {
		if ((insn & op->mask) == op->opcode) {
			db_printf("%s\t", op->name);
			fmt = op->fmt;
			need_comma = 0;

			/* Extract register fields */
			rd = (insn & RD_MASK) >> RD_SHIFT;
			rj = (insn & RJ_MASK) >> RJ_SHIFT;
			rk = (insn & RK_MASK) >> RK_SHIFT;
			ra = (insn & RA_MASK) >> RA_SHIFT;
			fd = (insn & FD_MASK) >> FD_SHIFT;
			fj = (insn & FJ_MASK) >> FJ_SHIFT;
			fk = (insn & FK_MASK) >> FK_SHIFT;
			fa = (insn & FA_MASK) >> FA_SHIFT;

			while (*fmt) {
				if (need_comma)
					db_printf(", ");
				need_comma = 1;

				switch (*fmt) {
				case 'd':
					db_printf("%s", GPR_NAME(rd));
					break;
				case 'j':
					db_printf("%s", GPR_NAME(rj));
					break;
				case 'k':
					db_printf("%s", GPR_NAME(rk));
					break;
				case 'a':
					db_printf("%s", GPR_NAME(ra));
					break;
				case 'D':
					db_printf("%s", FPR_NAME(fd));
					break;
				case 'J':
					db_printf("%s", FPR_NAME(fj));
					break;
				case 'K':
					db_printf("%s", FPR_NAME(fk));
					break;
				case 'A':
					db_printf("%s", FPR_NAME(fa));
					break;
				case 'i':
					/* Check instruction type for immediate size */
					/* break, syscall use 5-bit immediate (bits 6-10) */
					if ((insn & 0x003fc000) == 0x002a0000 ||  /* break */
					    (insn & 0x003fc000) == 0x002b0000) {  /* syscall */
						imm = (insn >> 6) & 0x1f;
						print_imm(imm, 5);
					}
					/* lu12i.w, lu32i.d use 20-bit immediate (bits 5-24) */
					else if ((insn & 0xffc00000) == 0x0a000000 ||  /* lu12i.w */
					         (insn & 0xffc00000) == 0x0b000000) {  /* lu32i.d */
						imm = (insn >> 5) & 0xfffff;
						print_imm(imm, 20);
					} else {
						/* 12-bit immediate for load/store (bits 10-21) */
						imm = (insn >> 10) & 0xfff;
						print_imm(imm, 12);
					}
					break;
				case 'b':
					if (op->opcode == 0x10000000 ||
					    op->opcode == 0x14000000) {
						/* b, bl: 26-bit offset */
						imm = insn & 0x3ffffff;
						print_branch(loc, sign_extend(imm, 26));
					} else if (op->opcode == 0x08000000 ||
					    op->opcode == 0x08200000) {
						/* beqz, bnez: 21-bit offset */
						imm = (insn >> IMM21_SHIFT) & 0x1fffff;
						print_branch(loc, sign_extend(imm, 21));
					} else {
						/* Other branches: 16-bit offset */
						imm = (insn >> IMM16_SHIFT) & 0xffff;
						print_branch(loc, sign_extend(imm, 16));
					}
					break;
				case 'c':
					/* 14-bit CSR index (bits 10-23) */
					imm = (insn >> 10) & 0x3fff;
					print_csr(imm);
					break;
				default:
					db_printf("?");
					break;
				}
				fmt++;
			}
			return (1);
		}
	}
	return (0);
}

vm_offset_t
db_disasm(vm_offset_t loc, bool altfmt)
{
	uint32_t insn;

	insn = db_get_value(loc, 4, 0);

	/* Try each instruction group */
	if (print_op(op_special, insn, loc))
		goto done;
	if (print_op(op_3r, insn, loc))
		goto done;
	if (print_op(op_2ri12, insn, loc))
		goto done;
	if (print_op(op_2ri16, insn, loc))
		goto done;
	if (print_op(op_1ri21, insn, loc))
		goto done;
	if (print_op(op_load_store, insn, loc))
		goto done;
	if (print_op(op_csr, insn, loc))
		goto done;
	if (print_op(op_iocsr, insn, loc))
		goto done;
	if (print_op(op_tlb, insn, loc))
		goto done;
	if (print_op(op_cache, insn, loc))
		goto done;
	if (print_op(op_fp, insn, loc))
		goto done;

	/* Unknown instruction */
	db_printf(".long\t0x%08x", insn);

done:
	db_printf("\n");
	return (loc + 4);
}
