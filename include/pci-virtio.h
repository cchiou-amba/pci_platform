// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Ambarela, Inc.
 */

#ifndef __PCI_VIRTIO_H__
#define __PCI_VIRTIO_H__
#include <linux/version.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/crc32.h>
#include <linux/mod_devicetable.h>
#include <linux/device/bus.h>
#include <linux/dma-mapping.h>
#include <linux/timer.h>
#include <linux/idr.h>
#include "pci-sg.h"

#define PCI_MAX_CHUNK_SIZE	SZ_2M
#define PCI_VIRTIO_CMD(c, a)	[c] = {.name= #c, .cmd=c, .attr=(a)}
#define pv_dev_to_drv(pdev)		container_of((pdev)->dev.driver, struct pci_virtio_driver, drv)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0)
#define PDE_DATA               pde_data
#define netif_rx_ni            netif_rx
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
#ifndef del_timer_sync
#define del_timer_sync(t)	timer_delete_sync(t)
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 16, 0)
#ifndef from_timer
#define from_timer(var, callback_timer, timer_fieldname) \
	timer_container_of(var, callback_timer, timer_fieldname)
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
#ifndef ida_simple_get
#define ida_simple_get(ida, start, end, gfp) \
	ida_alloc_range((ida), (start), (unsigned int)((end) - 1), (gfp))
#endif
#ifndef ida_simple_remove
#define ida_simple_remove(ida, id)	ida_free((ida), (id))
#endif
#endif

enum pci_role {
	PCI_ROLE_EP,
	PCI_ROLE_RC,
};

enum pci_cmd {
	PCI_CMD_HEART_BEAT,
	PCI_CMD_CHAN_CREATE,
	PCI_CMD_CHAN_DESTROY,
	PCI_CMD_TEST_OB,
	PCI_CMD_TEST_IB,
	PCI_CMD_XFER_DATA,
	PCI_CMD_XFER_DATA2,
	PCI_CMD_RC_TO_EP_DIRECT_IO,
	PCI_CMD_QUERY_INFO,
	PCI_CMD_SET_NETWORK,
	PCI_CMD_EP_REBOOT,
	PCI_CMD_SG_TABLE,
};

enum pci_cmd_attr {
	CMD_ATTR_RSVD 		= BIT(0),		/* RSVD */
	CMD_ATTR_TXBUF_FIXED 	= BIT(1),		/* use pre-alloc buffer to xfer */
	CMD_ATTR_EPADDR_VALID	= BIT(2),		/* use given phys addr as recv buffer */
	CMD_ATTR_EP_IB 		= BIT(3),		/* EP Inbound */
	CMD_ATTR_EP_OB 		= BIT(3),		/* EP Outbound */
};

enum pci_ctrl_flag {
	PCI_F_CHKSUM 	= BIT(0),
	PCI_F_NACK	= BIT(1),
	PCI_F_FS	= BIT(2),
	PCI_F_LS	= BIT(3),
	PCI_F_CHKDC 	= BIT(4),
	PCI_F_SYNC_DATA	= BIT(5),
	PCI_F_DMA_SG	= BIT(6),
	PCI_F_DIR_D2H	= BIT(7),
	PCI_F_DMA_IO	= BIT(8),
};

enum pci_status {
	PCI_S_OKAY = BIT(0),
	PCI_S_BUSY = BIT(1),
	PCI_S_ACK  = BIT(2),
	PCI_S_SYNC = BIT(3),
	PCI_S_DROP = BIT(5),
	PCI_S_6	   = BIT(6),
	PCI_S_7    = BIT(7),
	PCI_S_8    = BIT(8),
	PCI_S_9    = BIT(9),
	PCI_S_10   = BIT(10),
	PCI_S_11   = BIT(11),
	PCI_S_12   = BIT(12),
	PCI_S_13   = BIT(13),
	PCI_S_14   = BIT(14),
	PCI_S_15   = BIT(15),
};

enum pci_xfer_dir {
	PCI_XFER_RX,
	PCI_XFER_TX,
};

enum pci_cap {
	PCI_CAP_DMA_SG = BIT(0),
};

struct pci_xmit_req {
	u32 src;
	u32 dst;
	u32 cmd;
	u32 flag;
	u32 check_dc;		/* check data corruption */

	void *buf_virt;
	dma_addr_t buf_phys;
	u64 ep_addr;
	u32 size;
};

struct pci_xmit_req2 {
       phys_addr_t src;
       phys_addr_t dst;
       u32 size;
       u32 flags;
};

struct pci_virtio_device {
	void *pci_data;
	void *drv_data;
	struct device *dma_dev;
	struct device dev;
	struct of_device_id id;
	struct pci_virtio_xfer_ops *xfer_ops;
	u32 src;
	u32 dst;
	char init_name[32];
	char bdf_of_rc[32];
	struct list_head list;
	struct pci_sg_table *sgt;
	dma_addr_t dma_addr_sg;
	struct mutex sgt_mtx;
};

struct pci_virtio_xfer_ops {
	enum pci_role role;
	int (*xmit)(void *handle, struct pci_xmit_req *req);
	/* EP only: e.g., read a jpg with splice method, and transfer it to V4L2's sgl */
	int (*ep_sg2sg)(void *handle, struct scatterlist *sg, int nents, dma_addr_t pci_sg_addr);
	/* EP only: e.g., fetch a frame from IAV, and transfer it to V4L2's sgl */
	int (*ep_mem2sg)(void *handle, phys_addr_t phys, size_t size, dma_addr_t pci_sg_addr);
	int (*inbound)(void *handle, struct pci_xmit_req2 *req);
	int (*outbound)(void *handle, struct pci_xmit_req2 *req);
	int (*align_size)(void *handle);
	struct device * (*get_device)(void *handle);
};

