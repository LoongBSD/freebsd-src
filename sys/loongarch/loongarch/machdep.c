/*-
 * Copyright (c) 2014 Andrew Turner
 * Copyright (c) 2015-2017 Ruslan Bukin <br@bsdpad.com>
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

#include "opt_acpi.h"
#include "opt_platform.h"

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/boot.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/cons.h>
#include <sys/cpu.h>
#include <sys/efi.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/limits.h>
#include <sys/linker.h>
#include <sys/msgbuf.h>
#include <sys/pcpu.h>
#include <sys/physmem.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/reboot.h>
#include <sys/reg.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/signalvar.h>
#include <sys/smp.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/tslog.h>
#include <sys/ucontext.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_phys.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/fpe.h>
#include <machine/intr.h>
#include <machine/kdb.h>
#include <machine/machdep.h>
#include <machine/metadata.h>
#include <machine/pcb.h>
#include <machine/pte.h>
#include <machine/smp.h>
#include <machine/loongarchreg.h>
#include <machine/trap.h>
#include <machine/vmparam.h>

#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#endif

#ifdef FDT
#include <contrib/libfdt/libfdt.h>
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_subr.h>

#include <machine/bus.h>

extern struct bus_space memmap_bus;

static void
fdt_physmem_hardware_region_cb(const struct mem_region *mr, void *arg __unused)
{
	physmem_hardware_region(mr->mr_start, mr->mr_size);
}

static void
fdt_physmem_exclude_region_cb(const struct mem_region *mr, void *arg __unused)
{
	physmem_exclude_region(mr->mr_start, mr->mr_size,
	    EXFLAG_NODUMP | EXFLAG_NOALLOC);
}

int
OF_decode_addr(phandle_t dev, int regno, bus_space_tag_t *tag,
    bus_space_handle_t *handle, bus_size_t *sz)
{
	bus_addr_t addr;
	bus_size_t size;
	int err;

	err = ofw_reg_to_paddr(dev, regno, &addr, &size, NULL);
	if (err != 0)
		return (err);

	*tag = &memmap_bus;

	if (sz != NULL)
		*sz = size;

	return (bus_space_map(*tag, addr, size, 0, handle));
}
#endif

struct pcpu __pcpu[MAXCPU];

static struct trapframe proc0_tf;

int early_boot = 1;
int cold = 1;

/*
 * Bus method selection for ACPI/FDT dual-mode support
 */
enum loongarch_bus loongarch_bus_method = LOONGARCH_BUS_NONE;

/*
 * APEI NMI handler function pointer.
 * Set by acpi_apei.ko when APEI support is loaded.
 */
int (*apei_nmi)(void);

#define	DTB_SIZE_MAX	(1024 * 1024)

/* Direct UART progress marker - bypasses console subsystem */
#define UART_PROGRESS_CHAR(c) do { \
	volatile uint8_t *uart = (volatile uint8_t *)0x800000001FE001E0UL; \
	*uart = (uint8_t)(c); \
} while(0)

struct kva_md_info kmi;

int64_t dcache_line_size;	/* The minimum D cache line size */
int64_t icache_line_size;	/* The minimum I cache line size */
int64_t idcache_line_size;	/* The minimum cache line size */

#define BOOT_HART_INVALID	0xffffffff
uint32_t boot_hart = BOOT_HART_INVALID;	/* The hart we booted on. */

/*
 * Physical address of the EFI System Table. Stashed from the metadata hints
 * passed into the kernel and used by the EFI code to call runtime services.
 */
vm_paddr_t efi_systbl_phys;
static struct efi_map_header *efihdr;

cpuset_t all_harts;

extern int *end;

static char static_kenv[PAGE_SIZE];

static void
cpu_startup(void *dummy)
{

	printcpuinfo(0);

	printf("real memory  = %ju (%ju MB)\n", ptoa((uintmax_t)realmem),
	    ptoa((uintmax_t)realmem) / (1024 * 1024));

	/*
	 * Display any holes after the first chunk of extended memory.
	 */
	if (bootverbose) {
		int indx;

		printf("Physical memory chunk(s):\n");
		for (indx = 0; phys_avail[indx + 1] != 0; indx += 2) {
			vm_paddr_t size;

			size = phys_avail[indx + 1] - phys_avail[indx];
			printf(
			    "0x%016jx - 0x%016jx, %ju bytes (%ju pages)\n",
			    (uintmax_t)phys_avail[indx],
			    (uintmax_t)phys_avail[indx + 1] - 1,
			    (uintmax_t)size, (uintmax_t)size / PAGE_SIZE);
		}
	}

	vm_ksubmap_init(&kmi);

	printf("avail memory = %ju (%ju MB)\n",
	    ptoa((uintmax_t)vm_free_count()),
	    ptoa((uintmax_t)vm_free_count()) / (1024 * 1024));

	bufinit();
	vm_pager_bufferinit();
}

SYSINIT(cpu, SI_SUB_CPU, SI_ORDER_FIRST, cpu_startup, NULL);

int
cpu_idle_wakeup(int cpu)
{

#ifdef SMP
	if (smp_started)
		ipi_cpu(cpu, IPI_AST);
#endif
	return (0);
}

void
cpu_idle(int busy)
{
	struct thread *td;
	register_t s;

	/*
	 * Do NOT use spinlock_enter() here. spinlock_enter() increments
	 * spinlock_count, which causes smp_rendezvous_cpus() to panic or
	 * deadlock because it asserts spinlock_count == 0.
	 *
	 * Instead, we manually disable interrupts and increment td_critnest
	 * to prevent preemption while keeping spinlock_count at 0.
	 */
	s = intr_disable();
	td = curthread;
	td->td_critnest++;

	if (!busy)
		cpu_idleclock();

	while (!sched_runnable()) {
		/*
		 * Enable interrupts so that IPIs and timer interrupts can
		 * wake us up. spinlock_count remains 0, allowing
		 * smp_rendezvous_cpus() to proceed if an IPI arrives.
		 */
		mb();
		intr_enable();
		__asm __volatile("idle 0");
		intr_disable();

		/* If an IPI requested preemption, exit idle loop */
		if (td->td_owepreempt)
			break;
	}

	if (!busy)
		cpu_activeclock();

	td->td_critnest--;
	intr_restore(s);
}

void
cpu_halt(void)
{
	intr_disable();

	for (;;)
		__asm __volatile("idle 0");
	/* NOTREACHED */
}

/*
 * Flush the D-cache for non-DMA I/O so that the I-cache can
 * be made coherent later.
 *
 * LoongArch maintains ICache/DCache coherency by hardware,
 * so explicit data cache flush is not required. A data barrier
 * ensures memory ordering is complete.
 */
void
cpu_flush_dcache(void *ptr, size_t len)
{

	/*
	 * Hardware maintains I/D cache coherency.
	 * Just ensure memory operations are complete.
	 */
	__asm __volatile("dbar 0" ::: "memory");
}

/* Get current clock frequency for the given CPU ID. */
int
cpu_est_clockrate(int cpu_id, uint64_t *rate)
{
	uint32_t config;

	/*
	 * Try to read the core clock frequency from CPUCFG4.
	 * CPUCFG4_CCFREQ contains the core clock frequency in Hz.
	 * If not available, fall back to a default of 1 GHz.
	 */
	config = read_cpucfg(LOONGARCH_CPUCFG4);
	if (config != 0) {
		*rate = (uint64_t)config;
		return (0);
	}

	/* Fallback: 1 GHz default */
	*rate = 1000000000ULL;
	return (0);
}

