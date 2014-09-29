/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/DWC_usb3/driver/driver.c $
 * $Revision: #61 $
 * $Date: 2012/01/24 $
 * $Change: 1925313 $
 *
 * Synopsys SS USB3 Linux Software Driver and documentation (hereinafter,
 * "Software") is an Unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you.
 *
 * The Software IS NOT an item of Licensed Software or Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Product
 * with Synopsys or any supplement thereto. You are permitted to use and
 * redistribute this Software in source and binary forms, with or without
 * modification, provided that redistributions of source code must retain this
 * notice. You may not view, use, disclose, copy or distribute this file or
 * any information contained herein except pursuant to this license grant from
 * Synopsys. If you do not agree with this notice, including the disclaimer
 * below, then you are not authorized to use the Software.
 *
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================== */

/** @file
 * The driver module provides the initialization and cleanup entry
 * points for the DWC_usb3 driver. This module will be dynamically installed
 * after Linux is booted using the insmod command. When the module is
 * installed, the driver_init function is called. When the module is
 * removed (using rmmod), the driver_cleanup function is called.
 *
 * This module also defines a data structure for the driver, which is
 * used in conjunction with the standard pci_dev structure. These
 * structures allow the USB3 driver to comply with the standard Linux driver
 * model in which devices and drivers are registered with a bus driver. This
 * has the benefit that Linux can expose attributes of the driver and device
 * in its special sysfs file system. Users can then read or write files in
 * this file system to perform diagnostics on the driver components or the
 * device.
 */

#include "os_dep.h"
#include "hw.h"
#include "usb.h"
#include "pcd.h"
#include "driver.h"
#include "cil.h"
#include <linux/platform_device.h>
#include <mach/tca_ckc.h>
#include <mach/globals.h>
#include <mach/tcc_board_power.h>
#include <linux/clk.h>

#include <mach/reg_physical.h>
#include <mach/structures_hsio.h>
#include <asm/mach-types.h>

#define DWC_DRIVER_VERSION	"2.10a - 1/24/2012"
#define DWC_DRIVER_DESC		"SS USB3 Controller driver"

static const char dwc_driver_name[] = "dwc_usb3";

static unsigned usb3_instance_num;


static struct clk *clk_usb_otg = NULL;
static struct clk *clk_hsiocfg = NULL;

extern int tcc_usb30_vbus_init(void);
extern int tcc_usb30_vbus_exit(void);
extern int tcc_dwc3_vbus_ctrl(int on);

extern void tcc_usb30_phy_off(void);
extern void tcc_usb30_phy_on(void);

extern int tcc_usb30_clkset(int on);
/*-------------------------------------------------------------------------*/
/* Encapsulate the module parameter settings */

static dwc_usb3_core_params_t dwc_usb3_module_params = {
#if 1
	.burst = 0,
	.newcore = 1,
	.phy = 1,
	.wakeup = 0,
#ifdef DWC_STAR_9000446947_WORKAROUND
	.pwrctl = 0,
#else
# if defined(DWC_STAR_9000449814_WORKAROUND) || \
     defined(DWC_STAR_9000459034_WORKAROUND)
	.pwrctl = 2,
# else
	.pwrctl = 3,
# endif
#endif
	.u1u2ctl = 0,
	.lpmctl = 0,
	.phyctl = 0,
	.usb2mode = 0,
	.hibernate = 0,
	.hiberdisc = 1,
	.clkgatingen = 0,
	.ssdisquirk = 0,
	.nobos = 0,
	.loop = 0,
	.nump = 16,
	.rx = 0,
	.tx0 = 0, .tx1 = 0, .tx2 = 0, .tx3 = 0,

#else
	.burst = 0,
	.newcore = 1,
	.phy = 1,
	.wakeup = 0,
#ifdef DWC_STAR_9000446947_WORKAROUND
	.pwrctl = 0,
#else
# if defined(DWC_STAR_9000449814_WORKAROUND) || \
     defined(DWC_STAR_9000459034_WORKAROUND)
	.pwrctl = 2,
# else
	.pwrctl = 3,
# endif
#endif
	.u1u2ctl = 0,
	.lpmctl = 0,
	.phyctl = 0,
	.usb2mode = 2,
	.hibernate = 0,
	.hiberdisc = 1,
	.clkgatingen = 0,
	.ssdisquirk = 0,
	.nobos = 0,
	.loop = 0,
	.nump = 16,
	.rx = 0,
	.tx0 = 0, .tx1 = 0, .tx2 = 0, .tx3 = 0,
#endif
};

module_param_named(burst, dwc_usb3_module_params.burst, int, S_IRUGO);
MODULE_PARM_DESC(burst, "Enable gadget to set USB3 max burst size (0=no, 1=yes)");

module_param_named(new, dwc_usb3_module_params.newcore, int, S_IRUGO);
MODULE_PARM_DESC(new, "Force new core behavior [rev >= 1.09a] (0=no, 1=yes)");

