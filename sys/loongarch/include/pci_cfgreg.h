/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * Copyright (c) 2024 Shanwei Yu <mpysw@vip.163.com>
 * Copyright (c) 2024 Xiaoqiang Zhao <zxq_yx_007@163.com>
 * Copyright (c) 2026 Haowu Ge <gehaowu@bitmoe.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * PCI configuration register access routines for LoongArch
 * 
 * These functions are used by ACPI to access PCI configuration space.
 * Implementation is in loongarch/loongarch/pci_cfgreg.c
 */

#ifndef _MACHINE_PCI_CFGREG_H
#define	_MACHINE_PCI_CFGREG_H

#ifdef _KERNEL

/*
 * Open PCI configuration space access.
 * Returns 1 on success, 0 on failure.
 */
int pci_cfgregopen(void);

/*
 * Read from PCI configuration space.
 * 
 * domain:   PCI domain number
 * bus:      PCI bus number
 * slot:     PCI slot number
 * func:     PCI function number
 * reg:      Configuration register offset
 * width:    Access width in bytes (1, 2, 4)
 * 
 * Returns the value read from the configuration register.
 */
uint32_t pci_cfgregread(int domain, int bus, int slot, int func, int reg, int width);

/*
 * Write to PCI configuration space.
 * 
 * domain:   PCI domain number
 * bus:      PCI bus number
 * slot:     PCI slot number
 * func:     PCI function number
 * reg:      Configuration register offset
 * width:    Access width in bytes (1, 2, 4)
 * val:      Value to write
 */
void pci_cfgregwrite(int domain, int bus, int slot, int func, int reg, int width, uint32_t val);

#endif /* _KERNEL */

#endif /* !_MACHINE_PCI_CFGREG_H */