void
cpu_pcpu_init(struct pcpu *pcpu, int cpuid, size_t size)
{
}

void
spinlock_enter(void)
{
	struct thread *td;
	register_t reg;

	td = curthread;
	if (td->td_md.md_spinlock_count == 0) {
		reg = intr_disable();
		td->td_md.md_spinlock_count = 1;
		td->td_md.md_saved_crmd_ie = reg;
		critical_enter();
	} else
		td->td_md.md_spinlock_count++;
}

void
spinlock_exit(void)
{
	struct thread *td;
	register_t crmd_ie;

	td = curthread;
	crmd_ie = td->td_md.md_saved_crmd_ie;
	td->td_md.md_spinlock_count--;
	if (td->td_md.md_spinlock_count == 0) {
		critical_exit();
		intr_restore(crmd_ie);
	}
}

/*
 * Construct a PCB from a trapframe. This is called from kdb_trap() where
 * we want to start a backtrace from the function that caused us to enter
 * the debugger. We have the context in the trapframe, but base the trace
 * on the PCB. The PCB doesn't have to be perfect, as long as it contains
 * enough for a backtrace.
 */
void
makectx(struct trapframe *tf, struct pcb *pcb)
{

	memcpy(pcb->pcb_regs, tf->tf_regs, sizeof(tf->tf_regs));

	pcb->pcb_era = tf->tf_era;
	pcb->pcb_a0 = tf->tf_regs[4];
	pcb->pcb_crmd = tf->tf_crmd;
	pcb->pcb_prmd = tf->tf_prmd;
	pcb->pcb_ecfg = tf->tf_ecfg;
	pcb->pcb_estat = tf->tf_estat;
}

static void
init_proc0(vm_offset_t kstack)
{
	struct pcpu *pcpup;

	pcpup = &__pcpu[0];

	proc_linkup0(&proc0, &thread0);
	thread0.td_kstack = kstack;
	thread0.td_kstack_pages = kstack_pages;
	thread0.td_pcb = (struct pcb *)(thread0.td_kstack +
	    thread0.td_kstack_pages * PAGE_SIZE) - 1;
	/* DEBUG: init_proc0: td=%p, kstack=%p, kstack_pages=%d, td_pcb=%p */
	/*    &thread0, (void*)kstack, kstack_pages, thread0.td_pcb); */
	bzero(thread0.td_pcb, sizeof(*thread0.td_pcb));
	thread0.td_pcb->pcb_fpflags = 0;
	thread0.td_frame = &proc0_tf;
	pcpup->pc_curpcb = thread0.td_pcb;
	/* DEBUG: init_proc0: done, thread0.td_pcb=%p */
}

typedef void (*efi_map_entry_cb)(struct efi_md *, void *argp);

static void
foreach_efi_map_entry(struct efi_map_header *efihdr, efi_map_entry_cb cb, void *argp)
{
	struct efi_md *map, *p;
	size_t efisz;
	int ndesc, i;

	/*
	 * Memory map data provided by UEFI via the GetMemoryMap
	 * Boot Services API.
	 */
	efisz = (sizeof(struct efi_map_header) + 0xf) & ~0xf;
	map = (struct efi_md *)((uint8_t *)efihdr + efisz);

	if (efihdr->descriptor_size == 0)
		return;
	ndesc = efihdr->memory_size / efihdr->descriptor_size;

	for (i = 0, p = map; i < ndesc; i++,
	    p = efi_next_descriptor(p, efihdr->descriptor_size)) {
		cb(p, argp);
	}
}

/*
 * Handle the EFI memory map list.
 *
 * We will make two passes at this, the first (exclude == false) to populate
 * physmem with valid physical memory ranges from recognized map entry types.
 * In the second pass we will exclude memory ranges from physmem which must not
 * be used for general allocations, either because they are used by runtime
 * firmware or otherwise reserved.
 *
 * Adding the runtime-reserved memory ranges to physmem and excluding them
 * later ensures that they are included in the DMAP, but excluded from
 * phys_avail[].
 *
 * Entry types not explicitly listed here are ignored and not mapped.
 */
static void
handle_efi_map_entry(struct efi_md *p, void *argp)
{
	bool exclude = *(bool *)argp;

	switch (p->md_type) {
	case EFI_MD_TYPE_RECLAIM:
		/*
		 * The recomended location for ACPI tables. Map into the
		 * DMAP so we can access them from userspace via /dev/mem.
		 */
	case EFI_MD_TYPE_RT_CODE:
		/*
		 * Some UEFI implementations put the system table in the
		 * runtime code section. Include it in the DMAP, but will
		 * be excluded from phys_avail.
		 */
	case EFI_MD_TYPE_RT_DATA:
		/*
		 * Runtime data will be excluded after the DMAP
		 * region is created to stop it from being added
		 * to phys_avail.
		 */
		if (exclude) {
			physmem_exclude_region(p->md_phys,
			    p->md_pages * EFI_PAGE_SIZE, EXFLAG_NOALLOC);
			break;
		}
		/* FALLTHROUGH */
	case EFI_MD_TYPE_CODE:
	case EFI_MD_TYPE_DATA:
	case EFI_MD_TYPE_BS_CODE:
	case EFI_MD_TYPE_BS_DATA:
	case EFI_MD_TYPE_FREE:
		/*
		 * We're allowed to use any entry with these types.
		 */
		if (!exclude)
			physmem_hardware_region(p->md_phys,
			    p->md_pages * EFI_PAGE_SIZE);
		break;
	default:
		/* Other types shall not be handled by physmem. */
		break;
	}
}

static void
add_efi_map_entries(struct efi_map_header *efihdr)
{
	bool exclude = false;
	foreach_efi_map_entry(efihdr, handle_efi_map_entry, &exclude);
}

static void
exclude_efi_map_entries(struct efi_map_header *efihdr)
{
	bool exclude = true;
	foreach_efi_map_entry(efihdr, handle_efi_map_entry, &exclude);
}

static void
print_efi_map_entry(struct efi_md *p, void *argp __unused)
{
	const char *type;
	static const char *types[] = {
		"Reserved",
		"LoaderCode",
		"LoaderData",
		"BootServicesCode",
		"BootServicesData",
		"RuntimeServicesCode",
		"RuntimeServicesData",
		"ConventionalMemory",
		"UnusableMemory",
		"ACPIReclaimMemory",
		"ACPIMemoryNVS",
		"MemoryMappedIO",
		"MemoryMappedIOPortSpace",
		"PalCode",
		"PersistentMemory"
	};

	if (p->md_type < nitems(types))
		type = types[p->md_type];
	else
		type = "<INVALID>";
	printf("%23s %012lx %012lx %08lx ", type, p->md_phys,
	    p->md_virt, p->md_pages);
	if (p->md_attr & EFI_MD_ATTR_UC)
		printf("UC ");
	if (p->md_attr & EFI_MD_ATTR_WC)
		printf("WC ");
	if (p->md_attr & EFI_MD_ATTR_WT)
		printf("WT ");
	if (p->md_attr & EFI_MD_ATTR_WB)
		printf("WB ");
	if (p->md_attr & EFI_MD_ATTR_UCE)
		printf("UCE ");
	if (p->md_attr & EFI_MD_ATTR_WP)
		printf("WP ");
	if (p->md_attr & EFI_MD_ATTR_RP)
		printf("RP ");
	if (p->md_attr & EFI_MD_ATTR_XP)
		printf("XP ");
	if (p->md_attr & EFI_MD_ATTR_NV)
		printf("NV ");
	if (p->md_attr & EFI_MD_ATTR_MORE_RELIABLE)
		printf("MORE_RELIABLE ");
	if (p->md_attr & EFI_MD_ATTR_RO)
		printf("RO ");
	if (p->md_attr & EFI_MD_ATTR_RT)
		printf("RUNTIME");
	printf("\n");
}

