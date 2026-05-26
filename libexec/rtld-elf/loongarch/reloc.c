/*-
 * Copyright (c) 2015-2017 Ruslan Bukin <br@bsdpad.com>
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

#include <sys/cdefs.h>
#include <sys/types.h>

#include <stdlib.h>

#include "debug.h"
#include "rtld.h"
#include "rtld_printf.h"

/*
 * It is possible for the compiler to emit relocations for unaligned data.
 * We handle this situation with these inlines.
 */
#define	RELOC_ALIGNED_P(x) \
	(((uintptr_t)(x) & (sizeof(void *) - 1)) == 0)

void _rtld_relocate_nonplt_self(Elf_Dyn *, Elf_Addr);

void
_rtld_relocate_nonplt_self(Elf_Dyn *dynp, Elf_Addr relocbase)
{
	const Elf_Rela *rela = NULL, *relalim;
	Elf_Addr relasz = 0;
	Elf_Addr *where;

	for (; dynp->d_tag != DT_NULL; dynp++) {
		switch (dynp->d_tag) {
		case DT_RELA:
			rela = (const Elf_Rela *)(relocbase + dynp->d_un.d_ptr);
			break;
		case DT_RELASZ:
			relasz = dynp->d_un.d_val;
			break;
		}
	}
	relalim = (const Elf_Rela *)((const char *)rela + relasz);
	for (; rela < relalim; rela++) {
		/*
		 * Only process R_LARCH_RELATIVE.  Skip NONE (which
		 * has r_offset=0 and would corrupt the ELF header if
		 * treated as RELATIVE).  Other entry types are handled
		 * later by reloc_non_plt.
		 */
		if (ELF_R_TYPE(rela->r_info) != R_LARCH_RELATIVE)
			continue;
		where = (Elf_Addr *)(relocbase + rela->r_offset);
		*where = (Elf_Addr)(relocbase + rela->r_addend);
	}
}

void
init_pltgot(Obj_Entry *obj)
{

	if (obj->pltgot != NULL) {
		obj->pltgot[0] = (Elf_Addr)&_rtld_bind_start;
		obj->pltgot[1] = (Elf_Addr)obj;
	}
}

int
do_copy_relocations(Obj_Entry *dstobj)
{
	const Obj_Entry *srcobj, *defobj;
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	const Elf_Sym *srcsym;
	const Elf_Sym *dstsym;
	const void *srcaddr;
	const char *name;
	void *dstaddr;
	SymLook req;
	size_t size;
	int res;

	/*
	 * COPY relocs are invalid outside of the main program
	 */
	assert(dstobj->mainprog);

	relalim = (const Elf_Rela *)((const char *)dstobj->rela +
	    dstobj->relasize);
	for (rela = dstobj->rela; rela < relalim; rela++) {
		if (ELF_R_TYPE(rela->r_info) != R_LARCH_COPY)
			continue;

		dstaddr = (void *)(dstobj->relocbase + rela->r_offset);
		dstsym = dstobj->symtab + ELF_R_SYM(rela->r_info);
		name = dstobj->strtab + dstsym->st_name;
		size = dstsym->st_size;

		symlook_init(&req, name);
		req.ventry = fetch_ventry(dstobj, ELF_R_SYM(rela->r_info));
		req.flags = SYMLOOK_EARLY;

		for (srcobj = globallist_next(dstobj); srcobj != NULL;
		     srcobj = globallist_next(srcobj)) {
			res = symlook_obj(&req, srcobj);
			if (res == 0) {
				srcsym = req.sym_out;
				defobj = req.defobj_out;
				break;
			}
		}
		if (srcobj == NULL) {
			_rtld_error(
"Undefined symbol \"%s\" referenced from COPY relocation in %s",
			    name, dstobj->path);
			return (-1);
		}

		srcaddr = (const void *)(defobj->relocbase + srcsym->st_value);
		memcpy(dstaddr, srcaddr, size);
	}

	return (0);
}

/*
 * Process the PLT relocations.
 */
