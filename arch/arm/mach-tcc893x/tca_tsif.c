/****************************************************************************
 *   FileName    :  tcc_tsif_hwset.c
 *   Description : 
 ****************************************************************************
 *
 *   TCC Version 1.0
 *   Copyright (c) Telechips, Inc.
 *   ALL RIGHTS RESERVED
 *
 ****************************************************************************/

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include <asm/io.h>
#include <asm/mach-types.h>

#include <mach/bsp.h>
#include <mach/io.h>
#include <mach/tcc_dxb_ctrl.h>
#include <mach/tca_tsif.h>
#include <mach/TCC893xHWDemux_cmd.h>

#define DEVICE_NAME	"mailbox"

#define MBOX_IRQ    INT_CM3MB
//#define SUPPORT_DEBUG_CM3HWDEMUX
#ifdef  SUPPORT_DEBUG_CM3HWDEMUX
#define CMD_TIMEOUT        1000000
#else
#define CMD_TIMEOUT        100
#endif

static struct clk *cm3_clk = NULL;

enum {
    QUEUE_HW_DEMUX_INIT = 0,
    QUEUE_HW_DEMUX_DEINIT,
    QUEUE_HW_DEMUX_ADD_FILTER,
    QUEUE_HW_DEMUX_DELETE_FILTER,
    QUEUE_HW_DEMUX_GET_STC,
    QUEUE_HW_DEMUX_SET_PCR_PID,
    QUEUE_HW_DEMUX_GET_VERSION,
    QUEUE_HW_DEMUX_INTERNAL_SET_INPUT,
    QUEUE_HW_DEMUX_TYPE_MAX
};

wait_queue_head_t cmd_queue[4][QUEUE_HW_DEMUX_TYPE_MAX];
static int g_ret = 0;

static FN_UPDATECALLBACK ts_demux_buffer_updated_callback[4];

static struct clk *pktgen_clk[4];

static int g_initCount = 0;

int TSDMX_Init(struct tcc_tsif_handle *h)
{
    int ret = 0;
    ARG_TSDMXINIT Arg;

    g_initCount++;

    printk("\n%s:%d:0x%08X:0x%08X:0x%08X\n",__func__, __LINE__, h->dma_buffer->dma_addr, (unsigned int)h->dma_buffer->v_addr, h->dma_buffer->buf_size);
    Arg.uiDMXID  = h->dmx_id;

#if defined(CONFIG_iTV_PLATFORM)
    Arg.uiMode = HW_DEMUX_BYPASS;
#else
    Arg.uiMode = HW_DEMUX_NORMAL; //HW_DEMUX_BYPASS
#endif    
    
    Arg.uiTSRingBufAddr = h->dma_buffer->dma_addr;
    Arg.uiTSRingBufSize = h->dma_buffer->buf_size;
    Arg.uiSECRingBufAddr = h->dma_buffer->dma_sec_addr;
    Arg.uiSECRingBufSize = h->dma_buffer->buf_sec_size;
    switch(h->working_mode)
    {
        case 0x00: //tsif for tdmb
            Arg.uiTSIFInterface = HW_DEMUX_SPI;
            break;
        case 0x02:
            Arg.uiTSIFInterface = HW_DEMUX_INTERNAL;
            break;     

        default: //0x01 for tsif of isdbt & dvbt
            if(h->serial_mode == 1)
                Arg.uiTSIFInterface = HW_DEMUX_TSIF_SERIAL;
            else
                Arg.uiTSIFInterface = HW_DEMUX_TSIF_PARALLEL;
            break;
    }
    
    if(Arg.uiTSIFInterface == HW_DEMUX_INTERNAL)
    {
        if(Arg.uiDMXID != 1) //in case of HW_DEMUX_INTERNAL mode, the demux id should be 1.
        {
            printk("[hwdemuxer] error !!! in case of HW_DEMUX_INTERNAL, dmx_id shoud be 1...\n");
            return -1;
        }    
    }

    Arg.uiTSIFCh = 0;
    Arg.uiTSIFPort = h->tsif_port_num;

    //TSIf Polarity : TSP-Sync Pulse(Bit0) : 0(hight active), TVEP-Valid Data(Bit1) : 1(high active), TCKP(Bit2) : 0(riging edge of TSCLK)
    Arg.uiTSIFPol = Hw1;

    TSDMXCMD_Init(&Arg);

    if((ret = interruptible_sleep_on_timeout(&(cmd_queue[Arg.uiDMXID][QUEUE_HW_DEMUX_INIT]), CMD_TIMEOUT)) != 0)
        ret = g_ret;

    return ret;
}