static void
print_efi_map_entries(struct efi_map_header *efihdr)
{

	printf("%23s %12s %12s %8s %4s\n",
	    "Type", "Physical", "Virtual", "#Pages", "Attr");
	foreach_efi_map_entry(efihdr, print_efi_map_entry, NULL);
}



/*
 * Map the passed in VA in EFI space to a void * using the efi memory table to
 * find the PA and return it in the DMAP, if it exists. We're used between the
 * calls to pmap_bootstrap() and physmem_init_kernel_globals() to parse CFG
 * tables We assume that either the entry you are mapping fits within its page,
 * or if it spills to the next page, that's contiguous in PA and in the DMAP.
 * All observed tables obey the first part of this precondition.
 */
struct early_map_data
{
	vm_offset_t va;
	vm_offset_t pa;
};

static void
efi_early_map_entry(struct efi_md *p, void *argp)
{
	struct early_map_data *emdp = argp;
	vm_offset_t s, e;

	if (emdp->pa != 0)
		return;
	if ((p->md_attr & EFI_MD_ATTR_RT) == 0)
		return;
	s = p->md_virt;
	e = p->md_virt + p->md_pages * EFI_PAGE_SIZE;
	if (emdp->va < s  || emdp->va >= e)
		return;
	emdp->pa = p->md_phys + (emdp->va - p->md_virt);
}

static void *
efi_early_map(vm_offset_t va)
{
	struct early_map_data emd = { .va = va };

	foreach_efi_map_entry(efihdr, efi_early_map_entry, &emd);
	if (emd.pa == 0)
		return NULL;
	return (void *)PHYS_TO_DMAP(emd.pa);
}


/*
 * When booted via kboot, the prior kernel will pass in reserved memory areas in
 * a EFI config table. We need to find that table and walk through it excluding
 * the memory ranges in it. btw, this is called too early for the printf to do
 * anything since msgbufp isn't initialized, let alone a console...
 */
static void
exclude_efi_memreserve(vm_offset_t efi_systbl_phys)
{
	struct efi_systbl *systbl;
	efi_guid_t efi_memreserve = LINUX_EFI_MEMRESERVE_TABLE;

	systbl = (struct efi_systbl *)PHYS_TO_DMAP(efi_systbl_phys);
	if (systbl == NULL) {
		printf("can't map systbl\n");
		return;
	}
	if (systbl->st_hdr.th_sig != EFI_SYSTBL_SIG) {
		printf("Bad signature for systbl %#lx\n", systbl->st_hdr.th_sig);
		return;
	}

	/*
	 * We don't yet have the pmap system booted enough to create a pmap for
	 * the efi firmware's preferred address space from the GetMemoryMap()
	 * table. The st_cfgtbl is a VA in this space, so we need to do the
	 * mapping ourselves to a kernel VA with efi_early_map. We assume that
	 * the cfgtbl entries don't span a page. Other pointers are PAs, as
	 * noted below.
	 */
	if (systbl->st_cfgtbl == 0)	/* Failsafe st_entries should == 0 in this case */
		return;
	for (int i = 0; i < systbl->st_entries; i++) {
		struct efi_cfgtbl *cfgtbl;
		struct linux_efi_memreserve *mr;

		cfgtbl = efi_early_map(systbl->st_cfgtbl + i * sizeof(*cfgtbl));
		if (cfgtbl == NULL)
			panic("Can't map the config table entry %d\n", i);
		if (memcmp(&cfgtbl->ct_guid, &efi_memreserve, sizeof(efi_guid_t)) != 0)
			continue;

		/*
		 * cfgtbl points are either VA or PA, depending on the GUID of
		 * the table. memreserve GUID pointers are PA and not converted
		 * after a SetVirtualAddressMap(). The list's mr_next pointer
		 * is also a PA.
		 */
		mr = (struct linux_efi_memreserve *)PHYS_TO_DMAP(
			(vm_offset_t)cfgtbl->ct_data);
		while (true) {
			for (int j = 0; j < mr->mr_count; j++) {
				struct linux_efi_memreserve_entry *mre;

				mre = &mr->mr_entry[j];
				physmem_exclude_region(mre->mre_base, mre->mre_size,
				    EXFLAG_NODUMP | EXFLAG_NOALLOC);
			}
			if (mr->mr_next == 0)
				break;
			mr = (struct linux_efi_memreserve *)PHYS_TO_DMAP(mr->mr_next);
		};
	}

}


#ifdef FDT
static void
try_load_dtb(caddr_t kmdp)
{
	vm_offset_t dtbp;

	UART_PROGRESS_CHAR('N');  /* Marker: entered try_load_dtb */

	dtbp = MD_FETCH(kmdp, MODINFOMD_DTBP, vm_offset_t);

#if defined(FDT_DTB_STATIC)
	/*
	 * In case the device tree blob was not retrieved (from metadata) try
	 * to use the statically embedded one.
	 */
	if (dtbp == (vm_offset_t)NULL)
		dtbp = (vm_offset_t)&fdt_static_dtb;
#endif

	if (dtbp == (vm_offset_t)NULL) {
		UART_PROGRESS_CHAR('O');  /* Marker: DTB not found (NULL) */
		printf("ERROR loading DTB\n");
		return;
	}

	UART_PROGRESS_CHAR('P');  /* Marker: DTB pointer found */

	if (OF_install(OFW_FDT, 0) == FALSE)
		panic("Cannot install FDT");

	if (OF_init((void *)dtbp) != 0)
		panic("OF_init failed with the found device tree");

	UART_PROGRESS_CHAR('Q');  /* Marker: OF_init succeeded */
}
#endif

/*
 * bus_probe - Detect and select the bus configuration method
 *
 * This function determines whether to use ACPI or FDT for device
 * enumeration based on:
 * 1. The kern.cfg.order environment variable (if set)
 * 2. Availability of ACPI tables
 * 3. Availability of FDT
 *
 * Returns true if a valid bus method was detected, false otherwise.
 */
