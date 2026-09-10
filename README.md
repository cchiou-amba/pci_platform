# Ambarella PCI Platform Kernel Driver

This repository packages the out-of-tree Ambarella PCI Platform
(`amba_pci_platform.ko`) kernel driver and inter-SoC PCIe communication
headers for Ambarella CV3 / N1-655 SoC platforms on EVE-OS.

---

## What the Kernel Module Does

The `amba_pci_platform.ko` module provides high-speed inter-processor
communication over PCIe between N1-655 and companion processors:

- **PCIe Endpoint & Host Bridge**: Establishes high-throughput,
  low-latency communication channels across PCIe root complex and
  endpoint interconnects.
- **DMA Transfer Engine**: Implements scatter-gather DMA transfers
  (`pci-sg.h`) and remote DMA (`pci_rdma_ioctl.h`) for zero-copy bulk
  data exchange between host memory and device memory.
- **Shared Memory Windows**: Manages inbound and outbound PCIe memory
  translation windows and address mapping tables.
- **VirtIO & IPC Transport**: Implements the transport layer for
  inter-SoC virtual Ethernet, RPC messaging, and accelerated network
  device emulation (`pci-virtio.h`, `pci-buzz.h`).
- **Character Device & Control**: Exposes `/dev/amba_pci_platform` with
  ioctl controls defined in `amba_pci_ioctl.h` for channel setup,
  buffer registration, and interrupt notification.

---

## Directory Layout

- `amba_pci_platform_main.c`: Driver initialization, probe, and remove
- `amba_pci_platform_table.c`: Address translation and channel tables
- `amba_pci_platform_transfer.c`: Scatter-gather DMA transfer logic
- `include/`: Communication headers:
  - `amba_pci_ioctl.h`: Ioctl interface and command structures
  - `pci-sg.h`: Scatter-gather memory management definitions
  - `pci_rdma_ioctl.h`: Remote DMA ioctl interfaces
  - `pci-virtio.h`: Virtual I/O inter-SoC interface definitions
  - `pci-buzz.h`: Inter-processor messaging protocol definitions

---

## Building the Kernel Module

To compile `amba_pci_platform.ko` out-of-tree against the prepared EVE
kernel headers:

```bash
make KDIR=/path/to/linux-headers \
     ARCH=arm64 \
     CROSS_COMPILE=aarch64-linux-gnu-
```

The resulting kernel module will be generated at:
`amba_pci_platform.ko`

To clean build artifacts:

```bash
make clean
```

---

## Deployment to EVE-OS

1. Copy the compiled module to the target node:

```bash
scp amba_pci_platform.ko <node>:/persist/modules/
```

2. Load the driver on the target node:

```bash
insmod /persist/modules/amba_pci_platform.ko
```