struct pci_virtio_bus {
	struct pci_virtio_xfer_ops *xfer_ops;
	struct bus_type *bus;
	struct mutex mtx;
	struct list_head master_list;
	struct list_head device_list;
	spinlock_t lock;
};

struct pci_virtio_master {
	void *pci_data;
	struct list_head list;
};

struct pci_virtio_channel {
	u32 src;
	u32 dst;
};

struct pci_rx_cb {
	int (*ep)(void *priv, u32 src, enum pci_cmd cmd, void *data, dma_addr_t phys,
		  size_t len, u32 flag, void *arg);
	int (*rc)(void *priv, u32 src, enum pci_cmd cmd, void *data, dma_addr_t phys,
		  size_t len, u32 flag, void *arg);

};

struct pci_virtio_driver {
	struct device_driver drv;
	const struct of_device_id *id_table;
	int (*probe)(struct pci_virtio_device *dev);
	void (*remove)(struct pci_virtio_device *dev);
	struct pci_rx_cb cb;
	const struct resource *res;
	struct list_head dev_list;
	uint32_t cap;
};

struct pci_virtio_cmd {
	const char *name;
	enum pci_cmd cmd;
	u32 attr;
};

static inline const char *pci_virtio_role_name(struct pci_virtio_device *dev)
{
	return dev->xfer_ops->role == PCI_ROLE_EP ? "EP" : "RC";
}

static inline bool pci_virtio_role_ep(struct pci_virtio_device *dev)
{
	return dev->xfer_ops->role == PCI_ROLE_EP ? true : false;
}

static inline u64 xfer_calc_rate(struct device *dev,
			  enum pci_xfer_dir dir,
			  u64 size, int nr_chan,
			  struct timespec64 ts)
{
	u64 rate = size * NSEC_PER_SEC / 1024 / 1024, ns;

	ns = timespec64_to_ns(&ts);
	if (!ns)
		return 0;

	/* calculate the rate */
	do_div(rate, ns);

	return rate;
}

static inline void xfer_checksum(struct device *dev, const char *label, void *buf_virt, size_t size)
{
#if 0	/* TODO */
	void *data;
	int remain_size, chunk_size;

	data = buf_virt;
	remain_size = size;

	while (remain_size > 0) {
		chunk_size = remain_size > SZ_16K ? SZ_16K : remain_size;
		dev_dbg(dev, "%08lx: %x\n", size - remain_size,
			crc32_be(~0, data, chunk_size));
		remain_size -= chunk_size;
		data += chunk_size;
	}
#endif
}

static inline bool resource_check(const struct resource *res, u64 addr)
{
       if (addr < res->start || addr >= res->end)
               return false;

       return true;
}

static inline bool req_need_wait_ack(struct pci_xmit_req *req)
{
	return req->flag & PCI_F_NACK ? false : true;
}

static inline bool req_need_sync_data(struct pci_xmit_req *req)
{
	return req->flag & PCI_F_SYNC_DATA ? true : false;
}

int pci_virtio_device_register(struct pci_virtio_device *pvdev);
int pci_virtio_driver_register(struct pci_virtio_driver *pvdrv, struct module *owner);
void pci_virtio_driver_unregister(struct pci_virtio_driver *pvdrv);
int pci_virtio_bus_create(struct pci_virtio_xfer_ops *ops);
void pci_virtio_bus_destroy(void);
int pci_virtio_channel_create(void *priv, struct device *dev, u32 src,
			      u32 dst, char *devname);
struct pci_virtio_device *pci_virtio_find_dev(u32 id);
int pci_virtio_xmit_data(struct pci_virtio_device *pdev, void *virt,
			 dma_addr_t phys, size_t len);
int pci_virtio_xmit_data2(struct pci_virtio_device *pdev, void *virt,
			 dma_addr_t phys, size_t len);
struct pci_virtio_cmd *pci_virtio_get_cmd(enum pci_cmd cmd);
void pci_virtio_master_register(struct pci_virtio_master *master);
void pci_virtio_master_unregister(struct pci_virtio_master *master);
int pci_virtio_dma_inbound(struct pci_virtio_device *pdev,
			   phys_addr_t src, phys_addr_t dst, size_t size);
int pci_virtio_dma_outbound(struct pci_virtio_device *pdev,
			   phys_addr_t src, phys_addr_t dst, size_t size);
int pci_virtio_align_size(struct pci_virtio_device *pdev);
int pci_virtio_channel_sg_table(void *pci_data, struct device *dev,
				u32 addr, u32 dst, dma_addr_t sg);
int ep_xmit_sg2sg(struct pci_virtio_device *pdev, struct scatterlist *sgl, int nents);
int ep_xmit_mem2sg(struct pci_virtio_device *pdev, void *virt, phys_addr_t phys, size_t size);
int ep_xmit_sg_done(struct pci_virtio_device *pdev);

int pci_virtio_xmit_sg(struct pci_virtio_device *pdev, struct scatterlist *sgl, int nents, phys_addr_t phys, bool dir_h2d);
int pci_virtio_submit_sg_table(struct pci_virtio_device *pdev, struct pci_sg_array *sg, int n);
void pci_virtio_close_sg_table(struct pci_virtio_device *pdev);
size_t pci_virtio_sg_avail_size(struct pci_virtio_device *pdev);
int pci_virtio_dma_io(struct pci_virtio_device *pdev, phys_addr_t host_addr, phys_addr_t device_addr, size_t size, bool dir_h2d);
#endif