int
reloc_plt(Obj_Entry *obj, int flags __unused, RtldLockState *lockstate __unused)
{
	const Elf_Rela *relalim;
	const Elf_Rela *rela;

	/*
	 * Fallback: if pltrela is NULL but pltrel is not (e.g. because
	 * DT_PLTREL is missing in the ELF), treat the pltrel data as
	 * Elf_Rela.  LoongArch uses RELA exclusively for all dynamic
	 * relocations.
	 */
	if (obj->pltrela == NULL && obj->pltrel != NULL &&
	    obj->pltrelsize > 0) {
		obj->pltrela = (const Elf_Rela *)obj->pltrel;
		obj->pltrelasize = obj->pltrelsize;
	}

	if (obj->pltrela == NULL) {
		_rtld_error("%s: PLT: no PLT relocations (pltrela=NULL)",
		    obj->path);
		return (0);
	}

	relalim = (const Elf_Rela *)((const char *)obj->pltrela +
	    obj->pltrelasize);
	_rtld_error("%s: PLT: pltrela=%p size=%#zx entries=%zu",
	    obj->path, (const void *)obj->pltrela,
	    (size_t)obj->pltrelasize,
	    (size_t)(obj->pltrelasize / sizeof(Elf_Rela)));
	for (rela = obj->pltrela; rela < relalim; rela++) {
		Elf_Addr *where;

		where = (Elf_Addr *)(obj->relocbase + rela->r_offset);

		switch (ELF_R_TYPE(rela->r_info)) {
		case R_LARCH_JUMP_SLOT:
			*where += (Elf_Addr)obj->relocbase;
			break;
		case R_LARCH_IRELATIVE:
			obj->irelative = true;
			break;
		/*
		 * TLS relocations in PLT are typically placeholder entries
		 * or handled in reloc_non_plt. Skip them here.
		 */
		case R_LARCH_TLS_IE_HI20:
		case R_LARCH_TLS_IE_LO12:
		case R_LARCH_TLS_IE64_LO20:
		case R_LARCH_TLS_IE64_HI12:
		case R_LARCH_TLS_IE_PC_HI20:
		case R_LARCH_TLS_IE_PC_LO12:
		case R_LARCH_TLS_IE64_PC_LO20:
		case R_LARCH_TLS_IE64_PC_HI12:
		case R_LARCH_TLS_IE_PCADD_HI20:
		case R_LARCH_TLS_IE_PCADD_LO12:
		case R_LARCH_TLS_GD_HI20:
		case R_LARCH_TLS_GD_PC_HI20:
		case R_LARCH_TLS_GD_PCADD_HI20:
		case R_LARCH_TLS_GD_PCADD_LO12:
		case R_LARCH_TLS_GD_PCREL20_S2:
		case R_LARCH_TLS_LD_HI20:
		case R_LARCH_TLS_LD_PC_HI20:
		case R_LARCH_TLS_LD_PCADD_HI20:
		case R_LARCH_TLS_LD_PCADD_LO12:
		case R_LARCH_TLS_LD_PCREL20_S2:
		case R_LARCH_TLS_DESC_HI20:
		case R_LARCH_TLS_DESC_PC_HI20:
		case R_LARCH_TLS_DESC_PCADD_HI20:
		case R_LARCH_TLS_DESC_PCADD_LO12:
		case R_LARCH_TLS_DESC_PCREL20_S2:
		case R_LARCH_TLS_DTPMOD64:
		case R_LARCH_TLS_DTPREL64:
		case R_LARCH_TLS_TPREL64:
			/* These TLS relocations are handled in reloc_non_plt */
			break;
		default:
			_rtld_error(
		"%s: Unknown relocation type %u in PLT (entry %zu/%zu r_offset=%#lx r_info=%#lx)",
			    obj->path,
			    (unsigned int)ELF_R_TYPE(rela->r_info),
			    (size_t)(rela - obj->pltrela),
			    (size_t)(obj->pltrelasize / sizeof(Elf_Rela)),
			    (unsigned long)rela->r_offset,
			    (unsigned long)rela->r_info);
			return (-1);
		}
	}

	return (0);
}

/*
 * LD_BIND_NOW was set - force relocation for all jump slots
 */
