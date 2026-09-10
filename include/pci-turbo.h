// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Ambarella, Inc.
 *
 * RC-side pci-turbo slot helpers exported for amba_pci_platform.
 */
#ifndef __PCI_TURBO_H__
#define __PCI_TURBO_H__

#if defined(__KERNEL__)
#include <linux/types.h>
#else
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#endif

/*
 * Per-bus pciturbo char device name from the pci-turbo instance on that RC bus.
 * Fills buf with the node name (e.g. "pciturbo010"), suitable for /dev/%s.
 * Returns false if no RC pci-turbo on the bus.
 */
bool pci_turbo_bus_dev_get(u32 bus, char *buf, size_t len);

/*
 * Per-bus H2D transfer via the BUF_ALLOC / submit / free path.
 * Returns 0 on success, -ENODEV if no RC pci-turbo on @bus, or other -errno.
 */
int pci_turbo_bus_xfer_h2d(u32 bus, const void *data, size_t len, u64 ep_addr);

#endif /* __PCI_TURBO_H__ */