module_param_named(phy, dwc_usb3_module_params.phy, int, S_IRUGO);
MODULE_PARM_DESC(phy, "Select PHY type (0=RocketIO, 1=TI, 2=Synopsys)");

module_param_named(wakeup, dwc_usb3_module_params.wakeup, int, S_IRUGO);
MODULE_PARM_DESC(wakeup, "Enable remote wakeup (0=no, 1=yes)");

module_param_named(pwrctl, dwc_usb3_module_params.pwrctl, int, S_IRUGO);
MODULE_PARM_DESC(pwrctl, "Enable U1/U2 power states (bit0=U1, bit1=U2)");

module_param_named(u1u2ctl, dwc_usb3_module_params.u1u2ctl, int, S_IRUGO);
MODULE_PARM_DESC(u1u2ctl, "Enable dynamic U1/U2 control (0=no, 1=yes)");

module_param_named(lpmctl, dwc_usb3_module_params.lpmctl, int, S_IRUGO);
MODULE_PARM_DESC(lpmctl, "Enable LPM power control (0=no, 1=AppL1Res-0, 2=AppL1Res-1)");

module_param_named(phyctl, dwc_usb3_module_params.phyctl, int, S_IRUGO);
MODULE_PARM_DESC(phyctl, "Enable PHY suspend (0=no, 1=yes)");

module_param_named(usb2mode, dwc_usb3_module_params.usb2mode, int, S_IRUGO);
MODULE_PARM_DESC(usb2mode, "Force USB2 mode (0=no, 1=full-speed, 2=high-speed)");

module_param_named(hibernate, dwc_usb3_module_params.hibernate, int, S_IRUGO);
MODULE_PARM_DESC(hibernate, "Enable hibernation mode (0=no, 1=yes)");

module_param_named(hiberdisc, dwc_usb3_module_params.hiberdisc, int, S_IRUGO);
MODULE_PARM_DESC(hiberdisc, "Enable hibernation on disconnect (0=no, 1=yes)");

module_param_named(clkgatingen, dwc_usb3_module_params.clkgatingen, int, S_IRUGO);
MODULE_PARM_DESC(clkgatingen, "Enable clock gating (0=no, 1=yes)");

module_param_named(ssdisquirk, dwc_usb3_module_params.ssdisquirk, int, S_IRUGO);
MODULE_PARM_DESC(ssdisquirk, "Enable SS_DIS Quirk (0=no, 1=yes)");

module_param_named(nobos, dwc_usb3_module_params.nobos, int, S_IRUGO);
MODULE_PARM_DESC(nobos, "Fail GetDescriptor(BOS) request at USB2 speeds (0=no, 1=yes)");

module_param_named(loop, dwc_usb3_module_params.loop, int, S_IRUGO);
MODULE_PARM_DESC(loop, "Number of times to loop in reset (for debug only)");

module_param_named(nump, dwc_usb3_module_params.nump, int, S_IRUGO);
MODULE_PARM_DESC(nump, "Set NUMP to given value (1-16)");

module_param_named(rx, dwc_usb3_module_params.rx, int, S_IRUGO);
MODULE_PARM_DESC(rx, "Size of Rx FIFO in bytes");

module_param_named(tx0, dwc_usb3_module_params.tx0, int, S_IRUGO);
MODULE_PARM_DESC(tx0, "Size of Tx FIFO 0 in bytes");

module_param_named(tx1, dwc_usb3_module_params.tx1, int, S_IRUGO);
MODULE_PARM_DESC(tx1, "Size of Tx FIFO 1 in bytes");

module_param_named(tx2, dwc_usb3_module_params.tx2, int, S_IRUGO);
MODULE_PARM_DESC(tx2, "Size of Tx FIFO 2 in bytes");

module_param_named(tx3, dwc_usb3_module_params.tx3, int, S_IRUGO);
MODULE_PARM_DESC(tx3, "Size of Tx FIFO 3 in bytes");


static ssize_t show_wakeup(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
#ifdef CONFIG_HITECH
	struct pci_dev *device = container_of(dev, struct pci_dev, dev);
	dwc_usb3_device_t *usb3_dev = pci_get_drvdata(device);
#endif
#ifdef CONFIG_IPMATE
	struct lm_device *device = container_of(dev, struct lm_device, dev);
	dwc_usb3_device_t *usb3_dev = lm_get_drvdata(device);
#endif
	struct platform_device *device = to_platform_device(dev);
	dwc_usb3_device_t *usb3_dev = platform_get_drvdata(device);

	return sprintf(buf, "%d\n", usb3_dev->pcd.wkup_rdy);
}

static ssize_t store_wakeup(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
#ifdef CONFIG_HITECH
	struct pci_dev *device = container_of(dev, struct pci_dev, dev);
	dwc_usb3_device_t *usb3_dev = pci_get_drvdata(device);
#endif
#ifdef CONFIG_IPMATE
	struct lm_device *device = container_of(dev, struct lm_device, dev);
	dwc_usb3_device_t *usb3_dev = lm_get_drvdata(device);
#endif
	struct platform_device *device = to_platform_device(dev);
	dwc_usb3_device_t *usb3_dev = platform_get_drvdata(device);
	
	int		ret;

	ret = dwc_usb3_wakeup(usb3_dev->pcd.gadget);

	return ret < 0 ? ret : count;
}

