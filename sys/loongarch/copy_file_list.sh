#!/bin/sh
#
# 从 riscv/arm64 复制到 loongarch 的文件清单
# 由文件内容对比自动生成
#
# 分类说明：
#   [完全复制]    - 文件内容与源架构完全相同或仅改架构名/注释
#   [修改复制]    - 以源架构为基础，做了 loongarch ISA 适配修改
#   [原创]        - loongarch 完全独立编写，无源架构对应文件（以 touch 创建空文件形式列出）
#

# ===== lib/ 用户态库（源自 riscv） =====
cp lib/csu/riscv/crt1_s.S lib/csu/loongarch/crt1_s.S
cp lib/csu/riscv/Makefile.depend lib/csu/loongarch/Makefile.depend
cp lib/libc/riscv/arith.h lib/libc/loongarch/arith.h
cp lib/libc/riscv/Symbol.map lib/libc/loongarch/Symbol.map
cp lib/libc/riscv/_fpmath.h lib/libc/loongarch/_fpmath.h
cp lib/libc/riscv/gen/fabs.S lib/libc/loongarch/gen/fabs.S
cp lib/libc/riscv/gen/flt_rounds.c lib/libc/loongarch/gen/flt_rounds.c
cp lib/libc/riscv/gen/fpgetmask.c lib/libc/loongarch/gen/fpgetmask.c
cp lib/libc/riscv/gen/fpsetmask.c lib/libc/loongarch/gen/fpsetmask.c
cp lib/libc/riscv/gen/makecontext.c lib/libc/loongarch/gen/makecontext.c
cp lib/libc/riscv/gen/setjmp.S lib/libc/loongarch/gen/setjmp.S
cp lib/libc/riscv/gen/sigsetjmp.S lib/libc/loongarch/gen/sigsetjmp.S
cp lib/libc/riscv/gen/_ctx_start.S lib/libc/loongarch/gen/_ctx_start.S
cp lib/libc/riscv/gen/_setjmp.S lib/libc/loongarch/gen/_setjmp.S
cp lib/libc/riscv/softfloat/milieu.h lib/libc/loongarch/softfloat/milieu.h
cp lib/libc/riscv/softfloat/softfloat.h lib/libc/loongarch/softfloat/softfloat.h
cp lib/libc/riscv/softfloat/riscv-gcc.h lib/libc/loongarch/softfloat/loongarch-gcc.h
cp lib/libsys/riscv/cerror.S lib/libsys/loongarch/cerror.S
cp lib/libsys/riscv/vfork.S lib/libsys/loongarch/vfork.S
cp lib/msun/riscv/fenv.c lib/msun/loongarch/fenv.c
cp lib/msun/riscv/fenv.h lib/msun/loongarch/fenv.h
cp lib/msun/riscv/Symbol.map lib/msun/loongarch/Symbol.map

# ===== stand/ 启动代码（源自 riscv） =====
cp stand/efi/include/riscv/efibind.h stand/efi/include/loongarch/efibind.h
cp stand/efi/loader/arch/riscv/riscv.ldscript stand/efi/loader/arch/loongarch/loongarch.ldscript
cp stand/efi/loader/arch/riscv/Makefile.inc stand/efi/loader/arch/loongarch/Makefile.inc
cp stand/ficl/riscv/sysdep.c stand/ficl/loongarch/sysdep.c
cp stand/ficl/riscv/sysdep.h stand/ficl/loongarch/sysdep.h

