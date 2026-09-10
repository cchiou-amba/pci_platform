// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Ambarela, Inc.
 *
 * RC-side pci-buzz slot helpers exported for amba_pci_platform proc nodes.
 */
#ifndef __PCI_BUZZ_H__
#define __PCI_BUZZ_H__

#if defined(__KERNEL__)
#include <linux/types.h>
#else
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#endif

/*
 * Per-bus net string from the pci-buzz instance on that RC bus.
 * Returns false if no RC pci-buzz on the bus or net has not been configured.
 */
bool pci_buzz_bus_net_get(u32 bus, char *buf, size_t len);

/*
 * Per-bus EP info log via pci-buzz query path.
 * Returns bytes written, or <= 0 on failure.
 */
ssize_t pci_buzz_bus_epinfo_read(u32 bus, char *buf, size_t nbytes);

#endif /* __PCI_BUZZ_H__ */
