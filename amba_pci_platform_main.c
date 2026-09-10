/*
 * ambarella/drv_modules/platform/pci_platform/amba_pci_platform_main.c
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

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#include "amba_pci_platform_internal.h"
#include "amba_pci_platform_transfer.h"
#include "pci-buzz.h"
#include "pci-turbo.h"

#define AMBA_PCI_H2D_CHUNK_MAX		(2U << 20)

static struct amba_pci_platform g_pcie_drv;

static const char *pcie_state_str(enum amba_pci_dev_state state)
{
	const char *state_name = "Unknown";

	switch (state) {
	case AMBA_PCI_STATE_DISCONNECTED:
		state_name = "Disconnected";
		break;
	case AMBA_PCI_STATE_BOOTING:
		state_name = "Booting";
		break;
	case AMBA_PCI_STATE_REBOOTING:
		state_name = "Rebooting";
		break;
	case AMBA_PCI_STATE_CONNECTED:
		state_name = "Connected";
		break;
	case AMBA_PCI_STATE_BOOT_FAILED:
		state_name = "Boot failed";
		break;
	default:
		break;
	}

	return state_name;
}

static int pcie_entry_slot(const struct pcie_dev_entry *e)
{
	return (int)(e - g_pcie_drv.devs);
}

static int pcie_dev_refresh_board_info_locked(struct pcie_dev_entry *e)
{
	struct pci_dev_board_info bd = {0};
	int ret = 0;

	if (e->state != AMBA_PCI_STATE_DISCONNECTED) {
		ret = 0;
		goto PCIE_DEV_REFRESH_BOARD_INFO_LOCKED_EXIT;
	}

	if (!e->pdev) {
		ret = -ENODEV;
		goto PCIE_DEV_REFRESH_BOARD_INFO_LOCKED_EXIT;
	}

	if (!e->boot_mem_base) {
		ret = pcie_boot_bar_ensure_mapped(e);
		if (ret)
			goto PCIE_DEV_REFRESH_BOARD_INFO_LOCKED_EXIT;
	}

	if (!e->board_info_is_updated && e->boot_mem_base) {
		pcie_ring_read_board_info(e->boot_mem_base, &bd);
		e->board_info = bd;
		e->board_info_is_updated = true;
	}

PCIE_DEV_REFRESH_BOARD_INFO_LOCKED_EXIT:
	return ret;
}

static void pcie_query_dev_info_locked(struct amba_pci_dev_info *dst,
				       struct pcie_dev_entry *e, int slot)
{
	memset(dst, 0, sizeof(*dst));
	dst->id = e->id;
	dst->slot_id = (u32)slot;
	dst->state = e->state;
	dst->is_valid = e->in_use ? 1U : 0U;

	if (!e->in_use) {
		return;
	}

	pcie_dev_refresh_board_info_locked(e);

	if (e->pdev) {
		dst->vendor = (u32)e->pdev->vendor;
		dst->device = (u32)e->pdev->device;
	}

	if (e->board_info_is_updated) {
		dst->poc = (u32)e->board_info.poc;
		dst->dram_size_mb = (u32)e->board_info.dram_size_mb;
		strscpy(dst->dram_type, e->board_info.dram_type, sizeof(dst->dram_type));
		strscpy(dst->fw_vers, e->board_info.date, sizeof(dst->fw_vers));
	}

	return;
}

static void pcie_entry_detach_pdev(struct pcie_dev_entry *e)
{
	if (!e->pdev) {
		goto PCIE_ENTRY_DETACH_PDEV_EXIT;
	}
	pcie_boot_bar_unmap(e);
	pci_dev_put(e->pdev);
	e->pdev = NULL;

PCIE_ENTRY_DETACH_PDEV_EXIT:
	return;
}

static void pcie_boot_timeout_fn(struct work_struct *work)
{
	struct pcie_dev_entry *e = container_of(work, struct pcie_dev_entry,
						 boot_timeout_work.work);

	mutex_lock(&g_pcie_drv.lock);
	if (e->in_use && e->state == AMBA_PCI_STATE_BOOTING) {
		e->state = AMBA_PCI_STATE_BOOT_FAILED;
	}
	mutex_unlock(&g_pcie_drv.lock);

	return;
}

static void pcie_boot_timeout_schedule(struct pcie_dev_entry *e)
{
	cancel_delayed_work(&e->boot_timeout_work);
	schedule_delayed_work(&e->boot_timeout_work,
			      msecs_to_jiffies(PCIE_BOOT_TIMEOUT_MS));

	return;
}

static int pcie_file_open(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return stream_open(inode, filp);
}

static long pcie_ioctl_query_platform_info(void __user *argp)
{
	struct amba_pci_platform_info info;
	int i = 0, valid_count = 0, limit = 0;
	long ret = 0;

	limit = AMBA_PCI_DEV_MAX;

	if (!argp) {
		ret = -EINVAL;
		goto PCIE_IOCTL_QUERY_PLATFORM_INFO_EXIT;
	}

	memset(&info, 0, sizeof(info));
	info.version = 1U;

	mutex_lock(&g_pcie_drv.lock);
	for (i = 0; i < limit; i++) {
		pcie_query_dev_info_locked(&info.devs[i], &g_pcie_drv.devs[i], i);
		if (g_pcie_drv.devs[i].in_use) {
			valid_count++;
		}
	}
	mutex_unlock(&g_pcie_drv.lock);

	info.dev_num = (u32)valid_count;
	if (copy_to_user(argp, &info, sizeof(info))) {
		ret = -EFAULT;
		goto PCIE_IOCTL_QUERY_PLATFORM_INFO_EXIT;
	}

	ret = 0;

PCIE_IOCTL_QUERY_PLATFORM_INFO_EXIT:
	return ret;
}

static long pcie_ioctl_xfer_firmware(void __user *argp)
{
	struct amba_pci_fw_xfer req;
	struct pcie_dev_entry *e = NULL, *e_hot = NULL;
	const u8 __user *fw_user = NULL;
	const struct amba_pci_fw_image_desc *img = NULL;
	u32 fw_req_bitmap = 0;
	unsigned int i = 0;
	long ret = 0;

	if (copy_from_user(&req, argp, sizeof(req))) {
		ret = -EFAULT;
		goto PCIE_IOCTL_XFER_FIRMWARE_EXIT;
	}

	if (req.slot_id >= AMBA_PCI_DEV_MAX) {
		ret = -EINVAL;
		goto PCIE_IOCTL_XFER_FIRMWARE_EXIT;
	}
	if (!req.fw_user_ptr || !req.fw_size) {
		ret = -EINVAL;
		goto PCIE_IOCTL_XFER_FIRMWARE_EXIT;
	}

	for (i = 0; i < AMBA_PCI_FW_IMAGE_MAX; i++) {
		img = &req.images[i];

		if (img->offset > req.fw_size ||
		    !img->length ||
		    img->length > req.fw_size - img->offset) {
			ret = -EINVAL;
			goto PCIE_IOCTL_XFER_FIRMWARE_EXIT;
		}
		fw_req_bitmap |= (1U << img->type);
	}

	if (fw_req_bitmap != pcie_fw_req_bitmap_mask) {
		ret = -EINVAL;
		goto PCIE_IOCTL_XFER_FIRMWARE_EXIT;
	}

	fw_user = (const u8 __user *)(unsigned long)req.fw_user_ptr;

	mutex_lock(&g_pcie_drv.lock);
	e = &g_pcie_drv.devs[req.slot_id];

	do {
		if (!e->in_use) {
			ret = -ENODEV;
			break;
		}
		if (e->state != AMBA_PCI_STATE_DISCONNECTED) {
			ret = -EBUSY;
			break;
		}

		ret = pcie_dev_refresh_board_info_locked(e);
		if (ret)
			break;

		pci_set_master(e->pdev);
		ret = pcie_ring_wait_ep_ready(e->pdev, e->boot_mem_base);
		if (ret) {
			break;
		}

		for (i = 0; i < PCIE_FW_XFER_ORDER_CNT; i++) {
			img = pcie_fw_find_image(&req, pcie_fw_xfer_order[i]);
			ret = pcie_ring_xfer_user(e->pdev, e->boot_mem_base,
						  (size_t)e->boot_len,
						  fw_user, img);
			if (ret) {
				break;
			}
		}
		if (ret) {
			break;
		}

		e->state = AMBA_PCI_STATE_BOOTING;
		pcie_boot_timeout_schedule(e);
		e_hot = e;
	} while (0);
	mutex_unlock(&g_pcie_drv.lock);

	if (e_hot && e_hot->pdev)
		pci_stop_and_remove_bus_device_locked(e_hot->pdev);

PCIE_IOCTL_XFER_FIRMWARE_EXIT:
	return ret;
}

static long pcie_ioctl_h2d_send_file(void __user *argp)
{
	struct amba_pci_h2d_send_file req;
	struct pcie_dev_entry *e = NULL;
	char *kbuf = NULL;
	struct file *file = NULL;
	loff_t pos = 0;
	u64 ep_addr = 0;
	u32 bus = 0;
	ssize_t nread = 0;
	size_t chunk = AMBA_PCI_H2D_CHUNK_MAX;
	long ret = 0;
	int (*h2d_xfer)(u32, const void *, size_t, u64) = NULL;

	if (copy_from_user(&req, argp, sizeof(req))) {
		ret = -EFAULT;
		goto PCIE_IOCTL_H2D_SEND_FILE_EXIT;
	}

	req.path[AMBA_PCI_PATH_MAX - 1] = '\0';
	if (req.slot_id >= AMBA_PCI_DEV_MAX || !req.path[0] || !req.ep_addr) {
		/*
		 * ep_addr == 0 is rejected: low memory is unsafe for EP DMA sink.
		 * Use an explicit reserved EP address.
		 */
		ret = -EINVAL;
		goto PCIE_IOCTL_H2D_SEND_FILE_EXIT;
	}

	mutex_lock(&g_pcie_drv.lock);
	e = &g_pcie_drv.devs[req.slot_id];
	if (!e->in_use || !e->pdev) {
		ret = -ENODEV;
		mutex_unlock(&g_pcie_drv.lock);
		goto PCIE_IOCTL_H2D_SEND_FILE_EXIT;
	}
	if (e->state != AMBA_PCI_STATE_CONNECTED) {
		ret = -EBUSY;
		mutex_unlock(&g_pcie_drv.lock);
		goto PCIE_IOCTL_H2D_SEND_FILE_EXIT;
	}
	bus = (u32)e->pdev->bus->number;
	ep_addr = req.ep_addr;
	mutex_unlock(&g_pcie_drv.lock);

	h2d_xfer = (int (*)(u32, const void *, size_t, u64))
		symbol_get(pci_turbo_bus_xfer_h2d);
	if (!h2d_xfer) {
		ret = -ENODEV;
		goto PCIE_IOCTL_H2D_SEND_FILE_EXIT;
	}

	file = filp_open(req.path, O_RDONLY, 0);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		file = NULL;
		goto put_sym;
	}

	kbuf = vmalloc(chunk);
	if (!kbuf) {
		ret = -ENOMEM;
		goto close_file;
	}

	while (1) {
		nread = kernel_read(file, kbuf, chunk, &pos);
		if (nread < 0) {
			ret = nread;
			break;
		}
		if (!nread) {
			ret = 0;
			break;
		}

		ret = h2d_xfer(bus, kbuf, (size_t)nread, ep_addr);
		if (ret)
			break;
		ep_addr += (u64)nread;
	}

	vfree(kbuf);