/* /sys/module/dwc_usb3/drivers/pci:dwc_usb3/nnnn:nn:nn.n/wakeup */
static DEVICE_ATTR(wakeup, 0666, show_wakeup, store_wakeup);

static ssize_t store_disrupt(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
#ifdef CONFIG_HITECH
	struct pci_dev *device = container_of(dev, struct pci_dev, dev);
	dwc_usb3_device_t *usb3_dev = pci_get_drvdata(device);
#endif
#ifdef CONFIG_IPMATE
	struct lm_device *device = container_of(dev, struct lm_device, dev);
	dwc_usb3_device_t *usb3_dev = lm_get_drvdata(device);
#endif
	struct platform_device *device = to_platform_device(dev);
	dwc_usb3_device_t *usb3_dev = platform_get_drvdata(device);
	
	unsigned long	tmp = 0;
	ssize_t		rc;
	unsigned long	flags;

	sscanf(buf, "%ld", &tmp);
	rc = strnlen(buf, count);
	printk(USB3_DWC "disrupt: %ld ms\n", tmp);

	spin_lock_irqsave(&usb3_dev->pcd.lock, flags);
	mdelay(tmp);
	spin_unlock_irqrestore(&usb3_dev->pcd.lock, flags);
	return rc;
}

/* /sys/module/dwc_usb3/drivers/pci:dwc_usb3/nnnn:nn:nn.n/disrupt */
static DEVICE_ATTR(disrupt, 0666, NULL, store_disrupt);

static ssize_t store_hiber(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
#ifdef CONFIG_HITECH
	struct pci_dev *device = container_of(dev, struct pci_dev, dev);
	dwc_usb3_device_t *usb3_dev = pci_get_drvdata(device);
#endif
#ifdef CONFIG_IPMATE
	struct lm_device *device = container_of(dev, struct lm_device, dev);
	dwc_usb3_device_t *usb3_dev = lm_get_drvdata(device);
#endif
	struct platform_device *device = to_platform_device(dev);
	dwc_usb3_device_t *usb3_dev = platform_get_drvdata(device);
	
	int		tmp = 0;
	ssize_t		rc;
	unsigned long	flags;

	sscanf(buf, "%d", &tmp);
	rc = strnlen(buf, count);
	printk(USB3_DWC "hibernate: save_state=%d\n", tmp);

	spin_lock_irqsave(&usb3_dev->pcd.lock, flags);
	dwc_enter_hibernation(&usb3_dev->pcd, tmp);
	spin_unlock_irqrestore(&usb3_dev->pcd.lock, flags);
	return rc;
}

/* /sys/module/dwc_usb3/drivers/pci:dwc_usb3/nnnn:nn:nn.n/hibernate */
static DEVICE_ATTR(hibernate, 0666, NULL, store_hiber);

static ssize_t store_restore(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
#ifdef CONFIG_HITECH
	struct pci_dev *device = container_of(dev, struct pci_dev, dev);
	dwc_usb3_device_t *usb3_dev = pci_get_drvdata(device);
#endif
#ifdef CONFIG_IPMATE
	struct lm_device *device = container_of(dev, struct lm_device, dev);
	dwc_usb3_device_t *usb3_dev = lm_get_drvdata(device);
#endif
	struct platform_device *device = to_platform_device(dev);
	dwc_usb3_device_t *usb3_dev = platform_get_drvdata(device);
	
	int		tmp = 0;
	ssize_t		rc;
	unsigned long	flags;

	sscanf(buf, "%d", &tmp);
	rc = strnlen(buf, count);
	printk(USB3_DWC "restore: restore_state=%d\n", tmp);

	spin_lock_irqsave(&usb3_dev->pcd.lock, flags);
	tmp = dwc_exit_hibernation(&usb3_dev->pcd, tmp);
	spin_unlock_irqrestore(&usb3_dev->pcd.lock, flags);
	printk(USB3_DWC "dwc_exit_hibernation() returned %d\n", tmp);
	return rc;
}

/* /sys/module/dwc_usb3/drivers/pci:dwc_usb3/nnnn:nn:nn.n/restore */
static DEVICE_ATTR(restore, 0666, NULL, store_restore);

#ifdef CONFIG_USB_OTG_DWC
static ssize_t store_srp(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct otg_transceiver *otg = otg_get_transceiver();

	if (!otg)
		return count;
	otg_start_srp(otg);
	otg_put_transceiver(otg);
	return count;
}
static DEVICE_ATTR(srp, 0222, NULL, store_srp);

extern void dwc_usb3_end_session(dwc_usb3_pcd_t *pcd);

