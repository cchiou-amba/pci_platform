// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Ambarela, Inc.
 */
#ifndef __PCI_RDMA_IOC_H__
#define __PCI_RDMA_IOC_H__

#include <linux/ioctl.h>

#if defined(__KERNEL__)
#include <linux/types.h>
#else
#include <stdint.h>
#endif

struct pci_rdma_req {
	uint64_t src;
	uint64_t dest;
	size_t size;
};

struct pci_rdma_rate {
	uint64_t max;
	uint64_t min;
	uint64_t avg;
};

#define PCI_RDMA_INBOUND		_IO('p', 0)
#define PCI_RDMA_OUTBOUND		_IO('p', 1)
#define PCI_RDMA_INBOUND_RATE		_IO('p', 2)
#define PCI_RDMA_OUTBOUND_RATE		_IO('p', 3)

/* backward compat */
#define PCI_RDMA_IOC_INBOUND		PCI_RDMA_INBOUND
#define PCI_RDMA_IOC_OUTBOUND		PCI_RDMA_OUTBOUND
#define PCI_RDMA_IOC_INBOUND_RATE	PCI_RDMA_INBOUND_RATE
#define PCI_RDMA_IOC_OUTBOUND_RATE	PCI_RDMA_OUTBOUND_RATE

#endif /* __PCI_RDMA_IOC_H__ */