int TSDMX_DeInit(unsigned int uiDMXID)
{
    int ret = 0;

    if (g_initCount == 0)
        return ret;

    g_initCount--;

    TSDMXCMD_DeInit(uiDMXID);

    if((ret = interruptible_sleep_on_timeout(&(cmd_queue[uiDMXID][QUEUE_HW_DEMUX_DEINIT]), CMD_TIMEOUT)) != 0)
        ret = g_ret;

    return ret;
}

int TSDMX_ADD_Filter(unsigned int uiDMXID, struct tcc_tsif_filter *feed)
{
    int ret = 0;
    ARG_TSDMX_ADD_FILTER Arg;
    Arg.uiDMXID = uiDMXID;
    Arg.uiTYPE = feed->f_type;
    Arg.uiPID = feed->f_pid;
    if(feed->f_type == HW_DEMUX_SECTION) {
        printk("pid : %d, size = %d\n", feed->f_pid, feed->f_size);
        Arg.uiFSIZE = feed->f_size;
        Arg.uiFID = feed->f_id;
        if(feed->f_size*3 <= 24) {
            Arg.uiTotalIndex = 1;
        } else {
            Arg.uiTotalIndex = 1;
            Arg.uiTotalIndex += (feed->f_size * 3) / 28;
            if((feed->f_size * 3) % 28) {
                Arg.uiTotalIndex ++;
            }
        }
        memset(Arg.uiVectorData, 0x00, 20*4);
        memcpy(((unsigned char*)Arg.uiVectorData), feed->f_comp, feed->f_size);
        memcpy(((unsigned char*)Arg.uiVectorData) + (feed->f_size), feed->f_mask, feed->f_size);
        memcpy(((unsigned char*)Arg.uiVectorData) + (feed->f_size*2), feed->f_mode, feed->f_size);
        Arg.uiVectorIndex = 0;
        Arg.uiVectorCount = ((feed->f_size * 3) / 4) + ((feed->f_size * 3) % 4) == 0 ? 0 : 1;
        for(Arg.uiCurrentIndex=0; Arg.uiCurrentIndex<Arg.uiTotalIndex; Arg.uiCurrentIndex ++) {
            TSDMXCMD_ADD_Filter(&Arg);

            if(interruptible_sleep_on_timeout(&(cmd_queue[Arg.uiDMXID][QUEUE_HW_DEMUX_ADD_FILTER]), CMD_TIMEOUT) != 0)
                ret = g_ret;
            if(ret != 0)
                break;
        }
    } else {
        TSDMXCMD_ADD_Filter(&Arg);
		
        if(interruptible_sleep_on_timeout(&(cmd_queue[Arg.uiDMXID][QUEUE_HW_DEMUX_ADD_FILTER]), CMD_TIMEOUT) != 0)
            ret = g_ret;
    }

    return ret;
}

int TSDMX_DELETE_Filter(unsigned int uiDMXID, struct tcc_tsif_filter *feed)
{
    int ret = 0;

    ARG_TSDMX_DELETE_FILTER Arg;
    Arg.uiDMXID = uiDMXID;
    Arg.uiTYPE = feed->f_type;
    if(feed->f_type == HW_DEMUX_SECTION)
        Arg.uiFID = feed->f_id;
    else
        Arg.uiFID = 0;
    Arg.uiPID = feed->f_pid;

    TSDMXCMD_DELETE_Filter(&Arg);

    if(interruptible_sleep_on_timeout(&(cmd_queue[Arg.uiDMXID][QUEUE_HW_DEMUX_DELETE_FILTER]), CMD_TIMEOUT) != 0)
        ret = g_ret;

    return ret;
}

long long TSDMX_GET_STC(unsigned int uiDMXID)
{
    return TSDMXCMD_GET_STC(uiDMXID);
}

int TSDMX_SET_PCR_PID(unsigned int uiDMXID, unsigned int pid)
{
    int ret = 0;

    ARG_TSDMX_SET_PCR_PID Arg;
    Arg.uiDMXID = uiDMXID;
    Arg.uiPCRPID = pid;

    TSDMXCMD_SET_PCR_PID(&Arg);

    if(interruptible_sleep_on_timeout(&(cmd_queue[Arg.uiDMXID][QUEUE_HW_DEMUX_SET_PCR_PID]), CMD_TIMEOUT) != 0)
        ret = g_ret;

    return ret;
}

int TSDMX_GET_Version(unsigned int uiDMXID)
{
    int ret = 0;
    TSDMXCMD_GET_VERSION(uiDMXID);
    if((ret = interruptible_sleep_on_timeout(&(cmd_queue[uiDMXID][QUEUE_HW_DEMUX_GET_VERSION]), CMD_TIMEOUT)) != 0)
        ret = g_ret;

    return ret;
}

