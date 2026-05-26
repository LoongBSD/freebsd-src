/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * Copyright (c) 2016 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2024 Xiaoqiang Zhao <zxq_yx_007@163.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 * All rights reserved.
 *
 * Portions of this software were developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
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
#include "opt_kstack_pages.h"
#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/cpuset.h>
#include <sys/intr.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>

#include <machine/smp.h>
#include <machine/cpufunc.h>
#include <machine/loongarchreg.h>

#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/actables.h>
#include <dev/acpica/acpivar.h>
#include <machine/madt_var.h>
#endif

#ifdef FDT
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_cpu.h>
#endif

#define	MP_BOOTSTACK_SIZE	(kstack_pages * PAGE_SIZE)

uint32_t __loongarch_boot_ap[MAXCPU];

static enum {
	CPUS_UNKNOWN,
#ifdef FDT
	CPUS_FDT,
#endif
#ifdef DEV_ACPI
	CPUS_ACPI,
#endif
} cpu_enum_method;

static void ipi_ast(void *);
static void ipi_hardclock(void *);
static void ipi_preempt(void *);
static void ipi_rendezvous(void *);
static void ipi_stop(void *);
static void ipi_stop_hard(void *);

#ifdef DEV_ACPI
static bool cpu_init_acpi(u_int id, u_int cpu_id);
static void cpu_mp_late_init_acpi(void);
#endif

extern uint32_t boot_hart;
extern cpuset_t all_harts;

#if defined(INVARIANTS) && defined(FDT)
static uint32_t cpu_reg[MAXCPU][2];
#endif

void mpentry(u_long cpuid);
void init_secondary(uint64_t);

static struct mtx ap_boot_mtx;

/* Stacks for AP initialization, discarded once idle threads are started. */
void *bootstack;
void *bootstacks[MAXCPU];
void *bootpcpu[MAXCPU];

/* Count of started APs, used to synchronize access to bootstack. */
static volatile int aps_started;

/* Set to 1 once we're ready to let the APs out of the pen. */
static volatile int aps_ready;

/* Temporary variables for init_secondary()  */
void *dpcpu[MAXCPU - 1];

static void
loongarch_wakeup_aps(void)
{
	vm_paddr_t entry_pa;
	uint64_t val;
	u_int cpu, hart;

	entry_pa = pmap_kextract((vm_offset_t)mpentry);

	CPU_FOREACH(cpu) {
		if (cpu == 0)
			continue;

		hart = __pcpu[cpu].pc_hart;

		val = IOCSR_MBUF_SEND_BLOCKING;
		val |= (IOCSR_MBUF_SEND_BOX_HI(0) << IOCSR_MBUF_SEND_BOX_SHIFT);
		val |= ((uint64_t)hart << IOCSR_MBUF_SEND_CPU_SHIFT);
		val |= (entry_pa & IOCSR_MBUF_SEND_H32_MASK);
		iocsr_write64(val, LOONGARCH_IOCSR_MBUF_SEND);

		val = IOCSR_MBUF_SEND_BLOCKING;
		val |= (IOCSR_MBUF_SEND_BOX_LO(0) << IOCSR_MBUF_SEND_BOX_SHIFT);
		val |= ((uint64_t)hart << IOCSR_MBUF_SEND_CPU_SHIFT);
		val |= (entry_pa << IOCSR_MBUF_SEND_BUF_SHIFT);
		iocsr_write64(val, LOONGARCH_IOCSR_MBUF_SEND);

		val = IOCSR_IPI_SEND_BLOCKING;
		val |= ((uint64_t)hart << IOCSR_IPI_SEND_CPU_SHIFT);
		val |= (1ULL << IOCSR_IPI_SEND_IP_SHIFT);
		iocsr_write32((uint32_t)val, LOONGARCH_IOCSR_IPI_SEND);
	}
}

static void
release_aps(void *dummy __unused)
{
	int i;

#ifdef DEV_ACPI
	if (mp_ncpus == 1 && loongarch_num_core_pic > 1)
		cpu_mp_late_init_acpi();
#endif

	if (mp_ncpus == 1)
		return;

	/* Setup the IPI handlers */
	intr_ipi_setup(IPI_AST, "ast", ipi_ast, NULL);
	intr_ipi_setup(IPI_PREEMPT, "preempt", ipi_preempt, NULL);
	intr_ipi_setup(IPI_RENDEZVOUS, "rendezvous", ipi_rendezvous, NULL);
	intr_ipi_setup(IPI_STOP, "stop", ipi_stop, NULL);
	intr_ipi_setup(IPI_STOP_HARD, "stop hard", ipi_stop_hard, NULL);
	intr_ipi_setup(IPI_HARDCLOCK, "hardclock", ipi_hardclock, NULL);

	atomic_store_rel_int(&aps_ready, 1);

	loongarch_wakeup_aps();

	if (bootverbose)
		printf("Release APs\n");

	for (i = 0; i < 2000; i++) {
		if (atomic_load_acq_int(&smp_started))
			return;
		DELAY(1000);
	}

	printf("APs not started\n");
}
SYSINIT(start_aps, SI_SUB_SMP, SI_ORDER_FIRST, release_aps, NULL);