close_file:
	if (file)
		filp_close(file, NULL);
put_sym:
	symbol_put(pci_turbo_bus_xfer_h2d);

PCIE_IOCTL_H2D_SEND_FILE_EXIT:
	return ret;
}

static long pcie_ioctl_h2d_send_buffer(void __user *argp)
{
	struct amba_pci_h2d_send_buffer req;
	struct pcie_dev_entry *e = NULL;
	const u8 __user *src = NULL;
	char *kbuf = NULL;
	u64 offset = 0;
	u32 bus = 0;
	long ret = 0;
	int (*h2d_xfer)(u32, const void *, size_t, u64) = NULL;

	if (copy_from_user(&req, argp, sizeof(req))) {
		ret = -EFAULT;
		goto PCIE_IOCTL_H2D_SEND_BUFFER_EXIT;
	}

	if (req.slot_id >= AMBA_PCI_DEV_MAX || !req.src_user_ptr ||
	    !req.size || !req.ep_addr ||
	    req.src_user_ptr > U64_MAX - req.size ||
	    req.ep_addr > U64_MAX - req.size) {
		ret = -EINVAL;
		goto PCIE_IOCTL_H2D_SEND_BUFFER_EXIT;
	}

	mutex_lock(&g_pcie_drv.lock);
	e = &g_pcie_drv.devs[req.slot_id];
	if (!e->in_use || !e->pdev) {
		ret = -ENODEV;
		mutex_unlock(&g_pcie_drv.lock);
		goto PCIE_IOCTL_H2D_SEND_BUFFER_EXIT;
	}
	if (e->state != AMBA_PCI_STATE_CONNECTED) {
		ret = -EBUSY;
		mutex_unlock(&g_pcie_drv.lock);
		goto PCIE_IOCTL_H2D_SEND_BUFFER_EXIT;
	}
	bus = (u32)e->pdev->bus->number;
	mutex_unlock(&g_pcie_drv.lock);

	h2d_xfer = (int (*)(u32, const void *, size_t, u64))
		symbol_get(pci_turbo_bus_xfer_h2d);
	if (!h2d_xfer) {
		ret = -ENODEV;
		goto PCIE_IOCTL_H2D_SEND_BUFFER_EXIT;
	}

	kbuf = vmalloc(AMBA_PCI_H2D_CHUNK_MAX);
	if (!kbuf) {
		ret = -ENOMEM;
		goto put_sym;
	}

	src = (const u8 __user *)(unsigned long)req.src_user_ptr;
	while (offset < req.size) {
		size_t n = (size_t)min_t(u64, req.size - offset,
					 AMBA_PCI_H2D_CHUNK_MAX);

		if (copy_from_user(kbuf, src + offset, n)) {
			ret = -EFAULT;
			break;
		}

		ret = h2d_xfer(bus, kbuf, n, req.ep_addr + offset);
		if (ret)
			break;
		offset += n;
	}

	vfree(kbuf);
put_sym:
	symbol_put(pci_turbo_bus_xfer_h2d);

PCIE_IOCTL_H2D_SEND_BUFFER_EXIT:
	return ret;
}