int
reloc_jmpslots(Obj_Entry *obj, int flags, RtldLockState *lockstate)
{
	const Obj_Entry *defobj;
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	const Elf_Sym *def;

	/*
	 * P1-3.2 DEBUG: Print overview of jmpslot relocation processing.
	 */
	{
		rtld_fdprintf(2,
		    "RTLD-DEBUG: reloc_jmpslots obj=%s pltrela=%p "
		    "pltrelasize=%#zx entries=%zu\n",
		    obj->path, (const void *)obj->pltrela,
		    (size_t)obj->pltrelasize,
		    (size_t)(obj->pltrelasize / sizeof(Elf_Rela)));
	}

	relalim = (const Elf_Rela *)((const char *)obj->pltrela +
	    obj->pltrelasize);
	for (rela = obj->pltrela; rela < relalim; rela++) {
		Elf_Addr *where;

		where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
		switch(ELF_R_TYPE(rela->r_info)) {
		case R_LARCH_JUMP_SLOT:
			{
				rtld_fdprintf(2,
				    "RTLD-DEBUG: reloc_jmpslots sym=%lu "
				    "r_info=%#lx r_offset=%#lx\n",
				    (unsigned long)ELF_R_SYM(rela->r_info),
				    (unsigned long)rela->r_info,
				    (unsigned long)rela->r_offset);
			}
			def = find_symdef(ELF_R_SYM(rela->r_info), obj,
			    &defobj, SYMLOOK_IN_PLT | flags, NULL, lockstate);
			if (def == NULL) {
				dbg("reloc_jmpslots: sym not found");
				return (-1);
			}

			if (ELF_ST_TYPE(def->st_info) == STT_GNU_IFUNC) {
				obj->gnu_ifunc = true;
				continue;
			}

			*where = (Elf_Addr)(defobj->relocbase + def->st_value);
			break;
		/* Skip TLS relocations - they are handled in reloc_non_plt */
		case R_LARCH_TLS_IE_HI20:
		case R_LARCH_TLS_IE_LO12:
		case R_LARCH_TLS_IE64_LO20:
		case R_LARCH_TLS_IE64_HI12:
		case R_LARCH_TLS_IE_PC_HI20:
		case R_LARCH_TLS_IE_PC_LO12:
		case R_LARCH_TLS_IE64_PC_LO20:
		case R_LARCH_TLS_IE64_PC_HI12:
		case R_LARCH_TLS_IE_PCADD_HI20:
		case R_LARCH_TLS_IE_PCADD_LO12:
		case R_LARCH_TLS_GD_HI20:
		case R_LARCH_TLS_GD_PC_HI20:
		case R_LARCH_TLS_GD_PCADD_HI20:
		case R_LARCH_TLS_GD_PCADD_LO12:
		case R_LARCH_TLS_GD_PCREL20_S2:
		case R_LARCH_TLS_LD_HI20:
		case R_LARCH_TLS_LD_PC_HI20:
		case R_LARCH_TLS_LD_PCADD_HI20:
		case R_LARCH_TLS_LD_PCADD_LO12:
		case R_LARCH_TLS_LD_PCREL20_S2:
		case R_LARCH_TLS_DESC_HI20:
		case R_LARCH_TLS_DESC_PC_HI20:
		case R_LARCH_TLS_DESC_PCADD_HI20:
		case R_LARCH_TLS_DESC_PCADD_LO12:
		case R_LARCH_TLS_DESC_PCREL20_S2:
		case R_LARCH_TLS_DTPMOD64:
		case R_LARCH_TLS_DTPREL64:
		case R_LARCH_TLS_TPREL64:
			break;
		default:
			_rtld_error("Unknown relocation type %x in jmpslot",
			    (unsigned int)ELF_R_TYPE(rela->r_info));
			return (-1);
		}
	}

	return (0);
}

static void
reloc_iresolve_one(Obj_Entry *obj, const Elf_Rela *rela,
    RtldLockState *lockstate)
{
	Elf_Addr *where, target, *ptr;

	ptr = (Elf_Addr *)(obj->relocbase + rela->r_addend);
	where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
	lock_release(rtld_bind_lock, lockstate);
	target = call_ifunc_resolver(ptr);
	wlock_acquire(rtld_bind_lock, lockstate);
	*where = target;
}

int
reloc_iresolve(Obj_Entry *obj, struct Struct_RtldLockState *lockstate)
{
	const Elf_Rela *relalim;
	const Elf_Rela *rela;

	if (!obj->irelative)
		return (0);

	obj->irelative = false;
	relalim = (const Elf_Rela *)((const char *)obj->pltrela +
	    obj->pltrelasize);
	for (rela = obj->pltrela; rela < relalim; rela++) {
		if (ELF_R_TYPE(rela->r_info) == R_LARCH_IRELATIVE)
			reloc_iresolve_one(obj, rela, lockstate);
	}
	return (0);
}