static ssize_t store_end(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct otg_transceiver *otg = otg_get_transceiver();
	if (!otg)
		return count;
	otg->end_session(otg);
	otg_put_transceiver(otg);
	return count;
}
static DEVICE_ATTR(end, 0222, NULL, store_end);

extern void dwc_usb3_start_hnp(dwc_usb3_pcd_t *pcd);

static ssize_t store_hnp(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
#ifdef CONFIG_HITECH
	struct pci_dev *device = container_of(dev, struct pci_dev, dev);
	dwc_usb3_device_t *usb3_dev = pci_get_drvdata(device);
#endif
#ifdef CONFIG_IPMATE
	struct lm_device *device = container_of(dev, struct lm_device, dev);
	dwc_usb3_device_t *usb3_dev = lm_get_drvdata(device);
#endif
	struct platform_device *device = to_platform_device(dev);
	dwc_usb3_device_t *usb3_dev = platform_get_drvdata(device);

	if (usb3_dev->pcd.b_hnp_enable) {
		usb3_dev->pcd.b_hnp_enable = 0;
		usb3_dev->pcd.wants_host = 0;
		dwc_usb3_start_hnp(&usb3_dev->pcd);
	} else {
		usb3_dev->pcd.wants_host = 1;
		/* TODO if we don't receive the SET_FEATURE within 4 secs,
		 * reset this value
		 */
	}
	return count;
}
static DEVICE_ATTR(hnp, 0222, NULL, store_hnp);

extern void dwc_usb3_start_rsp(dwc_usb3_pcd_t *pcd);

static ssize_t store_rsp(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct otg_transceiver *otg = otg_get_transceiver();
	if (!otg)
		return count;
	otg->start_rsp(otg);
	otg_put_transceiver(otg);
	return count;
}

static DEVICE_ATTR(rsp, 0222, NULL, store_rsp);

#endif

#ifdef DWC_UTE
static void save_fifosiz_def_values(dwc_usb3_device_t *dev)
{
	unsigned i, size;
	dwc_usb3_pcd_t *pcd;

	if (!dev)
		return;

	pcd = &dev->pcd;

	for (i = 0; i < DWC_MAX_TX_FIFOS; i++) {
		size = dwc_rd32(dev, &dev->core_global_regs->gtxfifosiz[i]) &
							DWC_FIFOSZ_DEPTH_BITS;
		dwc_print2(dev, "Saving %d TxFIFO default size: %d\n", i, size);
		pcd->def_txf_size[i] = size;
	}

	size = dwc_rd32(dev, &dev->core_global_regs->grxfifosiz) &
							DWC_FIFOSZ_DEPTH_BITS;
	pcd->def_rxf_size = size;

	dwc_print1(dev, "Saving RxFIFO default size: %d\n", size);
}
#endif

/**
 * This function is the top level interrupt handler for the Common
 * (Core and Device) interrupts.
 */
static irqreturn_t dwc_usb3_common_irq(int irq, void *dev
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
				       , struct pt_regs *regs
#endif
				      )
{
	dwc_usb3_device_t *usb3_dev = dev;
	int retval = 0;

#ifdef CONFIG_USB_OTG_DWC
	uint32_t gsts = 0;

	/* Skip OTG IRQ handler if in hibernation */
	int temp = atomic_read(&usb3_dev->hibernate);
	if (temp < DWC_HIBER_SLEEPING)
		gsts = dwc_rd32(usb3_dev, &usb3_dev->core_global_regs->gsts);

	if (gsts & DWC_GSTS_OTG_EVT_PENDING_BIT ||
	    gsts & DWC_GSTS_ADP_EVT_PENDING_BIT ||
	    gsts & DWC_GSTS_BC_EVT_PENDING_BIT) {
		struct otg_transceiver *otg = otg_get_transceiver();

		printk("%s() OTG IRQ\n", __func__);
		printk("gsts = %08x\n", gsts);
		if (otg->irq)
			otg->irq(otg);
		otg_put_transceiver(otg);
	}

	if (temp >= DWC_HIBER_SLEEPING || (gsts & DWC_GSTS_DEV_EVT_PENDING_BIT))
#endif
	{
		spin_lock(&usb3_dev->pcd.lock);
		retval = dwc_usb3_irq(usb3_dev, irq);
		spin_unlock(&usb3_dev->pcd.lock);
	}

	return IRQ_RETVAL(retval);
}

/**
 * This function is called when a pci_dev is unregistered with the
 * dwc_usb3_driver. This happens, for example, when the rmmod command is
 * executed. The device may or may not be electrically present. If it is
 * present, the driver stops device processing. Any resources used on behalf
 * of this device are freed.
 *
 * @param dev pci_dev struct
 */
