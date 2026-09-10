/*
 * ambarella/drv_modules/platform/pci_platform/amba_pci_platform_internal.h
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
#ifndef __AMBA_PCI_PLATFORM_INTERNAL_H__
#define __AMBA_PCI_PLATFORM_INTERNAL_H__

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <amba_pci_ioctl.h>

#include "amba_pci_platform_table.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
#define PDE_DATA	pde_data
#endif

#define AMBA_PCI_NET_INFO_MAX	32

#define DRIVER_NAME			"amba_pci_platform"
#define CLASS_NAME			"amba_pci_platform"
#define PCIE_BOOT_TIMEOUT_MS		60000U
#define AMBA_PCI_VIRTIO_DEVICE_ID	0x0500U

struct pci_dev_board_info {
	u32 id;
	u32 vers;
	u32 poc;
	u32 dram_size_mb;
	char dram_type[8];
	char date[32];
};

struct pcie_dev_entry {
	struct pci_dev		*pdev;
	u32			id;
	struct proc_dir_entry	*proc_dir;
	char			proc_name[16];
	bool			in_use;
	enum amba_pci_dev_state	state;
	struct pcie_physical_slot phy_slot;
	int			boot_bar;
	resource_size_t		boot_len;
	void __iomem		*boot_mem_base;
	struct pci_dev_board_info board_info;
	bool			board_info_is_updated;
	struct delayed_work	boot_timeout_work;
};

struct amba_pci_platform {
	struct mutex		lock;
	struct pcie_dev_entry	devs[AMBA_PCI_DEV_MAX];
	u32			next_dev_id;
	dev_t			devt_base;
	dev_t			ctrl_devt;
	int			major;
	struct class		*class;
	struct cdev		ctrl_cdev;
	struct device		*ctrl_dev;
	struct proc_dir_entry	*proc_root;
	struct notifier_block	pci_nb;
};

#endif
