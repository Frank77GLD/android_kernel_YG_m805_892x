/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/DWC_usb3/driver/cil_intr.c $
 * $Revision: #28 $
 * $Date: 2012/01/23 $
 * $Change: 1924640 $
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
 *
 * The Core Interface Layer provides basic services for accessing and
 * managing the DWC_usb3 hardware. These services are used by both the
 * Peripheral Controller Driver and the On The Go Driver.
 *
 * This file contains the common interrupt handling functions.
 */

#include "os_dep.h"
#include "hw.h"
#include "usb.h"
#include "pcd.h"
#include "driver.h"
#include "cil.h"

/**
 * This function enables the Event Buffer interrupt.
 */
static void ena_eventbuf_intr(dwc_usb3_device_t *dev, int bufno)
{
	uint32_t eventsiz;

	eventsiz =
	      dwc_rd32(dev, &dev->core_global_regs->geventbuf[bufno].geventsiz);
	eventsiz &= ~DWC_EVENTSIZ_INT_MSK_BIT;
	dwc_wr32(dev, &dev->core_global_regs->geventbuf[bufno].geventsiz,
		 eventsiz);
}

/**
 * This function disables the Event Buffer interrupt.
 */
static void dis_eventbuf_intr(dwc_usb3_device_t *dev, int bufno)
{
	uint32_t eventsiz;

	eventsiz =
	      dwc_rd32(dev, &dev->core_global_regs->geventbuf[bufno].geventsiz);
	eventsiz |= DWC_EVENTSIZ_INT_MSK_BIT;
	dwc_wr32(dev, &dev->core_global_regs->geventbuf[bufno].geventsiz,
		 eventsiz);
}

/**
 * This function disables the Event Buffer interrupt and flushes any pending
 * events from the buffer.
 */
void dwc_usb3_dis_flush_eventbuf_intr(dwc_usb3_device_t *dev, int bufno)
{
	uint32_t cnt;

	if (atomic_read(&dev->hibernate) >= DWC_HIBER_SLEEPING)
		return;

	dis_eventbuf_intr(dev, bufno);
	cnt = dwc_rd32(dev, &dev->core_global_regs->geventbuf[bufno].geventcnt);
	dwc_wr32(dev, &dev->core_global_regs->geventbuf[bufno].geventcnt, cnt);
}

/**
 * This function reads the current Event Buffer count.
 */
static int get_eventbuf_count(dwc_usb3_device_t *dev, int bufno)
{
	uint32_t cnt;

	cnt = dwc_rd32(dev, &dev->core_global_regs->geventbuf[bufno].geventcnt);
	return cnt & DWC_EVENTCNT_CNT_BITS;
}

/**
 * This function writes the Event Buffer count.
 */
static void update_eventbuf_count(dwc_usb3_device_t *dev, int bufno, int cnt)
{
	dwc_wr32(dev, &dev->core_global_regs->geventbuf[bufno].geventcnt, cnt);
}

/**
 * This function fetches the next event from the Event Buffer.
 */
static uint32_t get_eventbuf_event(dwc_usb3_device_t *dev, int bufno, int size)
{
	uint32_t event;

	event = *dev->event_ptr[bufno]++;
	if (dev->event_ptr[bufno] >= dev->event_buf[bufno] + size)
		dev->event_ptr[bufno] = dev->event_buf[bufno];
	return event;
}

/**
 * This function initializes an Event Buffer.
 */
void dwc_usb3_init_eventbuf(dwc_usb3_device_t *dev, int bufno,
			    uint32_t *addr, int size, dwc_dma_t dma_addr)
{
	dwc_debug4(dev, "Event buf %d addr 0x%08lx phys 0x%08lx size %d\n",
		   bufno, (unsigned long)addr, (unsigned long)dma_addr, size);
	dwc_wr32(dev, &dev->core_global_regs->geventbuf[bufno].geventadr_lo,
		 dma_addr & 0xffffffffU);
#ifdef DWC_64_BIT_ARCH
	dwc_wr32(dev, &dev->core_global_regs->geventbuf[bufno].geventadr_hi,
		 (dma_addr >> 32U) & 0xffffffffU);
#else
	dwc_wr32(dev, &dev->core_global_regs->geventbuf[bufno].geventadr_hi, 0);
#endif
	dwc_wr32(dev, &dev->core_global_regs->geventbuf[bufno].geventsiz,
		 size << 2);
	dwc_wr32(dev, &dev->core_global_regs->geventbuf[bufno].geventcnt, 0);
}

/**
 * This function initializes the commmon interrupts.
 *
 * @param dev Programming view of DWC_usb3 controller.
 *
 */
void dwc_usb3_enable_common_interrupts(dwc_usb3_device_t *dev)
{
	/* Clear any pending interrupts */
	dwc_usb3_dis_flush_eventbuf_intr(dev, 0);

	ena_eventbuf_intr(dev, 0);
}

/**
 * This function enables the Device mode interrupts.
 *
 * @param dev Programming view of DWC_usb3 controller.
 */