static void dwc_usb3_driver_remove(

	struct platform_device *dev
	)
{
	dwc_usb3_device_t *usb3_dev = platform_get_drvdata(dev);
	uint32_t *event_buf;
	dwc_dma_t event_buf_dma;
	//int irq = platform_get_irq(dev, 0);
	int irq = dev->resource[1].start;

	dev_dbg(&dev->dev, "%s(%p)\n", __func__, dev);

	if (!usb3_dev) {
		/* Memory allocation for the dwc_usb3_device failed */
		dev_dbg(&dev->dev, "%s: usb3_dev NULL\n", __func__);
		goto disable;
	}

	/*
	 * Free the IRQ
	 */
	if (usb3_dev->common_irq_installed) {
		usb3_dev->common_irq_installed = 0;
		free_irq(irq, usb3_dev);  //taejin TODO
	}

	if (usb3_dev->pcd_initialized) {
		usb3_dev->pcd_initialized = 0;
		dwc_usb3_remove(dev);
	}

	if (usb3_dev->cmn_initialized) {
		usb3_dev->cmn_initialized = 0;
		dwc_usb3_common_remove(usb3_dev);
	}

	event_buf = usb3_dev->event_buf[0];
	event_buf_dma = usb3_dev->event_buf_dma[0];
	if (event_buf) {
		dwc_usb3_dis_flush_eventbuf_intr(usb3_dev, 0);
		usb3_dev->event_buf[0] = NULL;
		dma_free_coherent(NULL, DWC_EVENT_BUF_SIZE, event_buf,
				  event_buf_dma);
	}

	if (usb3_dev->sysfs_initialized) {
		usb3_dev->sysfs_initialized = 0;
#ifdef CONFIG_USB_OTG_DWC
		device_remove_file(&dev->dev, &dev_attr_end);
		device_remove_file(&dev->dev, &dev_attr_srp);
		device_remove_file(&dev->dev, &dev_attr_rsp);
		device_remove_file(&dev->dev, &dev_attr_hnp);
#endif
		device_remove_file(&dev->dev, &dev_attr_restore);
		device_remove_file(&dev->dev, &dev_attr_hibernate);
		device_remove_file(&dev->dev, &dev_attr_disrupt);
		device_remove_file(&dev->dev, &dev_attr_wakeup);
	}

	/*
	 * Clear the drvdata pointer.
	 */
	platform_set_drvdata(dev, NULL);

	//if (usb3_dev->base)
	//	iounmap(usb3_dev->base);
	
	usb3_instance_num--;
	kfree(usb3_dev);

disable:
#ifdef CONFIG_HITECH
	//pci_disable_device(dev);
#endif
	return;
}

static void tcc_stop_dwc3(void)
{
   tcc_usb30_clkset(0);
}

static void tcc_start_dwc3(void)
{
   tcc_usb30_clkset(1);
}


#define TXVRT_SHIFT 6
#define TXVRT_MASK (0xF << TXVRT_SHIFT)
#define TXRISE_SHIFT 10
#define TXRISE_MASK (0x3 << TXRISE_SHIFT)

static void dwc_usb3_phy_init (void) {
	PUSBPHYCFG USBPHYCFG = (PUSBPHYCFG)tcc_p2v(HwUSBPHYCFG_BASE);

   unsigned int uTmp;

   if (!machine_is_m805_893x()) {
      tcc_dwc3_vbus_ctrl(1);
   }

   tcc_usb30_vbus_init();
   tcc_dwc3_vbus_ctrl(1);
   tcc_start_dwc3();
   tcc_usb30_phy_on();
	// Initialize All PHY & LINK CFG Registers
	USBPHYCFG->UPCR0 = 0xB5484068;
	USBPHYCFG->UPCR1 = 0x0000041C;
	USBPHYCFG->UPCR2 = 0x919E14C8;
	USBPHYCFG->UPCR3 = 0x4AB05D00;
	USBPHYCFG->UPCR4 = 0x00000000;
	USBPHYCFG->UPCR5 = 0x00108001;
	USBPHYCFG->LCFG	 = 0x80420013;
#if defined (CONFIG_TCC_USB_DRD)
   BITSET(USBPHYCFG->UPCR4, Hw20|Hw21); //ID pin interrupt enable
#endif
	USBPHYCFG->UPCR1 = 0x00000412;
	USBPHYCFG->LCFG	 = 0x86420013;

	// PHY Clock Setting (all are reset values)
	// USB3PHY_CFG0[26:21] : FSEL = 6'b101010 
	// USB3PHY_CFG0[28:27] : REFCLKSEL = 2'b10
	// USB3PHY_CFG0[29:29] : REF_SSP_EN = 1'b1
	// USB3PHY_CFG0[30:30] : REF_USE_PAD = 1'b0
	// USB3PHY_CFG0[31:31] : SSC_EN = 1'b1

	// Setting LINK Suspend clk dividor (all are reset values)
	//HwHSB_CFG->uUSB30LINK_CFG.bReg.SUSCLK_DIV_EN = 0x1;
	//HwHSB_CFG->uUSB30LINK_CFG.bReg.SUSCLK_DIV = 0x3; // busclk / (3+1) = 250 / 4 = 15.625MHz

   //HC DC controll
   BITCSET(USBPHYCFG->UPCR2, TXVRT_MASK, 0xC << TXVRT_SHIFT);

	// USB20 Only Mode
#if 0
	USBPHYCFG->LCFG	 |= Hw30;	//USB2.0 HS only mode
	USBPHYCFG->UPCR1 |= Hw9;	//test_powerdown_ssp
#else
	BITCLR(USBPHYCFG->LCFG, Hw30);
	BITCLR(USBPHYCFG->UPCR1,Hw9);

	//USBPHYCFG->UPCR1 |= Hw8; 	//test_powerdown_hsp
#endif
	// Wait USB30 PHY initialization finish. PHY FREECLK should be stable.
	while(1) {
		// GDBGLTSSM : Check LTDB Sub State is non-zero
		// When PHY FREECLK is valid, LINK mac_resetn is released and LTDB Sub State change to non-zero
		uTmp = readl(tcc_p2v(HwUSBDEVICE_BASE+0x164));
		uTmp = (uTmp>>18)&0xF;

		// Break when LTDB Sub state is non-zero and CNR is zero
		if (uTmp != 0){
			break;
        }
    }
}