int TSDMX_SET_InBuffer(unsigned int uiDMXID, unsigned int uiBufferAddr, unsigned int uiBufferSize)
{
    int ret = 0;
    ARG_TSDMX_SET_IN_BUFFER Arg;
    Arg.uiDMXID = uiDMXID;
    Arg.uiInBufferAddr = uiBufferAddr;
    Arg.uiInBufferSize = uiBufferSize;

    TSDMXCMD_SET_IN_BUFFER(&Arg);
    if((ret = interruptible_sleep_on_timeout(&(cmd_queue[uiDMXID][QUEUE_HW_DEMUX_INTERNAL_SET_INPUT]), CMD_TIMEOUT)) != 0)
        ret = g_ret;

    return ret;
}


/*****************************************************************************
* Function Name : static void TSDMX_CM3UnloadBinary(void);
* Description : CM3 Code Un-Loading
* Arguments :
******************************************************************************/
static void TSDMX_CM3UnloadBinary(void)
{
    volatile PCM3_TSD_CFG pTSDCfg = (volatile PCM3_TSD_CFG)tcc_p2v(HwCORTEXM3_TSD_CFG_BASE);
    BITSET(pTSDCfg->CM3_RESET.nREG, Hw1|Hw2); //m3 no reset
    printk("%s:%d\n",__func__, __LINE__);
}

/*****************************************************************************
* Function Name : static void TSDMX_CM3LoadBinary(void);
* Description : CM3 Code Loading
* Arguments :
******************************************************************************/
static void TSDMX_CM3LoadBinary(unsigned char *fw_data, unsigned int fw_size)
{
    volatile unsigned int * pCodeMem = (volatile unsigned int *)tcc_p2v(HwCORTEXM3_CODE_MEM_BASE);
    volatile PCM3_TSD_CFG pTSDCfg = (volatile PCM3_TSD_CFG)tcc_p2v(HwCORTEXM3_TSD_CFG_BASE);

#ifndef     SUPPORT_DEBUG_CM3HWDEMUX
    TSDMX_CM3UnloadBinary();
    if(fw_data && fw_size > 0)
        memcpy((void *)pCodeMem, (void *)fw_data, fw_size);
    else
        printk("Using previous loading the firmware\n");
    BITCLR(pTSDCfg->CM3_RESET.nREG, Hw1|Hw2); //m3 reset
#endif    
}

/*****************************************************************************
* Function Name : static void TSDMX_CM3Configure(void);
* Description : MailBox register init
* Arguments :
******************************************************************************/
static void TSDMX_CM3Configure(void)
{
    volatile PMAILBOX pMailBoxMain = (volatile PMAILBOX)tcc_p2v(HwCORTEXM3_MAILBOX0_BASE);
    volatile PMAILBOX pMailBoxSub = (volatile PMAILBOX)tcc_p2v(HwCORTEXM3_MAILBOX1_BASE);
    volatile PCM3_TSD_CFG pTSDCfg = (volatile PCM3_TSD_CFG)tcc_p2v(HwCORTEXM3_TSD_CFG_BASE);
    BITSET(pMailBoxMain->uMBOX_CTL_016.nREG, Hw0|Hw1|Hw4|Hw5|Hw6);
    BITSET(pMailBoxSub->uMBOX_CTL_016.nREG, Hw0|Hw1|Hw4|Hw5|Hw6);
    BITSET(pTSDCfg->IRQ_MASK_POL.nREG, Hw16|Hw22); //IRQ_MASK_POL, IRQ, FIQ(CHANGE POLARITY)
}