void
init_secondary(uint64_t cpu_id)
{
	struct pcpu *pcpup;
	u_int cpuid;

	cpuid = cpu_id;
	if (cpuid < boot_hart)
		cpuid += mp_maxid + 1;
	cpuid -= boot_hart;

	pcpup = &__pcpu[cpuid];
	__asm __volatile("move $r21, %0" :: "r"(pcpup));
	__asm __volatile("csrwr %0, %1" : "+r"(pcpup) : "i"(PERCPU_BASE_KS));

	atomic_add_int(&aps_started, 1);
	while (!atomic_load_int(&aps_ready))
		__asm __volatile("idle 0");

	/* Initialize curthread */
	KASSERT(PCPU_GET(idlethread) != NULL, ("no idle thread"));
	pcpup->pc_curthread = pcpup->pc_idlethread;
	schedinit_ap();

	/* Setup and enable interrupts */
	intr_pic_init_secondary();

	/* Enable all IPI types on this AP */
	iocsr_write32(0xffffffff, LOONGARCH_IOCSR_IPI_EN);

	/* Enable timer interrupt now that DPCPU and scheduler are ready.
	 * IPI interrupt was already enabled in mpentry(). */
	write_csr_ecfg(read_csr_ecfg() | (1 << IRQ_TI));

#ifndef EARLY_AP_STARTUP
	/* Start per-CPU event timers. */
	cpu_initclocks_ap();
#endif

	CPU_SET_ATOMIC(cpuid, &kernel_pmap->pm_active);

	/* Activate process 0's pmap. */
	pmap_activate_boot(vmspace_pmap(proc0.p_vmspace));

	mtx_lock_spin(&ap_boot_mtx);

	atomic_add_rel_32(&smp_cpus, 1);

	if (smp_cpus == mp_ncpus) {
		/* enable IPI's, tlb shootdown, freezes etc */
		atomic_store_rel_int(&smp_started, 1);
	}

	mtx_unlock_spin(&ap_boot_mtx);

	if (bootverbose)
		printf("Secondary CPU %u fully online\n", cpuid);

	/* Enter the scheduler */
	sched_ap_entry();

	panic("scheduler returned us to init_secondary");
	/* NOTREACHED */
}

static void
smp_after_idle_runnable(void *arg __unused)
{
	int cpu;

	if (mp_ncpus == 1)
		return;

	KASSERT(smp_started != 0, ("%s: SMP not started yet", __func__));

	/*
	 * Wait for all APs to handle an interrupt.  After that, we know that
	 * the APs have entered the scheduler at least once, so the boot stacks
	 * are safe to free.
	 */
	smp_rendezvous(smp_no_rendezvous_barrier, NULL,
	    smp_no_rendezvous_barrier, NULL);

	for (cpu = 1; cpu <= mp_maxid; cpu++) {
		if (bootstacks[cpu] != NULL)
			kmem_free(bootstacks[cpu], MP_BOOTSTACK_SIZE);
	}
}
SYSINIT(smp_after_idle_runnable, SI_SUB_SMP, SI_ORDER_ANY,
    smp_after_idle_runnable, NULL);

static void
ipi_ast(void *dummy __unused)
{
	CTR0(KTR_SMP, "IPI_AST");
}

static void
ipi_preempt(void *dummy __unused)
{
	CTR1(KTR_SMP, "%s: IPI_PREEMPT", __func__);
	sched_preempt(curthread);
}

static void
ipi_rendezvous(void *dummy __unused)
{
	CTR0(KTR_SMP, "IPI_RENDEZVOUS");
	smp_rendezvous_action();
}