static long pcie_file_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	long ret = -ENOTTY;

	(void)filp;

	switch (cmd) {
	case AMBA_PCI_QUERY_PLATFORM_INFO:
		ret = pcie_ioctl_query_platform_info(argp);
		break;
	case AMBA_PCI_XFER_FIRMWARE:
		ret = pcie_ioctl_xfer_firmware(argp);
		break;
	case AMBA_PCI_H2D_SEND_FILE:
		ret = pcie_ioctl_h2d_send_file(argp);
		break;
	case AMBA_PCI_H2D_SEND_BUFFER:
		ret = pcie_ioctl_h2d_send_buffer(argp);
		break;
	default:
		break;
	}

	return ret;
}

static const struct file_operations pcie_fops = {
	.owner		= THIS_MODULE,
	.open		= pcie_file_open,
	.unlocked_ioctl	= pcie_file_ioctl,
	.llseek		= noop_llseek,
};

static ssize_t pcie_status_read_entry(struct pcie_dev_entry *e, char *buf, size_t size)
{
	return scnprintf(buf, size, "state=%s\n", pcie_state_str(e->state));
}

static ssize_t pcie_format_dev_info_proc(char *buf, size_t size,
					 const struct amba_pci_dev_info *info,
					 const struct pcie_dev_entry *e,
					 u16 domain, u8 bus, u8 devfn,
					 const char *pci_nm)
{
	ssize_t len = 0;

	len = scnprintf(buf, size,
			"state=%s\n"
			"domain=%04x bus=%02x slot=%02x func=%x\n"
			"vendor=0x%04x device=0x%04x\n"
			"pci_name=%s\n",
			pcie_state_str(info->state),
			domain, bus, PCI_SLOT(devfn), PCI_FUNC(devfn),
			(u16)info->vendor, (u16)info->device,
			pci_nm);
	len += scnprintf(buf + len, size - len,
			 "board_fw_vers=%s\n"
			 "board_poc=0x%08x\n"
			 "board_dram=%u.%02u GB(%s)\n"
			 "board_bld_vers=%u.%02u\n",
			 e->board_info.date,
			 e->board_info.poc,
			 e->board_info.dram_size_mb >> 10,
			 (e->board_info.dram_size_mb % 1024) * 100 / 1024,
			 e->board_info.dram_type,
			 e->board_info.vers >> 16, e->board_info.vers & 0xffff);

	return len;
}

