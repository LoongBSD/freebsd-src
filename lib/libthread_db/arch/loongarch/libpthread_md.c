/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 The FreeBSD Foundation
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

#include <sys/types.h>
#include <string.h>
#include <thread_db.h>

#include "libpthread_db.h"

void
pt_reg_to_ucontext(const struct reg *r, ucontext_t *uc)
{
	mcontext_t *mc;

	mc = &uc->uc_mcontext;

	memcpy(mc->mc_gpregs.gp_regs, r->regs, sizeof(mc->mc_gpregs.gp_regs));
	mc->mc_gpregs.gp_crmd = r->crmd;
	mc->mc_gpregs.gp_prmd = r->prmd;
	mc->mc_gpregs.gp_euen = r->euen;
	mc->mc_gpregs.gp_misc = r->misc;
	mc->mc_gpregs.gp_ecfg = r->ecfg;
	mc->mc_gpregs.gp_estat = r->estat;
	mc->mc_gpregs.gp_era = r->era;
	mc->mc_gpregs.gp_badvaddr = r->badvaddr;
	mc->mc_gpregs.gp_orig_a0 = r->orig_a0;
}

void
pt_ucontext_to_reg(const ucontext_t *uc, struct reg *r)
{
	const mcontext_t *mc;

	mc = &uc->uc_mcontext;

	memcpy(r->regs, mc->mc_gpregs.gp_regs, sizeof(r->regs));
	r->crmd = mc->mc_gpregs.gp_crmd;
	r->prmd = mc->mc_gpregs.gp_prmd;
	r->euen = mc->mc_gpregs.gp_euen;
	r->misc = mc->mc_gpregs.gp_misc;
	r->ecfg = mc->mc_gpregs.gp_ecfg;
	r->estat = mc->mc_gpregs.gp_estat;
	r->era = mc->mc_gpregs.gp_era;
	r->badvaddr = mc->mc_gpregs.gp_badvaddr;
	r->orig_a0 = mc->mc_gpregs.gp_orig_a0;
}

void
pt_fpreg_to_ucontext(const struct fpreg *r, ucontext_t *uc)
{
	mcontext_t *mc = &uc->uc_mcontext;

	memcpy(&mc->mc_fpregs, r, sizeof(*r));
	mc->mc_flags |= _MC_FP_VALID;
}

void
pt_ucontext_to_fpreg(const ucontext_t *uc, struct fpreg *r)
{
	const mcontext_t *mc = &uc->uc_mcontext;

	if (mc->mc_flags & _MC_FP_VALID)
		memcpy(r, &mc->mc_fpregs, sizeof(*r));
	else
		memset(r, 0, sizeof(*r));
}

void
pt_md_init(void)
{
}

int
pt_reg_sstep(struct reg *reg __unused, int step __unused)
{

	return (0);
}