static irqreturn_t MailBox_Handler(int irq, void *dev)
{	
    unsigned int value1, value2;
    volatile int nReg[8];
    unsigned char cmd, dmxid;
    volatile PMAILBOX pMailBox = (volatile PMAILBOX)tcc_p2v(HwCORTEXM3_MAILBOX0_BASE);

    nReg[0] = pMailBox->uMBOX_RX0.nREG;
    nReg[1] = pMailBox->uMBOX_RX1.nREG; // Updated Position
    nReg[2] = pMailBox->uMBOX_RX2.nREG; // End Buffer Position
    nReg[3] = pMailBox->uMBOX_RX3.nREG;
    nReg[4] = pMailBox->uMBOX_RX4.nREG;
    nReg[5] = pMailBox->uMBOX_RX5.nREG;
    nReg[6] = pMailBox->uMBOX_RX6.nREG;
    nReg[7] = pMailBox->uMBOX_RX7.nREG;

    cmd = (nReg[0]&0xFF000000)>>24;
    dmxid = (nReg[0]&0x00FF0000)>>16;

    switch(cmd)
    {
        case HW_DEMUX_INIT: // DEMUX_INIT
            g_ret = (nReg[0]&0x0000FFFF);
            wake_up_interruptible(&(cmd_queue[dmxid][QUEUE_HW_DEMUX_INIT]));
            break;

        case HW_DEMUX_DEINIT: // DEMUX_DEINIT
            g_ret = (nReg[0]&0x0000FFFF);
            wake_up_interruptible(&(cmd_queue[dmxid][QUEUE_HW_DEMUX_DEINIT]));
            break;

        case HW_DEMUX_ADD_FILTER: // DEMUX_ADD_FILTER
            g_ret = (nReg[0]&0x0000FFFF);
            wake_up_interruptible(&(cmd_queue[dmxid][QUEUE_HW_DEMUX_ADD_FILTER]));
            break;

        case HW_DEMUX_DELETE_FILTER: // DEMUX_DELETE_FILTER
            g_ret = (nReg[0]&0x0000FFFF);
            wake_up_interruptible(&(cmd_queue[dmxid][QUEUE_HW_DEMUX_DELETE_FILTER]));
            break;

        case HW_DEMUX_GET_STC: // DEMUX_GET_STC
            {
                //g_ret = (nReg[0]&0x0000FFFF);
                //wake_up_interruptible(&(cmd_queue[dmxid][QUEUE_HW_DEMUX_GET_STC]));
                int filterType = 3;
                value1 = nReg[1]; // STC Base
                value2 = nReg[2]; // Bit 31: STC Extension
                if(ts_demux_buffer_updated_callback[dmxid])
                {
                    ts_demux_buffer_updated_callback[dmxid](dmxid, filterType, 0, nReg[1], nReg[2], 0);
                }
            }
            break;

        case HW_DEMUX_SET_PCR_PID: // DEMUX_SET_PCR_PID
            g_ret = (nReg[0]&0x0000FFFF);
            wake_up_interruptible(&(cmd_queue[dmxid][QUEUE_HW_DEMUX_SET_PCR_PID]));
            break;
        case HW_DEMUX_GET_VERSION: // HW_DEMUX_GET_VERSION
            g_ret = (nReg[0]&0x0000FFFF);
            printk("[HWDMX]Version[%X] Date[%X]\n", nReg[1], nReg[2]);
            wake_up_interruptible(&(cmd_queue[dmxid][QUEUE_HW_DEMUX_GET_VERSION]));
            break;            
        case HW_DEMUX_INTERNAL_SET_INPUT: // HW_DEMUX_INTERNAL_SET_INPUT
            g_ret = (nReg[0]&0x0000FFFF);
            wake_up_interruptible(&(cmd_queue[dmxid][QUEUE_HW_DEMUX_INTERNAL_SET_INPUT]));
            break;            

        case HW_DEMUX_BUFFER_UPDATED: // DEMUX_BUFFER_UPDATED
            {
                unsigned int filterType = (nReg[0]&0x0000FF00)>>8;
                unsigned int filterID = nReg[0]&0x000000FF;
                unsigned int bErrCRC = (nReg[3]&80000000) ? 1 : 0;
                value1 = nReg[1];     // ts: end point of updated buffer(offset), sec: start point of updated buffer(offset)
                value2 = nReg[2] + 1; // ts: end point of buffer(offset),         sec: end point of updated buffer(offset)
                if (filterType == HW_DEMUX_TS)
                {
                    value1++;
                }
                else
                {
#ifdef     SUPPORT_DEBUG_CM3HWDEMUX
                //    if(filterType == HW_DEMUX_SECTION)
                //        printk("Section:[0x%X][0x%X][0x%X][0x%X][0x%08X]\n", nReg[0], nReg[1], nReg[2], nReg[3], nReg[4]); 
#endif                    
                }
                if(ts_demux_buffer_updated_callback[dmxid])
                {
                    ts_demux_buffer_updated_callback[dmxid](dmxid, filterType, filterID, value1, value2, bErrCRC);
                }
            }
            break;
        case HW_DEMUX_DEBUG_DATA:
            {
                unsigned char str[9];
                unsigned int *p = (unsigned int *)str;
                p[0] = nReg[1];
                p[1] = nReg[2];
                str[8] = 0;
				
#if !defined(CONFIG_iTV_PLATFORM)
                printk("[HWDMX]%s :[0x%X][0x%X][0x%X][0x%X][0x%X].\n", str, nReg[3], nReg[4], nReg[5], nReg[6], nReg[7]);
#endif
            }
            break;
        default:
            break;
    }

    return IRQ_HANDLED;
}