static ssize_t pcie_dev_info_read_entry(struct pcie_dev_entry *e, char *buf, size_t size)
{
	struct amba_pci_dev_info info;
	u16 domain = 0;
	u8 bus = 0, devfn = 0;
	const char *pci_nm = "(offline)";
	int slot = 0;
	ssize_t len = 0;

	mutex_lock(&g_pcie_drv.lock);
	slot = pcie_entry_slot(e);
	pcie_query_dev_info_locked(&info, e, slot);
	if (e->pdev) {
		pci_nm = pci_name(e->pdev);
		domain = (u16)pci_domain_nr(e->pdev->bus);
		bus = e->pdev->bus->number;
		devfn = e->pdev->devfn;
	} else {
		bus = e->phy_slot.bus;
	}
	len = pcie_format_dev_info_proc(buf, size, &info, e,
					domain, bus, devfn, pci_nm);
	mutex_unlock(&g_pcie_drv.lock);

	return len;
}

static ssize_t pcie_net_read_entry(struct pcie_dev_entry *e, char *buf, size_t size)
{
	char net[AMBA_PCI_NET_INFO_MAX] = {0};
	bool ok = false;
	u32 bus = 0;
	ssize_t ret = -ENODEV;
	bool (*net_get)(u32 bus, char *net, size_t len) = NULL;

	if (!e->in_use) {
		goto PCIE_NET_READ_ENTRY_EXIT;
	}

	bus = e->pdev ? (u32)e->pdev->bus->number : (u32)e->phy_slot.bus;
	net_get = (bool (*)(u32, char *, size_t))
		symbol_get(pci_buzz_bus_net_get);
	if (net_get) {
		ok = net_get(bus, net, sizeof(net));
		symbol_put(pci_buzz_bus_net_get);
	}
	if (!ok) {
		ret = scnprintf(buf, size, "invalid\n");
		goto PCIE_NET_READ_ENTRY_EXIT;
	}

	ret = scnprintf(buf, size, "%s\n", net);

PCIE_NET_READ_ENTRY_EXIT:
	return ret;
}

static ssize_t pcie_dev_log_read_entry(struct pcie_dev_entry *e, char *buf, size_t size)
{
	ssize_t ret = -ENODEV;
	u32 bus = 0;
	ssize_t (*epinfo_read)(u32, char *, size_t) = NULL;

	if (!e->in_use) {
		goto PCIE_DEV_LOG_READ_ENTRY_EXIT;
	}

	bus = e->pdev ? (u32)e->pdev->bus->number : (u32)e->phy_slot.bus;
	epinfo_read = (ssize_t (*)(u32, char *, size_t))
		symbol_get(pci_buzz_bus_epinfo_read);
	if (epinfo_read) {
		ret = epinfo_read(bus, buf, size);
		symbol_put(pci_buzz_bus_epinfo_read);
	}
	if (ret <= 0) {
		ret = scnprintf(buf, size, "invalid\n");
	}

PCIE_DEV_LOG_READ_ENTRY_EXIT:
	return ret;
}

