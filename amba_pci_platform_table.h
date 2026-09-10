/*
 * ambarella/drv_modules/platform/pci_platform/amab_pci_platform_table.h
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
#ifndef __AMBA_PCI_PLATFORM_TABLE_H__
#define __AMBA_PCI_PLATFORM_TABLE_H__

#include <linux/types.h>
#include <amba_pci_ioctl.h>

struct pcie_physical_slot {
	u8 bus;
	u16 target_vendor;
	u16 target_device;
};

extern const struct pcie_physical_slot pcie_physical_slots[AMBA_PCI_DEV_MAX];

#endif