int tca_tsif_set_port(struct tcc_tsif_handle *h)
{
    int ret = 0;
    printk("%s : select port => %d\n", __func__, h->tsif_port_num);

    switch (h->tsif_port_num) {
        case 0://TS0
            tcc_gpio_config(TCC_GPB(0), GPIO_FN(7));		//clk
            tcc_gpio_config(TCC_GPB(2), GPIO_FN(7));		//valid
            tcc_gpio_config(TCC_GPB(1), GPIO_FN(7));		//sync
            tcc_gpio_config(TCC_GPB(3), GPIO_FN(7));		//d0
            if (h->serial_mode == 0) {
                tcc_gpio_config(TCC_GPB(4), GPIO_FN(7));	//d1
                tcc_gpio_config(TCC_GPB(5), GPIO_FN(7));	//d2
                tcc_gpio_config(TCC_GPB(6), GPIO_FN(7));	//d3
                tcc_gpio_config(TCC_GPB(7), GPIO_FN(7));	//d4
                tcc_gpio_config(TCC_GPB(8), GPIO_FN(7));	//d5
                tcc_gpio_config(TCC_GPB(9), GPIO_FN(7));	//d6
                tcc_gpio_config(TCC_GPB(10), GPIO_FN(7));	//d7
            }
            break;

        case 1://TS1
            tcc_gpio_config(TCC_GPB(28), GPIO_FN(7));		//clk
            tcc_gpio_config(TCC_GPB(26), GPIO_FN(7));		//valid
            tcc_gpio_config(TCC_GPB(27), GPIO_FN(7));		//sync
            tcc_gpio_config(TCC_GPB(25), GPIO_FN(7));		//d0
            if (h->serial_mode == 0) {
                tcc_gpio_config(TCC_GPB(24), GPIO_FN(7));	//d1
                tcc_gpio_config(TCC_GPB(23), GPIO_FN(7));	//d2
                tcc_gpio_config(TCC_GPB(22), GPIO_FN(7));	//d3
                tcc_gpio_config(TCC_GPB(21), GPIO_FN(7));	//d4
                tcc_gpio_config(TCC_GPB(20), GPIO_FN(7));	//d5
                tcc_gpio_config(TCC_GPB(19), GPIO_FN(7));	//d6
                tcc_gpio_config(TCC_GPB(18), GPIO_FN(7));	//d7
            }
            break;

        case 2://TS2
            tcc_gpio_config(TCC_GPB(24), GPIO_FN(8));		//clk
            tcc_gpio_config(TCC_GPB(22), GPIO_FN(8));		//valid
            tcc_gpio_config(TCC_GPB(23), GPIO_FN(8));		//sync
            tcc_gpio_config(TCC_GPB(21), GPIO_FN(8));		//d0
            break;

		case 3://TS3
            tcc_gpio_config(TCC_GPD(8), GPIO_FN(15));		//clk
            tcc_gpio_config(TCC_GPD(9), GPIO_FN(15));		//valid
            tcc_gpio_config(TCC_GPD(10), GPIO_FN(15));		//sync
            tcc_gpio_config(TCC_GPD(7), GPIO_FN(15));		//d0
            if (h->serial_mode == 0) {
                tcc_gpio_config(TCC_GPD(6), GPIO_FN(15));	//d1
                tcc_gpio_config(TCC_GPD(5), GPIO_FN(15));	//d2
                tcc_gpio_config(TCC_GPD(4), GPIO_FN(15));	//d3
                tcc_gpio_config(TCC_GPD(3), GPIO_FN(15));	//d4
                tcc_gpio_config(TCC_GPD(2), GPIO_FN(15));	//d5
                tcc_gpio_config(TCC_GPD(1), GPIO_FN(15));	//d6
                tcc_gpio_config(TCC_GPD(0), GPIO_FN(15));	//d7
            }
            break;

        case 4://TS4
            tcc_gpio_config(TCC_GPD(8), GPIO_FN(3));		//clk
            tcc_gpio_config(TCC_GPD(9), GPIO_FN(3));		//valid
            tcc_gpio_config(TCC_GPD(10), GPIO_FN(3));		//sync
            tcc_gpio_config(TCC_GPD(7), GPIO_FN(3));		//d0
            break;

        case 5://TS5
            tcc_gpio_config(TCC_GPE(27), GPIO_FN(4));		//clk
            tcc_gpio_config(TCC_GPE(25), GPIO_FN(4));		//valid
            tcc_gpio_config(TCC_GPE(26), GPIO_FN(4));		//sync
            tcc_gpio_config(TCC_GPE(24), GPIO_FN(4));		//d0
            if (h->serial_mode == 0) {
                tcc_gpio_config(TCC_GPE(23), GPIO_FN(4));	//d1
                tcc_gpio_config(TCC_GPE(22), GPIO_FN(4));	//d2
                tcc_gpio_config(TCC_GPE(21), GPIO_FN(4));	//d3
                tcc_gpio_config(TCC_GPE(20), GPIO_FN(4));	//d4
                tcc_gpio_config(TCC_GPE(19), GPIO_FN(4));	//d5
                tcc_gpio_config(TCC_GPE(18), GPIO_FN(4));	//d6
                tcc_gpio_config(TCC_GPE(17), GPIO_FN(4));	//d7
            }
            break;		

        case 6://TS6
            tcc_gpio_config(TCC_GPE(20), GPIO_FN(5));		//clk
            tcc_gpio_config(TCC_GPE(18), GPIO_FN(5));		//valid
            tcc_gpio_config(TCC_GPE(19), GPIO_FN(5));		//sync
            tcc_gpio_config(TCC_GPE(17), GPIO_FN(5));		//d0
            break;		

        case 7://TS7
            tcc_gpio_config(TCC_GPF(11), GPIO_FN(3));		//clk
            tcc_gpio_config(TCC_GPF(13), GPIO_FN(3));		//valid
            tcc_gpio_config(TCC_GPF(12), GPIO_FN(3));		//sync
            tcc_gpio_config(TCC_GPF(14), GPIO_FN(3));		//d0
            break;		

        default:
            printk("%s : select wrong port => %d\n", __func__, h->tsif_port_num);
            ret = -1;
            break;
	}
    return ret;
}

