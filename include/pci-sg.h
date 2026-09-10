// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Ambarela, Inc.
 */
#ifndef __PCI_SG_H__
#define __PCI_SG_H__

#include <linux/version.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/crc32.h>
#include <linux/mod_devicetable.h>
#include <linux/device/bus.h>
#include <linux/dma-mapping.h>

#define MAX_SG_NUM		4096
#define pci_sg_addr(p)		((p)->addr_lo | ((uint64_t)(p)->addr_hi << 32))
#define sgl_addr(sgt, n)	((sgt) + 8 + (n) * sizeof(struct pci_sg_list))

#define PCI_SG_AVAIL_OFFSET	0
#define PCI_SG_USED_OFFSET	4

struct pci_sg_array {
	dma_addr_t addr;
	size_t size;
};

struct pci_sg_list {
	dma_addr_t addr;
	u32 length;
	u32 offset;
};

typedef volatile struct pci_sg_list pci_sg_list_t;

struct pci_sg_table {
	u32 avail;
	u32 used;
	struct pci_sg_list sgl[MAX_SG_NUM];
};
typedef volatile struct pci_sg_table pci_sg_table_t;

static inline u32 sgt_get_avail(void *sgt)
{
	return readl(sgt + PCI_SG_AVAIL_OFFSET);
}

static inline u32 sgt_get_used(void *sgt)
{
	return readl(sgt + PCI_SG_USED_OFFSET);
}

static inline void sgt_set_used(void *sgt, u32 used)
{
	writel(used, sgt + PCI_SG_USED_OFFSET);
}

#endif