static ssize_t pcie_reboot_write_entry(struct pcie_dev_entry *e,
				       const char *buf, size_t count)
{
	char tmp[16] = {0};
	size_t n = 0;
	int cmd = 0, ret = 0;
	ssize_t result = -ENODEV;

	if (!e->in_use) {
		goto PCIE_REBOOT_WRITE_ENTRY_EXIT;
	}
	if (!count) {
		result = -EINVAL;
		goto PCIE_REBOOT_WRITE_ENTRY_EXIT;
	}

	n = min_t(size_t, count, sizeof(tmp) - 1);
	memcpy(tmp, buf, n);
	tmp[n] = '\0';
	strim(tmp);
	if (kstrtoint(tmp, 10, &cmd) || cmd != 1) {
		result = -EINVAL;
		goto PCIE_REBOOT_WRITE_ENTRY_EXIT;
	}

	mutex_lock(&g_pcie_drv.lock);
	if (!e->pdev) {
		ret = -ENODEV;
		goto out_cards;
	}
	if (e->state != AMBA_PCI_STATE_CONNECTED) {
		ret = -EBUSY;
		goto out_cards;
	}

	e->state = AMBA_PCI_STATE_REBOOTING;

	mutex_unlock(&g_pcie_drv.lock);
	pci_stop_and_remove_bus_device_locked(e->pdev);
	if (ret < 0) {
		result = ret;
	} else {
		result = count;
	}
	goto PCIE_REBOOT_WRITE_ENTRY_EXIT;

out_cards:
	mutex_unlock(&g_pcie_drv.lock);
	if (ret < 0) {
		result = ret;
	}

PCIE_REBOOT_WRITE_ENTRY_EXIT:
	return result;
}

typedef ssize_t (*pcie_proc_entry_read_fn)(struct pcie_dev_entry *e,
					   char *buf, size_t size);

static ssize_t pcie_proc_read_page(struct file *file, char __user *buf,
				   size_t nbytes, loff_t *ppos,
				   pcie_proc_entry_read_fn reader)
{
	struct pcie_dev_entry *e = NULL;
	char *kbuf = NULL;
	ssize_t len = 0, ret = -EINVAL;

	e = PDE_DATA(file_inode(file));

	kbuf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!kbuf) {
		ret = -ENOMEM;
		goto PCIE_PROC_READ_PAGE_EXIT;
	}

	len = reader(e, kbuf, PAGE_SIZE);
	if (len < 0) {
		ret = len;
		goto PCIE_PROC_READ_PAGE_EXIT;
	}

	ret = simple_read_from_buffer(buf, nbytes, ppos, kbuf, len);

PCIE_PROC_READ_PAGE_EXIT:
	kfree(kbuf);
	return ret;
}

static ssize_t pcie_proc_status_read(struct file *file, char __user *buf,
				     size_t nbytes, loff_t *ppos)
{
	return pcie_proc_read_page(file, buf, nbytes, ppos, pcie_status_read_entry);
}

static ssize_t pcie_proc_dev_info_read(struct file *file, char __user *buf,
				       size_t nbytes, loff_t *ppos)
{
	return pcie_proc_read_page(file, buf, nbytes, ppos, pcie_dev_info_read_entry);
}

static ssize_t pcie_proc_net_read(struct file *file, char __user *buf,
				  size_t nbytes, loff_t *ppos)
{
	return pcie_proc_read_page(file, buf, nbytes, ppos, pcie_net_read_entry);
}

static ssize_t pcie_proc_dev_log_read(struct file *file, char __user *buf,
				      size_t nbytes, loff_t *ppos)
{
	struct pcie_dev_entry *e = NULL;
	const size_t max_log = SZ_2M;
	char *kbuf = NULL;
	ssize_t len = 0, ret = 0;

	e = PDE_DATA(file_inode(file));

	kbuf = vzalloc(max_log);
	if (!kbuf) {
		ret = -ENOMEM;
		goto PCIE_PROC_DEV_LOG_READ_EXIT;
	}

	len = pcie_dev_log_read_entry(e, kbuf, max_log);
	if (len < 0) {
		ret = len;
		goto PCIE_PROC_DEV_LOG_READ_EXIT;
	}

	ret = simple_read_from_buffer(buf, nbytes, ppos, kbuf, len);

PCIE_PROC_DEV_LOG_READ_EXIT:
	vfree(kbuf);
	return ret;
}

static ssize_t pcie_proc_reboot_write(struct file *file, const char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct pcie_dev_entry *e = NULL;
	char *kbuf = NULL;
	ssize_t ret = -EINVAL;

	e = PDE_DATA(file_inode(file));

	if (*ppos != 0) {
		goto PCIE_PROC_REBOOT_WRITE_EXIT;
	}
	if (count > 32) {
		goto PCIE_PROC_REBOOT_WRITE_EXIT;
	}

	kbuf = kmalloc(count + 1, GFP_KERNEL);
	if (!kbuf) {
		ret = -ENOMEM;
		goto PCIE_PROC_REBOOT_WRITE_EXIT;
	}
	if (copy_from_user(kbuf, buf, count)) {
		ret = -EFAULT;
		goto PCIE_PROC_REBOOT_WRITE_EXIT;
	}
	kbuf[count] = '\0';

	ret = pcie_reboot_write_entry(e, kbuf, count);
	if (ret < 0) {
		goto PCIE_PROC_REBOOT_WRITE_EXIT;
	}

	*ppos += count;
	ret = count;

PCIE_PROC_REBOOT_WRITE_EXIT:
	kfree(kbuf);
	return ret;
}

static const struct proc_ops pcie_proc_status_ops = {
	.proc_read = pcie_proc_status_read,
	.proc_lseek = default_llseek,
};