static void
ipi_stop(void *dummy __unused)
{
	u_int cpu;

	CTR0(KTR_SMP, "IPI_STOP");

	cpu = PCPU_GET(cpuid);

	/*
	 * If the kernel has already panicked, do not attempt to save
	 * context or wait for restart.  Just spin forever to avoid
	 * interfering with the panic dump on the winning CPU.
	 */
	if (KERNEL_PANICKED()) {
		for (;;)
			cpu_spinwait();
	}

	savectx(&stoppcbs[cpu]);

	/* Indicate we are stopped */
	CPU_SET_ATOMIC(cpu, &stopped_cpus);

	/* Wait for restart */
	while (!CPU_ISSET(cpu, &started_cpus))
		cpu_spinwait();

	CPU_CLR_ATOMIC(cpu, &started_cpus);
	CPU_CLR_ATOMIC(cpu, &stopped_cpus);
	CTR0(KTR_SMP, "IPI_STOP (restart)");

	flush_icache();
}

static void
ipi_stop_hard(void *dummy __unused)
{
	u_int cpu;

	CTR0(KTR_SMP, "IPI_STOP_HARD");

	cpu = PCPU_GET(cpuid);

	/*
	 * Do NOT call savectx() here.  IPI_STOP_HARD is used during
	 * panic/dump and must not touch memory that could corrupt the
	 * crash dump.  Just set the stopped flag and spin.
	 */
	CPU_SET_ATOMIC(cpu, &stopped_cpus);

	/* Wait for restart (may never happen if dumping) */
	while (!CPU_ISSET(cpu, &started_cpus))
		cpu_spinwait();

	CPU_CLR_ATOMIC(cpu, &started_cpus);
	CPU_CLR_ATOMIC(cpu, &stopped_cpus);
	CTR0(KTR_SMP, "IPI_STOP_HARD (restart)");
}

static void
ipi_hardclock(void *dummy __unused)
{
	CTR1(KTR_SMP, "%s: IPI_HARDCLOCK", __func__);
	hardclockintr();
}

struct cpu_group *
cpu_topo(void)
{

	return (smp_topo_none());
}

/* Determine if we running MP machine */
int
cpu_mp_probe(void)
{

	return (1);
}

#ifdef FDT
static bool
cpu_check_mmu(u_int id __unused, phandle_t node, u_int addr_size __unused,
    pcell_t *reg __unused)
{

	return (true);
}

static bool
cpu_init_fdt(u_int id, phandle_t node, u_int addr_size, pcell_t *reg)
{
	struct pcpu *pcpup;
	uint64_t cpu_id;
	u_int cpuid;

	if (!cpu_check_mmu(id, node, addr_size, reg))
		return (false);

	KASSERT(id < MAXCPU, ("Too many CPUs"));

	KASSERT(addr_size == 1 || addr_size == 2, ("Invalid register size"));
#if defined(INVARIANTS)
	cpu_reg[id][0] = reg[0];
	if (addr_size == 2)
		cpu_reg[id][1] = reg[1];
#endif

	cpu_id = reg[0];
	if (addr_size == 2) {
		cpu_id <<= 32;
		cpu_id |= reg[1];
	}

	KASSERT(cpu_id < MAXCPU, ("Too many CPUs."));

	if (cpu_id == boot_hart)
		return (true);

	cpuid = cpu_id;
	if (cpuid < boot_hart)
		cpuid += mp_maxid + 1;
	cpuid -= boot_hart;

	if (cpuid > mp_maxid)
		return (false);

	pcpup = &__pcpu[cpuid];
	pcpu_init(pcpup, cpuid, sizeof(struct pcpu));
	pcpup->pc_hart = cpu_id;
	bootpcpu[cpuid] = pcpup;

	dpcpu[cpuid - 1] = kmem_malloc(DPCPU_SIZE, M_WAITOK | M_ZERO);
	dpcpu_init(dpcpu[cpuid - 1], cpuid);

	bootstacks[cpuid] = kmem_malloc(MP_BOOTSTACK_SIZE, M_WAITOK | M_ZERO);

	if (bootverbose)
		printf("Starting CPU %u (id %lx)\n", cpuid, cpu_id);
	atomic_store_32(&__loongarch_boot_ap[cpu_id], 1);

	CPU_SET(cpuid, &all_cpus);
	CPU_SET(cpu_id, &all_harts);

	return (true);
}
#endif /* FDT */

#ifdef DEV_ACPI
static void
cpu_count_acpi_handler(ACPI_SUBTABLE_HEADER *entry, void *arg)
{

	if (entry->Type == ACPI_MADT_TYPE_CORE_PIC) {
		ACPI_MADT_CORE_PIC *core;
		core = (ACPI_MADT_CORE_PIC *)entry;
		if ((core->Flags & ACPI_MADT_ENABLED) != 0)
			mp_ncpus++;
	}
}

