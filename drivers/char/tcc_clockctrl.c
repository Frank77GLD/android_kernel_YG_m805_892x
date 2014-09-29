/* 
 * linux/drivers/char/tcc_clockctrl.c
 *
 * Author:  <android@telechips.com>
 * Created: 10th Jun, 2008 
 * Description: Telechips Linux BACK-LIGHT DRIVER
 *
 * Copyright (c) Telechips, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/errno.h>

#include <linux/cpufreq.h>
#include <linux/clk.h>

#define dbg(msg...)	if (0) { printk( "TCC_CLOCKCTRL: " msg); }

#define CLOCKCTRL_DEV_NAME		"clockctrl"

enum {
	TCC_CLOCKCTRL_APP_MAX_CLOCK_DISABLE = 100,
	TCC_CLOCKCTRL_APP_MAX_CLOCK_ENABLE,
	TCC_CLOCKCTRL_USB_MAX_CLOCK_DISABLE = 102,
	TCC_CLOCKCTRL_USB_MAX_CLOCK_ENABLE,
	TCC_CLOCKCTRL_MULTI_VPU_CLOCK_DISABLE = 104,
	TCC_CLOCKCTRL_MULTI_VPU_CLOCK_ENABLE,
	TCC_CLOCKCTRL_VOIP_MAX_CLOCK_DISABLE = 106,
	TCC_CLOCKCTRL_VOIP_MAX_CLOCK_ENABLE,
	TCC_CLOCKCTRL_FLASH_MAX_CLOCK_DISABLE = 108,
	TCC_CLOCKCTRL_FLASH_MAX_CLOCK_ENABLE,	
#if defined(CONFIG_CPU_HIGHSPEED)
	TCC_CLOCKCTRL_LIMIT_OVERCLOCK_DISABLE = 110,
	TCC_CLOCKCTRL_LIMIT_OVERCLOCK_ENABLE,
#endif
	TCC_CLOCKCTRL_JPEG_MAX_CLOCK_DISABLE = 112,
	TCC_CLOCKCTRL_JPEG_MAX_CLOCK_ENABLE,
    TCC_CLOCKCTRL_WFD_MAX_CLOCK_DISABLE = 114,
    TCC_CLOCKCTRL_WFD_MAX_CLOCK_ENABLE,
    TCC_CLOCKCTRL_HWC_DDI_CLOCK_DISABLE = 116,
    TCC_CLOCKCTRL_HWC_DDI_CLOCK_ENABLE
};

// for JPU control
#if defined(CONFIG_ARCH_TCC892X)
static struct clk *vcore_clk;
static struct clk *vcache_clk; // for pwdn and vBus.
static struct clk *vcodec_clk; // for pwdn and vBus.
static struct clk *jpu_clk;
#elif defined(CONFIG_ARCH_TCC893X)
static struct clk *vcore_clk;
static struct clk *vbus_clk;
#endif

static struct class *clockctrl_class;
static struct device *clockctrl_dev;
static struct workqueue_struct *tcc_clockctrl_wq;
static int MajorNum;

#if defined(CONFIG_TCC_CLOCK_TABLE)
extern struct tcc_freq_table_t gtAppClockLimitTable;
extern struct tcc_freq_table_t gtUSBClockLimitTable[];
extern struct tcc_freq_table_t gtJpegMaxClockLimitTable;
#if defined(CONFIG_ARCH_TCC892X) //VPU bug related with high-clock.
extern struct tcc_freq_table_t gtMultiVpuClockLimitTable;
#endif
extern struct tcc_freq_table_t gtVoipClockLimitTable[];
extern struct tcc_freq_table_t gtWFDClockLimitTable[];

extern struct tcc_freq_table_t gtVpuNormalClockLimitTable[];
static struct tcc_freq_table_t gtHWCClockLimitTable;
static unsigned int gtVpuSizeTable[] ={	720*480,1280*720,1920*1080};
static int Get_Index_sizeTable(unsigned int Image_Size)
{
	int i, len = sizeof(gtVpuSizeTable)/sizeof(unsigned int);

	//exception process in case of error-size in parser.!!
	if(Image_Size < 64*64)
		Image_Size = 1920*1080;

	for(i=0; i<len; i++)
	{
		if( gtVpuSizeTable[i] >= Image_Size)
		{
			return i;
		}
	}
	
	return (len -1);
}
#endif


static long tcc_clockctrl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	dbg("%s:\n", __func__);

	switch (cmd) {
#if defined(CONFIG_TCC_CLOCK_TABLE)
		case TCC_CLOCKCTRL_APP_MAX_CLOCK_ENABLE:
			tcc_cpufreq_set_limit_table(&gtAppClockLimitTable, TCC_FREQ_LIMIT_APP, 1);
			break;
		case TCC_CLOCKCTRL_APP_MAX_CLOCK_DISABLE:
			tcc_cpufreq_set_limit_table(&gtAppClockLimitTable, TCC_FREQ_LIMIT_APP, 0);
			break;
		case TCC_CLOCKCTRL_USB_MAX_CLOCK_ENABLE:
			tcc_cpufreq_set_limit_table(&gtUSBClockLimitTable[1], TCC_FREQ_LIMIT_USB_ACTIVED, 1);
			break;
		case TCC_CLOCKCTRL_USB_MAX_CLOCK_DISABLE:
			tcc_cpufreq_set_limit_table(&gtUSBClockLimitTable[1], TCC_FREQ_LIMIT_USB_ACTIVED, 0);
			break;
		case TCC_CLOCKCTRL_MULTI_VPU_CLOCK_ENABLE:
#if defined(CONFIG_ARCH_TCC892X) //VPU bug related with high-clock.
			tcc_cpufreq_set_limit_table(&gtMultiVpuClockLimitTable, TCC_FREQ_LIMIT_MULTI_VPU, 1);
#else
			tcc_cpufreq_set_limit_table(&gtJpegMaxClockLimitTable, TCC_FREQ_LIMIT_MULTI_VPU, 1);
#endif
			break;
		case TCC_CLOCKCTRL_MULTI_VPU_CLOCK_DISABLE:
#if defined(CONFIG_ARCH_TCC892X) //VPU bug related with high-clock.
			tcc_cpufreq_set_limit_table(&gtMultiVpuClockLimitTable, TCC_FREQ_LIMIT_MULTI_VPU, 0);
#else
			tcc_cpufreq_set_limit_table(&gtJpegMaxClockLimitTable, TCC_FREQ_LIMIT_MULTI_VPU, 0);
#endif
			break;	
		case TCC_CLOCKCTRL_FLASH_MAX_CLOCK_ENABLE:
			tcc_cpufreq_set_limit_table(&gtJpegMaxClockLimitTable, TCC_FREQ_LIMIT_FLASH, 1);
			break;
		case TCC_CLOCKCTRL_FLASH_MAX_CLOCK_DISABLE:			
			tcc_cpufreq_set_limit_table(&gtJpegMaxClockLimitTable, TCC_FREQ_LIMIT_FLASH, 0);
			break;	
			
		case TCC_CLOCKCTRL_VOIP_MAX_CLOCK_ENABLE:
			tcc_cpufreq_set_limit_table(&gtVoipClockLimitTable[arg], TCC_FREQ_LIMIT_VOIP+arg, 1);
			break;
		case TCC_CLOCKCTRL_VOIP_MAX_CLOCK_DISABLE:
			tcc_cpufreq_set_limit_table(&gtVoipClockLimitTable[arg], TCC_FREQ_LIMIT_VOIP+arg, 0);
			break;	
#if defined(CONFIG_CPU_HIGHSPEED)
		case TCC_CLOCKCTRL_LIMIT_OVERCLOCK_ENABLE:
			tcc_cpufreq_set_limit_table(NULL, TCC_FREQ_LIMIT_OVERCLOCK, 1);
			break;
		case TCC_CLOCKCTRL_LIMIT_OVERCLOCK_DISABLE:
			tcc_cpufreq_set_limit_table(NULL, TCC_FREQ_LIMIT_OVERCLOCK, 0);
			break;
#endif
#endif
		case TCC_CLOCKCTRL_JPEG_MAX_CLOCK_ENABLE:
		#if defined(CONFIG_ARCH_TCC892X)
			clk_enable(vcore_clk);
			clk_enable(vcache_clk);
			clk_enable(vcodec_clk);
			clk_set_rate(jpu_clk, 200*1000*1000);
			clk_enable(jpu_clk);			
			#if defined(CONFIG_TCC_CLOCK_TABLE)
				tcc_cpufreq_set_limit_table(&gtJpegMaxClockLimitTable, TCC_FREQ_LIMIT_JPEG, 1);
			#endif
		#elif defined(CONFIG_ARCH_TCC893X)
			clk_enable(vcore_clk);
			clk_enable(vbus_clk);
		#endif
			break;
		case TCC_CLOCKCTRL_JPEG_MAX_CLOCK_DISABLE:
		#if defined(CONFIG_ARCH_TCC892X)
			clk_disable(vcache_clk);
			clk_disable(vcodec_clk);
			clk_disable(vcore_clk);
			clk_disable(jpu_clk);
			#if defined(CONFIG_TCC_CLOCK_TABLE)
				tcc_cpufreq_set_limit_table(&gtJpegMaxClockLimitTable, TCC_FREQ_LIMIT_JPEG, 0);
			#endif
		#elif defined(CONFIG_ARCH_TCC893X)
			clk_disable(vcore_clk);
			clk_disable(vbus_clk);
		#endif
			break;
        case TCC_CLOCKCTRL_WFD_MAX_CLOCK_ENABLE:
		#if defined(CONFIG_ARCH_TCC892X)
            tcc_cpufreq_set_limit_table(&gtWFDClockLimitTable[arg], TCC_FREQ_LIMIT_WFD+arg, 1);
		#endif
            break;
        case TCC_CLOCKCTRL_WFD_MAX_CLOCK_DISABLE:
		#if defined(CONFIG_ARCH_TCC892X)
            tcc_cpufreq_set_limit_table(&gtWFDClockLimitTable[arg], TCC_FREQ_LIMIT_WFD+arg , 0);
		#endif
            break;

		case TCC_CLOCKCTRL_HWC_DDI_CLOCK_ENABLE:
		#if defined(CONFIG_ARCH_TCC892X)
			memset(&gtHWCClockLimitTable, 0x00, sizeof(struct tcc_freq_table_t));
			gtHWCClockLimitTable.ddi_freq = gtVpuNormalClockLimitTable[Get_Index_sizeTable((int)arg)].ddi_freq;

			tcc_cpufreq_set_limit_table(&gtHWCClockLimitTable, TCC_FREQ_LIMIT_HWC, 1);
		#endif
			break;
		case TCC_CLOCKCTRL_HWC_DDI_CLOCK_DISABLE:
		#if defined(CONFIG_ARCH_TCC892X)
			tcc_cpufreq_set_limit_table(&gtHWCClockLimitTable, TCC_FREQ_LIMIT_HWC, 0);
		#endif
			break;

		default:
			break;
	}
	
	return 0;
}

#if defined(CONFIG_SATA_AHCI)
extern void ahci_set_retry(void);
#endif

int android_system_booting_finished = 0;
static ssize_t tcc_clockctrl_write(struct file *filp, const char __user * user, size_t size, loff_t * sss)
{
	if (android_system_booting_finished == 0)
	{
		printk("kernel boot complete\n");


		/* If SATA HDD has a problem, System will not boot-up because of SATA retry fucntion. 
		So disable retry function during boot-up, after then enable it for SATA hotplug-in/out. */
		#if defined(CONFIG_SATA_AHCI)
		ahci_set_retry();
		#endif
	}

	android_system_booting_finished = 1;
	return size;
}