static const struct proc_ops pcie_proc_dev_info_ops = {
	.proc_read = pcie_proc_dev_info_read,
	.proc_lseek = default_llseek,
};

static const struct proc_ops pcie_proc_net_ops = {
	.proc_read = pcie_proc_net_read,
	.proc_lseek = default_llseek,
};

static const struct proc_ops pcie_proc_dev_log_ops = {
	.proc_read = pcie_proc_dev_log_read,
	.proc_lseek = default_llseek,
};

static const struct proc_ops pcie_proc_reboot_ops = {
	.proc_write = pcie_proc_reboot_write,
	.proc_lseek = default_llseek,
};

static int pcie_create_proc_nodes(struct pcie_dev_entry *e, int index)
{
	struct proc_dir_entry *entry = NULL;
	bool err_flag = false;
	int ret = 0;

	if (!g_pcie_drv.proc_root) {
		ret = -ENODEV;
		goto PCIE_CREATE_PROC_NODES_EXIT;
	}

	scnprintf(e->proc_name, sizeof(e->proc_name), "pcie_%d", index);
	e->proc_dir = proc_mkdir(e->proc_name, g_pcie_drv.proc_root);
	if (!e->proc_dir) {
		ret = -ENOMEM;
		goto PCIE_CREATE_PROC_NODES_EXIT;
	}

	entry = proc_create_data("status", 0444, e->proc_dir, &pcie_proc_status_ops, e);
	if (!entry) {
		err_flag = true;
		goto PCIE_CREATE_PROC_NODES_EXIT;
	}
	entry = proc_create_data("dev_info", 0444, e->proc_dir, &pcie_proc_dev_info_ops, e);
	if (!entry) {
		err_flag = true;
		goto PCIE_CREATE_PROC_NODES_EXIT;
	}
	entry = proc_create_data("net", 0444, e->proc_dir, &pcie_proc_net_ops, e);
	if (!entry) {
		err_flag = true;
		goto PCIE_CREATE_PROC_NODES_EXIT;
	}
	entry = proc_create_data("dev_log", 0444, e->proc_dir, &pcie_proc_dev_log_ops, e);
	if (!entry) {
		err_flag = true;
		goto PCIE_CREATE_PROC_NODES_EXIT;
	}
	entry = proc_create_data("reboot", 0220, e->proc_dir, &pcie_proc_reboot_ops, e);
	if (!entry) {
		err_flag = true;
		goto PCIE_CREATE_PROC_NODES_EXIT;
	}

	ret = 0;

PCIE_CREATE_PROC_NODES_EXIT:
	if (err_flag) {
		remove_proc_subtree(e->proc_name, g_pcie_drv.proc_root);
		e->proc_dir = NULL;
		e->proc_name[0] = '\0';
		ret = -ENOMEM;
	}
	return ret;
}

static void pcie_release_dev(struct pcie_dev_entry *e)
{
	if (!e->in_use) {
		goto PCIE_RELEASE_DEV_EXIT;
	}

	pcie_entry_detach_pdev(e);
	cancel_delayed_work(&e->boot_timeout_work);
	if (e->proc_dir) {
		remove_proc_subtree(e->proc_name, g_pcie_drv.proc_root);
		e->proc_dir = NULL;
		e->proc_name[0] = '\0';
	}
	e->id = 0U;
	e->in_use = false;
	e->state = AMBA_PCI_STATE_DISCONNECTED;
	e->boot_bar = -1;
	e->boot_len = 0;
	e->boot_mem_base = NULL;
	e->board_info_is_updated = false;
	memset(&e->board_info, 0, sizeof(e->board_info));

PCIE_RELEASE_DEV_EXIT:
	return;
}

static int pcie_init_slot(struct pci_dev *pdev, int index)
{
	struct pcie_dev_entry *e = NULL;
	int ret = 0;
	bool proc_created = false;

	e = &g_pcie_drv.devs[index];

	ret = pcie_create_proc_nodes(e, index);
	if (ret) {
		goto PCIE_INIT_SLOT_EXIT;
	}
	proc_created = true;

	e->pdev = pci_dev_get(pdev);
	e->id = g_pcie_drv.next_dev_id;
	g_pcie_drv.next_dev_id++;
	e->phy_slot = pcie_physical_slots[index];
	e->board_info_is_updated = false;
	memset(&e->board_info, 0, sizeof(e->board_info));
	e->boot_bar = -1;
	e->boot_len = 0;
	e->boot_mem_base = NULL;
	e->state = AMBA_PCI_STATE_DISCONNECTED;
	e->in_use = true;

PCIE_INIT_SLOT_EXIT:
	if (ret && proc_created) {
		remove_proc_subtree(e->proc_name, g_pcie_drv.proc_root);
		e->proc_dir = NULL;
		e->proc_name[0] = '\0';
	}
	return ret;
}

static int get_pci_slot_id(const struct pci_dev *pdev)
{
	int i = 0, ret = -1;

	for (i = 0; i < AMBA_PCI_DEV_MAX; i++) {
		if (pdev->bus->number != pcie_physical_slots[i].bus) {
			continue;
		}
		ret = i;
		goto GET_PCI_SLOT_ID_EXIT;
	}

GET_PCI_SLOT_ID_EXIT:
	return ret;
}

