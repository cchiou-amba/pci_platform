/*
 * ambarella/drv_modules/platform/pci_platform/amba_pci_platform_transfer.c
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
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "amba_pci_platform_transfer.h"

static_assert(RH_BD_OFF == 24U);
static_assert(RH_DESC0_OFF == 80U);

const u32 pcie_fw_xfer_order[PCIE_FW_XFER_ORDER_CNT] = {
	AMBA_PCI_FW_IMAGE_DTB,
	AMBA_PCI_FW_IMAGE_ROOTFS,
	AMBA_PCI_FW_IMAGE_KERNEL,
};

const u32 pcie_fw_req_bitmap_mask =
	(1U << AMBA_PCI_FW_IMAGE_DTB) |
	(1U << AMBA_PCI_FW_IMAGE_ROOTFS) |
	(1U << AMBA_PCI_FW_IMAGE_KERNEL);

static int pcie_enable_mem_io_resource(struct pci_dev *pdev)
{
	int err = 0;

	err = pci_write_config_byte(pdev, PCI_COMMAND, 0x06);
	if (err) {
		goto PCIE_ENABLE_MEM_IO_RESOURCE_EXIT;
	}
	msleep(100);

	err = pci_write_config_byte(pdev, PCI_COMMAND, 0x07);
	if (err) {
		goto PCIE_ENABLE_MEM_IO_RESOURCE_EXIT;
	}
	msleep(100);

	err = pci_write_config_byte(pdev, PCI_COMMAND, 0x06);
	if (err) {
		goto PCIE_ENABLE_MEM_IO_RESOURCE_EXIT;
	}
	msleep(100);

	err = 0;

PCIE_ENABLE_MEM_IO_RESOURCE_EXIT:
	return err;
}

static int pcie_boot_bar_find(struct pci_dev *pdev, int *bar_out, resource_size_t *len_out)
{
	int i = 0, ret = -ENXIO;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (!(pci_resource_flags(pdev, i) & IORESOURCE_MEM))
			continue;
		if (!pci_resource_len(pdev, i))
			continue;
		*bar_out = i;
		*len_out = pci_resource_len(pdev, i);
		ret = 0;
		break;
	}

	return ret;
}

static int pcie_map_boot_bar(struct pcie_dev_entry *e)
{
	struct resource *res = NULL;
	int ret = 0;

	if (e->boot_bar < 0 || !e->boot_len) {
		ret = pcie_boot_bar_find(e->pdev, &e->boot_bar, &e->boot_len);
		if (ret)
			goto PCIE_MAP_BOOT_BAR_EXIT;
	}

	res = &e->pdev->resource[e->boot_bar];
	if ((res->flags & IORESOURCE_UNSET) || !res->parent ||
	    !pci_resource_start(e->pdev, e->boot_bar)) {
		ret = -ENXIO;
		goto PCIE_MAP_BOOT_BAR_EXIT;
	}

	ret = pcie_enable_mem_io_resource(e->pdev);
	if (ret)
		goto PCIE_MAP_BOOT_BAR_EXIT;

	e->boot_mem_base = pci_iomap(e->pdev, e->boot_bar, (unsigned long)e->boot_len);
	if (!e->boot_mem_base) {
		ret = -EIO;
		goto PCIE_MAP_BOOT_BAR_EXIT;
	}

PCIE_MAP_BOOT_BAR_EXIT:
	return ret;
}

int pcie_boot_bar_ensure_mapped(struct pcie_dev_entry *e)
{
	int attempt = 0, ret = 0;

	if (e->boot_mem_base) {
		return 0;
	}

	for (attempt = 0; attempt < PCIE_BOOT_BAR_MAP_RETRY_MAX; attempt++) {
		ret = pcie_map_boot_bar(e);
		if (ret == 0) {
			break;
		}
		if (ret != -ENXIO) {
			break;
		}
		msleep(PCIE_BOOT_BAR_MAP_RETRY_MS);
	}

	if (ret == -ENXIO) {
		pr_warn("%s: %s BAR not assigned after %d x %d ms\n",
			DRIVER_NAME, pci_name(e->pdev),
			PCIE_BOOT_BAR_MAP_RETRY_MAX, PCIE_BOOT_BAR_MAP_RETRY_MS);
		ret = -ETIMEDOUT;
	}

	return ret;
}

void pcie_boot_bar_unmap(struct pcie_dev_entry *e)
{
	if (!e->boot_mem_base) {
		return;
	}

	pci_iounmap(e->pdev, e->boot_mem_base);
	e->boot_mem_base = NULL;

	return;
}

static void __iomem *pcie_ring_ptr(void __iomem *base, size_t off)
{
	return (char __iomem *)base + off;
}

static u32 pcie_ring_r32(void __iomem *base, unsigned int off)
{
	return readl(pcie_ring_ptr(base, off));
}

static void pcie_ring_w32(void __iomem *base, unsigned int off, u32 v)
{
	writel(v, pcie_ring_ptr(base, off));

	return;
}

static void __iomem *pcie_ring_desc(void __iomem *base, u32 cur_avail, u32 num)
{
	u32 slot = 0;

	slot = cur_avail % num;

	return pcie_ring_ptr(base, RH_DESC0_OFF + (size_t)slot * 20U);
}

static void __iomem *pcie_ring_data_blk(void __iomem *base, u32 blkid)
{
	return pcie_ring_ptr(base, (size_t)(blkid + 1U) * (size_t)RING_BLOCK_SIZE);
}

static int pcie_ring_wait_u32(void __iomem *base, unsigned int off,
			      u32 want, bool equal, unsigned int timeout_ms)
{
	unsigned long deadline = 0;
	u32 val = 0;
	int ret = -ETIMEDOUT;

	deadline = jiffies + msecs_to_jiffies(timeout_ms);

	while (time_before(jiffies, deadline)) {
		val = pcie_ring_r32(base, off);
		if (equal ? (val == want) : (val != want)) {
			ret = 0;
			goto PCIE_RING_WAIT_U32_EXIT;
		}
		usleep_range(1000, 2000);
		cond_resched();
	}

PCIE_RING_WAIT_U32_EXIT:
	return ret;
}

static int pcie_ring_xfer_poll(void __iomem *base, unsigned int timeout_ms)
{
	unsigned long deadline = 0;
	u32 used = 0, num = 0, avail = 0;
	int ret = -ETIMEDOUT;

	deadline = jiffies + msecs_to_jiffies(timeout_ms);

	while (time_before(jiffies, deadline)) {
		used = pcie_ring_r32(base, RH_USED_OFF);
		num = pcie_ring_r32(base, RH_NUM_OFF);
		avail = pcie_ring_r32(base, RH_AVAIL_OFF);

		if (used + num == avail) {
			ret = 0;
			goto PCIE_RING_XFER_POLL_EXIT;
		}
		usleep_range(1000, 2000);
		cond_resched();
	}

PCIE_RING_XFER_POLL_EXIT:
	return ret;
}

void pcie_ring_read_board_info(void __iomem *info_base,
			       struct pci_dev_board_info *bd)
{
	void __iomem *bd_io = NULL;
	unsigned int i = 0;
	u32 *dst = NULL;

	bd_io = pcie_ring_ptr(info_base, RH_BD_OFF);
	dst = (u32 *)bd;

	for (i = 0; i < sizeof(*bd) / sizeof(u32); i++)
		dst[i] = readl(bd_io + i * sizeof(u32));

	bd->dram_type[sizeof(bd->dram_type) - 1] = '\0';
	bd->date[sizeof(bd->date) - 1] = '\0';

	return;
}

int pcie_ring_wait_ep_ready(struct pci_dev *pdev, void __iomem *ring_base)
{
	int ret = 0;

	ret = pcie_ring_wait_u32(ring_base, RH_MAGIC_OFF, RING_MAGIC, true, 5000);
	if (ret) {
		dev_err(&pdev->dev, "ring: timeout magic\n");
		goto PCIE_RING_WAIT_EP_READY_EXIT;
	}
	ret = pcie_ring_wait_u32(ring_base, RH_STATE_OFF, RING_STATE_ONLINE, true, 5000);
	if (ret) {
		dev_err(&pdev->dev, "ring: timeout online\n");
		goto PCIE_RING_WAIT_EP_READY_EXIT;
	}
	ret = 0;

PCIE_RING_WAIT_EP_READY_EXIT:
	return ret;
}

static u64 pcie_fw_load_addr(u32 type)
{
	switch (type) {
	case AMBA_PCI_FW_IMAGE_DTB:
		return AMBA_PCI_FW_DTB_LOAD_ADDR;
	case AMBA_PCI_FW_IMAGE_ROOTFS:
		return AMBA_PCI_FW_ROOTFS_LOAD_ADDR;
	case AMBA_PCI_FW_IMAGE_KERNEL:
		return AMBA_PCI_FW_KERNEL_LOAD_ADDR;
	default:
		return 0;
	}
}

struct pcie_ring_ctx {
	void __iomem *base;
	u32 num;
	u32 max_blk;
	u32 cur_avail;
	u32 cur_blk;
};

static int pcie_ring_ctx_init(struct pci_dev *pdev, void __iomem *ring_base,
			      size_t ring_map_size, struct pcie_ring_ctx *ctx)
{
	u32 num = 0, max_blk = 0;

	num = pcie_ring_r32(ring_base, RH_NUM_OFF);
	max_blk = pcie_ring_r32(ring_base, RH_MAX_BLK_OFF);
	if (!num || !max_blk)
		return -EINVAL;
	if ((u64)(max_blk + 1U) * (u64)RING_BLOCK_SIZE > (u64)ring_map_size) {
		dev_err(&pdev->dev, "ring: map too small (need >= %u B)\n",
			(max_blk + 1U) * RING_BLOCK_SIZE);
		return -EINVAL;
	}

	ctx->base = ring_base;
	ctx->num = num;
	ctx->max_blk = max_blk;
	ctx->cur_avail = pcie_ring_r32(ring_base, RH_AVAIL_OFF) - num;
	ctx->cur_blk = 0;
	return 0;
}

static int pcie_ring_post_data(struct pci_dev *pdev, struct pcie_ring_ctx *ctx,
			       const void *chunk, u32 csz, u64 ep_addr)
{
	void __iomem *desc = NULL, *dst = NULL;
	int ret = 0;

	ret = pcie_ring_wait_u32(ctx->base, RH_AVAIL_OFF, ctx->cur_avail, false, 5000);
	if (ret) {
		dev_err(&pdev->dev, "ring: timeout avail\n");
		goto PCIE_RING_POST_DATA_EXIT;
	}

	desc = pcie_ring_desc(ctx->base, ctx->cur_avail, ctx->num);
	pcie_ring_w32(desc, 0, RING_CMD_DATA);
	pcie_ring_w32(desc, 4, csz);
	pcie_ring_w32(desc, 8, (u32)(ep_addr & 0xffffffffULL));
	pcie_ring_w32(desc, 12, (u32)(ep_addr >> 32));
	pcie_ring_w32(desc, 16, ctx->cur_blk % ctx->max_blk);

	dst = pcie_ring_data_blk(ctx->base, ctx->cur_blk % ctx->max_blk);
	memcpy_toio(dst, chunk, csz);

	ctx->cur_blk++;
	pcie_ring_w32(ctx->base, RH_USED_OFF,
		      pcie_ring_r32(ctx->base, RH_USED_OFF) + 1U);
	ctx->cur_avail++;
	ret = 0;

PCIE_RING_POST_DATA_EXIT:
	return ret;
}

static int pcie_ring_post_cmd(struct pci_dev *pdev, struct pcie_ring_ctx *ctx,
			      u32 cmd, u32 size, u64 addr)
{
	void __iomem *desc = NULL;
	int ret = 0;

	ret = pcie_ring_wait_u32(ctx->base, RH_AVAIL_OFF, ctx->cur_avail, false, 5000);
	if (ret) {
		dev_err(&pdev->dev, "ring: timeout avail\n");
		goto PCIE_RING_POST_CMD_EXIT;
	}

	desc = pcie_ring_desc(ctx->base, ctx->cur_avail, ctx->num);
	pcie_ring_w32(desc, 0, cmd);
	pcie_ring_w32(desc, 4, size);
	pcie_ring_w32(desc, 8, (u32)(addr & 0xffffffffULL));
	pcie_ring_w32(desc, 12, (u32)(addr >> 32));
	pcie_ring_w32(desc, 16, 0);
	pcie_ring_w32(ctx->base, RH_USED_OFF,
		      pcie_ring_r32(ctx->base, RH_USED_OFF) + 1U);
	ctx->cur_avail++;
	ret = 0;

PCIE_RING_POST_CMD_EXIT:
	return ret;
}

/*
 * Stream one in-memory image in <=512B chunks (IOCTL path).
 * [img->offset, img->offset + img->length) are byte offsets within the fw blob.
 */