bool
bus_probe(void)
{
	bool has_acpi, has_fdt;
	char *env, *order;

	has_acpi = has_fdt = false;

#ifdef FDT
	has_fdt = (OF_peer(0) != 0);
#endif
#ifdef DEV_ACPI
	has_acpi = (AcpiOsGetRootPointer() != 0);
#endif

	env = kern_getenv("kern.cfg.order");
	if (env != NULL) {
		order = env;
		while (order != NULL) {
			if (has_acpi &&
			    strncmp(order, "acpi", 4) == 0 &&
			    (order[4] == ',' || order[4] == '\0')) {
				loongarch_bus_method = LOONGARCH_BUS_ACPI;
				break;
			}
			if (has_fdt &&
			    strncmp(order, "fdt", 3) == 0 &&
			    (order[3] == ',' || order[3] == '\0')) {
				loongarch_bus_method = LOONGARCH_BUS_FDT;
				break;
			}
			order = strchr(order, ',');
			if (order != NULL)
				order++;	/* Skip comma */
		}
		freeenv(env);

		/* If we set the bus method it is valid */
		if (loongarch_bus_method != LOONGARCH_BUS_NONE)
			return (true);
	}

	/* If no order or an invalid order was set use the default */
	if (loongarch_bus_method == LOONGARCH_BUS_NONE) {
		if (has_acpi)
			loongarch_bus_method = LOONGARCH_BUS_ACPI;
		else if (has_fdt)
			loongarch_bus_method = LOONGARCH_BUS_FDT;
	}

	/* Report which bus method was selected */
	if (bootverbose) {
		switch (loongarch_bus_method) {
		case LOONGARCH_BUS_ACPI:
			printf("bus_probe: selected ACPI\n");
			break;
		case LOONGARCH_BUS_FDT:
			printf("bus_probe: selected FDT\n");
			break;
		default:
			printf("bus_probe: no valid bus method detected\n");
			break;
		}
	}

	/*
	 * If no option was set the default is valid, otherwise we are
	 * setting one to get cninit() working, then calling panic to tell
	 * the user about the invalid bus setup.
	 */
	return (env == NULL);
}

static void
cache_setup(void)
{
	uint32_t config, detail;
	int leaf;
	uint32_t min_dlinesz, min_ilinesz;
	uint32_t linesz;

	min_dlinesz = 0;
	min_ilinesz = 0;

	/*
	 * Read CPUCFG16 to determine which cache levels are present.
	 * Each bit indicates a cache leaf presence.
	 */
	config = read_cpucfg(LOONGARCH_CPUCFG16);

	/* Check L1 instruction cache (leaf 0) */
	if (config & CPUCFG16_L1_IUPRE) {
		detail = read_cpucfg(LOONGARCH_CPUCFG17);
		min_ilinesz = 1u << ((detail & CPUCFG_CACHE_LSIZE_M) >> CPUCFG_CACHE_LSIZE);
	}

	/* Check L1 data cache (leaf 1 if unified, or separate) */
	if (config & CPUCFG16_L1_DPRE) {
		leaf = (config & CPUCFG16_L1_IUUNIFY) ? 0 : 1;
		detail = read_cpucfg(LOONGARCH_CPUCFG17 + leaf);
		min_dlinesz = 1u << ((detail & CPUCFG_CACHE_LSIZE_M) >> CPUCFG_CACHE_LSIZE);
	}

	/*
	 * For higher-level caches, find the minimum line size across
	 * all present leaves. This ensures cache maintenance operations
	 * use the most conservative (smallest) line size.
	 */
	config = config >> 3;
	leaf = 2;
	while (config != 0) {
		if (config & 0x1) {
			detail = read_cpucfg(LOONGARCH_CPUCFG17 + leaf);
			linesz = 1u << ((detail & CPUCFG_CACHE_LSIZE_M) >> CPUCFG_CACHE_LSIZE);
			if (config & 0x4) {
				/* Unified cache - affects both I and D */
				if (min_ilinesz == 0 || linesz < min_ilinesz)
					min_ilinesz = linesz;
				if (min_dlinesz == 0 || linesz < min_dlinesz)
					min_dlinesz = linesz;
			} else if (config & 0x2) {
				/* Instruction cache */
				if (min_ilinesz == 0 || linesz < min_ilinesz)
					min_ilinesz = linesz;
			} else {
				/* Data cache */
				if (min_dlinesz == 0 || linesz < min_dlinesz)
					min_dlinesz = linesz;
			}
		}
		config >>= 7;
		leaf++;
	}

	/*
	 * Fallback to a safe default if no cache info was found.
	 * LoongArch typically uses 64-byte cache lines.
	 */
	if (min_dlinesz == 0)
		min_dlinesz = 64;
	if (min_ilinesz == 0)
		min_ilinesz = 64;

	dcache_line_size = min_dlinesz;
	icache_line_size = min_ilinesz;
	idcache_line_size = min_dlinesz < min_ilinesz ? min_dlinesz : min_ilinesz;

	if (bootverbose)
		printf("Cache: D-line=%jd I-line=%jd\n",
		    (intmax_t)dcache_line_size, (intmax_t)icache_line_size);
}

/*
 * Fake up a boot descriptor table (FDT version).
 * This function handles Device Tree Blob (DTB) setup for FDT-based boot.
 */
#ifdef FDT
static void
fake_preload_metadata_fdt(struct loongarch_bootparams *bp)
{
	static uint32_t fake_preload[48];
	vm_offset_t lastaddr;
	size_t fake_size, dtb_size;

#define PRELOAD_PUSH_VALUE(type, value) do {			\
	*(type *)((char *)fake_preload + fake_size) = (value);	\
	fake_size += sizeof(type);				\
} while (0)

#define PRELOAD_PUSH_STRING(str) do {				\
	uint32_t ssize;						\
	ssize = strlen(str) + 1;				\
	PRELOAD_PUSH_VALUE(uint32_t, ssize);			\
	strcpy(((char *)fake_preload + fake_size), str);	\
	fake_size += ssize;					\
	fake_size = roundup(fake_size, sizeof(u_long));		\
} while (0)

	fake_size = 0;
	lastaddr = (vm_offset_t)&end;

	PRELOAD_PUSH_VALUE(uint32_t, MODINFO_NAME);
	PRELOAD_PUSH_STRING("kernel");
	PRELOAD_PUSH_VALUE(uint32_t, MODINFO_TYPE);
	PRELOAD_PUSH_STRING("elf kernel");

	PRELOAD_PUSH_VALUE(uint32_t, MODINFO_ADDR);
	PRELOAD_PUSH_VALUE(uint32_t, sizeof(vm_offset_t));
	PRELOAD_PUSH_VALUE(uint64_t, KERNBASE);

	PRELOAD_PUSH_VALUE(uint32_t, MODINFO_SIZE);
	PRELOAD_PUSH_VALUE(uint32_t, sizeof(size_t));
	PRELOAD_PUSH_VALUE(uint64_t, (size_t)((vm_offset_t)&end - KERNBASE));

	/* Copy the DTB to KVA space. */
	lastaddr = roundup(lastaddr, sizeof(int));
	PRELOAD_PUSH_VALUE(uint32_t, MODINFO_METADATA | MODINFOMD_DTBP);
	PRELOAD_PUSH_VALUE(uint32_t, sizeof(vm_offset_t));
	PRELOAD_PUSH_VALUE(vm_offset_t, lastaddr);
	dtb_size = fdt_totalsize(bp->dtbp_virt);
	memmove((void *)lastaddr, (const void *)bp->dtbp_virt, dtb_size);
	lastaddr = roundup(lastaddr + dtb_size, sizeof(int));

	PRELOAD_PUSH_VALUE(uint32_t, MODINFO_METADATA | MODINFOMD_KERNEND);
	PRELOAD_PUSH_VALUE(uint32_t, sizeof(vm_offset_t));
	PRELOAD_PUSH_VALUE(vm_offset_t, lastaddr);

	PRELOAD_PUSH_VALUE(uint32_t, MODINFO_METADATA | MODINFOMD_HOWTO);
	PRELOAD_PUSH_VALUE(uint32_t, sizeof(int));
	PRELOAD_PUSH_VALUE(int, RB_VERBOSE);

	/* End marker */
	PRELOAD_PUSH_VALUE(uint32_t, 0);
	PRELOAD_PUSH_VALUE(uint32_t, 0);
	preload_metadata = (caddr_t)fake_preload;

	/* Check if bootloader clobbered part of the kernel with the DTB. */
	KASSERT(bp->dtbp_phys + dtb_size <= bp->kern_phys ||
	    bp->dtbp_phys >= bp->kern_phys + (lastaddr - KERNBASE),
	    ("FDT (%lx-%lx) and kernel (%lx-%lx) overlap", bp->dtbp_phys,
		bp->dtbp_phys + dtb_size, bp->kern_phys,
		bp->kern_phys + (lastaddr - KERNBASE)));
	KASSERT(fake_size < sizeof(fake_preload),
	    ("Too many fake_preload items"));

	if (boothowto & RB_VERBOSE)
		printf("FDT phys (%lx-%lx), kernel phys (%lx-%lx)\n",
		    bp->dtbp_phys, bp->dtbp_phys + dtb_size,
		    bp->kern_phys, bp->kern_phys + (lastaddr - KERNBASE));
}
#endif /* FDT */