int tca_tsif_release_port(struct tcc_tsif_handle *h)
{
#if 0
    volatile unsigned long* TSIFPORT = (volatile unsigned long *)tcc_p2v(HwTSIF_TSCHS_BASE);
#endif
    int ret = 0;
    printk("%s : select port => %d\n", __func__, h->tsif_port_num);

    switch (h->tsif_port_num) {
        case 0:
            tcc_gpio_config(TCC_GPB(0), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//clk
            tcc_gpio_config(TCC_GPB(2), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//valid
            tcc_gpio_config(TCC_GPB(1), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//sync
            tcc_gpio_config(TCC_GPB(3), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//d0
            if (h->serial_mode == 0) {
                tcc_gpio_config(TCC_GPB(4), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d1
                tcc_gpio_config(TCC_GPB(5), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d2
                tcc_gpio_config(TCC_GPB(6), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d3
                tcc_gpio_config(TCC_GPB(7), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d4
                tcc_gpio_config(TCC_GPB(8), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d5
                tcc_gpio_config(TCC_GPB(9), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d6
                tcc_gpio_config(TCC_GPB(10), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d7
            }
            break;

        case 1:
            tcc_gpio_config(TCC_GPB(28), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//clk
            tcc_gpio_config(TCC_GPB(26), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//valid
            tcc_gpio_config(TCC_GPB(27), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//sync
            tcc_gpio_config(TCC_GPB(25), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//d0
            if (h->serial_mode == 0) {
                tcc_gpio_config(TCC_GPB(24), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d1
                tcc_gpio_config(TCC_GPB(23), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d2
                tcc_gpio_config(TCC_GPB(22), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d3
                tcc_gpio_config(TCC_GPB(21), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d4
                tcc_gpio_config(TCC_GPB(20), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d5
                tcc_gpio_config(TCC_GPB(19), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d6
                tcc_gpio_config(TCC_GPB(18), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d7
            }
            break;

        case 2:
            tcc_gpio_config(TCC_GPB(24), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//clk
            tcc_gpio_config(TCC_GPB(22), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//valid
            tcc_gpio_config(TCC_GPB(23), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//sync
            tcc_gpio_config(TCC_GPB(21), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//d0
            break;

		case 3:
            tcc_gpio_config(TCC_GPD(8), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//clk
            tcc_gpio_config(TCC_GPD(9), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//valid
            tcc_gpio_config(TCC_GPD(10), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//sync
            tcc_gpio_config(TCC_GPD(7), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//d0
            if (h->serial_mode == 0) {
                tcc_gpio_config(TCC_GPD(6), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d1
                tcc_gpio_config(TCC_GPD(5), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d2
                tcc_gpio_config(TCC_GPD(4), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d3
                tcc_gpio_config(TCC_GPD(3), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d4
                tcc_gpio_config(TCC_GPD(2), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d5
                tcc_gpio_config(TCC_GPD(1), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d6
                tcc_gpio_config(TCC_GPD(0), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d7
            }
            break;

        case 4:
            tcc_gpio_config(TCC_GPD(8), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//clk
            tcc_gpio_config(TCC_GPD(9), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//valid
            tcc_gpio_config(TCC_GPD(10), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//sync
            tcc_gpio_config(TCC_GPD(7), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//d0
            break;

        case 5:
            tcc_gpio_config(TCC_GPE(27), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//clk
            tcc_gpio_config(TCC_GPE(25), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//valid
            tcc_gpio_config(TCC_GPE(26), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//sync
            tcc_gpio_config(TCC_GPE(24), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//d0
            if (h->serial_mode == 0) {
                tcc_gpio_config(TCC_GPE(23), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d1
                tcc_gpio_config(TCC_GPE(22), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d2
                tcc_gpio_config(TCC_GPE(21), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d3
                tcc_gpio_config(TCC_GPE(20), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d4
                tcc_gpio_config(TCC_GPE(19), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d5
                tcc_gpio_config(TCC_GPE(18), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d6
                tcc_gpio_config(TCC_GPE(17), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);	//d7
            }
            break;		

        case 6:
            tcc_gpio_config(TCC_GPE(20), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//clk
            tcc_gpio_config(TCC_GPE(18), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//valid
            tcc_gpio_config(TCC_GPE(19), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//sync
            tcc_gpio_config(TCC_GPE(17), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//d0
            break;		

        case 7:
            tcc_gpio_config(TCC_GPF(11), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//clk
            tcc_gpio_config(TCC_GPF(13), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//valid
            tcc_gpio_config(TCC_GPF(12), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//sync
            tcc_gpio_config(TCC_GPF(14), GPIO_FN(0)|GPIO_OUTPUT|GPIO_LOW);		//d0
            break;		

        default:
            printk("%s : select wrong port => %d\n", __func__, h->tsif_port_num);
            ret = -1;
            break;
	}
    return ret;
}

int tca_tsif_init(struct tcc_tsif_handle *h)
{
/*
 * CAN NOT support TSIF slave function because it moves to CM3 Bus.
 */
 	char szBuf[128];
    int ret = 0;

    if(cm3_clk == NULL) {
        cm3_clk = clk_get(NULL, "cortex-m3");
        if(IS_ERR(cm3_clk)) {// || IS_ERR(pktgen0_clk)) {
            printk("can't find cortex-m3 clk driver!\n");
            cm3_clk = NULL;
        } else {
            clk_set_rate(cm3_clk, 200*1000*1000);
            clk_enable(cm3_clk);	
        }
    }

    if(!IS_ERR(cm3_clk)) {
        memset(szBuf, 0x00, sizeof(szBuf));
        sprintf(szBuf, "pkt_gen%d", h->dmx_id);

        if(pktgen_clk[h->dmx_id] == NULL) {
            pktgen_clk[h->dmx_id] = clk_get(NULL, szBuf);
            if(IS_ERR(pktgen_clk[h->dmx_id])) {
                printk("can't find %s clk driver!\n", szBuf);
                pktgen_clk[h->dmx_id] = NULL;
            } else {
                clk_set_rate(pktgen_clk[h->dmx_id], 27*1000*1000);	
                clk_enable(pktgen_clk[h->dmx_id]);

                if(g_initCount == 0) {
					int dmx_id, cmd_id;
                    for(dmx_id=0; dmx_id<4;dmx_id++) {
                        for(cmd_id=QUEUE_HW_DEMUX_INIT; cmd_id<QUEUE_HW_DEMUX_TYPE_MAX;cmd_id++) {
                            init_waitqueue_head(&cmd_queue[dmx_id][cmd_id]);
                        }
                    }

                    TSDMX_CM3Configure();
                    TSDMX_CM3LoadBinary(h->fw_data, h->fw_size);
                    ret = request_irq(MBOX_IRQ, MailBox_Handler, IRQ_TYPE_LEVEL_LOW | IRQF_DISABLED, DEVICE_NAME, NULL);

                    msleep(100); //Wait for CM3 Booting
                    h->serial_mode = 1;
            #ifdef CONFIG_STB_BOARD_YJ8935T
                    h->tsif_port_num = 3;
                    if(tcc_dxb_ctrl_interface() == 1)//if Parallel TSIF
                        h->serial_mode = 0; //1:serialmode 0:parallel mode
            #else
                #if defined(CONFIG_CHIP_TCC8930)
                  #if defined(CONFIG_STB_BOARD_EV_TCC893X)
                    h->tsif_port_num = 4; //Port4 (GPIOD7 ~ GPIOD10)
                  #else
                    h->tsif_port_num = 0; //Port0 (GPIOB0 ~ GPIOB3)
                  #endif
                #elif defined(CONFIG_CHIP_TCC8935)
                    h->tsif_port_num = 4; //Port4 (GPIOD7 ~ GPIOD10)
                #else
                    h->tsif_port_num = 0; //Port0 (GPIOB0 ~ GPIOB3)
                #endif
            #endif

#if defined(CONFIG_CHIP_TCC8930) && defined(CONFIG_iTV_PLATFORM)
			h->tsif_port_num = 3;	//gpio_d[0:10] ts3
			h->serial_mode = 0;	//parallel mode
			printk("%s : tsif_port_num[%d] serial_mode[%d]\n\n", __func__, h->tsif_port_num, h->serial_mode);			
#endif

                    tca_tsif_set_port(h);
                }

                ret = TSDMX_GET_Version(h->dmx_id); //Getting f/w version
                ret = TSDMX_Init(h);
            }
        }
    }

    return 0;
}

void tca_tsif_clean(struct tcc_tsif_handle *h)
{
/*
 * CAN NOT support TSIF slave function because it moves to CM3 Bus.
 */
    int ret = 0;
    if(h) {
        ts_demux_buffer_updated_callback[h->dmx_id] = NULL;

        if(pktgen_clk[h->dmx_id]) {
            clk_disable(pktgen_clk[h->dmx_id]);
            clk_put(pktgen_clk[h->dmx_id]);
            pktgen_clk[h->dmx_id] = NULL;
        }

        ret = TSDMX_DeInit(h->dmx_id);
        if(g_initCount == 0) {
			tca_tsif_release_port(h);
#ifndef     SUPPORT_DEBUG_CM3HWDEMUX
            if(!cm3_clk) {
                TSDMX_CM3UnloadBinary();
                free_irq(MBOX_IRQ, NULL);
                clk_disable(cm3_clk);
                clk_put(cm3_clk);
                cm3_clk = NULL;
            }
#endif                
        }
        printk("TSDMX_DeInit Command Result : %d\n", ret);
    }
}

int tca_tsif_register_pids(struct tcc_tsif_handle *h, unsigned int *pids, unsigned int count)
{
/*
 * CAN NOT support TSIF slave function because it moves to CM3 Bus.
 */
    printk("[DEMUX #%d]tca_tsif_register_pids\n", h->dmx_id);
    return 0;
}

int tca_tsif_can_support(void)
{
/*
 * CAN NOT support TSIF slave function because it moves to CM3 Bus.
 */    
    return 1;
}

int tca_tsif_buffer_updated_callback(struct tcc_tsif_handle *h, FN_UPDATECALLBACK buffer_updated)
{
    ts_demux_buffer_updated_callback[h->dmx_id] = buffer_updated;
    return 0;
}

int tca_tsif_set_pcrpid(struct tcc_tsif_handle *h, unsigned int pid)
{
    printk("[DEMUX #%d]tca_tsif_set_pcrpid(pid=%d)\n", h->dmx_id, pid);
    return TSDMX_SET_PCR_PID(h->dmx_id, pid);
}

long long tca_tsif_get_stc(struct tcc_tsif_handle *h)
{
    //printk("[DEMUX #%d]tca_tsif_get_stc\n", h->dmx_id);
    return TSDMX_GET_STC(h->dmx_id);
}

int tca_tsif_add_filter(struct tcc_tsif_handle *h, struct tcc_tsif_filter *feed)
{
    printk("[DEMUX #%d]tca_tsif_add_filter(type=%d, pid=%d)\n", h->dmx_id, feed->f_type, feed->f_pid);
    return TSDMX_ADD_Filter(h->dmx_id, feed);
}

int tca_tsif_remove_filter(struct tcc_tsif_handle *h, struct tcc_tsif_filter *feed)
{
    printk("[DEMUX #%d]tca_tsif_remove_filter(type=%d, pid=%d)\n", h->dmx_id, feed->f_type, feed->f_pid);
    return TSDMX_DELETE_Filter(h->dmx_id, feed);
}

EXPORT_SYMBOL(tca_tsif_init);
EXPORT_SYMBOL(tca_tsif_clean);
EXPORT_SYMBOL(tca_tsif_register_pids);
EXPORT_SYMBOL(tca_tsif_can_support);
EXPORT_SYMBOL(tca_tsif_buffer_updated_callback);
EXPORT_SYMBOL(tca_tsif_set_pcrpid);
EXPORT_SYMBOL(tca_tsif_get_stc);
EXPORT_SYMBOL(tca_tsif_add_filter);
EXPORT_SYMBOL(tca_tsif_remove_filter);
