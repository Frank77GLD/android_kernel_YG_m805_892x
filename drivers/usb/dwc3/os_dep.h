#ifndef _DWC_OS_DEP_H_
#define _DWC_OS_DEP_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 *
 * This file contains OS dependent structures.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/stat.h>
#include <linux/pci.h>

#include <linux/version.h>
#include <linux/platform_device.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
# include <linux/irq.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,21)
# include <linux/usb/ch9.h>
#else
# include <linux/usb_ch9.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
# include <linux/usb/gadget.h>
# include <linux/usb/otg.h>
#else
# include <linux/usb_gadget.h>
# include <linux/usb_otg.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
# include <asm/irq.h>
#endif


#define CTAG_S  "\x1b[1;33m"
#define CTAG_S_R  "\x1b[1;31m"
#define CTAG_E  "\x1b[0m"

#if 0
# include <asm/unaligned.h>
# include <asm/sizes.h>
# include <asm/param.h>
# include <asm/io.h>

# include <asm/arch/lm.h>
# include <asm/arch/irqs.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
typedef int gfp_t;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
# define IRQF_SHARED SA_SHIRQ
#endif

static __inline__
void *dev_get_dwc3data(const struct device *dev)
{
	return dev->platform_data;
}

static __inline__
void dev_set_dwc3data(struct device *dev, void *data)
{
	dev->platform_data = data;
}

#define platform_get_dwc3data(dev)		dev_get_dwc3data(&(dev)->dev)
#define platform_set_dwc3data(dev,data)	dev_set_dwc3data(&(dev)->dev, (data))

/** @name Error Codes */
#define DWC_E_INVALID		EINVAL
#define DWC_E_NO_MEMORY		ENOMEM
#define DWC_E_NO_DEVICE		ENODEV
#define DWC_E_NOT_SUPPORTED	EOPNOTSUPP
#define DWC_E_TIMEOUT		ETIMEDOUT
#define DWC_E_BUSY		EBUSY
#define DWC_E_AGAIN		EAGAIN
#define DWC_E_RESTART		ERESTART
#define DWC_E_ABORT		ECONNABORTED
#define DWC_E_SHUTDOWN		ESHUTDOWN
#define DWC_E_NO_DATA		ENODATA
#define DWC_E_DISCONNECT	ECONNRESET
#define DWC_E_UNKNOWN		EINVAL
#define DWC_E_NO_STREAM_RES	ENOSR
#define DWC_E_COMMUNICATION	ECOMM
#define DWC_E_OVERFLOW		EOVERFLOW
#define DWC_E_PROTOCOL		EPROTO
#define DWC_E_IN_PROGRESS	EINPROGRESS
#define DWC_E_PIPE		EPIPE
#define DWC_E_IO		EIO
#define DWC_E_NO_SPACE		ENOSPC

/** Compiler 'packed' attribute for structs */
#define UPACKED	__attribute__((__packed__))

/** Type for DMA addresses */
typedef dma_addr_t		dwc_dma_t;
#define DWC_DMA_ADDR_INVALID	(~(dwc_dma_t)0)

/**
 * The number of DMA Descriptors (TRBs) to allocate for each endpoint type.
 * NOTE: The driver currently supports more than 1 TRB for Isoc EPs only.
 * So the values for Bulk and Intr must be 1.
 */
#define DWC_NUM_BULK_TRBS	1
#define DWC_NUM_INTR_TRBS	1
#define DWC_NUM_ISOC_TRBS	256

#ifdef __cplusplus
}
#endif

#endif /* _DWC_OS_DEP_H_ */