# ===== sys/ 内核 include 头文件（源自 riscv） =====
# --- 完全复制（仅改架构名/注释） ---
cp sys/riscv/include/_align.h sys/loongarch/include/_align.h
cp sys/riscv/include/_bus.h sys/loongarch/include/_bus.h
cp sys/riscv/include/_inttypes.h sys/loongarch/include/_inttypes.h
cp sys/riscv/include/_limits.h sys/loongarch/include/_limits.h
cp sys/riscv/include/_stdint.h sys/loongarch/include/_stdint.h
cp sys/riscv/include/_types.h sys/loongarch/include/_types.h
cp sys/riscv/include/db_machdep.h sys/loongarch/include/db_machdep.h
cp sys/riscv/include/dump.h sys/loongarch/include/dump.h
cp sys/riscv/include/efi.h sys/loongarch/include/efi.h
cp sys/riscv/include/floatingpoint.h sys/loongarch/include/floatingpoint.h
cp sys/riscv/include/kdb.h sys/loongarch/include/kdb.h
cp sys/riscv/include/memdev.h sys/loongarch/include/memdev.h
cp sys/riscv/include/minidump.h sys/loongarch/include/minidump.h
cp sys/riscv/include/pcpu_aux.h sys/loongarch/include/pcpu_aux.h
cp sys/riscv/include/procctl.h sys/loongarch/include/procctl.h
cp sys/riscv/include/resource.h sys/loongarch/include/resource.h
cp sys/riscv/include/sdt_machdep.h sys/loongarch/include/sdt_machdep.h
cp sys/riscv/include/setjmp.h sys/loongarch/include/setjmp.h
cp sys/riscv/include/sf_buf.h sys/loongarch/include/sf_buf.h
cp sys/riscv/include/sigframe.h sys/loongarch/include/sigframe.h
cp sys/riscv/include/smp.h sys/loongarch/include/smp.h
cp sys/riscv/include/stack.h sys/loongarch/include/stack.h
cp sys/riscv/include/stdarg.h sys/loongarch/include/stdarg.h
cp sys/riscv/include/sysarch.h sys/loongarch/include/sysarch.h

# --- 修改复制（以 riscv 为基础做架构适配） ---
cp sys/riscv/include/asm.h sys/loongarch/include/asm.h
cp sys/riscv/include/atomic.h sys/loongarch/include/atomic.h
cp sys/riscv/include/bus.h sys/loongarch/include/bus.h
cp sys/riscv/include/bus_dma.h sys/loongarch/include/bus_dma.h
cp sys/riscv/include/bus_dma_impl.h sys/loongarch/include/bus_dma_impl.h
cp sys/riscv/include/clock.h sys/loongarch/include/clock.h
cp sys/riscv/include/counter.h sys/loongarch/include/counter.h
cp sys/riscv/include/cpu.h sys/loongarch/include/cpu.h
cp sys/riscv/include/cpufunc.h sys/loongarch/include/cpufunc.h
cp sys/riscv/include/elf.h sys/loongarch/include/elf.h
cp sys/riscv/include/endian.h sys/loongarch/include/endian.h
cp sys/riscv/include/exec.h sys/loongarch/include/exec.h
cp sys/riscv/include/float.h sys/loongarch/include/float.h
cp sys/riscv/include/fpe.h sys/loongarch/include/fpe.h
cp sys/riscv/include/frame.h sys/loongarch/include/frame.h
cp sys/riscv/include/gdb_machdep.h sys/loongarch/include/gdb_machdep.h
cp sys/riscv/include/ieeefp.h sys/loongarch/include/ieeefp.h
cp sys/riscv/include/ifunc.h sys/loongarch/include/ifunc.h
cp sys/riscv/include/in_cksum.h sys/loongarch/include/in_cksum.h
cp sys/riscv/include/intr.h sys/loongarch/include/intr.h
#cp sys/riscv/include/limits.h sys/loongarch/include/limits.h
cp sys/riscv/include/machdep.h sys/loongarch/include/machdep.h
cp sys/riscv/include/md_var.h sys/loongarch/include/md_var.h
cp sys/riscv/include/metadata.h sys/loongarch/include/metadata.h
cp sys/riscv/include/ofw_machdep.h sys/loongarch/include/ofw_machdep.h
cp sys/riscv/include/param.h sys/loongarch/include/param.h
cp sys/riscv/include/pcb.h sys/loongarch/include/pcb.h
cp sys/riscv/include/pcpu.h sys/loongarch/include/pcpu.h
cp sys/riscv/include/pmap.h sys/loongarch/include/pmap.h
cp sys/riscv/include/pmc_mdep.h sys/loongarch/include/pmc_mdep.h
cp sys/riscv/include/proc.h sys/loongarch/include/proc.h
cp sys/riscv/include/profile.h sys/loongarch/include/profile.h
cp sys/riscv/include/psl.h sys/loongarch/include/psl.h
cp sys/riscv/include/pte.h sys/loongarch/include/pte.h
cp sys/riscv/include/ptrace.h sys/loongarch/include/ptrace.h
cp sys/riscv/include/reg.h sys/loongarch/include/reg.h
cp sys/riscv/include/reloc.h sys/loongarch/include/reloc.h
cp sys/riscv/include/riscvreg.h sys/loongarch/include/loongarchreg.h
cp sys/riscv/include/signal.h sys/loongarch/include/signal.h
cp sys/riscv/include/tls.h sys/loongarch/include/tls.h
cp sys/riscv/include/ucontext.h sys/loongarch/include/ucontext.h
cp sys/riscv/include/vdso.h sys/loongarch/include/vdso.h
cp sys/riscv/include/vm.h sys/loongarch/include/vm.h
cp sys/riscv/include/vmparam.h sys/loongarch/include/vmparam.h

