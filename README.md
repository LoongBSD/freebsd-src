本版本基于 FreeBSD 15 RELEASE ，在 hitmoon(https://gitee.com/hitmoon/loongarch-fb/) 及 yushanwei(https://gitee.com/yushanwei/freebsd-src) 的基础上合并，并完善驱动等；

// 未完成开发，只有约20%的几率能启动到login界面，不过大部分时候是提前崩溃了。
// 有小伙伴需要参考，故提前开放出来。代码未经审计，未来可能会推到重来，请注意。

[dmesg.txt](dmesg.txt)

感谢 hitmoon 及 yushanwei 的贡献！

make buildenv TARGET=loongarch TARGET_ARCH=loongarch64 SRC_ENV_CONF=/etc/src.conf

make -j4 kernel-toolchain TARGET=loongarch TARGET_ARCH=loongarch64 SRC_ENV_CONF=/etc/src.conf 
make -j4 buildkernel KERNCONF=QEMU TARGET=loongarch TARGET_ARCH=loongarch64 SRC_ENV_CONF=/etc/src.conf NO_MODULES=1 DESTDIR=/mnt
make -j4 buildworld TARGET=loongarch TARGET_ARCH=loongarch64 SRC_ENV_CONF=/etc/src.conf DESTDIR=/mnt
make -j4 installworld TARGET=loongarch TARGET_ARCH=loongarch64 SRC_ENV_CONF=/etc/src.conf DESTDIR=/mnt
make -j4 distribution TARGET=loongarch TARGET_ARCH=loongarch64 SRC_ENV_CONF=/etc/src.conf DESTDIR=/mnt

boot1.efi 拷贝到 \EFI\BOOT\BOOTLOONGARCH64.EFI

qemu-system-loongarch64 -machine virt -cpu max -m 2G -bios /home/gehaowu/FreeBSD/edk2-loongarch64-code.fd -serial mon:stdio -nographic -drive file=/home/gehaowu/FreeBSD/freebsd-loongarch64.img,format=raw,if=virtio -device qemu-xhci,id=xhci0 -device qemu-xhci,id=xhci1 -device usb-kbd,bus=xhci0.0 -smp 1


❯ cat /etc/src.conf
#MAKEOBJDIRPREFIX?=/tmp/obj

WITHOUT_SYSTEM_COMPILER=
WITHOUT_SYSTEM_LINKER=
#WITH_LLVM_TOOLS=
WITH_LLVM_BINUTILS=
#WITH_LLVM_COV=
#WITHOUT_GCOV=

#WITH_LLVM_TARGET_ALL=
#WITHOUT_GAMES=
#WITHOUT_MAIL=
#WITHOUT_GCC=
#WITHOUT_GNU_DIFF=
#WITHOUT_GDB=
#WITHOUT_ACPI=
#WITHOUT_BSNMP=
#WITHOUT_NIS=
#WITHOUT_PROFILE=
#WITHOUT_KERBEROS=
#WITHOUT_ATM=
#WITHOUT_IPX=
#WITHOUT_DOCS=
#WITHOUT_MAN=
#WITHOUT_LLDB=
#WITHOUT_ZFS=
#WITHOUT_CDDL=
#WITHOUT_EFI=

#WITHOUT_ASAN=
#WITHOUT_TESTS=
#WITHOUT_TESTS_SUPPORT=
#WITHOUT_RESCUE=




FreeBSD Source:
---------------
This is the top level of the FreeBSD source directory.

FreeBSD is an operating system used to power modern servers, desktops, and embedded platforms.
A large community has continually developed it for more than thirty years.
Its advanced networking, security, and storage features have made FreeBSD the platform of choice for many of the busiest web sites and most pervasive embedded networking and storage devices.

For copyright information, please see [the file COPYRIGHT](COPYRIGHT) in this directory.
Additional copyright information also exists for some sources in this tree - please see the specific source directories for more information.

The Makefile in this directory supports a number of targets for building components (or all) of the FreeBSD source tree.
See build(7), config(8), [FreeBSD handbook on building userland](https://docs.freebsd.org/en/books/handbook/cutting-edge/#makeworld), and [Handbook for kernels](https://docs.freebsd.org/en/books/handbook/kernelconfig/) for more information, including setting make(1) variables.

For information on the CPU architectures and platforms supported by FreeBSD, see the [FreeBSD
website's Platforms page](https://www.freebsd.org/platforms/).

For official FreeBSD bootable images, see the [release page](https://download.freebsd.org/ftp/releases/ISO-IMAGES/).

Source Roadmap:
---------------
| Directory | Description |
| --------- | ----------- |
| bin | System/user commands. |
| cddl | Various commands and libraries under the Common Development and Distribution License. |
| contrib | Packages contributed by 3rd parties. |
| crypto | Cryptography stuff (see [crypto/README](crypto/README)). |
| etc | Template files for /etc. |
| gnu | Commands and libraries under the GNU General Public License (GPL) or Lesser General Public License (LGPL). Please see [gnu/COPYING](gnu/COPYING) and [gnu/COPYING.LIB](gnu/COPYING.LIB) for more information. |
| include | System include files. |
| kerberos5 | Kerberos5 (Heimdal) package. |
| lib | System libraries. |
| libexec | System daemons. |
| release | Release building Makefile & associated tools. |
| rescue | Build system for statically linked /rescue utilities. |
| sbin | System commands. |
| secure | Cryptographic libraries and commands. |
| share | Shared resources. |
| stand | Boot loader sources. |
| sys | Kernel sources (see [sys/README.md](sys/README.md)). |
| targets | Support for experimental `DIRDEPS_BUILD` |
| tests | Regression tests which can be run by Kyua.  See [tests/README](tests/README) for additional information. |
| tools | Utilities for regression testing and miscellaneous tasks. |
| usr.bin | User commands. |
| usr.sbin | System administration commands. |

For information on synchronizing your source tree with one or more of the FreeBSD Project's development branches, please see [FreeBSD Handbook](https://docs.freebsd.org/en/books/handbook/cutting-edge/#current-stable).