static void pcie_try_reconnect_booting_locked(struct pci_dev *pdev,
					      struct pcie_dev_entry *e)
{
	WARN_ONCE(e->pdev, "%s: reconnect with existing pdev\n", DRIVER_NAME);
	e->pdev = pci_dev_get(pdev);
	/* TODO: need to check if domain bus devfn will change. */
	cancel_delayed_work(&e->boot_timeout_work);
	e->state = AMBA_PCI_STATE_CONNECTED;

	return;
}

static void pcie_handle_add_locked(struct pci_dev *pdev)
{
	int slot_id = -1, ret = 0;
	struct pcie_dev_entry *e = NULL;

	slot_id = get_pci_slot_id(pdev);
	if (slot_id < 0)
		goto PCIE_HANDLE_ADD_LOCKED_EXIT;

	e = &g_pcie_drv.devs[slot_id];
	if (e->in_use) {
		if (e->state != AMBA_PCI_STATE_BOOTING)
			goto PCIE_HANDLE_ADD_LOCKED_EXIT;
		if (pdev->vendor == AMBA_PCI_TARGET_VENDOR_ID &&
		    pdev->device == AMBA_PCI_VIRTIO_DEVICE_ID &&
		    pci_resource_len(pdev, 0)) {
			pcie_try_reconnect_booting_locked(pdev, e);
		}
	} else if (pdev->vendor == AMBA_PCI_TARGET_VENDOR_ID &&
		   pdev->device == AMBA_PCI_VIRTIO_DEVICE_ID &&
		   pci_resource_len(pdev, 0)) {
		ret = pcie_init_slot(pdev, slot_id);
		if (ret) {
			pr_err("%s: init slot for %s failed: %d\n",
			       DRIVER_NAME, pci_name(pdev), ret);
			goto PCIE_HANDLE_ADD_LOCKED_EXIT;
		}
		e->state = AMBA_PCI_STATE_CONNECTED;
	} else if (pdev->vendor == AMBA_PCI_TARGET_VENDOR_ID &&
		   pdev->device == AMBA_PCI_TARGET_DEVICE_ID &&
		   (pdev->class & 0xff0000) == 0xff0000 &&
		   pci_resource_len(pdev, 0)) {
		ret = pcie_init_slot(pdev, slot_id);
		if (ret)
			pr_err("%s: init slot for %s failed: %d\n",
			       DRIVER_NAME, pci_name(pdev), ret);
	}

PCIE_HANDLE_ADD_LOCKED_EXIT:
	return;
}

static void pcie_handle_del_locked(struct pci_dev *pdev)
{
	int slot_id = -1;
	struct pcie_dev_entry *e = NULL;

	slot_id = get_pci_slot_id(pdev);
	if (slot_id < 0 || !g_pcie_drv.devs[slot_id].in_use)
		goto PCIE_HANDLE_DEL_LOCKED_EXIT;

	e = &g_pcie_drv.devs[slot_id];

	if (e->state == AMBA_PCI_STATE_BOOTING) {
		pcie_entry_detach_pdev(e);
	} else {
		pcie_release_dev(e);
	}

PCIE_HANDLE_DEL_LOCKED_EXIT:
	return;
}

static int pcie_pci_bus_notify(struct notifier_block *nb, unsigned long action,
			       void *data)
{
	struct device *dev = NULL;
	struct pci_dev *pdev = NULL;
	int ret = NOTIFY_DONE;

	dev = data;

	if (dev->bus != &pci_bus_type) {
		goto PCIE_PCI_BUS_NOTIFY_EXIT;
	}

	pdev = to_pci_dev(dev);
	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		mutex_lock(&g_pcie_drv.lock);
		pcie_handle_add_locked(pdev);
		mutex_unlock(&g_pcie_drv.lock);
		break;
	case BUS_NOTIFY_DEL_DEVICE:
		mutex_lock(&g_pcie_drv.lock);
		pcie_handle_del_locked(pdev);
		mutex_unlock(&g_pcie_drv.lock);
		break;
	default:
		break;
	}

PCIE_PCI_BUS_NOTIFY_EXIT:
	return ret;
}

static void pcie_enumerate_existed_devs(void)
{
	struct pci_dev *pdev = NULL;
	int i = 0;

	mutex_lock(&g_pcie_drv.lock);
	for (i = 0; i < AMBA_PCI_DEV_MAX; i++) {
		const struct pcie_physical_slot *slot = &pcie_physical_slots[i];

		pdev = NULL;
		while ((pdev = pci_get_device(slot->target_vendor,
					      slot->target_device, pdev))) {
			if (pdev->bus->number != slot->bus) {
				continue;
			}
			pcie_handle_add_locked(pdev);
		}
		pci_dev_put(pdev);
	}
	mutex_unlock(&g_pcie_drv.lock);

	return;
}