# ===== sys/ acpica 头文件（源自 arm64，riscv 无 acpica） =====
cp sys/arm64/include/acpica_machdep.h sys/loongarch/include/acpica_machdep.h
#cp sys/arm64/include/madt_var.h sys/loongarch/include/madt_var.h
cp sys/arm64/include/pci_cfgreg.h sys/loongarch/include/pci_cfgreg.h

# ===== sys/ 内核 .c/.S 实现文件（源自 riscv） =====
# --- 完全复制（仅改架构名/注释） ---
cp sys/riscv/riscv/autoconf.c sys/loongarch/loongarch/autoconf.c
cp sys/riscv/riscv/cpufunc_asm.S sys/loongarch/loongarch/cpufunc_asm.S
cp sys/riscv/riscv/db_interface.c sys/loongarch/loongarch/db_interface.c
cp sys/riscv/riscv/dump_machdep.c sys/loongarch/loongarch/dump_machdep.c
cp sys/riscv/riscv/minidump_machdep.c sys/loongarch/loongarch/minidump_machdep.c
cp sys/riscv/riscv/ofw_machdep.c sys/loongarch/loongarch/ofw_machdep.c
cp sys/riscv/riscv/ptrace_machdep.c sys/loongarch/loongarch/ptrace_machdep.c
cp sys/riscv/riscv/stack_machdep.c sys/loongarch/loongarch/stack_machdep.c
cp sys/riscv/riscv/sys_machdep.c sys/loongarch/loongarch/sys_machdep.c
cp sys/riscv/riscv/unwind.c sys/loongarch/loongarch/unwind.c

