# SPDX-License-Identifier: GPL-2.0
#
# Ambarella PCI Platform Kernel Module Kbuild

obj-m := amba_pci_platform.o

amba_pci_platform-objs := amba_pci_platform_main.o \
	amba_pci_platform_table.o \
	amba_pci_platform_transfer.o

ccflags-y := -I$(src)/include -I$(src)
