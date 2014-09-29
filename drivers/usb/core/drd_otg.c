/*
 * drd_otg.c: common usb otg control API
 *
 *  Copyright (C) 2013, Telechips, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>

#include <asm/io.h>
#include <asm/mach-types.h>

#include <mach/bsp.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <mach/reg_physical.h>
#include <mach/structures_hsio.h>
#include <mach/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>

#define OTG_HOST	1
#define OTG_DEVICE 	0

#define DRIVER_AUTHOR "Taejin"
#define DRIVER_DESC "'eXtensible' DRD otg Driver"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_LICENSE ("GPL");

struct platform_device *pdev;
int cnt;
int cmode;

struct usb3_data {
        struct work_struct 	wq;
		unsigned long 		flag; 
};
struct usb3_data *drd_otg_mod_wq;

spinlock_t lock;
static irqreturn_t dwc_usb3_otg_host_dwc_irq(int irq, struct platform_device *pdev )
{
	//int retval = 0;
	//unsigned long	flags;
	//spinlock_t lock;
	#if 1
	if(cnt==0){
		cnt = 1;
		printk("\x1b[1;35mfunc: %scnt 0 -> 1  \x1b[0m\n",__func__);
		return IRQ_HANDLED;
	}
	#endif
	printk("\x1b[1;36m Device mode -> Host mode(line : %d)\x1b[0m\n",__LINE__);	
	spin_lock(&lock);
	
	drd_otg_mod_wq->flag = 0;
	schedule_work(&drd_otg_mod_wq->wq);
	
	spin_unlock(&lock);		

	return IRQ_HANDLED;
}
static irqreturn_t dwc_usb3_otg_device_dwc_irq(int irq, struct platform_device *pdev )
{
	//int retval = 0;
	//spinlock_t lock;
	#if 0
	if(cnt==0)
	{
		cnt = 1;
		printk("\x1b[1;35mfunc: %scnt 0 -> 1  \x1b[0m\n",__func__,__LINE__);
		return IRQ_HANDLED;
	}
	#endif
	printk("\x1b[1;36m Host mode -> Device mode(line: %d)  \x1b[0m\n",__LINE__);
	spin_lock(&lock);

	drd_otg_mod_wq->flag = 1;
	schedule_work(&drd_otg_mod_wq->wq);

	spin_unlock(&lock);	
	
	return IRQ_HANDLED;
}
extern void msleep(unsigned int msecs);
extern int tcc_xhci_vbus_ctrl(int on);
static void tcc_usb30_module_change(struct work_struct *work)
{
   int retval = 0;
   struct usb3_data *p =  container_of(work, struct usb3_data, wq);
   unsigned long flag = p->flag;

   struct subprocess_info *sub_info_dwc, *sub_info_gadget, *sub_info_ss, *start_adbd, *stop_adbd, *set_property;
   static char *envp[] = {NULL};
   char *argv_dwc[] = {
      flag ? "/system/bin/insmod" : "/system/bin/rmmod",
      flag ? "/lib/modules/dwc_usb3.ko" : "dwc_usb3",
      NULL};
   char *argv_gadget[] = {
      flag ? "/system/bin/insmod" : "/system/bin/rmmod",
      flag ? "/lib/modules/g_android.ko" : "g_android",
      NULL};
   char *argv_ss[] = {
      !flag ? "/system/bin/insmod" : "/system/bin/rmmod",
      !flag ? "/lib/modules/xhci-hcd.ko" : "xhci_hcd",
      NULL};
   char *argv_start_adbd[] = {"/system/bin/start", "adbd", NULL};
   char *argv_stop_adbd[] = {"/system/bin/stop", "adbd", NULL};
   //char *argv_property[] = {"/system/bin/setprop", "persist.sys.usb.config", "mtp,adb", NULL};
   char *argv_property[] = {"/system/bin/setprop", "sys.drd.mode", flag ? "usb_device" : "usb_host", NULL};

   //msleep(500);
   free_irq(INT_OTGID, pdev);

#if 1
   sub_info_ss = call_usermodehelper_setup( argv_ss[0], argv_ss, envp, GFP_ATOMIC );
   sub_info_dwc = call_usermodehelper_setup( argv_dwc[0], argv_dwc, envp, GFP_ATOMIC );
   sub_info_gadget = call_usermodehelper_setup( argv_gadget[0], argv_gadget, envp, GFP_ATOMIC );
   start_adbd = call_usermodehelper_setup( argv_start_adbd[0], argv_start_adbd, envp, GFP_ATOMIC );
   stop_adbd = call_usermodehelper_setup( argv_stop_adbd[0], argv_stop_adbd, envp, GFP_ATOMIC );
   set_property = call_usermodehelper_setup(argv_property[0], argv_property, envp, GFP_ATOMIC );

   if (sub_info_dwc == NULL || sub_info_gadget == NULL ||
       sub_info_ss == NULL || start_adbd == NULL||
       stop_adbd == NULL || set_property == NULL) {
      printk("-> [%s:%d] ERROR-hs:0x%p\n", __func__, __LINE__, sub_info_dwc);
      if(sub_info_dwc) call_usermodehelper_freeinfo(sub_info_dwc);
      if(sub_info_gadget) call_usermodehelper_freeinfo(sub_info_gadget);
      if(sub_info_ss) call_usermodehelper_freeinfo(sub_info_ss);
      if(start_adbd) call_usermodehelper_freeinfo(start_adbd);
      if(stop_adbd) call_usermodehelper_freeinfo(stop_adbd);
      if(set_property) call_usermodehelper_freeinfo(set_property);
      retval = -ENOMEM;
      goto End;
   }

   if (flag) {
#if 0
      printk("\x1b[1;38mStop adbd\x1b[0m\n");
      retval = call_usermodehelper_exec(stop_adbd, UMH_WAIT_PROC);
      if(retval) printk("->Stop adbd [%s:%d] retval:%d\n", __func__, __LINE__, retval);

      retval = call_usermodehelper_exec(sub_info_ss, UMH_WAIT_PROC);
      if(retval) printk("-> [%s:%d] retval:%d\n", __func__, __LINE__, retval);
      retval = call_usermodehelper_exec(sub_info_dwc, UMH_WAIT_PROC);
      if(retval) printk("-> [%s:%d] retval:%d\n", __func__, __LINE__, retval);
#endif

#if 0
      retval = call_usermodehelper_exec(sub_info_gadget, UMH_WAIT_PROC);
      if(retval) printk("-> [%s:%d] retval:%d\n", __func__, __LINE__, retval);

      printk("\x1b[1;38mSetting property\x1b[0m\n");
      retval = call_usermodehelper_exec(set_property, UMH_WAIT_PROC);
      if(retval) printk("->Setting property [%s:%d] retval:%d\n", __func__, __LINE__, retval);

      printk("\x1b[1;38mStart adbd\x1b[0m\n");
      retval = call_usermodehelper_exec(start_adbd, UMH_WAIT_PROC);
      if(retval) printk("->Start adbd [%s:%d] retval:%d\n", __func__, __LINE__, retval);
#endif
      //tcc_xhci_vbus_ctrl(0);
      cnt = 0;

      retval = call_usermodehelper_exec(set_property, UMH_WAIT_PROC);
      if(retval) printk("setting property [%s:%d] retval:%d\n", __func__, __LINE__, retval);
   } else {
#if 0
      printk("\x1b[1;38mStop adbd\x1b[0m\n");
      retval = call_usermodehelper_exec(stop_adbd, UMH_WAIT_PROC);
      if(retval) printk("->Stop adbd [%s:%d] retval:%d\n", __func__, __LINE__, retval);

      retval = call_usermodehelper_exec(sub_info_gadget, UMH_WAIT_PROC);
      if(retval) printk("-> [%s:%d] retval:%d\n", __func__, __LINE__, retval);
      retval = call_usermodehelper_exec(sub_info_dwc, UMH_WAIT_PROC);
      if(retval) printk("-> [%s:%d] retval:%d\n", __func__, __LINE__, retval);
      retval = call_usermodehelper_exec(sub_info_ss, UMH_WAIT_PROC);
      if(retval) printk("-> [%s:%d] retval:%d\n", __func__, __LINE__, retval);
#endif
      //tcc_xhci_vbus_ctrl(1);
      retval = call_usermodehelper_exec(set_property, UMH_WAIT_PROC);
      if(retval) printk("setting property [%s:%d] retval:%d\n", __func__, __LINE__, retval);
   }
#endif

   if(!flag){
      printk("\x1b[1;38mChange Falling -> Rising (Catch Host to Device)\x1b[0m\n");

      retval = request_irq(INT_OTGID, &dwc_usb3_otg_device_dwc_irq, IRQF_SHARED|IRQ_TYPE_EDGE_RISING, "USB30_IRQ11", pdev);
      if (retval) {
         dev_err(&pdev->dev, "request rising edge of irq%d failed!\n", INT_OTGID);
         //printk("\x1b[1;31mfunc: %s, line: %d  \x1b[0m\n",__func__,__LINE__);
         retval = -EBUSY;
      }else cmode = OTG_HOST;
   }
   else
   {
      printk("\x1b[1;38mChange Rising -> Falling (Catch Device to Host) \x1b[0m\n");
      retval = request_irq(INT_OTGID, &dwc_usb3_otg_host_dwc_irq, IRQF_SHARED|IRQ_TYPE_EDGE_FALLING, "USB30_IRQ5", pdev);
      if (retval) {
         dev_err(&pdev->dev, "request falling edge of irq%d failed!\n", INT_OTGID);
         //printk("\x1b[1;31mfunc: %s, line: %d  \x1b[0m\n",__func__,__LINE__);
         retval = -EBUSY;
      }else cmode = OTG_DEVICE;
   }
   //printk("\x1b[1;33musb3.0 otg mode change success.  \x1b[0m\n");
   return;

End:
   printk("usb3.0 otg mode change fail.\n");
}

static int __devinit drd_tcc_drv_probe(struct platform_device *pdev2)
{
   int retval = 0;
   spin_lock_init(&lock);
   pdev = pdev2;
   cnt = 0;

   //printk("\x1b[1;33mfunc: %s, line: %d  \x1b[0m\n",__func__,__LINE__);
   /* Set gps_pwrctrl data
   */
   drd_otg_mod_wq = (struct usb3_data *)kzalloc(sizeof(struct usb3_data), GFP_KERNEL);
   if (drd_otg_mod_wq == NULL) {
      retval = -ENOMEM;
   }
   //drd_otg_mod_wq->flag = 0;
   INIT_WORK(&drd_otg_mod_wq->wq, tcc_usb30_module_change);

   //tcc_gpio_config(TCC_GPA(15), GPIO_FN(0)|GPIO_PULL_DISABLE);  // GPIOE[29]: input mode, disable pull-up/down
   //gpio_direction_input(TCC_GPA(15));
   //tcc_gpio_config_ext_intr(INT_EINT11, EXTINT_GPIOA_15);
	//tcc_gpio_config_ext_intr(INT_EINT5, EXTINT_GPIOA_15);
	//gpio_request(TCC_GPA(15), "USB_ID_IRQ");
	