/*
 * Fake up a boot descriptor table (ACPI version).
 * This function does not handle DTB, as ACPI uses RSDP/XSDT for device discovery.
 */
#ifdef DEV_ACPI
static void
fake_preload_metadata_acpi(struct loongarch_bootparams *bp)
{
	static uint32_t fake_preload[32];
	vm_offset_t lastaddr;
	size_t fake_size;

#define ACPI_PRELOAD_PUSH_VALUE(type, value) do {		\
	*(type *)((char *)fake_preload + fake_size) = (value);	\
	fake_size += sizeof(type);				\
} while (0)

#define ACPI_PRELOAD_PUSH_STRING(str) do {			\
	uint32_t ssize;						\
	ssize = strlen(str) + 1;				\
	ACPI_PRELOAD_PUSH_VALUE(uint32_t, ssize);		\
	strcpy(((char *)fake_preload + fake_size), str);	\
	fake_size += ssize;					\
	fake_size = roundup(fake_size, sizeof(u_long));		\
} while (0)

	fake_size = 0;
	lastaddr = (vm_offset_t)&end;

	ACPI_PRELOAD_PUSH_VALUE(uint32_t, MODINFO_NAME);
	ACPI_PRELOAD_PUSH_STRING("kernel");
	ACPI_PRELOAD_PUSH_VALUE(uint32_t, MODINFO_TYPE);
	ACPI_PRELOAD_PUSH_STRING("elf kernel");

	ACPI_PRELOAD_PUSH_VALUE(uint32_t, MODINFO_ADDR);
	ACPI_PRELOAD_PUSH_VALUE(uint32_t, sizeof(vm_offset_t));
	ACPI_PRELOAD_PUSH_VALUE(uint64_t, KERNBASE);

	ACPI_PRELOAD_PUSH_VALUE(uint32_t, MODINFO_SIZE);
	ACPI_PRELOAD_PUSH_VALUE(uint32_t, sizeof(size_t));
	ACPI_PRELOAD_PUSH_VALUE(uint64_t, (size_t)((vm_offset_t)&end - KERNBASE));

	ACPI_PRELOAD_PUSH_VALUE(uint32_t, MODINFO_METADATA | MODINFOMD_KERNEND);
	ACPI_PRELOAD_PUSH_VALUE(uint32_t, sizeof(vm_offset_t));
	ACPI_PRELOAD_PUSH_VALUE(vm_offset_t, lastaddr);

	ACPI_PRELOAD_PUSH_VALUE(uint32_t, MODINFO_METADATA | MODINFOMD_HOWTO);
	ACPI_PRELOAD_PUSH_VALUE(uint32_t, sizeof(int));
	ACPI_PRELOAD_PUSH_VALUE(int, RB_VERBOSE);

	/* End marker */
	ACPI_PRELOAD_PUSH_VALUE(uint32_t, 0);
	ACPI_PRELOAD_PUSH_VALUE(uint32_t, 0);
	preload_metadata = (caddr_t)fake_preload;

	KASSERT(fake_size < sizeof(fake_preload),
	    ("Too many fake_preload items"));

	if (boothowto & RB_VERBOSE)
		printf("ACPI mode: kernel phys (%lx-%lx)\n",
		    bp->kern_phys, bp->kern_phys + (lastaddr - KERNBASE));
}
#endif /* DEV_ACPI */

/*
 * Support for ACPI or FDT configurations.
 * At least one of DEV_ACPI or FDT must be defined for the kernel to work.
 */

#ifdef FDT
static void
parse_fdt_bootargs(void)
{
	char bootargs[512];

	bootargs[sizeof(bootargs) - 1] = '\0';
	if (fdt_get_chosen_bootargs(bootargs, sizeof(bootargs) - 1) == 0) {
		boothowto |= boot_parse_cmdline(bootargs);
	}
}
#endif

static vm_offset_t
parse_metadata(void)
{
	caddr_t kmdp;
	vm_offset_t lastaddr;
#ifdef DDB
	vm_offset_t ksym_start, ksym_end;
#endif
	char *kern_envp;

	UART_PROGRESS_CHAR('k');  /* Marker: entered parse_metadata */

	/* Find the kernel address */
	kmdp = preload_search_by_type("elf kernel");
	if (kmdp == NULL)
		kmdp = preload_search_by_type("elf64 kernel");
	if (kmdp == NULL) {
		/*
		 * No preload metadata found. This can happen when booting
		 * directly from firmware without a bootloader.
		 * Fall back to defaults and continue.
		 */
		UART_PROGRESS_CHAR('!');  /* Marker: no preload metadata */
		printf("WARNING: No preload metadata found, using defaults.\n");
		init_static_kenv(static_kenv, sizeof(static_kenv));
		return (0);
	}

	UART_PROGRESS_CHAR('m');  /* Marker: kmdp found */

	/* Read the boot metadata */
	boothowto = MD_FETCH(kmdp, MODINFOMD_HOWTO, int);
	lastaddr = MD_FETCH(kmdp, MODINFOMD_KERNEND, vm_offset_t);
	kern_envp = MD_FETCH(kmdp, MODINFOMD_ENVP, char *);
	if (kern_envp != NULL)
		init_static_kenv(kern_envp, 0);
	else
		init_static_kenv(static_kenv, sizeof(static_kenv));
#ifdef DDB
	ksym_start = MD_FETCH(kmdp, MODINFOMD_SSYM, uintptr_t);
	ksym_end = MD_FETCH(kmdp, MODINFOMD_ESYM, uintptr_t);
	db_fetch_ksymtab(ksym_start, ksym_end, 0);
#endif
#ifdef FDT
	try_load_dtb(kmdp);
	if (kern_envp == NULL)
		parse_fdt_bootargs();
#endif
	return (lastaddr);
}