void dwc_usb3_enable_device_interrupts(dwc_usb3_device_t *dev)
{
	dwc_debug1(dev, "%s()\n", __func__);

	/* Enable global interrupts */
	dwc_usb3_enable_common_interrupts(dev);

	/* Enable device interrupts */
	dwc_wr32(dev, &dev->pcd.dev_global_regs->devten,
		 DWC_DEVTEN_DISCONN_BIT | DWC_DEVTEN_USBRESET_BIT |
		 DWC_DEVTEN_CONNDONE_BIT | DWC_DEVTEN_ULST_CHNG_BIT |
		 DWC_DEVTEN_HIBER_REQ_EVT_BIT /*| DWC_DEVTEN_WKUP_BIT*/);

	dwc_debug2(dev, "%s() devten=%0x\n", __func__,
		   dwc_rd32(dev, &dev->pcd.dev_global_regs->devten));
}

/**
 * This function handles all interrupt events. It is called by the
 * dwc_usb3_irq() interrupt handler function, and by the enter_hibernation()
 * function after clearing the Run/Stop bit and waiting for the Halted bit to
 * be set.
 * @param dev Programming view of DWC_usb3 controller.
 * return     1 if an interrupt event was seen, 0 if not.
 */
int dwc_usb3_handle_event(dwc_usb3_device_t *dev)
{
	dwc_usb3_pcd_t *pcd = &dev->pcd;
	uint32_t event;
	int count, intr, physep, i;
	int ret = 0;
	static int msg_cnt;

	count = get_eventbuf_count(dev, 0);
	if (count)
		dwc_debug1(dev, "Interrupt count %d\n", count);

	if ((count & DWC_EVENTCNT_CNT_BITS) ==
					(0xffffffff & DWC_EVENTCNT_CNT_BITS) ||
	    count >= DWC_EVENT_BUF_SIZE * 4) {
		if (msg_cnt > 100) {
			if (msg_cnt < 200) {
				msg_cnt = 200;
				dwc_warn0(dev,
					"Too many bad events, disabling IRQ\n");
			}
		} else {
			msg_cnt++;
			dwc_warn1(dev,
				"Bad event count 0x%01x in dwc_usb3_irq() !!\n",
				count);
		}

		dis_eventbuf_intr(dev, 0);
		update_eventbuf_count(dev, 0, count);
		count = 0;
	}

	if (!count)
		goto out;
	ret = 1;

#if defined(CONFIG_IPMATE) || defined(COSIM) || defined(VIRTIO_MODEL)
#error
	dis_eventbuf_intr(dev, 0);
#endif
//	dis_eventbuf_intr(dev, 0); //taejin
	
	for (i = 0; i < count; i += 4) {
		dwc_debug1(dev, "Event addr 0x%08lx\n",
			   (unsigned long)dev->event_ptr[0]);
		event = get_eventbuf_event(dev, 0, DWC_EVENT_BUF_SIZE);
		update_eventbuf_count(dev, 0, 4);
		if (event == 0) {
			dwc_print0(dev, "## Null event! ##\n");

			/* Ignore null events */
			continue;
		}

		//dwc_debug1(dev, "Interrupt event 0x%08x\n", event);
		if (event & DWC_EVENT_NON_EP_BIT) {
			//dwc_debug0(dev, "Non-EP interrupt event\n");
			intr = event & DWC_EVENT_INTTYPE_BITS;

			if (intr ==
			    (DWC_EVENT_DEV_INT << DWC_EVENT_INTTYPE_SHIFT)) {
				dwc_debug1(dev,
					   "## Device interrupt 0x%08x ##\n",
					   event);
				ret = dwc_usb3_handle_dev_intr(pcd, event);
				if (ret) {
					ret = 2;
					goto out;
				}
				ret = 1;
			} else {
				dwc_debug1(dev, "## Core interrupt 0x%08x ##\n",
					   event);

				/* @todo Handle non-Device interrupts
				 * (OTG, CarKit, I2C)
				 */
			}
		} else {
			physep = (event >> DWC_DEPEVT_EPNUM_SHIFT) &
			      (DWC_DEPEVT_EPNUM_BITS >> DWC_DEPEVT_EPNUM_SHIFT);
			dwc_debug2(dev,
				   "## Physical EP%d interrupt 0x%08x ##\n",
				   physep, event);
			dwc_debug2(dev, "[EP%d] %s\n", (physep >> 1) & 0xf,
				   physep & 1 ? "IN" : "OUT");
			dwc_usb3_handle_ep_intr(pcd, physep, event);
		}
	}

#if defined(CONFIG_IPMATE) || defined(COSIM) || defined(VIRTIO_MODEL)
	#error
	ena_eventbuf_intr(dev, 0);
#endif
//	ena_eventbuf_intr(dev, 0); //taejin
out:
	return ret;
}

/**
 * This is the common interrupt handler function.
 * @param dev Programming view of DWC_usb3 controller.
 * return     1 if an interrupt event was seen, 0 if not.
 */
int dwc_usb3_irq(dwc_usb3_device_t *dev, int irq)
{
	int temp, ret;

	if (!dev->common_irq_installed)
		return 0;

	temp = atomic_read(&dev->hibernate);
	if (temp >= DWC_HIBER_SLEEPING) {
		if (temp == DWC_HIBER_SS_DIS_QUIRK) {
			atomic_set(&dev->hibernate, DWC_HIBER_AWAKE);
		} else {
			if (dev->pme_ready) {
				ret = dwc_usb3_handle_pme_intr(dev);
				return ret;
			} else {
				if (temp != DWC_HIBER_WAIT_LINK_UP)
					dwc_info0(dev, "Intr in hibernate but"
						  " pme_ready not set\n");
				return 1;
			}
		}
	}

	ret = dwc_usb3_handle_event(dev);
	if (ret == 2)
		ret = 1;

	return ret;
}