static int __init amba_pci_platform_init(void)
{
	int ret = 0, i = 0;
	bool init_failed = false, chrdev_registered = false;
	bool cdev_registered = false, notifier_registered = false;

	mutex_init(&g_pcie_drv.lock);

	for (i = 0; i < AMBA_PCI_DEV_MAX; i++) {
		INIT_DELAYED_WORK(&g_pcie_drv.devs[i].boot_timeout_work, pcie_boot_timeout_fn);
		g_pcie_drv.devs[i].id = 0U;
	}
	g_pcie_drv.next_dev_id = 0U;

	ret = alloc_chrdev_region(&g_pcie_drv.devt_base, 0, 1, DRIVER_NAME);
	if (ret < 0) {
		init_failed = true;
		goto AMBA_PCI_PLATFORM_INIT_EXIT;
	}
	chrdev_registered = true;
	g_pcie_drv.major = MAJOR(g_pcie_drv.devt_base);
	g_pcie_drv.ctrl_devt = MKDEV(g_pcie_drv.major, 0);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	g_pcie_drv.class = class_create(CLASS_NAME);
#else
	g_pcie_drv.class = class_create(THIS_MODULE, CLASS_NAME);
#endif
	if (IS_ERR(g_pcie_drv.class)) {
		ret = PTR_ERR(g_pcie_drv.class);
		g_pcie_drv.class = NULL;
		init_failed = true;
		goto AMBA_PCI_PLATFORM_INIT_EXIT;
	}

	cdev_init(&g_pcie_drv.ctrl_cdev, &pcie_fops);
	g_pcie_drv.ctrl_cdev.owner = THIS_MODULE;
	ret = cdev_add(&g_pcie_drv.ctrl_cdev, g_pcie_drv.ctrl_devt, 1);
	if (ret) {
		init_failed = true;
		goto AMBA_PCI_PLATFORM_INIT_EXIT;
	}
	cdev_registered = true;

	g_pcie_drv.ctrl_dev = device_create(g_pcie_drv.class, NULL, g_pcie_drv.ctrl_devt, NULL,
				      DRIVER_NAME);
	if (IS_ERR(g_pcie_drv.ctrl_dev)) {
		ret = PTR_ERR(g_pcie_drv.ctrl_dev);
		g_pcie_drv.ctrl_dev = NULL;
		init_failed = true;
		goto AMBA_PCI_PLATFORM_INIT_EXIT;
	}

	g_pcie_drv.proc_root = proc_mkdir(DRIVER_NAME, NULL);

	g_pcie_drv.pci_nb.notifier_call = pcie_pci_bus_notify;
	ret = bus_register_notifier(&pci_bus_type, &g_pcie_drv.pci_nb);
	if (ret) {
		init_failed = true;
		goto AMBA_PCI_PLATFORM_INIT_EXIT;
	}
	notifier_registered = true;

	pcie_enumerate_existed_devs();

AMBA_PCI_PLATFORM_INIT_EXIT:
	if (init_failed) {
		if (notifier_registered) {
			bus_unregister_notifier(&pci_bus_type, &g_pcie_drv.pci_nb);
		}
		if (g_pcie_drv.proc_root) {
			proc_remove(g_pcie_drv.proc_root);
			g_pcie_drv.proc_root = NULL;
		}
		if (g_pcie_drv.ctrl_dev) {
			device_destroy(g_pcie_drv.class, g_pcie_drv.ctrl_devt);
			g_pcie_drv.ctrl_dev = NULL;
		}
		if (cdev_registered) {
			cdev_del(&g_pcie_drv.ctrl_cdev);
		}
		if (g_pcie_drv.class) {
			class_destroy(g_pcie_drv.class);
			g_pcie_drv.class = NULL;
		}
		if (chrdev_registered) {
			unregister_chrdev_region(g_pcie_drv.devt_base, 1);
		}
		mutex_destroy(&g_pcie_drv.lock);
	}

	return ret;
}

static void __exit amba_pci_platform_exit(void)
{
	int i = 0;

	bus_unregister_notifier(&pci_bus_type, &g_pcie_drv.pci_nb);

	for (i = 0; i < AMBA_PCI_DEV_MAX; i++)
		cancel_delayed_work_sync(&g_pcie_drv.devs[i].boot_timeout_work);

	mutex_lock(&g_pcie_drv.lock);
	for (i = 0; i < AMBA_PCI_DEV_MAX; i++) {
		pcie_release_dev(&g_pcie_drv.devs[i]);
	}
	g_pcie_drv.next_dev_id = 0U;
	mutex_unlock(&g_pcie_drv.lock);

	if (g_pcie_drv.proc_root) {
		proc_remove(g_pcie_drv.proc_root);
		g_pcie_drv.proc_root = NULL;
	}
	if (g_pcie_drv.ctrl_dev) {
		device_destroy(g_pcie_drv.class, g_pcie_drv.ctrl_devt);
		g_pcie_drv.ctrl_dev = NULL;
	}
	cdev_del(&g_pcie_drv.ctrl_cdev);
	if (g_pcie_drv.class) {
		class_destroy(g_pcie_drv.class);
		g_pcie_drv.class = NULL;
	}
	unregister_chrdev_region(g_pcie_drv.devt_base, 1);
	mutex_destroy(&g_pcie_drv.lock);

	return;
}

module_init(amba_pci_platform_init);
module_exit(amba_pci_platform_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("amba_pci_platform: PCI card lifecycle, proc nodes, ioctl FW xfer (Linux 6.12)");
MODULE_ALIAS("amba_pci_platform");
MODULE_AUTHOR("Ambarella BSP");
