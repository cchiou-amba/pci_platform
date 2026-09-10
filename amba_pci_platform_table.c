/*
 * ambarella/drv_modules/platform/pci_platform/amab_pci_platform_table.c
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

#include <linux/kernel.h>
#include <linux/types.h>

#include "amba_pci_platform_table.h"

/*
 * Physical slot table (one entry per expansion slot).
 * Slot identity: PCI bus; target_vendor/target_device filter module-init scan.
 */
const struct pcie_physical_slot pcie_physical_slots[AMBA_PCI_DEV_MAX] = {
	[0] = { .bus = 0x01, .target_vendor = AMBA_PCI_TARGET_VENDOR_ID,
		.target_device = AMBA_PCI_TARGET_DEVICE_ID },
	[1] = { .bus = 0x02, .target_vendor = AMBA_PCI_TARGET_VENDOR_ID,
		.target_device = AMBA_PCI_TARGET_DEVICE_ID },
	[2] = { .bus = 0x03, .target_vendor = AMBA_PCI_TARGET_VENDOR_ID,
		.target_device = AMBA_PCI_TARGET_DEVICE_ID },
	[3] = { .bus = 0x04, .target_vendor = AMBA_PCI_TARGET_VENDOR_ID,
		.target_device = AMBA_PCI_TARGET_DEVICE_ID },
};
