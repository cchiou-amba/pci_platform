// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Ambarela, Inc.
 */
#ifndef __PCI_TURBO_IOC_H__
#define __PCI_TURBO_IOC_H__

#include <linux/ioctl.h>

#if defined(__KERNEL__)
#include <linux/types.h>
#else
#include <stdint.h>
#endif

struct pci_turbo_req {
	uint64_t dev_addr;
	uint64_t host_addr;
	size_t size;
	uint32_t flags;		// TBD
};

struct pci_turbo_buf {
	size_t size;
	uint64_t host_addr;
	uint32_t flags;		// TBD
	long offset;		// set by driver
};

#define PCI_TURBO_REQ_H2D		_IO('p', 4)
#define PCI_TURBO_REQ_D2H		_IO('p', 5)
#define PCI_TURBO_BUF_ALLOC		_IO('p', 6)
#define PCI_TURBO_BUF_FREE		_IO('p', 7)
#define PCI_TURBO_BUF_SUBMIT		_IO('p', 8)

#endif /* __PCI_RDMA_IOC_H__ */
