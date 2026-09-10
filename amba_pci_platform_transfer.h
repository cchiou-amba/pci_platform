/*
 * ambarella/drv_modules/platform/pci_platform/amba_pci_platform_transfer.h
 *
 * History:
 *    2026/06/30 - [Qucheng Yan] Created file.
 *
 * Copyright (C) 2026  Ambarella, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef __AMBA_PCI_PLATFORM_TRANSFER_H__
#define __AMBA_PCI_PLATFORM_TRANSFER_H__

#include <linux/io.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <amba_pci_ioctl.h>

#include "amba_pci_platform_internal.h"

struct pci_dev;

#define PCIE_BOOT_BAR_MAP_RETRY_MAX	3
#define PCIE_BOOT_BAR_MAP_RETRY_MS	50

#define RING_BLOCK_SIZE			512U
#define RING_MAGIC			0x61626d61U
#define RING_CMD_DATA			1U
#define RING_CMD_SET_DTB_ADDR		3U
#define RING_CMD_SET_LNX_ADDR		4U
#define RING_CMD_SET_INITRD_ADDR	5U
#define RING_STATE_ONLINE		1U

struct pci_ring_header_layout {
	u32 magic;
	u32 state;
	u32 avail;
	u32 used;
	u32 num;
	u32 max_blk;
	struct pci_dev_board_info bd;
};

#define RH_MAGIC_OFF		offsetof(struct pci_ring_header_layout, magic)
#define RH_STATE_OFF		offsetof(struct pci_ring_header_layout, state)
#define RH_AVAIL_OFF		offsetof(struct pci_ring_header_layout, avail)
#define RH_USED_OFF		offsetof(struct pci_ring_header_layout, used)
#define RH_NUM_OFF		offsetof(struct pci_ring_header_layout, num)
#define RH_MAX_BLK_OFF		offsetof(struct pci_ring_header_layout, max_blk)
#define RH_BD_OFF		offsetof(struct pci_ring_header_layout, bd)
#define RH_DESC0_OFF		(sizeof(struct pci_ring_header_layout))

#define AMBA_PCI_FW_EP_RSVD_END		0x02000000ULL
#define AMBA_PCI_FW_KERNEL_LOAD_ADDR	AMBA_PCI_FW_EP_RSVD_END
#define AMBA_PCI_FW_DTB_LOAD_ADDR \
	(AMBA_PCI_FW_KERNEL_LOAD_ADDR + 64ULL * 0x100000)
#define AMBA_PCI_FW_ROOTFS_LOAD_ADDR \
	(AMBA_PCI_FW_DTB_LOAD_ADDR + 2ULL * 0x100000)

#define PCIE_FW_XFER_ORDER_CNT		AMBA_PCI_FW_IMAGE_MAX

extern const u32 pcie_fw_xfer_order[PCIE_FW_XFER_ORDER_CNT];
extern const u32 pcie_fw_req_bitmap_mask;

const struct amba_pci_fw_image_desc *
pcie_fw_find_image(const struct amba_pci_fw_xfer *req, u32 type);

int pcie_boot_bar_ensure_mapped(struct pcie_dev_entry *e);
void pcie_boot_bar_unmap(struct pcie_dev_entry *e);

void pcie_ring_read_board_info(void __iomem *info_base,
			       struct pci_dev_board_info *bd);
int pcie_ring_wait_ep_ready(struct pci_dev *pdev, void __iomem *ring_base);
int pcie_ring_xfer_user(struct pci_dev *pdev, void __iomem *ring_base,
			size_t ring_map_size,
			const u8 __user *user_base,
			const struct amba_pci_fw_image_desc *img);

#endif /* __AMBA_PCI_PLATFORM_TRANSFER_H__ */