/**
 * This function is called when a pci_dev is bound to a
 * dwc_usb3_driver. It creates the driver components required to
 * control the device (CIL, HCD, and PCD) and it initializes the
 * device. The driver components are stored in a dwc_usb3_device
 * structure. A reference to the dwc_usb3_device is saved in the
 * pci_dev. This allows the driver to access the dwc_usb3_device
 * structure on subsequent calls to driver methods for this device.
 *
 * @param dev pci_dev struct
 * @param id  pci_dev_id struct
 */
dwc_usb3_device_t *pusb3_dev;
static int dwc_usb3_driver_probe(struct platform_device *dev)
{
	dwc_usb3_device_t *usb3_dev;
	uint32_t addr_ofs = 0xc000;
	int retval = 0;
	struct resource *resource;
	int irq;
	unsigned int uDEVTEN;

   usb3_dev = kmalloc(sizeof(dwc_usb3_device_t), GFP_KERNEL);
   if (!usb3_dev) {
		dev_err(&dev->dev, "kmalloc of dwc_usb3_device failed!\n");
		retval = -ENOMEM;
		goto fail2;
	}
	memset(usb3_dev, 0, sizeof(*usb3_dev));
	
	dev_dbg(&dev->dev, "dwc_usb3_driver_probe(%p)\n", dev);

   pusb3_dev = usb3_dev;

	/* taejin
	 * Map the DWC_otg Core memory into virtual address space.
	 */
	resource = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!resource) {
		dev_err(&dev->dev, "no resource for DWC_otg Core Memory?\n");
		retval = -ENODEV;
		goto fail;
	}
	usb3_dev->base = (void *)resource->start;
	
	if (!usb3_dev->base) {
		dev_err(&dev->dev, "ioremap() failed\n");
		retval = -ENOMEM;
		goto fail;
	}
	//printk(KERN_CRIT "dwc_usb3 base=0x%08x\n", (unsigned)usb3_dev->base);
	dev_dbg(&dev->dev, "base=0x%08x\n", (unsigned)usb3_dev->base);

	usb3_dev->pcd.devnum = usb3_instance_num++;
	usb3_dev->reg_offset = 0xFFFFFFFF;

	/*
	 * Initialize driver data to point to the global DWC_usb3
	 * Device structure.
	 */
	usb3_dev->platform_dev = dev;

	platform_set_dwc3data(dev, usb3_dev);
	
	dev_dbg(&dev->dev, "dwc_usb3_device=0x%p\n", usb3_dev);

	if (!usb3_dev->base) {
		dev_err(&dev->dev, "ioremap_nocache() failed!\n");
		retval = -ENOMEM;
		goto fail;
	}

	dev_dbg(&dev->dev, "base=%p\n", usb3_dev->base);

	retval = device_create_file(&dev->dev, &dev_attr_wakeup);
	if (retval) {
		dev_err(&dev->dev, "sysfs initialization failed!\n");
		goto fail;
	}

	retval = device_create_file(&dev->dev, &dev_attr_disrupt);
	if (retval) {
		dev_err(&dev->dev, "sysfs initialization failed!\n");
		goto fail;
	}

	retval = device_create_file(&dev->dev, &dev_attr_hibernate);
	if (retval) {
		dev_err(&dev->dev, "sysfs initialization failed!\n");
		goto fail;
	}

	retval = device_create_file(&dev->dev, &dev_attr_restore);
	if (retval) {
		dev_err(&dev->dev, "sysfs initialization failed!\n");
		goto fail;
	}

