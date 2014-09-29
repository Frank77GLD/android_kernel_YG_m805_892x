/*
 * linux/drivers/char/tsif/tcc_tsif.c
 *
 * Author:  <linux@telechips.com>
 * Created: 1st April, 2009
 * Description: Driver for Telechips TS Parallel/Serial Controllers
 *
 * Copyright (c) Telechips, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
//#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/spi/tcc_tsif.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>

#include <mach/tca_tsif.h>

#include <asm/io.h>

#include "tsdemux/TSDEMUX_sys.h"

#define USE_STATIC_DMA_BUFFER
struct tea_dma_buf *g_static_dma_buffer;
#undef  TSIF_DMA_SIZE
#define TSIF_DMA_SIZE (188*10000)
#define SECTION_DMA_SIZE (512*1024)

#define HWDMX_NUM         4

#define	MAX_PCR_CNT 2

static const char fw_name[] = "tcc_tsif/HWDemux.bin";
static struct device *pdev = NULL;

struct ts_demux_feed_struct {
    int is_active; //0:don't use external demuxer, 1:use external decoder
    int index;
    int call_decoder_index;
    int (*ts_demux_decoder)(char *p1, int p1_size, char *p2, int p2_size);
};

struct tcc_hwdmx_handle {
    struct ts_demux_feed_struct ts_demux_feed_handle;
    struct tcc_tsif_handle tsif_ex_handle;
    struct tcc_tsif_pid_param ts_pid;
    int pcr_pid[MAX_PCR_CNT];
    int read_buff_offset;
    int write_buff_offset;
    int end_buff_offset;
    int loop_count;
    int state;
    unsigned char * mem;
    wait_queue_head_t wait_q;
    unsigned int dma_mode; //0:normal mode (not mpeg2ts mode), 1:mpeg2ts mode(start with 0x47)
};

struct tcc_tsif_pri_handle {
    int open_cnt;
    u32 drv_major_num;

    struct mutex mutex;
    struct tcc_hwdmx_handle demux[HWDMX_NUM];
    struct timer_list timer;
};

static int use_tsif_export_ioctl = 0;
static struct tcc_tsif_pri_handle tsif_ex_pri;
static struct class *tsif_ex_class;
static int tcc_tsif_init(struct firmware *fw, struct tcc_hwdmx_handle *demux, int id);
static void tcc_tsif_deinit(struct tcc_hwdmx_handle *demux);

static void tcc_tsif_timer_handler(unsigned long data)
{
    struct tcc_hwdmx_handle *demux;
    int i;

    for(i=0; i< HWDMX_NUM; i++)
    {
        demux = &tsif_ex_pri.demux[i];

        if(demux->state == 1 && 0 < demux->pcr_pid[0] && demux->pcr_pid[0] < 0x1FFF)
        {
            tca_tsif_get_stc(&demux->tsif_ex_handle);
        }
    }
    if (tsif_ex_pri.open_cnt) {
        tsif_ex_pri.timer.expires = jiffies + msecs_to_jiffies(1000);//msecs_to_jiffies(125);
        add_timer(&tsif_ex_pri.timer);
    }
}

static int tcc_tsif_update_stc(struct tcc_hwdmx_handle *demux, char *p1, int p1_size, char *p2, int p2_size)
{
    int i;

    for (i = 1; i < MAX_PCR_CNT; i++)
    {
        if (0 < demux->pcr_pid[i] && demux->pcr_pid[i] < 0x1FFFF)
        {
            if (p1)
                TSDEMUX_MakeSTC((unsigned char *)p1, p1_size, demux->pcr_pid[i], i);
            if (p2)
                TSDEMUX_MakeSTC((unsigned char *)p2, p2_size, demux->pcr_pid[i], i);
        }
    }
    return 0;
}

static int tcc_tsif_parse_packet(struct tcc_hwdmx_handle *demux, char *pBuffer, int updated_buff_offset, int end_buff_offset)
{
    int ret = 0;
    char *p1 = NULL, *p2 = NULL;
    int p1_size = 0, p2_size = 0;
    int packet_th = demux->ts_demux_feed_handle.call_decoder_index;
    if(++demux->ts_demux_feed_handle.index >= packet_th) {

        if( updated_buff_offset > demux->write_buff_offset ) {
            p1 = (char *)(pBuffer + demux->write_buff_offset);
            p1_size = updated_buff_offset - demux->write_buff_offset;
        } else if (end_buff_offset == demux->write_buff_offset) {
            p1 = (char *)pBuffer;
            p1_size = updated_buff_offset;
        } else {
            p1 = (char *)(pBuffer + demux->write_buff_offset);
            p1_size = end_buff_offset - demux->write_buff_offset;

            p2 = (char *)pBuffer;
            p2_size = updated_buff_offset;
        }

        if (p1 == NULL || p1_size < 188) return 0;

        tcc_tsif_update_stc(demux, p1, p1_size, p2, p2_size);

        if(demux->ts_demux_feed_handle.ts_demux_decoder(p1, p1_size, p2, p2_size) == 0)
        {
            demux->write_buff_offset = updated_buff_offset;
            demux->ts_demux_feed_handle.index = 0;
        }
    }
    return ret;
}

static int tcc_tsif_push_buffer(struct tcc_hwdmx_handle *demux, char *pBuffer, int updated_buff_offset, int end_buff_offset)
{
    char *p1 = NULL, *p2 = NULL;
    int p1_size = 0, p2_size = 0;
    int ret = 0;

    demux->loop_count++;
    if(demux->loop_count) {
        demux->loop_count = 0;

        demux->end_buff_offset = end_buff_offset;
        if( updated_buff_offset > demux->write_buff_offset ){
            p1 = (char *)(pBuffer + demux->write_buff_offset);
            p1_size = updated_buff_offset - demux->write_buff_offset;
        } else if (end_buff_offset == demux->write_buff_offset) {
            p1 = (char *)pBuffer;
            p1_size = updated_buff_offset;
        } else {
            p1 = (char *)(pBuffer + demux->write_buff_offset);
            p1_size = end_buff_offset - demux->write_buff_offset;

            p2 = (char *)pBuffer;
            p2_size = updated_buff_offset;
        }

        tcc_tsif_update_stc(demux, p1, p1_size, p2, p2_size);

		demux->write_buff_offset = updated_buff_offset;

        wake_up(&(demux->wait_q));
    }
    return ret;
}

static int tcc_tsif_updated_callback(unsigned int dmxid, unsigned int ftype, unsigned int fid, unsigned int value1, unsigned int value2, unsigned int bErrCRC)
{
    struct tcc_hwdmx_handle *demux = &tsif_ex_pri.demux[dmxid];
    unsigned int uiSTC;
    int ret = 0;

    switch(ftype)
    {
        case 0: // HW_DEMUX_SECTION
        {
            if (demux->ts_demux_feed_handle.ts_demux_decoder)
            {
                //printk("0x%x, 0x%x, 0x%x\n", demux->tsif_ex_handle.dma_buffer->v_sec_addr, value1, demux->tsif_ex_handle.dma_buffer->dma_sec_addr);
                ret = demux->ts_demux_feed_handle.ts_demux_decoder((char *)fid,
                                                                   bErrCRC,
                                                                   demux->tsif_ex_handle.dma_buffer->v_sec_addr + value1,
                                                                   value2 - value1);
            }
            break;
        }
        case 1: // HW_DEMUX_TS
        {
            if (demux->ts_demux_feed_handle.ts_demux_decoder)
            {
                ret = tcc_tsif_parse_packet(demux, demux->tsif_ex_handle.dma_buffer->v_addr, value1, value2);
            }
            else
            {
                ret = tcc_tsif_push_buffer(demux, demux->tsif_ex_handle.dma_buffer->v_addr, value1, value2);
            }
            break;
        }
        case 2: // HW_DEMUX_PES
        {
            break;
        }
        case 3: // HW_DEMUX_PCR
        {
            uiSTC = (unsigned int)value2 & 0x80000000;
            uiSTC |= (unsigned int)value1 >> 1;
            TSDEMUX_UpdatePCR(uiSTC / 45, dmxid);
            break;
        }
        default:
        {
            printk("Invalid parameter: Filter Type : %d\n", ftype);
            break;
        }
    }

    return ret;
}

int tcc_ex_tsif_set_external_tsdemux(int (*decoder)(char *p1, int p1_size, char *p2, int p2_size), int max_dec_packet)
{
    struct tcc_hwdmx_handle *demux = &tsif_ex_pri.demux[0];

    if(max_dec_packet == 0) {
        //turn off external decoding
        memset(&demux->ts_demux_feed_handle, 0x0, sizeof(struct ts_demux_feed_struct));
        return 0;
    }
    if(demux->ts_demux_feed_handle.call_decoder_index != max_dec_packet) {
        demux->ts_demux_feed_handle.is_active = 1;
        demux->ts_demux_feed_handle.ts_demux_decoder = decoder;
        demux->ts_demux_feed_handle.index = 0;
        demux->ts_demux_feed_handle.call_decoder_index = max_dec_packet; //every max_dec_packet calling isr, call decoder
        printk("%s::%d::max_dec_packet[%d]int_packet[%d]\n", __func__, __LINE__, demux->ts_demux_feed_handle.call_decoder_index, demux->tsif_ex_handle.dma_intr_packet_cnt);
    }
    return 0;
}

static char is_use_tsif_export_ioctl(void)
{
	return use_tsif_export_ioctl;
}

static ssize_t tcc_tsif_copy_from_user(void *dest, void *src, size_t copy_size)
{
	int ret = 0;
	if(is_use_tsif_export_ioctl() == 1) {
		memcpy(dest, src, copy_size);
	} else {
		ret = copy_from_user(dest, src, copy_size);
	}
	return ret;
}

static ssize_t tcc_tsif_copy_to_user(void *dest, void *src, size_t copy_size)
{
	int ret = 0;
	if(is_use_tsif_export_ioctl() == 1) {
		memcpy(dest, src, copy_size);
	} else {
		ret = copy_to_user(dest, src, copy_size);
	}
	return ret;
}

static ssize_t tcc_tsif_read(struct file *filp, char *buf, size_t len, loff_t *ppos)
{
    struct tcc_hwdmx_handle *demux = filp->private_data;
    ssize_t ret = 0;

    char *packet_data = NULL;
    char *pBuffer = demux->tsif_ex_handle.dma_buffer->v_addr;

    packet_data = (char *)(pBuffer + demux->read_buff_offset);

    if(packet_data){
        if (copy_to_user(buf, packet_data, len)) {
            return -EFAULT;
        }
        ret = len;
    }

    demux->read_buff_offset += len;
    if(demux->end_buff_offset <= demux->read_buff_offset) {
        demux->read_buff_offset = 0;
    }

    return ret;
}

static unsigned int tcc_tsif_poll(struct file *filp, struct poll_table_struct *wait)
{
    struct tcc_hwdmx_handle *demux = filp->private_data;

    if (demux->read_buff_offset != demux->write_buff_offset) {
		return  (POLLIN | POLLRDNORM);
    }

    poll_wait(filp, &(demux->wait_q), wait);

    if (demux->read_buff_offset != demux->write_buff_offset) {
		return  (POLLIN | POLLRDNORM);
    }
    return 0;
}

static ssize_t tcc_tsif_write(struct file *filp, const char *buf, size_t len, loff_t *ppos)
{
    return 0;
}

static long tcc_tsif_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct tcc_hwdmx_handle *demux = filp->private_data;
    int ret = 0;

    switch (cmd) {
    case IOCTL_TSIF_DMA_START:
        {
            int i;
            struct tcc_tsif_param param;
            if (tcc_tsif_copy_from_user(&param, (void *)arg, sizeof(struct tcc_tsif_param))) {
                printk("cannot copy from user tcc_tsif_param in IOCTL_TSIF_DMA_START !!! \n");
                return -EFAULT;
            }
            if (demux->state == 1) {
                demux->state = 0;
                tcc_tsif_deinit(demux);
            }
            demux->dma_mode = param.dma_mode;
            for (i = 0; i < HWDMX_NUM; i++)
            {
                if (demux == &tsif_ex_pri.demux[i])
                    break;
            }
            demux->state = 1;
            tcc_tsif_init(NULL, demux, i);
        }
        break;			
    case IOCTL_TSIF_DMA_STOP:
        {
            demux->state = 0;
            tcc_tsif_deinit(demux);
        }
        break;			
    case IOCTL_TSIF_GET_MAX_DMA_SIZE:
        {
        }
        break;        
    case IOCTL_TSIF_SET_PID:
        {
            struct tcc_tsif_pid_param pid_param;
            struct tcc_tsif_filter filter_param;
            int i, j;

            if (tcc_tsif_copy_from_user((void *)&pid_param, (void*)arg, sizeof(struct tcc_tsif_pid_param)))
                return -EFAULT;

            // check changed pid
            for (i = 0; i < demux->ts_pid.valid_data_cnt; i++)
            {
                for (j = 0; j < pid_param.valid_data_cnt; j++)
                {
                    if (demux->ts_pid.pid_data[i] == pid_param.pid_data[j])
                    {
                        demux->ts_pid.pid_data[i] = pid_param.pid_data[j] = -1;
                        break;
                    }
                }
            }

            filter_param.f_id = 0;
            filter_param.f_type = 1;
            filter_param.f_size = 0;
            filter_param.f_comp = NULL;
            filter_param.f_mask = NULL;
            filter_param.f_mode = NULL;

            // delete pid
            for (i = 0; i < demux->ts_pid.valid_data_cnt; i++)
            {
                if (demux->ts_pid.pid_data[i] != -1)
                {
                    filter_param.f_pid = demux->ts_pid.pid_data[i];
                    ret = tca_tsif_remove_filter(&demux->tsif_ex_handle, &filter_param);
                }
            }

            // add pid
            for (i = 0; i < pid_param.valid_data_cnt; i++)
            {
                if (pid_param.pid_data[i] != -1)
                {
                    filter_param.f_pid = pid_param.pid_data[i];
                    ret = tca_tsif_add_filter(&demux->tsif_ex_handle, &filter_param);
                }
            }

            // save pid
            if (tcc_tsif_copy_from_user((void *)&demux->ts_pid, (void*)arg, sizeof(struct tcc_tsif_pid_param)))
                return -EFAULT;
        } 
        break;		    
    case IOCTL_TSIF_DXB_POWER:
    	{
    	}
        break;
    case IOCTL_TSIF_ADD_PID:
        {
            struct tcc_tsif_filter param;

            if (tcc_tsif_copy_from_user((void *)&param, (void*)arg, sizeof(struct tcc_tsif_filter)))
                return -EFAULT;

            ret = tca_tsif_add_filter(&demux->tsif_ex_handle, &param);
        }
        break;
    case IOCTL_TSIF_REMOVE_PID:
        {
            struct tcc_tsif_filter param;

            if (tcc_tsif_copy_from_user((void *)&param, (void*)arg, sizeof(struct tcc_tsif_filter)))
                return -EFAULT;

            ret = tca_tsif_remove_filter(&demux->tsif_ex_handle, &param);
        }
        break;
    case IOCTL_TSIF_SET_PCRPID:
        {
            struct tcc_tsif_pcr_param param;

            if (tcc_tsif_copy_from_user((void *)&param, (void*)arg, sizeof(struct tcc_tsif_pcr_param)))
                return -EFAULT;

            if (param.index >= MAX_PCR_CNT) return -EFAULT;

            if(param.pcr_pid < 0x1FFF)
            {
                TSDEMUX_Open(param.index);
            }
            demux->pcr_pid[param.index] = param.pcr_pid;
            if (param.index == 0) {
                ret = tca_tsif_set_pcrpid(&demux->tsif_ex_handle, demux->pcr_pid[param.index]);
                tca_tsif_get_stc(&demux->tsif_ex_handle);
            }
        }		
        break;
    case IOCTL_TSIF_GET_STC:
        {
            unsigned int uiSTC, index;

            if (tcc_tsif_copy_from_user((void*)&index, (void*)arg, sizeof(int)))
                return - EFAULT;

            uiSTC = TSDEMUX_GetSTC(index);
            if (tcc_tsif_copy_to_user((void *)arg, (void *)&uiSTC, sizeof(int))) {
                printk("cannot copy to user tcc_tsif_param in IOCTL_TSIF_GET_PCR !!! \n");
                return -EFAULT;
            }
        }
        break;
    case IOCTL_TSIF_RESET:
        {
        }
        break;
    default:
        printk("tcc-tsif : unrecognized ioctl (0x%X)\n", cmd);
        ret = -EINVAL;
        break;
    }

    return ret;
}

static int tcc_tsif_init(struct firmware *fw, struct tcc_hwdmx_handle *demux, int id)
{
    struct tea_dma_buf *dma_buffer;
    memset(&demux->tsif_ex_handle, 0, sizeof(tcc_tsif_handle_t));
    demux->tsif_ex_handle.dmx_id = id;

    dma_buffer = kmalloc(sizeof(struct tea_dma_buf), GFP_KERNEL);
    if(dma_buffer) {
        if(g_static_dma_buffer) {
            dma_buffer->buf_size = g_static_dma_buffer->buf_size;
            dma_buffer->v_addr = g_static_dma_buffer->v_addr;
            dma_buffer->dma_addr = g_static_dma_buffer->dma_addr;
            dma_buffer->buf_sec_size = g_static_dma_buffer->buf_sec_size;
            dma_buffer->v_sec_addr = g_static_dma_buffer->v_sec_addr;
            dma_buffer->dma_sec_addr = g_static_dma_buffer->dma_sec_addr;
        } else {
            dma_buffer->buf_size = TSIF_DMA_SIZE;
            dma_buffer->v_addr = dma_alloc_writecombine(0, dma_buffer->buf_size, &dma_buffer->dma_addr, GFP_KERNEL);
            printk("tcc893x_tsif : dma buffer alloc @0x%X(Phy=0x%X), size:%d\n", (unsigned int)dma_buffer->v_addr, (unsigned int)dma_buffer->dma_addr, dma_buffer->buf_size);
            if(dma_buffer->v_addr == NULL) {
                kfree(dma_buffer);
                dma_buffer = NULL;
            } else {
                dma_buffer->buf_sec_size = SECTION_DMA_SIZE;
                dma_buffer->v_sec_addr = dma_alloc_writecombine(0, dma_buffer->buf_sec_size, &dma_buffer->dma_sec_addr, GFP_KERNEL);
                printk("tcc893x_tsif : dma sec buffer alloc @0x%X(Phy=0x%X), size:%d\n", (unsigned int)dma_buffer->v_sec_addr, (unsigned int)dma_buffer->dma_sec_addr, dma_buffer->buf_sec_size);
                if(dma_buffer->v_sec_addr == NULL) {
                    if(dma_buffer->v_addr) {
                        dma_free_writecombine(0, dma_buffer->buf_size, dma_buffer->v_addr, dma_buffer->dma_addr);
                        printk("tcc893x_tsif_deinit : dma buffer free @0x%X(Phy=0x%X), size:%d\n", (unsigned int)dma_buffer->v_addr, (unsigned int)dma_buffer->dma_addr, dma_buffer->buf_size);
                    }
                    kfree(dma_buffer);
                    dma_buffer = NULL;
                }
            }
        }
    }

    if (dma_buffer == NULL) {
        return -ENOMEM;
    }

    demux->tsif_ex_handle.dma_buffer = dma_buffer;
    if (fw && fw->size)
    {
        demux->tsif_ex_handle.fw_data = fw->data;
        demux->tsif_ex_handle.fw_size = fw->size;
    }
    else
    {
        demux->tsif_ex_handle.fw_data = NULL;
        demux->tsif_ex_handle.fw_size = 0;
    }

    demux->tsif_ex_handle.dma_mode = demux->dma_mode;
    //Check working mode
    if( demux->tsif_ex_handle.dma_mode == 0) {
        //not mepg2ts mode
        demux->tsif_ex_handle.working_mode = 0; //tsif for tdmb
    }
    else {
        //mpeg2ts mode
        demux->tsif_ex_handle.working_mode = 1; //tsif for isdbt & dvbt
    }
            
    printk("%s : dma_mode[%d] working_mod[%d]\n",__func__, demux->tsif_ex_handle.dma_mode, demux->tsif_ex_handle.working_mode);
    if (tca_tsif_init(&demux->tsif_ex_handle)) {
        printk("%s: tca_tsif_init error !!!!!\n", __func__);
		if(dma_buffer) {
            if(g_static_dma_buffer) {
            } else {
                if(dma_buffer->v_addr) {
                    dma_free_writecombine(0, dma_buffer->buf_size, dma_buffer->v_addr, dma_buffer->dma_addr);
                    printk("tcc893x_tsif_init : dma buffer free @0x%X(Phy=0x%X), size:%d\n", (unsigned int)dma_buffer->v_addr, (unsigned int)dma_buffer->dma_addr, dma_buffer->buf_size);
                }
                if(dma_buffer->v_sec_addr) {
                    dma_free_writecombine(0, dma_buffer->buf_sec_size, dma_buffer->v_sec_addr, dma_buffer->dma_sec_addr);
                    printk("tcc893x_tsif_deinit : dma sec buffer free @0x%X(Phy=0x%X), size:%d\n", (unsigned int)dma_buffer->v_sec_addr, (unsigned int)dma_buffer->dma_sec_addr, dma_buffer->buf_sec_size);
                }
            }
			kfree(dma_buffer);
			demux->tsif_ex_handle.dma_buffer = NULL;
		}
        tca_tsif_clean(&demux->tsif_ex_handle);
        return -EBUSY;
    }

    demux->write_buff_offset = 0;
    demux->read_buff_offset = 0;
    demux->end_buff_offset = 0;
    demux->loop_count = 0;

	tca_tsif_buffer_updated_callback(&demux->tsif_ex_handle, tcc_tsif_updated_callback);

    return 0;
}

static void tcc_tsif_deinit(struct tcc_hwdmx_handle *demux)
{
    struct tea_dma_buf *dma_buffer = demux->tsif_ex_handle.dma_buffer;

    if(demux->mem)
        vfree(demux->mem);
    demux->mem = NULL;

    demux->write_buff_offset = 0;
    demux->read_buff_offset = 0;
    demux->end_buff_offset = 0;
    demux->loop_count = 0;

    tca_tsif_clean(&demux->tsif_ex_handle);

    if(dma_buffer) {
        if(g_static_dma_buffer) {
        } else {
            if(dma_buffer->v_addr) {
                dma_free_writecombine(0, dma_buffer->buf_size, dma_buffer->v_addr, dma_buffer->dma_addr);
                printk("tcc893x_tsif_deinit : dma buffer free @0x%X(Phy=0x%X), size:%d\n", (unsigned int)dma_buffer->v_addr, (unsigned int)dma_buffer->dma_addr, dma_buffer->buf_size);
            }
            if(dma_buffer->v_sec_addr) {
                dma_free_writecombine(0, dma_buffer->buf_sec_size, dma_buffer->v_sec_addr, dma_buffer->dma_sec_addr);
                printk("tcc893x_tsif_deinit : dma sec buffer free @0x%X(Phy=0x%X), size:%d\n", (unsigned int)dma_buffer->v_sec_addr, (unsigned int)dma_buffer->dma_sec_addr, dma_buffer->buf_sec_size);
            }
        }
        kfree(dma_buffer);
        demux->tsif_ex_handle.dma_buffer = NULL;
    }
}


static int tcc_tsif_open(struct inode *inode, struct file *filp)
{
    struct tcc_hwdmx_handle *demux;
    int i, ret = -EBUSY, err;
    const struct firmware *fw = NULL;

    mutex_lock(&(tsif_ex_pri.mutex));
    for (i = 0; i < HWDMX_NUM; i++)
    {
        demux = &tsif_ex_pri.demux[i];
        if (demux->state == 0)
        {
#if 1
            if (tsif_ex_pri.open_cnt == 0 && filp->private_data == NULL && pdev) {
                err = request_firmware(&fw, fw_name, pdev);
                if (err == 0)
                {
                    filp->private_data = (void *)fw;
                }
                else
                {
                    fw = NULL;
                    printk("Failed to load[ID:%d, Err:%d]\n", i, err);
                    break;
                }
            }
#endif
            demux->dma_mode = 1; //0:normal mode (not mpeg2ts mode), 1:mpeg2ts mode(start with 0x47)
            if (tcc_tsif_init((struct firmware *)filp->private_data, demux, i)) {
                break;
            }

            demux->state = 1;
            filp->private_data = demux;

            init_waitqueue_head(&(demux->wait_q));

            tsif_ex_pri.open_cnt++;
            if (tsif_ex_pri.open_cnt == 1)
            {
                init_timer(&tsif_ex_pri.timer);
                tsif_ex_pri.timer.data = 0;
                tsif_ex_pri.timer.function = tcc_tsif_timer_handler;
                tsif_ex_pri.timer.expires = jiffies + msecs_to_jiffies(1000);//msecs_to_jiffies(125);
                add_timer(&tsif_ex_pri.timer);
            }
            ret = 0;
            break;
        }
    }
    if (fw)
    {
        release_firmware(fw);
    }
    mutex_unlock(&(tsif_ex_pri.mutex));
    return ret;
}


static int tcc_tsif_release(struct inode *inode, struct file *filp)
{
    struct tcc_hwdmx_handle *demux = filp->private_data;

   	mutex_lock(&(tsif_ex_pri.mutex));   	   
    if (tsif_ex_pri.open_cnt > 0)
    {
        if (demux->state == 1)
        {
            tsif_ex_pri.open_cnt--;
            if (tsif_ex_pri.open_cnt == 0)
            {
                del_timer(&tsif_ex_pri.timer);
            }

            demux->state = 0;
            tcc_tsif_deinit(demux);
        }
    }
    mutex_unlock(&(tsif_ex_pri.mutex));
    return 0;
}

struct file_operations tcc_tsif_ex_fops = {
    .owner          = THIS_MODULE,
    .read           = tcc_tsif_read,
    .write          = tcc_tsif_write,
    .unlocked_ioctl = tcc_tsif_ioctl,
    .open           = tcc_tsif_open,
    .release        = tcc_tsif_release,
    .poll           = tcc_tsif_poll,
};

int tsif_ex_init(void)
{
    int ret;

    memset(&tsif_ex_pri, 0, sizeof(struct tcc_tsif_pri_handle));
    mutex_init(&(tsif_ex_pri.mutex));
    ret = register_chrdev(0, TSIF_DEV_NAME, &tcc_tsif_ex_fops);
    if (ret < 0) {
        printk("[%s:%d] register_chrdev error !!!!!\n", __func__, __LINE__); 
        return ret;
    }
    tsif_ex_pri.drv_major_num = ret;
    printk("[%s:%d] major number = %d\n", __func__, __LINE__, tsif_ex_pri.drv_major_num);

    g_static_dma_buffer = NULL;
#if defined(USE_STATIC_DMA_BUFFER)
    g_static_dma_buffer = kmalloc(sizeof(struct tea_dma_buf), GFP_KERNEL);
    if(g_static_dma_buffer) {
        g_static_dma_buffer->buf_size = TSIF_DMA_SIZE;
        g_static_dma_buffer->v_addr = dma_alloc_writecombine(0, g_static_dma_buffer->buf_size, &g_static_dma_buffer->dma_addr, GFP_KERNEL);
        printk("tcc893x_tsif : static dma buffer alloc @0x%X(Phy=0x%X), size:%d\n", (unsigned int)g_static_dma_buffer->v_addr, (unsigned int)g_static_dma_buffer->dma_addr, g_static_dma_buffer->buf_size);
        if(g_static_dma_buffer->v_addr == NULL) {
            kfree(g_static_dma_buffer);
            g_static_dma_buffer = NULL;
        } else {
            g_static_dma_buffer->buf_sec_size = SECTION_DMA_SIZE;
            g_static_dma_buffer->v_sec_addr = dma_alloc_writecombine(0, g_static_dma_buffer->buf_sec_size, &g_static_dma_buffer->dma_sec_addr, GFP_KERNEL);
            printk("tcc893x_tsif : static dma sec buffer alloc @0x%X(Phy=0x%X), size:%d\n", (unsigned int)g_static_dma_buffer->v_sec_addr, (unsigned int)g_static_dma_buffer->dma_sec_addr, g_static_dma_buffer->buf_sec_size);
            if(g_static_dma_buffer->v_sec_addr == NULL) {
                if(g_static_dma_buffer->v_addr) {
                    dma_free_writecombine(0, g_static_dma_buffer->buf_size, g_static_dma_buffer->v_addr, g_static_dma_buffer->dma_addr);
                    printk("tcc893x_tsif_deinit : static dma buffer free @0x%X(Phy=0x%X), size:%d\n", (unsigned int)g_static_dma_buffer->v_addr, (unsigned int)g_static_dma_buffer->dma_addr, g_static_dma_buffer->buf_size);
                }
                kfree(g_static_dma_buffer);
                g_static_dma_buffer = NULL;
            }
        }
    }
#endif

    tsif_ex_class = class_create(THIS_MODULE, TSIF_DEV_NAME);
    pdev = device_create(tsif_ex_class, NULL, MKDEV(tsif_ex_pri.drv_major_num, 1), NULL, TSIF_DEV_NAME);

    return 0;
}

void tsif_ex_exit(void)
{
    if(g_static_dma_buffer) {
        if(g_static_dma_buffer->v_addr) {
            dma_free_writecombine(0, g_static_dma_buffer->buf_size, g_static_dma_buffer->v_addr, g_static_dma_buffer->dma_addr);
            printk("tcc893x_tsif_deinit : static dma buffer free @0x%X(Phy=0x%X), size:%d\n", (unsigned int)g_static_dma_buffer->v_addr, (unsigned int)g_static_dma_buffer->dma_addr, g_static_dma_buffer->buf_size);
        }
        if(g_static_dma_buffer->v_sec_addr) {
            dma_free_writecombine(0, g_static_dma_buffer->buf_sec_size, g_static_dma_buffer->v_sec_addr, g_static_dma_buffer->dma_sec_addr);
            printk("tcc893x_tsif_deinit : static dma sec buffer free @0x%X(Phy=0x%X), size:%d\n", (unsigned int)g_static_dma_buffer->v_sec_addr, (unsigned int)g_static_dma_buffer->dma_sec_addr, g_static_dma_buffer->buf_sec_size);
        }
        kfree(g_static_dma_buffer);
		g_static_dma_buffer = NULL;
    }

    unregister_chrdev(tsif_ex_pri.drv_major_num, TSIF_DEV_NAME);
    device_destroy(tsif_ex_class, MKDEV(tsif_ex_pri.drv_major_num, 1));
    class_destroy(tsif_ex_class);
    pdev = NULL;
}

int tcc_ex_tsif_open(struct inode *inode, struct file *filp)
{
    return tcc_tsif_open(inode, filp);
}

int tcc_ex_tsif_release(struct inode *inode, struct file *filp)
{
    return tcc_tsif_release(inode, filp);
}

int tcc_ex_tsif_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret = 0;

    use_tsif_export_ioctl = 1;
    ret = tcc_tsif_ioctl(filp, cmd, arg);
    use_tsif_export_ioctl = 0;

    return ret;
}

EXPORT_SYMBOL(tsif_ex_init);
EXPORT_SYMBOL(tsif_ex_exit);
EXPORT_SYMBOL(tcc_ex_tsif_open);
EXPORT_SYMBOL(tcc_ex_tsif_release);
EXPORT_SYMBOL(tcc_ex_tsif_ioctl);
EXPORT_SYMBOL(tcc_ex_tsif_set_external_tsdemux);