int pcie_ring_xfer_user(struct pci_dev *pdev, void __iomem *ring_base,
			       size_t ring_map_size,
			       const u8 __user *user_base,
			       const struct amba_pci_fw_image_desc *img)
{
	u64 load_addr = 0, data_len = 0, src_off = 0;
	u64 ep_pos = 0, total = 0, tail_addr = 0;
	u32 csz = 0, cmd = 0, tail_size = 0;
	u8 *chunk = NULL;
	int ret = 0;
	struct pcie_ring_ctx ring = {};
	size_t nfile = 0;

	src_off = img->offset;
	data_len = img->length;

	load_addr = pcie_fw_load_addr(img->type);

	chunk = kmalloc(RING_BLOCK_SIZE, GFP_KERNEL);
	if (!chunk) {
		ret = -ENOMEM;
		goto out_free;
	}

	total = (data_len + 0xFULL) & ~(u64)0xFULL;

	ret = pcie_ring_ctx_init(pdev, ring_base, ring_map_size, &ring);
	if (ret)
		goto out_free;

	for (ep_pos = 0; ep_pos < total; ) {
		csz = (u32)min_t(u64, (u64)RING_BLOCK_SIZE, total - ep_pos);
		nfile = 0;

		if (ep_pos >= data_len) {
			memset(chunk, 0, csz);
		} else {
			nfile = (size_t)min_t(u64, (u64)csz, data_len - ep_pos);
			if (copy_from_user(chunk, user_base + src_off + ep_pos, nfile)) {
				ret = -EFAULT;
				goto out_free;
			}
			if (nfile < csz) {
				memset(chunk + nfile, 0, csz - nfile);
			}
		}

		ret = pcie_ring_post_data(pdev, &ring, chunk, csz, load_addr + ep_pos);
		if (ret)
			goto out_free;
		ep_pos += csz;
	}

	cmd = 0;
	tail_addr = load_addr;
	tail_size = 0;

	switch (img->type) {
	case AMBA_PCI_FW_IMAGE_KERNEL:
		cmd = RING_CMD_SET_LNX_ADDR;
		break;
	case AMBA_PCI_FW_IMAGE_DTB:
		cmd = RING_CMD_SET_DTB_ADDR;
		break;
	case AMBA_PCI_FW_IMAGE_ROOTFS:
		cmd = RING_CMD_SET_INITRD_ADDR;
		tail_size = (u32)data_len;
		break;
	}

	ret = pcie_ring_post_cmd(pdev, &ring, cmd, tail_size, tail_addr);
	if (ret)
		goto out_free;

	ret = pcie_ring_xfer_poll(ring_base, 5000);
	if (ret) {
		dev_err(&pdev->dev, "ring: xfer_poll timeout\n");
	}

out_free:
	kfree(chunk);
	return ret;
}

const struct amba_pci_fw_image_desc *
pcie_fw_find_image(const struct amba_pci_fw_xfer *req, u32 type)
{
	unsigned int i = 0;

	for (i = 0; i < AMBA_PCI_FW_IMAGE_MAX; i++) {
		if (req->images[i].type == type)
			return &req->images[i];
	}

	return NULL;
}