# --- 修改复制（以 riscv 为基础做架构适配） ---
cp sys/riscv/riscv/bus_machdep.c sys/loongarch/loongarch/bus_machdep.c
cp sys/riscv/riscv/bus_space_asm.S sys/loongarch/loongarch/bus_space_asm.S
cp sys/riscv/riscv/busdma_bounce.c sys/loongarch/loongarch/busdma_bounce.c
cp sys/riscv/riscv/busdma_machdep.c sys/loongarch/loongarch/busdma_machdep.c
cp sys/riscv/riscv/cache.c sys/loongarch/loongarch/cache.c
cp sys/riscv/riscv/clock.c sys/loongarch/loongarch/clock.c
cp sys/riscv/riscv/copyinout.S sys/loongarch/loongarch/copyinout.S
cp sys/riscv/riscv/db_disasm.c sys/loongarch/loongarch/db_disasm.c
cp sys/riscv/riscv/db_trace.c sys/loongarch/loongarch/db_trace.c
cp sys/riscv/riscv/elf_machdep.c sys/loongarch/loongarch/elf_machdep.c
cp sys/riscv/riscv/exception.S sys/loongarch/loongarch/exception.S
cp sys/riscv/riscv/exec_machdep.c sys/loongarch/loongarch/exec_machdep.c
cp sys/riscv/riscv/fpe.c sys/loongarch/loongarch/fpe.c
cp sys/riscv/riscv/gdb_machdep.c sys/loongarch/loongarch/gdb_machdep.c
cp sys/riscv/riscv/genassym.c sys/loongarch/loongarch/genassym.c
cp sys/riscv/riscv/identcpu.c sys/loongarch/loongarch/identcpu.c
cp sys/riscv/riscv/locore.S sys/loongarch/loongarch/locore.S
cp sys/riscv/riscv/machdep.c sys/loongarch/loongarch/machdep.c
cp sys/riscv/riscv/mem.c sys/loongarch/loongarch/mem.c
cp sys/riscv/riscv/mp_machdep.c sys/loongarch/loongarch/mp_machdep.c
cp sys/riscv/riscv/nexus.c sys/loongarch/loongarch/nexus.c
cp sys/riscv/riscv/pmap.c sys/loongarch/loongarch/pmap.c
cp sys/riscv/riscv/sigtramp.S sys/loongarch/loongarch/sigtramp.S
cp sys/riscv/riscv/support.S sys/loongarch/loongarch/support.S
cp sys/riscv/riscv/swtch.S sys/loongarch/loongarch/swtch.S
cp sys/riscv/riscv/timer.c sys/loongarch/loongarch/timer.c
cp sys/riscv/riscv/trap.c sys/loongarch/loongarch/trap.c
cp sys/riscv/riscv/uio_machdep.c sys/loongarch/loongarch/uio_machdep.c
cp sys/riscv/riscv/vm_machdep.c sys/loongarch/loongarch/vm_machdep.c

# ===== sys/ acpica 实现文件（源自 arm64，riscv 无 acpica） =====
cp sys/arm64/acpica/OsdEnvironment.c sys/loongarch/acpica/OsdEnvironment.c
cp sys/arm64/acpica/acpi_machdep.c sys/loongarch/acpica/acpi_machdep.c
cp sys/arm64/acpica/acpi_wakeup.c sys/loongarch/acpica/acpi_wakeup.c
cp sys/arm64/acpica/pci_cfgreg.c sys/loongarch/acpica/pci_cfgreg.c

# ===== conf/ =====
cp sys/riscv/conf/DEFAULTS sys/loongarch/conf/DEFAULTS
cp sys/riscv/conf/GENERIC sys/loongarch/conf/GENERIC
cp sys/riscv/conf/QEMU sys/loongarch/conf/QEMU

# ===== cddl/dtrace =====
cp sys/cddl/dev/dtrace/riscv/dtrace_asm.S sys/cddl/dev/dtrace/loongarch/dtrace_asm.S
cp sys/cddl/dev/dtrace/riscv/dtrace_isa.c sys/cddl/dev/dtrace/loongarch/dtrace_isa.c
cp sys/cddl/dev/dtrace/riscv/dtrace_subr.c sys/cddl/dev/dtrace/loongarch/dtrace_subr.c
cp sys/cddl/dev/dtrace/riscv/instr_size.c sys/cddl/dev/dtrace/loongarch/instr_size.c
cp sys/cddl/dev/dtrace/riscv/regset.h sys/cddl/dev/dtrace/loongarch/regset.h

# ===== 重命名指令（文件名不同于源） =====
mv sys/loongarch/softfloat/riscv-gcc.h sys/loongarch/softfloat/loongarch-gcc.h
mv sys/loongarch/include/riscvreg.h sys/loongarch/include/loongarchreg.h

