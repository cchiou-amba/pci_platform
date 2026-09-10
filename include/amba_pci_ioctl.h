/*
 * ambarella/drv_modules/platform/pci_platform/include/amba_pci_ioctl.h
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

#ifndef __AMBA_PCI_IOCTL_H__
#define __AMBA_PCI_IOCTL_H__

#include <linux/ioctl.h>
#if defined(__KERNEL__)
#include <linux/types.h>
#else
#include <basetypes.h>
#endif

#define AMBA_PCI_IOC_MAGIC			'P'

#define AMBA_PCI_NAME_MAX			32U
#define AMBA_PCI_DEV_MAX			4

/* Only support CV7 for now */
#define AMBA_PCI_TARGET_VENDOR_ID		0x2134U
#define AMBA_PCI_TARGET_DEVICE_ID		0x0006U

enum amba_pci_dev_state {
	AMBA_PCI_STATE_DISCONNECTED = 0,
	AMBA_PCI_STATE_BOOTING = 1,
	AMBA_PCI_STATE_REBOOTING = 2,
	AMBA_PCI_STATE_CONNECTED = 3,
	AMBA_PCI_STATE_BOOT_FAILED = 4,
};

struct amba_pci_dev_info {
	u32 id;
	u32 slot_id;
	enum amba_pci_dev_state state;
	u32 is_valid;
	u32 ip;
	u32 poc;
	u32 dram_size_mb;
	u32 vendor;
	u32 device;
	char dram_type[AMBA_PCI_NAME_MAX];
	char fw_vers[AMBA_PCI_NAME_MAX];
};

struct amba_pci_platform_info {
	u32 version;
	u32 dev_num;
	struct amba_pci_dev_info devs[AMBA_PCI_DEV_MAX];
};

#define AMBA_PCI_FW_IMAGE_MAX		3U

enum amba_pci_fw_image_type {
	AMBA_PCI_FW_IMAGE_DTB = 0,
	AMBA_PCI_FW_IMAGE_ROOTFS,
	AMBA_PCI_FW_IMAGE_KERNEL,
};

struct amba_pci_fw_image_desc {
	u32 type;
	u32 reserved;
	u64 offset;
	u64 length;
};

struct amba_pci_fw_xfer {
	u32 slot_id;
	u64 fw_size;
	u64 fw_user_ptr;
	struct amba_pci_fw_image_desc images[AMBA_PCI_FW_IMAGE_MAX];
};

#define AMBA_PCI_PATH_MAX			256U

/* Send a host file to EP memory. */
struct amba_pci_h2d_send_file {
	u32 slot_id;
	u32 reserved;
	u64 ep_addr;
	char path[AMBA_PCI_PATH_MAX];
};

/* Send a userspace buffer to EP memory. */
struct amba_pci_h2d_send_buffer {
	u32 slot_id;
	u32 reserved;
	u64 src_user_ptr;
	u64 size;
	u64 ep_addr;
};

#define AMBA_PCI_QUERY_PLATFORM_INFO \
	_IOWR(AMBA_PCI_IOC_MAGIC, 0x01, struct amba_pci_platform_info)
#define AMBA_PCI_XFER_FIRMWARE \
	_IOW(AMBA_PCI_IOC_MAGIC, 0x04, struct amba_pci_fw_xfer)
#define AMBA_PCI_H2D_SEND_FILE \
	_IOW(AMBA_PCI_IOC_MAGIC, 0x02, struct amba_pci_h2d_send_file)
#define AMBA_PCI_H2D_SEND_BUFFER \
	_IOW(AMBA_PCI_IOC_MAGIC, 0x03, struct amba_pci_h2d_send_buffer)

#endif /* __AMBA_PCI_IOCTL_H__ */