static void
madt_handler(ACPI_SUBTABLE_HEADER *entry, void *arg)
{
	ACPI_MADT_CORE_PIC *core;
	u_int *cpuid;

	if (entry->Type != ACPI_MADT_TYPE_CORE_PIC)
		return;

	core = (ACPI_MADT_CORE_PIC *)entry;
	if ((core->Flags & ACPI_MADT_ENABLED) == 0)
		return;

	cpuid = arg;
	cpu_init_acpi(*cpuid, core->CoreId);
	(*cpuid)++;
}

static bool
cpu_check_acpi(u_int id __unused, u_int cpu_id __unused)
{

	return (true);
}

static bool
cpu_init_acpi(u_int id, u_int cpu_id)
{
	struct pcpu *pcpup;
	u_int cpuid;

	KASSERT(id < MAXCPU, ("Too many CPUs"));
	KASSERT(cpu_id < MAXCPU, ("Too many CPUs."));

	if (cpu_id == boot_hart)
		return (true);

	cpuid = cpu_id;
	if (cpuid < boot_hart)
		cpuid += mp_maxid + 1;
	cpuid -= boot_hart;

	if (cpuid > mp_maxid)
		return (false);

	pcpup = &__pcpu[cpuid];
	pcpu_init(pcpup, cpuid, sizeof(struct pcpu));
	pcpup->pc_hart = cpu_id;
	bootpcpu[cpuid] = pcpup;

	dpcpu[cpuid - 1] = kmem_malloc(DPCPU_SIZE, M_WAITOK | M_ZERO);
	dpcpu_init(dpcpu[cpuid - 1], cpuid);

	bootstacks[cpuid] = kmem_malloc(MP_BOOTSTACK_SIZE, M_WAITOK | M_ZERO);

	if (bootverbose)
		printf("Starting CPU %u (id %lx)\n", cpuid, (unsigned long)cpu_id);
	atomic_store_32(&__loongarch_boot_ap[cpu_id], 1);

	CPU_SET(cpuid, &all_cpus);
	CPU_SET(cpu_id, &all_harts);

	return (true);
}

static void
cpu_mp_setmaxid_acpi(void)
{
	ACPI_TABLE_MADT *madt;
	vm_paddr_t physaddr;

	physaddr = acpi_find_table(ACPI_SIG_MADT);
	if (physaddr == 0) {
		printf("ACPI: cpu_mp_setmaxid: acpi_find_table(MADT) returned 0\n");
		return;
	}

	madt = acpi_map_table(physaddr, ACPI_SIG_MADT);
	if (madt == NULL) {
		printf("ACPI: cpu_mp_setmaxid: acpi_map_table(MADT) returned NULL\n");
		return;
	}

	mp_ncpus = 0;
	acpi_walk_subtables(madt + 1,
	    (char *)madt + madt->Header.Length,
	    cpu_count_acpi_handler, NULL);
	mp_ncpus = MIN(mp_ncpus, MAXCPU);
	mp_maxid = mp_ncpus - 1;

	printf("ACPI: cpu_mp_setmaxid: Found %d CPUs in MADT\n", mp_ncpus);

	acpi_unmap_table(madt);
}

static void
cpu_mp_start_acpi(void)
{
	ACPI_TABLE_MADT *madt;
	vm_paddr_t physaddr;
	u_int cpuid;

	physaddr = acpi_find_table(ACPI_SIG_MADT);
	if (physaddr == 0)
		return;

	madt = acpi_map_table(physaddr, ACPI_SIG_MADT);
	if (madt == NULL) {
		printf("Unable to map the MADT, not starting APs\n");
		return;
	}

	cpuid = 1;
	acpi_walk_subtables(madt + 1,
	    (char *)madt + madt->Header.Length,
	    madt_handler, &cpuid);

	acpi_unmap_table(madt);
}

/*
 * Late CPU enumeration using already-parsed MADT data.
 * Called when the early cpu_mp_setmaxid_acpi() failed to find CPUs
 * (e.g. because acpi_find_table was not ready at SI_SUB_CPU).
 * acpi_parse_madt() has already run by this point, so loongarch_core_pic[]
 * is populated.
 */