#if 0
	printk("\x1b[1;39mfunc: %s, line: %d Folling \x1b[0m\n",__func__,__LINE__);
	retval = request_irq(INT_OTGID, dwc_usb3_otg_host_dwc_irq,
			     IRQF_SHARED|IRQ_TYPE_EDGE_FALLING,
			     "USB30_IRQ5", pdev);
	if (retval) {
		dev_err(&pdev->dev, "request falling edge of irq%d failed!\n", INT_OTGID);
		retval = -EBUSY;
		goto fail_drd_change;
	}
#else	
	//printk("\x1b[1;39mfunc: %s, line: %d Rising \x1b[0m\n",__func__,__LINE__);

   retval = request_irq(INT_OTGID, &dwc_usb3_otg_device_dwc_irq, IRQF_SHARED|IRQ_TYPE_EDGE_RISING,
			     "USB30_IRQ", pdev);
	if (retval) {
		dev_err(&pdev->dev, "request rising edge of irq%d failed!\n", INT_OTGID);
		retval = -EBUSY;
		goto fail_drd_change;
   }else cmode = OTG_HOST;
#endif
   return retval;
fail_drd_change:
	printk("fail_drd_change\n");
	return retval;
}

static void drd_tcc_remove(struct platform_device *pdev)
{
	#if 0
	struct xhci_hcd *xhci;
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	xhci = hcd_to_xhci(hcd);
	if (xhci->shared_hcd) {
		usb_remove_hcd(xhci->shared_hcd);
		usb_put_hcd(xhci->shared_hcd);
	}

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	local_irq_disable();
	usb_hcd_irq(0, hcd);
	local_irq_enable();
	
	tcc_xhci_phy_off();
	tcc_xhci_phy_ctrl(0);
	tcc_stop_xhci();
	tcc_xhci_vbus_ctrl(0);
	
   kfree(xhci);
#endif
}