int
reloc_iresolve_nonplt(Obj_Entry *obj, struct Struct_RtldLockState *lockstate)
{
	const Elf_Rela *relalim;
	const Elf_Rela *rela;

	if (!obj->irelative_nonplt)
		return (0);

	obj->irelative_nonplt = false;
	relalim = (const Elf_Rela *)((const char *)obj->rela + obj->relasize);
	for (rela = obj->rela; rela < relalim; rela++) {
		if (ELF_R_TYPE(rela->r_info) == R_LARCH_IRELATIVE)
			reloc_iresolve_one(obj, rela, lockstate);
	}
	return (0);
}

int
reloc_gnu_ifunc(Obj_Entry *obj, int flags,
   struct Struct_RtldLockState *lockstate)
{
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	Elf_Addr *where, target;
	const Elf_Sym *def;
	const Obj_Entry *defobj;

	if (!obj->gnu_ifunc)
		return (0);

	relalim = (const Elf_Rela *)((const char *)obj->pltrela + obj->pltrelasize);
	for (rela = obj->pltrela; rela < relalim; rela++) {
		if (ELF_R_TYPE(rela->r_info) == R_LARCH_JUMP_SLOT) {
			where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
			def = find_symdef(ELF_R_SYM(rela->r_info), obj, &defobj,
			    SYMLOOK_IN_PLT | flags, NULL, lockstate);
			if (def == NULL)
				return (-1);
			if (ELF_ST_TYPE(def->st_info) != STT_GNU_IFUNC)
				continue;

			lock_release(rtld_bind_lock, lockstate);
			target = (Elf_Addr)rtld_resolve_ifunc(defobj, def);
			wlock_acquire(rtld_bind_lock, lockstate);
			reloc_jmpslot(where, target, defobj, obj,
			    (const Elf_Rel *)rela);
		}
	}
	obj->gnu_ifunc = false;
	return (0);
}

Elf_Addr
reloc_jmpslot(Elf_Addr *where, Elf_Addr target,
    const Obj_Entry *defobj __unused, const Obj_Entry *obj __unused,
    const Elf_Rel *rel)
{

	assert(ELF_R_TYPE(rel->r_info) == R_LARCH_JUMP_SLOT ||
	    ELF_R_TYPE(rel->r_info) == R_LARCH_IRELATIVE);

	if (*where != target && !ld_bind_not)
		*where = target;
	return (target);
}

/*
 * Process non-PLT relocations
 */