# ===== loongarch 原创文件（riscv/arm64 无对应，以空文件创建） =====
#
# --- sys/loongarch/include/ 原创头文件 ---
# bits.h          - 位操作宏（riscv 无，arm64 无）
# dintc_var.h     - 分布式中断控制器变量（riscv 无，arm64 无）
# eiointc_var.h   - 扩展 I/O 中断控制器变量（riscv 无，arm64 无）
# encoding.h      - 编码定义（riscv 有但内容完全不同，1252 vs 8 行）
# fpu.h           - FPU 头（riscv 无，arm64 有但内容不同）
# iodev.h         - I/O 设备头（riscv 无，arm64 有但内容不同）
# runq.h          - 运行队列（riscv 无，arm64 无，仅 sys/sys/ 有通用版）
# tlb.h           - TLB 操作（riscv 无，arm64 无）
#
touch sys/loongarch/include/bits.h
touch sys/loongarch/include/dintc_var.h
touch sys/loongarch/include/eiointc_var.h
touch sys/loongarch/include/encoding.h
touch sys/loongarch/include/fpu.h
touch sys/loongarch/include/iodev.h
touch sys/loongarch/include/runq.h
touch sys/loongarch/include/tlb.h
#
# --- sys/loongarch/loongarch/ 原创实现文件 ---
# cpuintc.c       - CPU 中断控制器（riscv 无，arm64 无）
# dintc.c         - 分布式中断控制器（riscv 无，arm64 无）
# eiointc.c       - 扩展 I/O 中断控制器（riscv 无，arm64 无）
# eiointc_fdt.c   - EIOINTC FDT 绑定（riscv 无，arm64 无）
# intr_machdep.c  - 中断机器相关（riscv 无，arm64 无）
# la64_pmc.c      - 性能监控计数器（riscv 无，arm64 无）
# ls7a_rtc.c      - 龙芯 7A RTC 驱动（riscv 无，arm64 无）
# pchmsi.c        - PCH MSI 中断（riscv 无，arm64 无）
# pchpic.c        - PCH PIC 中断控制器（riscv 无，arm64 无）
# pci_cfgreg.c    - PCI 配置寄存器实现（riscv 无，arm64 无）
# ptw_diag.c      - 页表行走诊断（riscv 无，arm64 无）
# uma_machdep.c   - UMA 机器相关（riscv 无，arm64 无）
#
touch sys/loongarch/loongarch/cpuintc.c
touch sys/loongarch/loongarch/dintc.c
touch sys/loongarch/loongarch/eiointc.c
touch sys/loongarch/loongarch/eiointc_fdt.c
touch sys/loongarch/loongarch/intr_machdep.c
touch sys/loongarch/loongarch/la64_pmc.c
touch sys/loongarch/loongarch/ls7a_rtc.c
touch sys/loongarch/loongarch/pchmsi.c
touch sys/loongarch/loongarch/pchpic.c
touch sys/loongarch/loongarch/pci_cfgreg.c
touch sys/loongarch/loongarch/ptw_diag.c
touch sys/loongarch/loongarch/uma_machdep.c
#
# --- sys/loongarch/acpica/ 原创 ACPI 文件 ---
# README.acpi     - ACPI 说明文档（riscv 无，arm64 无）
# acpi_madt.c     - MADT ACPI 表解析（riscv 无，arm64 无）
# acpi_pchmsi.c   - PCH MSI ACPI 支持（riscv 无，arm64 无）
# acpi_pchmsi.h   - PCH MSI ACPI 头文件（riscv 无，arm64 无）
# acpi_spcr.c     - SPCR 串口控制台重定向表（riscv 无，arm64 无）
#
touch sys/loongarch/acpica/README.acpi
touch sys/loongarch/acpica/acpi_madt.c
touch sys/loongarch/acpica/acpi_pchmsi.c
touch sys/loongarch/acpica/acpi_pchmsi.h
touch sys/loongarch/acpica/acpi_spcr.c

echo "生成完毕：共 $(($(grep -c '^cp ' "$0"))) 条 cp 命令，$(($(grep -c '^mv ' "$0"))) 条 mv 命令，$(($(grep -c '^touch ' "$0"))) 个原创空文件"