#ifdef CONFIG_USB_OTG_DWC
	retval = device_create_file(&dev->dev, &dev_attr_hnp);
	if (retval) {
		dev_err(&dev->dev, "sysfs initialization failed!\n");
		goto fail;
	}

	retval = device_create_file(&dev->dev, &dev_attr_rsp);
	if (retval) {
		dev_err(&dev->dev, "sysfs initialization failed!\n");
		goto fail;
	}

	retval = device_create_file(&dev->dev, &dev_attr_srp);
	if (retval) {
		dev_err(&dev->dev, "sysfs initialization failed!\n");
		goto fail;
	}

	retval = device_create_file(&dev->dev, &dev_attr_end);
	if (retval) {
		dev_err(&dev->dev, "sysfs initialization failed!\n");
		goto fail;
	}
#endif
	usb3_dev->sysfs_initialized = 1;

	/*
	 * Attempt to ensure this device is really a DWC_usb3 Controller.
	 * Read and verify the SNPSID register contents. The value should be
	 * 0x5533XXXX, which corresponds to "U3", as in "USB3 version X.XXX".
	 */
	dwc_usb3_phy_init();
		
	usb3_dev->snpsid = dwc_usb3_get_snpsid(usb3_dev, addr_ofs);
	if ((usb3_dev->snpsid & 0xFFFF0000) != 0x55330000) {
		dev_err(&dev->dev, "Bad value for SNPSID: 0x%08x!\n",
			usb3_dev->snpsid);
		retval = -EINVAL;
		goto fail;
	}

	if (dwc_usb3_module_params.newcore)
		usb3_dev->snpsid = 0x5533109a;

	/*
	 * Up to 32 Event Buffers are supported by the hardware,
	 * but we only use 1
	 */
	usb3_dev->event_buf[0] = dma_alloc_coherent(NULL, DWC_EVENT_BUF_SIZE,
						&usb3_dev->event_buf_dma[0],
						GFP_KERNEL | GFP_DMA);
	if (!usb3_dev->event_buf[0]) {
		dev_err(&dev->dev, "Allocation of event_buf failed!\n");
		retval = -ENOMEM;
		goto fail;
	}

	/*
	 * Initialize the DWC_usb3 Core.
	 */
	if (dwc_usb3_common_init(usb3_dev, usb3_dev->base + addr_ofs,
				 &dwc_usb3_module_params)) {
		dev_err(&dev->dev, "CIL initialization failed!\n");
		retval = -ENOMEM;
		goto fail;
	}

	usb3_dev->cmn_initialized = 1;
#ifdef DWC_UTE
	save_fifosiz_def_values(usb3_dev);
#endif
	spin_lock_init(&usb3_dev->pcd.lock);

	/*
	 * Initialize the PCD
	 */
	retval = dwc_usb3_init(dev);
	if (retval) {
		dev_err(&dev->dev, "PCD initialization failed!\n");
		goto fail;
	}

	usb3_dev->pcd_initialized = 1;

	/*
	 * Install the interrupt handler for the common interrupts.
	 */
	//irq = platform_get_irq(dev, 1);
	irq = dev->resource[1].start;
	
	dev_dbg(&dev->dev, "registering (common) handler for irq%d\n",irq);
#if 0
	if(request_threaded_irq(irq, NULL, tcc_rtc_alarmirq, IRQF_ONESHOT, dwc_driver_name, rtc)) {
		printk("%s: RTC timer interrupt IRQ%d already claimed\n", pdev->name, tcc_rtc_alarmno);
		return 0;
	}
#endif	

	uDEVTEN = readl(tcc_p2v(HwUSBDEVICE_BASE + 0x708));
    writel(0, tcc_p2v(HwUSBDEVICE_BASE + 0x708)); // DEVTEN

	retval = request_irq(irq, dwc_usb3_common_irq,
			     IRQF_SHARED | IRQF_DISABLED,
			     dwc_driver_name, usb3_dev);
	if (retval) {
		dev_err(&dev->dev, "request of irq%d failed!\n", irq);
		retval = -EBUSY;
		goto fail;
	}

    writel(uDEVTEN, tcc_p2v(HwUSBDEVICE_BASE + 0x708)); // DEVTEN

	usb3_dev->common_irq_installed = 1;

	dwc_usb3_enable_device_interrupts(usb3_dev);	// hosi

	platform_set_drvdata(dev, usb3_dev);

#if 0
	if (dwc_usb3_module_params.hibernate &&
	    (usb3_dev->hwparams1 & DWC_HWP1_EN_PWROPT_BITS) ==
	    (DWC_EN_PWROPT_HIBERNATION << DWC_HWP1_EN_PWROPT_SHIFT)) {
		unsigned long flags;

		spin_lock_irqsave(&usb3_dev->pcd.lock, flags);
		dwc_enter_hibernation(&usb3_dev->pcd, 0);
		spin_unlock_irqrestore(&usb3_dev->pcd.lock, flags);
	}
#endif
	return 0;

fail:
	usb3_instance_num--;
fail2:
	dwc_usb3_driver_remove(dev);
	return retval;
}

#define PCI_VENDOR_ID_SYNOPSYS		0x16c3
#define PCI_DEVICE_ID_SYNOPSYS_SITKA	0xabcd