int
reloc_non_plt(Obj_Entry *obj, Obj_Entry *obj_rtld, int flags,
    RtldLockState *lockstate)
{
	const Obj_Entry *defobj;
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	const Elf_Sym *def;
	SymCache *cache;
	Elf_Addr *where, symval;
	unsigned long symnum;

	/*
	 * The dynamic loader may be called from a thread, we have
	 * limited amounts of stack available so we cannot use alloca().
	 */
	if (obj == obj_rtld)
		cache = NULL;
	else
		cache = calloc(obj->dynsymcount, sizeof(SymCache));
		/* No need to check for NULL here */

	/*
	 * P1-3.2 DEBUG: Print overview of non-PLT relocation processing.
	 */
	{
		rtld_fdprintf(2,
		    "RTLD-DEBUG: reloc_non_plt obj=%s rela=%p "
		    "relasize=%#zx entries=%zu dynsymcount=%lu\n",
		    obj->path, (const void *)obj->rela,
		    (size_t)obj->relasize,
		    (size_t)(obj->relasize / sizeof(Elf_Rela)),
		    (unsigned long)obj->dynsymcount);
	}

	relalim = (const Elf_Rela *)((const char *)obj->rela + obj->relasize);
	for (rela = obj->rela; rela < relalim; rela++) {
		where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
		symnum = ELF_R_SYM(rela->r_info);

		switch (ELF_R_TYPE(rela->r_info)) {
		case R_LARCH_JUMP_SLOT:
			/* This will be handled by the plt/jmpslot routines */
			break;
		case R_LARCH_NONE:
			break;
		case R_LARCH_64:
			/*{
				rtld_fdprintf(2,
				    "RTLD-DEBUG: reloc_non_plt R_LARCH_64 "
				    "sym=%lu r_info=%#lx r_offset=%#lx\n",
				    (unsigned long)symnum,
				    (unsigned long)rela->r_info,
				    (unsigned long)rela->r_offset);
			}*/
			def = find_symdef(symnum, obj, &defobj, flags, cache,
			    lockstate);
			if (def == NULL)
				return (-1);

			if (ELF_ST_TYPE(def->st_info) == STT_GNU_IFUNC) {
				if ((flags & SYMLOOK_IFUNC) == 0) {
					obj->non_plt_gnu_ifunc = true;
					continue;
				}
				symval = (Elf_Addr)rtld_resolve_ifunc(defobj,
				    def);
			} else {
				if ((flags & SYMLOOK_IFUNC) != 0)
					continue;
				symval = (Elf_Addr)(defobj->relocbase +
				    def->st_value);
			}

			*where = symval + rela->r_addend;
			break;
		case R_LARCH_TLS_DTPMOD64:
			{
				rtld_fdprintf(2,
				    "RTLD-DEBUG: reloc_non_plt "
				    "R_LARCH_TLS_DTPMOD64 "
				    "sym=%lu r_info=%#lx r_offset=%#lx\n",
				    (unsigned long)symnum,
				    (unsigned long)rela->r_info,
				    (unsigned long)rela->r_offset);
			}
			def = find_symdef(symnum, obj, &defobj, flags, cache,
			    lockstate);
			if (def == NULL)
				return -1;

			*where = (Elf_Addr)defobj->tlsindex;
			break;
		case R_LARCH_COPY:
			/*
			 * These are deferred until all other relocations have
			 * been done. All we do here is make sure that the
			 * COPY relocation is not in a shared library. They
			 * are allowed only in executable files.
			 */
			if (!obj->mainprog) {
				_rtld_error("%s: Unexpected R_LARCH_COPY "
				    "relocation in shared library", obj->path);
				return (-1);
			}
			break;
		case R_LARCH_TLS_DTPREL64:
			{
				rtld_fdprintf(2,
				    "RTLD-DEBUG: reloc_non_plt "
				    "R_LARCH_TLS_DTPREL64 "
				    "sym=%lu r_info=%#lx r_offset=%#lx\n",
				    (unsigned long)symnum,
				    (unsigned long)rela->r_info,
				    (unsigned long)rela->r_offset);
			}
			def = find_symdef(symnum, obj, &defobj, flags, cache,
			    lockstate);
			if (def == NULL)
				return (-1);
			/*
			 * We lazily allocate offsets for static TLS as we
			 * see the first relocation that references the
			 * TLS block. This allows us to support (small
			 * amounts of) static TLS in dynamically loaded
			 * modules. If we run out of space, we generate an
			 * error.
			 */
			if (!defobj->tls_static) {
				if (!allocate_tls_offset(
				    __DECONST(Obj_Entry *, defobj))) {
					_rtld_error(
					    "%s: No space available for static "
					    "Thread Local Storage", obj->path);
					return (-1);
				}
			}

			*where += (Elf_Addr)(def->st_value + rela->r_addend
			    - TLS_DTV_OFFSET);
			break;
		case R_LARCH_TLS_TPREL64:
			{
				rtld_fdprintf(2,
				    "RTLD-DEBUG: reloc_non_plt "
				    "R_LARCH_TLS_TPREL64 "
				    "sym=%lu r_info=%#lx r_offset=%#lx\n",
				    (unsigned long)symnum,
				    (unsigned long)rela->r_info,
				    (unsigned long)rela->r_offset);
			}
			def = find_symdef(symnum, obj, &defobj, flags, cache,
			    lockstate);
			if (def == NULL)
				return (-1);

			/*
			 * We lazily allocate offsets for static TLS as we
			 * see the first relocation that references the
			 * TLS block. This allows us to support (small
			 * amounts of) static TLS in dynamically loaded
			 * modules. If we run out of space, we generate an
			 * error.
			 */
			if (!defobj->tls_static) {
				if (!allocate_tls_offset(
				    __DECONST(Obj_Entry *, defobj))) {
					_rtld_error(
					    "%s: No space available for static "
					    "Thread Local Storage", obj->path);
					return (-1);
				}
			}

			*where = (def->st_value + rela->r_addend +
			    defobj->tlsoffset - TLS_TP_OFFSET - TLS_TCB_SIZE);
			break;
		case R_LARCH_RELATIVE:
			*where = (Elf_Addr)(obj->relocbase + rela->r_addend);
			break;
		case R_LARCH_IRELATIVE:
			obj->irelative_nonplt = true;
			break;
		case R_LARCH_PCALA_HI20:
		{
			Elf_Addr val;
			uint32_t *insn32;
			int32_t sval;

			rtld_fdprintf(2,
			    "RTLD-DEBUG: reloc_non_plt R_LARCH_PCALA_HI20 "
			    "sym=%lu r_info=%#lx r_offset=%#lx\n",
			    (unsigned long)symnum,
			    (unsigned long)rela->r_info,
			    (unsigned long)rela->r_offset);
			def = find_symdef(symnum, obj, &defobj, flags, cache,
			    lockstate);
			if (def == NULL)
				return (-1);
			val = (Elf_Addr)(defobj->relocbase + def->st_value +
			    rela->r_addend);
			val -= (Elf_Addr)(obj->relocbase + rela->r_offset);
			sval = (int32_t)val;
			insn32 = (uint32_t *)where;
			*insn32 = (*insn32 & ~0xfffffU) |
			    ((((uint32_t)sval + 0x800U) >> 12) & 0xfffffU);
			break;
		}
		case R_LARCH_PCALA_LO12:
		{
			Elf_Addr val;
			uint32_t *insn32;

			rtld_fdprintf(2,
			    "RTLD-DEBUG: reloc_non_plt R_LARCH_PCALA_LO12 "
			    "sym=%lu r_info=%#lx r_offset=%#lx\n",
			    (unsigned long)symnum,
			    (unsigned long)rela->r_info,
			    (unsigned long)rela->r_offset);
			def = find_symdef(symnum, obj, &defobj, flags, cache,
			    lockstate);
			if (def == NULL)
				return (-1);
			val = (Elf_Addr)(defobj->relocbase + def->st_value +
			    rela->r_addend);
			val -= (Elf_Addr)(obj->relocbase + rela->r_offset);
			insn32 = (uint32_t *)where;
			*insn32 = (*insn32 & ~0x3ffc00U) |
			    ((val & 0xfffU) << 10);
			break;
		}
		case R_LARCH_ABS_HI20:
		case R_LARCH_ABS_LO12:
		case R_LARCH_ABS64_LO20:
		case R_LARCH_ABS64_HI12:
		case R_LARCH_PCALA64_LO20:
		case R_LARCH_PCALA64_HI12:
		case R_LARCH_GOT_PC_HI20:
		case R_LARCH_GOT_PC_LO12:
		case R_LARCH_GOT64_PC_LO20:
		case R_LARCH_GOT64_PC_HI12:
		case R_LARCH_GOT_HI20:
		case R_LARCH_GOT_LO12:
		case R_LARCH_GOT64_LO20:
		case R_LARCH_GOT64_HI12:
		case R_LARCH_GOT_PCADD_HI20:
		case R_LARCH_GOT_PCADD_LO12:
		case R_LARCH_B16:
		case R_LARCH_B21:
		case R_LARCH_B26:
		case R_LARCH_PCREL20_S2:
		case R_LARCH_CALL36:
		case R_LARCH_CALL30:
		case R_LARCH_PCADD_HI20:
		case R_LARCH_PCADD_LO12:
		case R_LARCH_ALIGN:
		case R_LARCH_RELAX:
		case R_LARCH_ADD8:
		case R_LARCH_ADD16:
		case R_LARCH_ADD24:
		case R_LARCH_ADD32:
		case R_LARCH_ADD64:
		case R_LARCH_SUB8:
		case R_LARCH_SUB16:
		case R_LARCH_SUB24:
		case R_LARCH_SUB32:
		case R_LARCH_SUB64:
		case R_LARCH_MARK_LA:
		case R_LARCH_MARK_PCREL:
		case R_LARCH_SOP_PUSH_PCREL:
		case R_LARCH_SOP_PUSH_ABSOLUTE:
		case R_LARCH_SOP_PUSH_DUP:
		case R_LARCH_SOP_PUSH_GPREL:
		case R_LARCH_SOP_PUSH_TLS_TPREL:
		case R_LARCH_SOP_PUSH_TLS_GOT:
		case R_LARCH_SOP_PUSH_TLS_GD:
		case R_LARCH_SOP_PUSH_PLT_PCREL:
		case R_LARCH_SOP_ASSERT:
		case R_LARCH_SOP_NOT:
		case R_LARCH_SOP_SUB:
		case R_LARCH_SOP_SL:
		case R_LARCH_SOP_SR:
		case R_LARCH_SOP_ADD:
		case R_LARCH_SOP_AND:
		case R_LARCH_SOP_IF_ELSE:
		case R_LARCH_SOP_POP_32_S_10_5:
		case R_LARCH_SOP_POP_32_U_10_12:
		case R_LARCH_SOP_POP_32_S_10_12:
		case R_LARCH_SOP_POP_32_S_10_16:
		case R_LARCH_SOP_POP_32_S_10_16_S2:
		case R_LARCH_SOP_POP_32_S_5_20:
		case R_LARCH_SOP_POP_32_S_0_5_10_16_S2:
		case R_LARCH_SOP_POP_32_S_0_10_10_16_S2:
		case R_LARCH_SOP_POP_32_U:
		case R_LARCH_ADD6:
		case R_LARCH_SUB6:
		case R_LARCH_ADD_ULEB128:
		case R_LARCH_SUB_ULEB128:
		case R_LARCH_GNU_VTINHERIT:
		case R_LARCH_GNU_VTENTRY:
		case R_LARCH_32:
		case R_LARCH_32_PCREL:
		case R_LARCH_64_PCREL:
			/*
			 * These instruction-level relocations are
			 * resolved by the static linker.  They
			 * should not appear in .rela.dyn of a
			 * shared object or PIE.  If they do, it's
			 * a toolchain or build configuration
			 * issue.
			 */
			break;
		default: {
			/*
			 * P1-4.1 DIAG: Dump raw 24-byte Elf64_Rela entry
			 * when an unknown relocation type is seen, to help
			 * diagnose memory corruption of the relocation table.
			 */
			{
				const unsigned char *raw =
				    (const unsigned char *)rela;
				rtld_fdprintf(2,
				    "RTLD-DEBUG: CORRUPTED-RELA "
				    "obj=%s entry=%zu/%zu "
				    "r_offset=%#lx r_info=%#lx r_addend=%#lx "
				    "raw_hex:"
				    " %02x%02x%02x%02x%02x%02x%02x%02x"
				    " %02x%02x%02x%02x%02x%02x%02x%02x"
				    " %02x%02x%02x%02x%02x%02x%02x%02x\n",
				    obj->path,
				    (size_t)(rela - obj->rela),
				    (size_t)(obj->relasize /
				        sizeof(Elf_Rela)),
				    (unsigned long)rela->r_offset,
				    (unsigned long)rela->r_info,
				    (unsigned long)rela->r_addend,
				    raw[0], raw[1], raw[2], raw[3],
				    raw[4], raw[5], raw[6], raw[7],
				    raw[8], raw[9], raw[10], raw[11],
				    raw[12], raw[13], raw[14], raw[15],
				    raw[16], raw[17], raw[18], raw[19],
				    raw[20], raw[21], raw[22], raw[23]);
			}
			_rtld_error("Unhandled relocation type %lu in %s",
			    (unsigned long)ELF_R_TYPE(rela->r_info),
			    obj->path);
			return (-1);
		}
		}
	}

	return (0);
}

unsigned long elf_hwcap;

void
ifunc_init(Elf_Auxinfo *aux_info[__min_size(AT_COUNT)])
{
	if (aux_info[AT_HWCAP] != NULL)
		elf_hwcap = aux_info[AT_HWCAP]->a_un.a_val;
}

void
allocate_initial_tls(Obj_Entry *objs)
{

	/*
	 * Fix the size of the static TLS block by using the maximum
	 * offset allocated so far and adding a bit for dynamic modules to
	 * use.
	 */
	tls_static_space = tls_last_offset + tls_last_size +
	    ld_static_tls_extra;

	_tcb_set(allocate_tls(objs, NULL, TLS_TCB_SIZE, TLS_TCB_ALIGN));
}

void *
__tls_get_addr(tls_index* ti)
{
	return (tls_get_addr_common(_tcb_get(), ti->ti_module, ti->ti_offset +
	    TLS_DTV_OFFSET));
}