void
initloongarch(struct loongarch_bootparams *bp)
{
	struct pcpu *pcpup;
	vm_offset_t lastaddr;
	vm_size_t kernlen;
	char *env;
	struct efi_fb *efifb;
	caddr_t kmdp;

	UART_PROGRESS_CHAR('F');  /* Marker: entered initloongarch */
	printf("in %s\n", __func__);
	UART_PROGRESS_CHAR('G');  /* Marker: printf worked */

	TSRAW(&thread0, TS_ENTER, __func__, NULL);

	/*
	 * PTW Diagnostic: Print CPU features early in boot
	 * This helps identify if hardware PTW is supported
	 */
#ifdef DIAGNOSTIC
	{
		uint32_t cpucfg2;
		cpucfg2 = read_cpucfg(LOONGARCH_CPUCFG2);
		printf("PTW_DIAG: CPU features - CPUCFG2=0x%08x\n", cpucfg2);
		printf("PTW_DIAG:   HPTW (Hardware PTW) support: %s\n",
		    (cpucfg2 & CPUCFG2_PTW) ? "YES" : "NO");
	}
	
	/*
	 * Print PTW diagnostic information from early boot
	 * This helps debug TLB refill issues
	 */
	{
		extern uint64_t ptw_diag_store[];
		uint64_t magic = ptw_diag_store[0];
		
		printf("PTW_DIAG: Diagnostic storage at %p\n", ptw_diag_store);
		printf("PTW_DIAG: Magic=0x%lx ", magic);
		
		if (magic == 0x5054575f5357) {
			printf("(Software PTW)\n");
		} else if (magic == 0x5054575f4857) {
			printf("(Hardware PTW)\n");
		} else {
			printf("(Unknown/Not initialized)\n");
		}
		
		if (magic != 0) {
			printf("PTW_DIAG: badvaddr=0x%lx\n", ptw_diag_store[1]);
			printf("PTW_DIAG: pgd_base=0x%lx\n", ptw_diag_store[2]);
			printf("PTW_DIAG: l0_entry=0x%lx\n", ptw_diag_store[3]);
			printf("PTW_DIAG: l1_entry=0x%lx\n", ptw_diag_store[4]);
			printf("PTW_DIAG: l2_entry=0x%lx\n", ptw_diag_store[5]);
			printf("PTW_DIAG: l3_entry=0x%lx\n", ptw_diag_store[6]);
			
			uint64_t status = ptw_diag_store[7];
			printf("PTW_DIAG: status=0x%lx ", status);
			if (status == 0x53575f4f4b) {
				printf("(Success)\n");
			} else if (status == 0x53575f485547) {
				printf("(Huge page)\n");
			} else if (status == 0x53575f464c54) {
				printf("(Fault)\n");
				printf("PTW_DIAG: fault_addr=0x%lx\n", ptw_diag_store[8]);
				printf("PTW_DIAG: tlbelo0=0x%lx\n", ptw_diag_store[9]);
				printf("PTW_DIAG: tlbelo1=0x%lx\n", ptw_diag_store[10]);
			} else {
				printf("(No TLB refill yet)\n");
			}
		}
	}
#endif

	/*
	 * Report which TLB refill handler is in use.
	 * locore.S always installs handle_tlbrefill_sw via TLBRENTRY
	 * which does a pure software page table walk via DMW1 (ldx.d).
	 * It does NOT depend on lddir/ldpte or CSR_PGD auto-selection.
	 */
	{
		uint32_t cpucfg2;

		cpucfg2 = read_cpucfg(LOONGARCH_CPUCFG2);

		if (cpucfg2 & CPUCFG2_PTW) {
			printf("PTW: Hardware PTW available,"
			    " refill via handle_tlbrefill_sw\n");
		} else {
			printf("PTW: Software TLB refill via"
			    " handle_tlbrefill_sw (DMW1 walk)\n");
		}
	}

	/* Set the pcpu data, this is needed by pmap_bootstrap */
	pcpup = &__pcpu[0];
	pcpu_init(pcpup, 0, sizeof(struct pcpu));

	/* Set the pcpu pointer */
	__asm __volatile("move $r21, %0" :: "r"(pcpup));
	__asm __volatile("csrwr %0, %1" : "+r"(pcpup) : "i"(PERCPU_BASE_KS));

	PCPU_SET(curthread, &thread0);

	UART_PROGRESS_CHAR('H');  /* Marker: before metadata parse */

	/* Parse the boot metadata. */
	if (bp->modulep != 0) {
		preload_metadata = (caddr_t)((uint64_t)bp->modulep |
		    0x8000000000000000ULL);
		UART_PROGRESS_CHAR('I');  /* Marker: modulep set via DMW0 */
	} else {
#if !defined(FDT) && !defined(DEV_ACPI)
		panic("Kernel compiled without FDT or DEV_ACPI support");
#endif
#if defined(DEV_ACPI)
		/*
		 * ACPI is preferred when both ACPI and FDT are available.
		 * At this early stage loongarch_bus_method is LOONGARCH_BUS_NONE
		 * (bus_probe runs later), so we default to ACPI metadata.
		 */
		fake_preload_metadata_acpi(bp);
#elif defined(FDT)
		fake_preload_metadata_fdt(bp);
#endif
		UART_PROGRESS_CHAR('J');  /* Marker: using fake metadata */
	}
	UART_PROGRESS_CHAR('K');  /* Marker: before parse_metadata() */
	lastaddr = parse_metadata();
	UART_PROGRESS_CHAR('L');  /* Marker: after parse_metadata() */

	/* Read boot hart ID from hardware CSR */
	boot_hart = csr_read32(LOONGARCH_CSR_CPUID);
	PCPU_SET(hart, boot_hart);

	/* Enable all IPI types on the BSP */
	iocsr_write32(0xffffffff, LOONGARCH_IOCSR_IPI_EN);

	kmdp = preload_search_by_type("elf kernel");
	if (kmdp == NULL)
		kmdp = preload_search_by_type("elf64 kernel");
	UART_PROGRESS_CHAR('M');  /* Marker: kmdp found */

	efi_systbl_phys = MD_FETCH(kmdp, MODINFOMD_FW_HANDLE, vm_paddr_t);
	UART_PROGRESS_CHAR('a');  /* Marker: MD_FETCH FW_HANDLE done */

	/* Load the physical memory ranges */
	efihdr = (struct efi_map_header *)preload_search_info(kmdp,
	    MODINFO_METADATA | MODINFOMD_EFI_MAP);
	UART_PROGRESS_CHAR('b');  /* Marker: preload_search_info EFI_MAP done */
	if (efihdr != NULL) {
		add_efi_map_entries(efihdr);
		UART_PROGRESS_CHAR('c');  /* Marker: add_efi_map_entries done */
		print_efi_map_entries(efihdr);
		UART_PROGRESS_CHAR('d');  /* Marker: print_efi_map_entries done */
	}
#ifdef FDT
	else if (loongarch_bus_method == LOONGARCH_BUS_FDT) {
		/*
		 * Exclude reserved memory specified by the device tree. Typically,
		 * this contains an entry for memory used by the runtime firmware.
		 */
		fdt_foreach_reserved_mem(fdt_physmem_exclude_region_cb, NULL);

		/* Grab physical memory regions information from device tree. */
		if (fdt_foreach_mem_region(fdt_physmem_hardware_region_cb,
		    NULL) != 0)
			panic("Cannot get physical memory regions");
	}
#endif
#ifdef DEV_ACPI
	else if (loongarch_bus_method == LOONGARCH_BUS_ACPI) {
		/*
		 * TODO: ACPI memory discovery via SRAT/EFI runtime services.
		 * For now, rely on EFI memory map passed by loader.
		 * If EFI memory map is not available, print a warning.
		 */
		printf("WARNING: No EFI memory map available in ACPI mode.\n");
		printf("         ACPI memory discovery via SRAT not yet implemented.\n");
	}
#endif
	else {
		/*
		 * No EFI memory map and no fallback available.
		 * This should not happen in normal operation.
		 */
		printf("WARNING: No memory discovery method available.\n");
	}
	UART_PROGRESS_CHAR('e');  /* Marker: memory discovery method chosen */

	/* Exclude the EFI framebuffer from our view of physical memory. */
	efifb = (struct efi_fb *)preload_search_info(kmdp,
	    MODINFO_METADATA | MODINFOMD_EFI_FB);
	if (efifb != NULL)
		physmem_exclude_region(efifb->fb_addr, efifb->fb_size,
		    EXFLAG_NOALLOC);

	/*
	 * Identify CPU/ISA features.
	 */
	identify_cpu(0);
	UART_PROGRESS_CHAR('f');  /* Marker: identify_cpu done */

	/*
	 * Set vm_guest for QEMU/KVM environments.
	 * This is needed for proper VM detection in various subsystems.
	 * On loongarch64 running in QEMU, we're always in a VM.
	 */
	vm_guest = VM_GUEST_KVM;

	/* Do basic tuning, hz etc */
	init_param1();

	cache_setup();

	/* Bootstrap enough of pmap to enter the kernel proper */
	kernlen = (lastaddr - KERNBASE);
	UART_PROGRESS_CHAR('g');  /* Marker: before pmap_bootstrap */
	pmap_bootstrap(bp->kern_l1pt, bp->kern_phys, kernlen);
	UART_PROGRESS_CHAR('h');  /* Marker: pmap_bootstrap done */

	/* Exclude entries needed in the DMAP region, but not phys_avail */
	if (efihdr != NULL)
		exclude_efi_map_entries(efihdr);
	/*  Do the same for reserve entries in the EFI MEMRESERVE table */
	if (efi_systbl_phys != 0)
		exclude_efi_memreserve(efi_systbl_phys);
	else if (bootverbose)
		printf("EFI: No system table handle, skipping memreserve exclusion.\n");
	UART_PROGRESS_CHAR('i');  /* Marker: exclude_efi done */

	physmem_init_kernel_globals();
	UART_PROGRESS_CHAR('j');  /* Marker: physmem_init done */

	/*
	 * Probe for bus configuration (ACPI vs FDT).
	 * This determines which bus method will be used for device enumeration.
	 */
	if (!bus_probe())
		panic("Invalid bus configuration: %s",
		    kern_getenv("kern.cfg.order"));
	UART_PROGRESS_CHAR('p');  /* Marker: bus_probe done */

	/*
	 * Early ACPI table parsing (MADT) for interrupt controller discovery.
	 * Must happen before cninit() so console can use interrupt info.
	 */
#ifdef DEV_ACPI
	if (loongarch_bus_method == LOONGARCH_BUS_ACPI) {
		/*
		 * TODO: Parse MADT table to discover:
		 * - CPU cores (CORE_PIC entries)
		 * - Interrupt controllers (EIO_PIC, BIO_PIC, MSI_PIC)
		 * For now, this is a placeholder for future implementation.
		 */
		if (bootverbose)
			printf("ACPI: Early MADT parsing not yet implemented.\n");
	}
#endif

	UART_PROGRESS_CHAR('q');  /* Marker: before cninit */
	cninit();
	UART_PROGRESS_CHAR('r');  /* Marker: cninit done */

	/*
	 * PTW Diagnostic: Print CPU features and page table structure
	 * AFTER console is initialized. This ensures printf output
	 * will be visible.
	 */
#ifdef DIAGNOSTIC
	{
		uint32_t cpucfg2;
		uint64_t pwcl, pwch, pgd, pgdl, pgdh;

		/* Print CPU features */
		cpucfg2 = read_cpucfg(LOONGARCH_CPUCFG2);
		printf("PTW_DIAG: CPU features (post-console) - CPUCFG2=0x%08x\n", cpucfg2);
		printf("PTW_DIAG:   HPTW (Hardware PTW) support: %s\n",
		    (cpucfg2 & CPUCFG2_PTW) ? "YES" : "NO");

		printf("\n=== PTW_DIAG: Page Table Structure ===\n");

		/* Print PWCL/PWCH configuration */
		pwcl = csr_read64(LOONGARCH_CSR_PWCTL0);
		pwch = csr_read64(LOONGARCH_CSR_PWCTL1);

		printf("PWCL0: 0x%016lx\n", pwcl);
		printf("  PTBASE:    %2lu  (L3/PTE base bit)\n", pwcl & 0x1f);
		printf("  PTWIDTH:   %2lu  (L3/PTE index width)\n", (pwcl >> 5) & 0x1f);
		printf("  DIR0BASE:  %2lu  (L2/PMD base bit)\n", (pwcl >> 10) & 0x1f);
		printf("  DIR0WIDTH: %2lu  (L2/PMD index width)\n", (pwcl >> 15) & 0x1f);
		printf("  DIR1BASE:  %2lu  (L1/PUD base bit)\n", (pwcl >> 20) & 0x1f);
		printf("  DIR1WIDTH: %2lu  (L1/PUD index width)\n", (pwcl >> 25) & 0x1f);
		printf("  PTEW:      %2lu  (0=64-bit PTE)\n", (pwcl >> 30) & 0x3);

		printf("PWCH1: 0x%016lx\n", pwch);
		printf("  DIR2BASE:  %2lu  (L0/PGD base bit)\n", pwch & 0x3f);
		printf("  DIR2WIDTH: %2lu  (L0/PGD index width)\n", (pwch >> 6) & 0x3f);
		printf("  PTW_EN:    %2lu  (Hardware PTW: %s)\n",
		    (pwch >> 24) & 0x1, ((pwch >> 24) & 0x1) ? "YES" : "NO");

		/* Print PGD registers */
		pgd = csr_read64(LOONGARCH_CSR_PGD);
		pgdl = csr_read64(LOONGARCH_CSR_PGDL);
		pgdh = csr_read64(LOONGARCH_CSR_PGDH);

		printf("PGD Registers:\n");
		printf("  PGD:  0x%016lx\n", pgd);
		printf("  PGDL: 0x%016lx (user space)\n", pgdl);
		printf("  PGDH: 0x%016lx (kernel space)\n", pgdh);

		/* Get kernel page table top */
		extern struct pmap kernel_pmap_store;
		pd_entry_t *l0 = kernel_pmap_store.pm_top;

		printf("\nKernel Page Table Top (L0): %p\n", l0);

		if (l0 != NULL) {
			/* Walk kernel address: KERNBASE */
			vm_offset_t va = KERNBASE;
			int l0_idx, l1_idx, l2_idx, l3_idx;
			pd_entry_t l0e, l1e, l2e;
			pt_entry_t l3e;

			l0_idx = (va >> L0_SHIFT) & Ln_ADDR_MASK;
			l1_idx = (va >> L1_SHIFT) & Ln_ADDR_MASK;
			l2_idx = (va >> L2_SHIFT) & Ln_ADDR_MASK;
			l3_idx = (va >> L3_SHIFT) & Ln_ADDR_MASK;

			printf("\nWalking KERNBASE (0x%lx):\n", va);
			printf("  Indices: L0=%d, L1=%d, L2=%d, L3=%d\n",
			    l0_idx, l1_idx, l2_idx, l3_idx);

			/* L0 */
			l0e = l0[l0_idx];
			printf("  L0[%d]: %p -> 0x%016lx [PPN=0x%lx]\n",
			    l0_idx, &l0[l0_idx], l0e,
			    (unsigned long)((l0e & PTE_PFN_MASK) >> L3_SHIFT));

			if (PDE_VALID(l0e)) {
				vm_paddr_t l1_phys = PTE_TO_PHYS(l0e);
				pd_entry_t *l1 = (pd_entry_t *)PHYS_TO_DMAP(l1_phys);

				l1e = l1[l1_idx];
				printf("  L1[%d]: %p (phys=0x%lx) -> 0x%016lx [PPN=0x%lx,HUGE=%d]\n",
				    l1_idx, &l1[l1_idx], (unsigned long)l1_phys, l1e,
				    (unsigned long)((l1e & PTE_PFN_MASK) >> L3_SHIFT),
				    (l1e & PTE_HUGE) ? 1 : 0);

				if (PDE_VALID(l1e) && !(l1e & PTE_HUGE)) {
					vm_paddr_t l2_phys = PTE_TO_PHYS(l1e);
					pd_entry_t *l2 = (pd_entry_t *)PHYS_TO_DMAP(l2_phys);

					l2e = l2[l2_idx];
					printf("  L2[%d]: %p (phys=0x%lx) -> 0x%016lx [PPN=0x%lx,HUGE=%d]\n",
					    l2_idx, &l2[l2_idx], (unsigned long)l2_phys, l2e,
					    (unsigned long)((l2e & PTE_PFN_MASK) >> L3_SHIFT),
					    (l2e & PTE_HUGE) ? 1 : 0);

					if (PDE_VALID(l2e) && !(l2e & PTE_HUGE)) {
						vm_paddr_t l3_phys = PTE_TO_PHYS(l2e);
						pt_entry_t *l3 = (pt_entry_t *)PHYS_TO_DMAP(l3_phys);

						l3e = l3[l3_idx];
						printf("  L3[%d]: %p (phys=0x%lx) -> 0x%016lx [V=%d,D=%d,W=%d,A=%d]\n",
						    l3_idx, &l3[l3_idx], (unsigned long)l3_phys, l3e,
						    (l3e & PTE_V) ? 1 : 0,
						    (l3e & PTE_D) ? 1 : 0,
						    (l3e & PTE_W) ? 1 : 0,
						    (l3e & PTE_A) ? 1 : 0);

						if (l3e & PTE_V) {
							vm_paddr_t pa = PTE_TO_PHYS(l3e);
							printf("  -> Physical: 0x%lx\n", (unsigned long)pa);
						}
					} else if (l2e & PTE_HUGE) {
						vm_paddr_t pa = L2PTE_TO_PHYS(l2e);
						printf("  -> L2 HUGE PAGE -> Physical: 0x%lx\n", (unsigned long)pa);
					}
				}
			}

			/* Also check TLB configuration */
			uint64_t stlbps = csr_read64(LOONGARCH_CSR_STLBPGSIZE);
			uint64_t tlbidx = csr_read64(LOONGARCH_CSR_TLBIDX);

			printf("\nTLB Configuration:\n");
			printf("  STLBPGSIZE: 0x%lx (PS=%lu, %s)\n",
			    stlbps, (stlbps >> 24) & 0x3f,
			    (stlbps == 0xc) ? "4KB" : "unknown");
			printf("  TLBIDX PS: %lu\n", (tlbidx >> 24) & 0x3f);
		}

		printf("=== End PTW_DIAG Page Table ===\n\n");
	}
#endif

	/*
	 * Enable hardware page table walker (PTW) if supported by CPU.
	 *
	 * When PTW_EN is set in PWCTL1, the CPU automatically walks the
	 * page table on TLB miss using the hardware walker.  This correctly
	 * handles huge pages (2MB/1GB) at any level.
	 *
	 * QEMU's hardware PTW uses bits 7-8 as P (Present) and W (Write):
	 *   - PTE_W (bit 7) is set in ALL valid leaf entries (P=1)
	 *   - PTE_SW_WIRED (bit 8) is set in writable entries (W=1)
	 *   - pmap_fault uses PTE_SW_WIRED for write-permission checks
	 *
	 * CSR write order matches Linux setup_ptwalker():
	 *   PWCTL0 → PWCTL1(PTW_EN) → PGDH → PGDL
	 */
	{
		uint32_t cpucfg2 = read_cpucfg(LOONGARCH_CPUCFG2);

		if (cpucfg2 & CPUCFG2_PTW) {
			uint64_t pgdh_phys, pwctl1;

			/* Calculate kernel page table physical address directly from bootparams */
			pgdh_phys = bp->kern_phys + (bp->kern_l1pt - VM_MIN_KERNEL_ADDRESS);

			/* Update global for later use */
			kernel_pmap_phys = pgdh_phys;

			/* 1. Rewrite PWCTL0 (same value, ensure visibility) */
			csr_write64(csr_read64(LOONGARCH_CSR_PWCTL0),
			    LOONGARCH_CSR_PWCTL0);

			/* 2. Set PTW_EN in PWCTL1 */
			pwctl1 = csr_read64(LOONGARCH_CSR_PWCTL1);
			pwctl1 |= CSR_PWCTL1_PTW;
			csr_write64(pwctl1, LOONGARCH_CSR_PWCTL1);

			/* 3. Set PGDH/PGDL AFTER PTW_EN (matches Linux) */
			csr_write64(pgdh_phys, LOONGARCH_CSR_PGDH);
			/*
			 * Set PGDL to invalid_pgdir (all zeros, mirrors Linux
			 * invalid_pg_dir).  Hardware PTW for user-space VAs
			 * will immediately fail (L0 entry = 0), triggering
			 * TLBR -> V=0 fill -> TLBL/TLBS -> normal page fault.
			 * pgdl is updated to the per-process page table by
			 * pmap_activate_sw() when a user process is scheduled.
			 */
			{
				extern uint64_t invalid_pgdir[];
				csr_write64(
				    pmap_kextract((vm_offset_t)invalid_pgdir),
				    LOONGARCH_CSR_PGDL);
			}

			printf("PTW: Hardware PTW enabled (PWCTL1=0x%lx, PGDH=0x%lx, PGDL=0x%lx)\n",
			    csr_read64(LOONGARCH_CSR_PWCTL1),
			    csr_read64(LOONGARCH_CSR_PGDH),
			    csr_read64(LOONGARCH_CSR_PGDL));
		}
	}

	/*
	 * Dump the boot metadata. We have to wait for cninit() since console
	 * output is required. If it's grossly incorrect the kernel will never
	 * make it this far.
	 */
	if (getenv_is_true("debug.dump_modinfo_at_boot"))
		preload_dump();

	init_proc0(bp->kern_stack);

	msgbufinit(msgbufp, msgbufsize);
	mutex_init();
	init_param2(physmem);
	kdb_init();
#ifdef KDB
	if ((boothowto & RB_KDB) != 0)
		kdb_enter(KDB_WHY_BOOTFLAGS, "Boot flags requested debugger");
#endif

	env = kern_getenv("kernelname");
	if (env != NULL)
		strlcpy(kernelname, env, sizeof(kernelname));

	if (boothowto & RB_VERBOSE)
		physmem_print_tables();

	early_boot = 0;

	TSEXIT();
}