static const struct pci_device_id pci_ids[] = {
	{
		/* The Synopsys PCIe card */
		PCI_DEVICE(PCI_VENDOR_ID_SYNOPSYS,
			   PCI_DEVICE_ID_SYNOPSYS_SITKA),
		.driver_data = (unsigned long)0xdeadbeef,
	},
	{ 0, }	/* end: all zeroes */
};
MODULE_DEVICE_TABLE(pci, pci_ids);

int dwc_usb3_driver_force_init(void)
{
   printk("%s\n", __func__);

   int retval = 0;
   unsigned int uDEVTEN;   
   uint32_t addr_ofs = 0xc000;
   struct resource *resource;

   dwc_usb3_phy_init();
  
   retval = dwc_usb3_pcd_reinit(pusb3_dev);

   if (retval) {
      printk("%s dwc_usb3_pcd_reinit error\n", __func__);
   }

   if (dwc_usb3_common_init(pusb3_dev, pusb3_dev->base + addr_ofs,
            &dwc_usb3_module_params)) {
      printk(KERN_INFO "CIL initialization failed!\n");
      retval = -ENOMEM;
   }

   writel(uDEVTEN, tcc_p2v(HwUSBDEVICE_BASE + 0x708)); 
   pusb3_dev->common_irq_installed = 1;
   dwc_usb3_enable_device_interrupts(pusb3_dev);    

   return 0;
}

/**
 * This structure defines the methods to be called by a bus driver
 * during the lifecycle of a device on that bus. Both drivers and
 * devices are registered with a bus driver. The bus driver matches
 * devices to drivers based on information in the device and driver
 * structures.
 *
 * The probe function is called when the bus driver matches a device
 * to this driver. The remove function is called when a device is
 * unregistered with the bus driver.
 */

#ifdef CONFIG_PM
static int dwc_usb3_driver_suspend(struct platform_device *dev, pm_message_t state)
{
   printk("%s\n", __func__);

   tcc_usb30_phy_off();
   tcc_stop_dwc3();
   tcc_dwc3_vbus_ctrl(0);
   return 0;
}

static int dwc_usb3_driver_resume(struct platform_device *dev)
{
   printk("%s\n", __func__);

   int retval = 0;
   unsigned int uDEVTEN;   
   uint32_t addr_ofs = 0xc000;
   struct resource *resource;

   dwc_usb3_phy_init();
  
   retval = dwc_usb3_pcd_reinit(pusb3_dev);
   if (retval) {
      printk("%s dwc_usb3_pcd_reinit error\n", __func__);
   }

   if (dwc_usb3_common_init(pusb3_dev, pusb3_dev->base + addr_ofs,
            &dwc_usb3_module_params)) {
      dev_err(&dev->dev, "CIL initialization failed!\n");
      retval = -ENOMEM;
   }

   writel(uDEVTEN, tcc_p2v(HwUSBDEVICE_BASE + 0x708)); 
   pusb3_dev->common_irq_installed = 1;
   dwc_usb3_enable_device_interrupts(pusb3_dev);    

   return 0;
}
#endif
static struct platform_driver dwc_usb3_driver = {
	.probe		= dwc_usb3_driver_probe,
	.remove		= dwc_usb3_driver_remove,
	.suspend		= dwc_usb3_driver_suspend,
	.resume		= dwc_usb3_driver_resume,
	//.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name	   = "dwc_usb3",
		.owner	= THIS_MODULE,
	},
};

/**
 * This function is called when the DWC_usb3 driver is installed with the
 * insmod command. It registers the dwc_usb3_driver structure with the
 * appropriate bus driver. This will cause the dwc_usb3_driver_probe function
 * to be called. In addition, the bus driver will automatically expose
 * attributes defined for the device and driver in the special sysfs file
 * system.
 */
static int __init dwc_usb3_driver_init(void)
{
	int retval = 0;

	printk(KERN_INFO "%s: %s version %s\n", dwc_driver_name,
	       DWC_DRIVER_DESC, DWC_DRIVER_VERSION);

	retval = platform_driver_register(&dwc_usb3_driver);
	if (retval < 0) {
		printk(KERN_ERR "%s retval=%d\n", __func__, retval);
		return retval;
	}

	printk(KERN_INFO "%s: module installed\n", dwc_driver_name);
	return retval;
}
module_init(dwc_usb3_driver_init);

/**
 * This function is called when the DWC_usb3 driver is removed from the kernel
 * with the rmmod command. The driver unregisters itself with its bus driver.
 *
 */
static void __exit dwc_usb3_driver_exit(void)
{
	printk(KERN_DEBUG "%s: driver_exit()\n", dwc_driver_name);

	platform_driver_unregister(&dwc_usb3_driver);

	printk(KERN_INFO "%s: module removed\n", dwc_driver_name);
}
module_exit(dwc_usb3_driver_exit);

MODULE_DESCRIPTION(DWC_DRIVER_DESC);
MODULE_AUTHOR("Synopsys Inc.");
MODULE_LICENSE("GPL");