static int drd_otg_suspend(struct platform_device *dev, pm_message_t state)
{
   PUSBPHYCFG USBPHYCFG = (PUSBPHYCFG)tcc_p2v(HwUSBPHYCFG_BASE);
   printk("%d\n", __func__);

   free_irq(INT_OTGID, pdev);
   BITCLR(USBPHYCFG->UPCR4, Hw20|Hw21);

   return 0;
}

static int drd_otg_resume(struct platform_device *dev)
{
   printk("%d\n", __func__);

   spin_lock(&lock);

   drd_otg_mod_wq->flag = 0;
   schedule_work(&drd_otg_mod_wq->wq);

   spin_unlock(&lock);	

   return 0;
}

struct platform_device drd_otg_device = {
   .name			= "tcc-drd",
   .id				= -1,
   //.num_resources  = ARRAY_SIZE(tcc8930_xhci_hs_resources),
   //.resource       = tcc8930_xhci_hs_resources,
   .dev			= {
      //.dma_mask 			= &tcc8930_device_xhci_dmamask,
      //.coherent_dma_mask	= 0xffffffff,
      //.platform_data	= tcc8930_ehci_hs_data,
   },
};

static struct platform_driver drd_otg_driver = {
   .probe		= drd_tcc_drv_probe,
   .remove		= __exit_p(drd_tcc_remove),
   .suspend	= drd_otg_suspend,
   .resume		= drd_otg_resume,
   //.shutdown	= NULL,
   .driver = {
      .name	= (char *) "tcc-drd",
      .owner	= THIS_MODULE,
      //.pm		= EHCI_TCC_PMOPS,
   }
};

static int __init drd_otg_init(void)
{
   int retval = 0;

   retval = platform_device_register(&drd_otg_device);
   if (retval < 0){
      printk("drd device register fail!\n");
      return retval;
   }


   retval = platform_driver_register(&drd_otg_driver);
   if (retval < 0){
      printk("drd drvier register fail!\n");
      return retval;
   }
   return retval;
}
module_init(drd_otg_init);