static void
cpu_mp_late_init_acpi(void)
{
	int enabled_count;
	int i;
	u_int cpuid;

	if (loongarch_num_core_pic == 0)
		return;

	enabled_count = 0;
	for (i = 0; i < loongarch_num_core_pic; i++) {
		if ((loongarch_core_pic[i].flags & ACPI_MADT_ENABLED) != 0)
			enabled_count++;
	}

	if (enabled_count <= mp_ncpus)
		return;

	printf("ACPI: Late CPU detection: %d enabled CPUs from MADT (was %d)\n",
	    enabled_count, mp_ncpus);

	mp_ncpus = MIN(enabled_count, MAXCPU);
	mp_ncores = mp_ncpus;
	mp_maxid = mp_ncpus - 1;
	cpu_enum_method = CPUS_ACPI;

	cpuid = 1;
	for (i = 0; i < loongarch_num_core_pic; i++) {
		if ((loongarch_core_pic[i].flags & ACPI_MADT_ENABLED) == 0)
			continue;
		cpu_init_acpi(cpuid, loongarch_core_pic[i].core_id);
		cpuid++;
	}

	{
		u_int cpu;
		CPU_FOREACH(cpu) {
			if (cpu == 0)
				continue;
			identify_cpu(cpu);
		}
	}
}
#endif /* DEV_ACPI */

/* Initialize and fire up non-boot processors */
void
cpu_mp_start(void)
{
	u_int cpu;

	cpu_mp_setmaxid();

	mtx_init(&ap_boot_mtx, "ap boot", NULL, MTX_SPIN);

	CPU_SET(0, &all_cpus);
	CPU_SET(boot_hart, &all_harts);

	switch(cpu_enum_method) {
#ifdef FDT
	case CPUS_FDT:
		ofw_cpu_early_foreach(cpu_init_fdt, true);
		break;
#endif
#ifdef DEV_ACPI
	case CPUS_ACPI:
		cpu_mp_start_acpi();
		break;
#endif
	case CPUS_UNKNOWN:
		break;
	}

	CPU_FOREACH(cpu) {
		/* Already identified. */
		if (cpu == 0)
			continue;

		identify_cpu(cpu);
	}
}

/* Introduce rest of cores to the world */
void
cpu_mp_announce(void)
{
	u_int cpu;

	CPU_FOREACH(cpu) {
		/* Already announced. */
		if (cpu == 0)
			continue;

		printcpuinfo(cpu);
	}
}

void
cpu_mp_setmaxid(void)
{
	int cores;

	/*
	 * Try ACPI first (ACPI preferred over FDT when both are available).
	 * Only fall back to FDT if ACPI finds 0 CPUs.
	 */
	cpu_enum_method = CPUS_UNKNOWN;

#ifdef DEV_ACPI
	cpu_mp_setmaxid_acpi();
	if (mp_ncpus > 1) {
		cpu_enum_method = CPUS_ACPI;
	} else if (mp_ncpus == 1) {
		cpu_enum_method = CPUS_ACPI;
		mp_ncpus = 1;
		mp_maxid = 0;
	} else {
		/* mp_ncpus == 0: ACPI found nothing, reset and try FDT */
		mp_ncpus = 1;
		mp_maxid = 0;
	}
#endif
#ifdef FDT
	if (cpu_enum_method == CPUS_UNKNOWN) {
		cores = ofw_cpu_early_foreach(cpu_check_mmu, true);
		if (cores > 0) {
			cores = MIN(cores, MAXCPU);
			if (bootverbose)
				printf("Found %d CPUs in the device tree\n", cores);
			mp_ncpus = cores;
			mp_maxid = cores - 1;
			cpu_enum_method = CPUS_FDT;
		}
	}
#endif
#if !defined(FDT) && !defined(DEV_ACPI)
	{
		mp_ncpus = 1;
		mp_maxid = 0;
	}
#endif

	if (TUNABLE_INT_FETCH("hw.ncpu", &cores)) {
		if (cores > 0 && cores < mp_ncpus) {
			mp_ncpus = cores;
			mp_maxid = cores - 1;
		}
	}
}

void
ipi_all_but_self(u_int ipi)
{
	cpuset_t other_cpus;

	other_cpus = all_cpus;
	CPU_CLR(PCPU_GET(cpuid), &other_cpus);

	CTR2(KTR_SMP, "%s: ipi: %x", __func__, ipi);
	intr_ipi_send(other_cpus, ipi);
}

void
ipi_cpu(int cpu, u_int ipi)
{
	cpuset_t cpus;

	CPU_ZERO(&cpus);
	CPU_SET(cpu, &cpus);

	CTR3(KTR_SMP, "%s: cpu: %d, ipi: %x", __func__, cpu, ipi);
	intr_ipi_send(cpus, ipi);
}

void
ipi_selected(cpuset_t cpus, u_int ipi)
{
	CTR1(KTR_SMP, "ipi_selected: ipi: %x", ipi);
	intr_ipi_send(cpus, ipi);
}