static int tcc_clockctrl_release(struct inode *inode, struct file *filp)
{
	dbg("%s:\n", __func__);
	return 0;
}

static int tcc_clockctrl_open(struct inode *inode, struct file *filp)
{
	dbg("%s:\n", __func__);
	return 0;
}

struct file_operations tcc_clockctrl_fops = {
	.owner          = THIS_MODULE,
	.open           = tcc_clockctrl_open,
	.release        = tcc_clockctrl_release,
	.unlocked_ioctl = tcc_clockctrl_ioctl,
	.write          = tcc_clockctrl_write,
};

int __init tcc_clockctrl_init(void)
{
	int err=0;

#if defined(CONFIG_ARCH_TCC892X) // for JPU control
	vcore_clk = clk_get(NULL, "vcore");
	BUG_ON(vcore_clk == NULL);

	vcache_clk = clk_get(NULL, "vcache");
	BUG_ON(vcache_clk == NULL);
	
	vcodec_clk = clk_get(NULL, "vcodec");
	BUG_ON(vcodec_clk == NULL);

	jpu_clk = clk_get(NULL, "jpeg");
	BUG_ON(jpu_clk == NULL);
#elif defined(CONFIG_ARCH_TCC893X)
	vcore_clk = clk_get(NULL, "vcore");
	BUG_ON(vcore_clk == NULL);

	vbus_clk = clk_get(NULL, "vbus");
	BUG_ON(vbus_clk == NULL);
#endif

	android_system_booting_finished = 0;

	tcc_clockctrl_wq = create_singlethread_workqueue("tcc_clockctrl_wq");
	if (!tcc_clockctrl_wq)
		return -ENOMEM;

	MajorNum = register_chrdev(0, CLOCKCTRL_DEV_NAME, &tcc_clockctrl_fops);
	if (MajorNum < 0) {
		printk("%s: device failed widh %d\n", __func__, MajorNum);
		err = -ENODEV;
		goto out_wq;
	}

	clockctrl_class = class_create(THIS_MODULE, CLOCKCTRL_DEV_NAME);
	if (IS_ERR(clockctrl_dev)) {
		err = PTR_ERR(clockctrl_dev);
		goto out_chrdev;
	}
	device_create(clockctrl_class,NULL,MKDEV(MajorNum, 0),NULL,CLOCKCTRL_DEV_NAME);

	printk(KERN_INFO "%s\n", __FUNCTION__);
	goto out;

out_chrdev:
	unregister_chrdev(MajorNum, CLOCKCTRL_DEV_NAME);
out_wq:
	if (tcc_clockctrl_wq)
		destroy_workqueue(tcc_clockctrl_wq);
out:
	return err;
}

void __exit tcc_clockctrl_exit(void)
{
#if defined(CONFIG_ARCH_TCC892X) // for JPU control
	clk_put(vcore_clk);
	clk_put(vcache_clk);
	clk_put(vcodec_clk);
	clk_put(jpu_clk);
#elif defined(CONFIG_ARCH_TCC893X)
	clk_put(vcore_clk);
	clk_put(vbus_clk);
#endif

	device_destroy(clockctrl_class, MKDEV(MajorNum, 0));
	class_destroy(clockctrl_class);
	unregister_chrdev(MajorNum, CLOCKCTRL_DEV_NAME);
	if (tcc_clockctrl_wq)
		 destroy_workqueue(tcc_clockctrl_wq);
	
    printk(KERN_INFO "%s\n", __FUNCTION__);
}

module_init(tcc_clockctrl_init);
module_exit(tcc_clockctrl_exit);

MODULE_DESCRIPTION("TCCxxx clockctrl driver");
MODULE_LICENSE("GPL");
