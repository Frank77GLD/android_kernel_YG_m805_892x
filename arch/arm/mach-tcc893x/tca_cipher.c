/****************************************************************************
 *   FileName    : tca_cipher.c
 *   Description :
 ****************************************************************************
 *
 *   TCC Version 1.0
 *   Copyright (c) Telechips, Inc.
 *   ALL RIGHTS RESERVED
 *
 ****************************************************************************/


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#ifdef CONFIG_PM
#include <linux/pm.h>
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/div64.h>
#include <asm/mach/map.h>

#include <mach/bsp.h>
#include <mach/tca_ckc.h>
#include <mach/globals.h>

//#include <mach/Structures_hsio.h>
#include <mach/tcc_pca953x.h>
#include <mach/gpio.h>
#include <mach/tcc_cipher_ioctl.h>

#define MAX_CIPHER_BUFFER_LENGTH	4096

#define MIN_CIPHER_BLOCK_SIZE		8

static int debug = 0;
#define dprintk(msg...)	if(debug) { printk( "tca_cipher: " msg); }

static int iIrqCipher=-1;
static int iDoneIrqHandled = FALSE;
static int iPacketIrqHandled = FALSE;

static dma_addr_t SrcDma;	/* physical */
static u_char *pSrcCpu;		/* virtual */
static dma_addr_t DstDma;	/* physical */
static u_char *pDstCpu;		/* virtual */
	
static struct clk *cipher_clk = NULL;
static struct clk *clk_dmax = NULL;

static int CIPHER_DMA_TYPE = -1;
static char key_swapbuf[128];
static char iv_swapbuf[128];


#if !defined(CONFIG_ARCH_TCC92XX)
extern struct tcc_freq_table_t gtHSIOClockLimitTable;
#endif

static DEFINE_MUTEX(tca_cipher_mutex);

static void tca_cipher_data_swap (unsigned char *srcdata, unsigned char *destdata, int size)
{
	unsigned int		i;
	for(i=0;i<=size; i+=4)
	{
		destdata[i] = srcdata[i+3];
		destdata[i+1] = srcdata[i+2];
		destdata[i+2] = srcdata[i+1];
		destdata[i+3] = srcdata[i];
	}
}

void tca_cipher_dma_enable(unsigned uEnable, unsigned uEndian, unsigned uAddrModeTx, unsigned uAddrModeRx)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);

	dprintk("%s\n", __func__);

	if(uEnable)
	{
		/* Set the Byte Endian Mode */
		if(uEndian == TCC_CIPHER_DMA_ENDIAN_LITTLE)
			BITCLR(pHwCIPHER->DMACTR.nREG, HwCIPHER_DMACTR_ByteEndian);
		else
			BITSET(pHwCIPHER->DMACTR.nREG, HwCIPHER_DMACTR_ByteEndian);

		/* Set the Addressing Mode */
		BITCSET(pHwCIPHER->DMACTR.nREG, HwCIPHER_DMACTR_AddrModeTx_Mask, HwCIPHER_DMACTR_AddrModeTx(uAddrModeTx));
		BITCSET(pHwCIPHER->DMACTR.nREG, HwCIPHER_DMACTR_AddrModeRx_Mask, HwCIPHER_DMACTR_AddrModeRx(uAddrModeRx));

		/* DMA Enable */
		BITSET(pHwCIPHER->DMACTR.nREG, HwCIPHER_DMACTR_Enable);

		/* Clear Done Interrupt Flag */
		iDoneIrqHandled = 0;
	}
	else
	{
		/* DMA Disable */
		BITCLR(pHwCIPHER->DMACTR.nREG, HwCIPHER_DMACTR_Enable);
	}
}

void tca_cipher_dma_enable_request(unsigned uEnable)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);

	dprintk("%s, Enable=%d\n", __func__, uEnable);

	/* Clear TX/RX packet counter */
	BITSET(pHwCIPHER->DMACTR.nREG, HwCIPHER_DMACTR_ClearPacketCount);

	/* DMA Request Enable/Disable */
	if(uEnable)
		BITSET(pHwCIPHER->DMACTR.nREG, HwCIPHER_DMACTR_RequestEnable);
	else
		BITCLR(pHwCIPHER->DMACTR.nREG, HwCIPHER_DMACTR_RequestEnable);
}

void tca_cipher_interrupt_config(unsigned uTxSel, unsigned uDoneIrq, unsigned uPacketIrq)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);
	
	dprintk("%s\n", __func__);

	/* IRQ Select Direction */
	if(uTxSel)
		BITCLR(pHwCIPHER->IRQCTR.nREG, HwCIPHER_IRQCTR_SelectIrq);
	else
		BITSET(pHwCIPHER->IRQCTR.nREG, HwCIPHER_IRQCTR_SelectIrq);

	/* Enable for "Done" Interrupt */
	if(uDoneIrq)
		BITSET(pHwCIPHER->IRQCTR.nREG, HwCIPHER_IRQCTR_EnableDoneIrq);
	else
		BITCLR(pHwCIPHER->IRQCTR.nREG, HwCIPHER_IRQCTR_EnableDoneIrq);

	/* Enable for "Packet" Interrupt */
	if(uPacketIrq)
		BITSET(pHwCIPHER->IRQCTR.nREG, HwCIPHER_IRQCTR_EnablePacketIrq);
	else
		BITCLR(pHwCIPHER->IRQCTR.nREG, HwCIPHER_IRQCTR_EnablePacketIrq);
}

void tca_cipher_interrupt_enable(unsigned uEnable)
{
	PPIC pHwPIC = (volatile PPIC)tcc_p2v(HwPIC_BASE);
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);

	dprintk("%s, Enabel=%d\n", __func__, uEnable);

	if(uEnable)
	{
		if(CIPHER_DMA_TYPE == TCC_CIPHER_DMA_DMAX)
		{
			/* DMAX Enable Start*/
			BITSET(pHwPIC->CLR0.nREG, Hw30);
			BITSET(pHwPIC->SEL0.nREG, Hw30);
			BITCLR(pHwPIC->POL0.nREG, Hw30);
			BITCLR(pHwPIC->MODE0.nREG, Hw30);
			BITSET(pHwPIC->INTMSK0.nREG, Hw30);
			BITSET(pHwPIC->SYNC0.nREG, Hw30);
			BITCLR(pHwPIC->MODEA0.nREG, Hw30);
			BITSET(pHwPIC->IEN0.nREG, Hw30);
			/* Cipher Block Enable End*/
		}
		else
		{
			/* Cipher Block Enable Start*/
			BITSET(pHwPIC->CLR1.nREG, Hw20);
			BITSET(pHwPIC->SEL1.nREG, Hw20);
			BITCLR(pHwPIC->POL1.nREG, Hw20);
			BITCLR(pHwPIC->MODE1.nREG, Hw20);
			BITSET(pHwPIC->IEN1.nREG, Hw20);
			/* Cipher Block Enable End*/
		}
	}
	else
	{
		if(CIPHER_DMA_TYPE == TCC_CIPHER_DMA_DMAX)
		{
			BITCLR(pHwPIC->INTMSK0.nREG, Hw30);
			BITCLR(pHwPIC->IEN0.nREG, Hw30);
		}
		else
		{
			BITCLR(pHwPIC->INTMSK1.nREG, Hw20);
			BITCLR(pHwPIC->IEN1.nREG, Hw20);
		}
	}
}

irqreturn_t tca_cipher_interrupt_handler(int irq, void *dev_id)
{
	PPIC pHwPIC = (volatile PPIC)tcc_p2v(HwPIC_BASE);
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);

	//dprintk("%s\n", __func__);

	dprintk("%s, RXPCNT=%d, TXPCNT=%d\n", __func__, (pHwCIPHER->DMASTR.nREG & 0xFF00)>>16, (pHwCIPHER->DMASTR.nREG & 0xFF));

	if(pHwCIPHER->IRQCTR.nREG & HwCIPHER_IRQCTR_DoneIrqStatus)
	{
		dprintk("%s, Done Interrupt\n", __func__);

		/* Clear IRQ Status */
		BITSET(pHwCIPHER->IRQCTR.nREG, HwCIPHER_IRQCTR_DoneIrqStatus);

		/* Set Done Interrupt Flag */
		iDoneIrqHandled = TRUE;
	}
	else if(pHwCIPHER->IRQCTR.nREG & HwCIPHER_IRQCTR_PacketIrqStatus)
	{
		dprintk("%s, Packet Interrupt\n", __func__);

		/* Clear IRQ Status */
		BITSET(pHwCIPHER->IRQCTR.nREG, HwCIPHER_IRQCTR_PacketIrqStatus);
	}
	else
	{
		dprintk("%s, No Cipher Interrupt\n", __func__);
	}
	
    return IRQ_HANDLED;
}

void tca_cipher_set_dmamode(unsigned uDmaMode)
{
	CIPHER_DMA_TYPE = uDmaMode;
	/* Enable Cipher Interrupt */
	tca_cipher_interrupt_enable(FALSE);
}

void tca_cipher_set_opmode(unsigned uOpMode)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);

	dprintk("%s, Operation Mode: %d\n", __func__, uOpMode);
	
	switch(uOpMode)
	{
		case TCC_CIPHER_OPMODE_ECB:
			BITCSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_OperationMode_Mask, HwCIPHER_CTRL_OperationMode_ECB);
			break;
		case TCC_CIPHER_OPMODE_CBC:
			BITCSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_OperationMode_Mask, HwCIPHER_CTRL_OperationMode_CBC);
			break;
		case TCC_CIPHER_OPMODE_CFB:
			BITCSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_OperationMode_Mask, HwCIPHER_CTRL_OperationMode_CFB);
			break;
		case TCC_CIPHER_OPMODE_OFB:
			BITCSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_OperationMode_Mask, HwCIPHER_CTRL_OperationMode_OFB);
			break;
		case TCC_CIPHER_OPMODE_CTR:
		case TCC_CIPHER_OPMODE_CTR_1:
		case TCC_CIPHER_OPMODE_CTR_2:
		case TCC_CIPHER_OPMODE_CTR_3:
			BITCSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_OperationMode_Mask, HwCIPHER_CTRL_OperationMode_CTR);
			break;

		default:
			dprintk("%s, Err: Invalid Operation Mode\n", __func__);
			break;
	}

	dprintk("%s, Operation Mode Set End\n", __func__);
	
}

void tca_cipher_set_algorithm(unsigned uAlgorithm, unsigned uArg1, unsigned uArg2)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);
	
	dprintk("%s, Algorithm: %d, uArg2 = %d \n", __func__, uAlgorithm, uArg2);
	
	switch(uAlgorithm)
	{
		case TCC_CIPHER_ALGORITM_AES:
			{
				/* uArg1: The Key Length in AES */
				/* uArg2: None                  */
				BITCSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_Algorithm_Mask, HwCIPHER_CTRL_Algorithm_AES);
				BITCSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_KeyLength_Mask, HwCIPHER_CTRL_KeyLength(uArg1));
			}
			break;

		case TCC_CIPHER_ALGORITM_DES:
			{
				/* uArg1: The Mode in DES     */
				/* uArg2: Parity Bit Location */
				BITCSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_Algorithm_Mask, HwCIPHER_CTRL_Algorithm_DES);
				BITCSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_DESMode_Mask, HwCIPHER_CTRL_DESMode(uArg1));
				if(uArg2)
					BITSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_ParityBit);
				else
					BITCLR(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_ParityBit);
			}
			break;

		default:
			dprintk("%s, Err: Invalid Algorithm\n", __func__);
			break;
	}

	dprintk("%s, Algorithm Set End\n", __func__);
	
}

void tca_cipher_set_baseaddr(unsigned uTxRx, unsigned char *pBaseAddr)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);

	dprintk("%s\n", __func__);

	if(uTxRx == TCC_CIPHER_BASEADDR_TX)
		pHwCIPHER->TXBASE.nREG = pBaseAddr;
	else
		pHwCIPHER->RXBASE.nREG = pBaseAddr;
}

void tca_cipher_set_packet(unsigned uCount, unsigned uSize)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);

	dprintk("%s, Count: %d, Size: %d\n", __func__, uCount, uSize);

	if(uCount)
		uCount -= 1;

	pHwCIPHER->PACKET.nREG = ((uCount & 0xFFFF) << 16) | (uSize & 0xFFFF);
}

void tca_cipher_set_key(unsigned char *pucData, unsigned uLength, unsigned uOption)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);
	unsigned long ulAlgorthm, ulAESKeyLength, ulDESMode;
	unsigned long *pulKeyData;

	dprintk("%s, Lenth: %d, Option: %d\n", __func__, uLength, uOption);

	ulAlgorthm = pHwCIPHER->CTRL.nREG & HwCIPHER_CTRL_Algorithm_Mask;

	tca_cipher_data_swap(pucData, key_swapbuf, uLength);

	pulKeyData = (unsigned long *)key_swapbuf;

	/* Write Key Data */
	switch(ulAlgorthm)
	{
		case HwCIPHER_CTRL_Algorithm_AES:
			{

				ulAESKeyLength = pHwCIPHER->CTRL.nREG & HwCIPHER_CTRL_KeyLength_Mask;

				if(ulAESKeyLength == HwCIPHER_CTRL_keyLength_128)
				{
					pHwCIPHER->KEY0.nREG = *pulKeyData++;
					pHwCIPHER->KEY1.nREG = *pulKeyData++;
					pHwCIPHER->KEY2.nREG = *pulKeyData++;
					pHwCIPHER->KEY3.nREG = *pulKeyData++;
				}
				else if(ulAESKeyLength == HwCIPHER_CTRL_KeyLength_192)
				{
					pHwCIPHER->KEY0.nREG = *pulKeyData++;
					pHwCIPHER->KEY1.nREG = *pulKeyData++;
					pHwCIPHER->KEY2.nREG = *pulKeyData++;
					pHwCIPHER->KEY3.nREG = *pulKeyData++;
					pHwCIPHER->KEY4.nREG = *pulKeyData++;
					pHwCIPHER->KEY5.nREG = *pulKeyData++;
				}
				else if(ulAESKeyLength == HwCIPHER_CTRL_KeyLength_256)
				{
					pHwCIPHER->KEY0.nREG = *pulKeyData++;
					pHwCIPHER->KEY1.nREG = *pulKeyData++;
					pHwCIPHER->KEY2.nREG = *pulKeyData++;
					pHwCIPHER->KEY3.nREG = *pulKeyData++;
					pHwCIPHER->KEY4.nREG = *pulKeyData++;
					pHwCIPHER->KEY5.nREG = *pulKeyData++;
					pHwCIPHER->KEY6.nREG = *pulKeyData++;
					pHwCIPHER->KEY7.nREG = *pulKeyData++;
				}
			}
			break;

		case HwCIPHER_CTRL_Algorithm_DES:
			{
				ulDESMode = pHwCIPHER->CTRL.nREG & HwCIPHER_CTRL_DESMode_Mask;

				if(ulDESMode == HwCIPHER_CTRL_DESMode_SingleDES)
				{
					pHwCIPHER->KEY0.nREG = *pulKeyData++;
					pHwCIPHER->KEY1.nREG = *pulKeyData++;
				}
				else if(ulDESMode == HwCIPHER_CTRL_DESMode_DoubleDES)
				{
					pHwCIPHER->KEY0.nREG = *pulKeyData++;
					pHwCIPHER->KEY1.nREG = *pulKeyData++;
					pHwCIPHER->KEY2.nREG = *pulKeyData++;
					pHwCIPHER->KEY3.nREG = *pulKeyData++;
				}
				else if(ulDESMode == HwCIPHER_CTRL_DESMode_TripleDES2)
				{
					pHwCIPHER->KEY0.nREG = *pulKeyData++;
					pHwCIPHER->KEY1.nREG = *pulKeyData++;
					pHwCIPHER->KEY2.nREG = *pulKeyData++;
					pHwCIPHER->KEY3.nREG = *pulKeyData++;
				}
				else if(ulDESMode == HwCIPHER_CTRL_DESMode_TripleDES3)
				{
					pHwCIPHER->KEY0.nREG = *pulKeyData++;
					pHwCIPHER->KEY1.nREG = *pulKeyData++;
					pHwCIPHER->KEY2.nREG = *pulKeyData++;
					pHwCIPHER->KEY3.nREG = *pulKeyData++;
					pHwCIPHER->KEY4.nREG = *pulKeyData++;
					pHwCIPHER->KEY5.nREG = *pulKeyData++;
				}
			}
			break;

		default:
			break;
	}

	/* Load Key Data */
	BITSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_KeyDataLoad);
}

void tca_cipher_set_vector(unsigned char *pucData, unsigned uLength)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);
	unsigned long ulAlgorthm;
	unsigned long *pulVectorData;

	dprintk("%s, Length: %d\n", __func__, uLength);

	ulAlgorthm = pHwCIPHER->CTRL.nREG & HwCIPHER_CTRL_Algorithm_Mask;

	if(CIPHER_DMA_TYPE == TCC_CIPHER_DMA_INTERNAL)
	{
		tca_cipher_data_swap(pucData, iv_swapbuf, uLength);
		pulVectorData = (unsigned long *)iv_swapbuf;
	}
	else
		pulVectorData = (unsigned long *)pucData;

	/* Write Initial Vector */
	switch(ulAlgorthm)
	{
		case HwCIPHER_CTRL_Algorithm_AES:
			{
				pHwCIPHER->IV0.nREG = *pulVectorData++;
				pHwCIPHER->IV1.nREG = *pulVectorData++;
				pHwCIPHER->IV2.nREG = *pulVectorData++;
				pHwCIPHER->IV3.nREG = *pulVectorData++;
			}
			break;

		case HwCIPHER_CTRL_Algorithm_DES:
			{
				pHwCIPHER->IV0.nREG = *pulVectorData++;
				pHwCIPHER->IV1.nREG = *pulVectorData++;
			}
			break;

		default:
			break;
	}

	/* Load Initial Vector */
	BITSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_InitVectorLoad);
}

int tca_cipher_get_packetcount(unsigned uTxRx)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);
	int iPacketCount;

	dprintk("%s\n", __func__);

	if(uTxRx == TCC_CIPHER_PACKETCOUNT_TX)
		iPacketCount = (pHwCIPHER->DMASTR.nREG & 0x00FF);
	else
		iPacketCount = (pHwCIPHER->DMASTR.nREG & 0xFF00) >> 16;

	return iPacketCount;
}

int tca_cipher_get_blocknum(void)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);

	dprintk("%s\n", __func__);
	
	return pHwCIPHER->BLKNUM.nREG;
}

void tca_cipher_clear_counter(unsigned uIndex)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);

	dprintk("%s, Index: 0x%02x\n", __func__, uIndex);
	
	/* Clear Transmit FIFO Counter */
	if(uIndex & TCC_CIPHER_CLEARCOUNTER_TX)
		BITSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_ClearTxFIFO);
	/* Clear Receive FIFO Counter */
	if(uIndex & TCC_CIPHER_CLEARCOUNTER_RX)
		BITSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_ClearRxFIFO);
	/* Clear Block Counter */
	if(uIndex & TCC_CIPHER_CLEARCOUNTER_BLOCK)
		BITSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_ClearBlkCount);
}

void tca_cipher_wait_done(void)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);

	dprintk("%s\n", __func__);

	/* Wait for Done Interrupt */
	while(1)
	{
		if(iDoneIrqHandled)
		{
			dprintk("Receive Done IRQ Handled\n");
			/* Clear Done IRQ Flag */
			iDoneIrqHandled = FALSE;
			break;
		}

		msleep(1);
	}

	/* Wait for DMA to be Disabled */
	while(1)
	{
		if(!(pHwCIPHER->DMACTR.nREG & HwCIPHER_DMACTR_Enable))
		{
			dprintk("DMA Disabled\n");
			break;
		}

		msleep(1);
	}
}

int tca_cipher_encrypt(unsigned char *pucSrcAddr, unsigned char *pucDstAddr, unsigned uLength)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);
	PDMAX 	pHwDMAX = (volatile PDMAX)tcc_p2v(HwDMAX_BASE);
	uint /*bucket = 0,*/ nSize, nCount;

 	dprintk("%s, Length=%d\n", __func__, uLength);
	
	if(uLength> MAX_CIPHER_BUFFER_LENGTH)
	{
#if 0
		if(pSrcCpu != NULL)
			dma_free_writecombine(0, MAX_CIPHER_BUFFER_LENGTH, pSrcCpu, SrcDma);
		if(pDstCpu != NULL)
			dma_free_writecombine(0, MAX_CIPHER_BUFFER_LENGTH, pDstCpu, DstDma);

		/* Allocate Physical & Virtual Address for DMA */
		pSrcCpu = dma_alloc_writecombine(0, uLength, &SrcDma, GFP_KERNEL);
		pDstCpu = dma_alloc_writecombine(0, uLength, &DstDma, GFP_KERNEL);
#else
		if(pSrcCpu != NULL)
			dma_free_coherent(0, MAX_CIPHER_BUFFER_LENGTH, pSrcCpu, SrcDma);
		if(pDstCpu != NULL)
			dma_free_coherent(0, MAX_CIPHER_BUFFER_LENGTH, pDstCpu, DstDma);

		/* Allocate Physical & Virtual Address for DMA */
		pSrcCpu = dma_alloc_coherent(0, uLength, &SrcDma, GFP_KERNEL);
		pDstCpu = dma_alloc_coherent(0, uLength, &DstDma, GFP_KERNEL);

#endif
	}

	if((pSrcCpu == NULL) || (pDstCpu == NULL))
	{
		dprintk("pSrcCpu = %x, pDstCpu = %x\n", pSrcCpu, pDstCpu)
		return -1;
	}

	/* Init Virtual Address */
	memset(pSrcCpu, 0x00, uLength);
	memset(pDstCpu, 0x00, uLength);

	/* Copy Plain Text from Source Buffer */
	copy_from_user(pSrcCpu, pucSrcAddr, uLength);
	
 	/* Clear All Conunters */
	tca_cipher_clear_counter(TCC_CIPHER_CLEARCOUNTER_ALL);

	/* Select Encryption */
	BITSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_Encrytion);

	/* Load Key & InitVector Value */
	BITSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_KeyDataLoad);
	BITSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_InitVectorLoad);

	if(CIPHER_DMA_TYPE == TCC_CIPHER_DMA_DMAX)
	{
		pHwDMAX->SBASE.nREG = SrcDma;
		pHwDMAX->DBASE.nREG = DstDma;

		pHwDMAX->SIZE.bREG.SIZE = uLength;
		pHwDMAX->SIZE.bREG.CIPHER = 1;
		pHwDMAX->CTRL.bREG.CH 	= 0;
		pHwDMAX->CTRL.bREG.TYPE = 0;
		
		pHwCIPHER->ROUND.bREG.ROUND = 0x33;

		pHwDMAX->CTRL.bREG.EN = 1;

		while(!pHwDMAX->FIFO0.bREG.IDLE) ;
#if 0 /* For Debugging */
		{
			int i;
			unsigned int *pDataAddr;

			pDataAddr = (unsigned int *)pHwDMAX;
			printk("\n[ Register Setting ]\n");
			for(i=0; i<=(0x74/4); i+=4)
			{
				printk("0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n", pDataAddr + i, *(pDataAddr+i+0), *(pDataAddr+i+1), *(pDataAddr+i+2), *(pDataAddr+i+3));
			}
		}		
#endif		
		pHwDMAX->INTSTS = pHwDMAX->INTSTS;
		pHwDMAX->SIZE.bREG.CIPHER		= 0;
	}
	else		/*(CIPHER_DMA_TYPE == TCC_CIPHER_DMA_INTERNAL)*/
	{
		/* Set the Base Address */
		pHwCIPHER->TXBASE.nREG = SrcDma;
		pHwCIPHER->RXBASE.nREG = DstDma;

	        tca_cipher_set_packet(1, uLength);
		/* Configure Interrupt - Receiving */
		tca_cipher_interrupt_config(FALSE, TRUE, FALSE);
		/* Request Enable DMA */
		tca_cipher_dma_enable_request(TRUE);
		/* Clear Packet Counter */
		BITSET(pHwCIPHER->DMACTR.nREG, HwCIPHER_DMACTR_ClearPacketCount);

		/* Enable DMA */ 
		if(uLength > MIN_CIPHER_BLOCK_SIZE)
			tca_cipher_dma_enable(TRUE, TCC_CIPHER_DMA_ENDIAN_LITTLE, TCC_CIPHER_DMA_ADDRMODE_MULTI, TCC_CIPHER_DMA_ADDRMODE_MULTI);	
		else
			tca_cipher_dma_enable(TRUE, TCC_CIPHER_DMA_ENDIAN_LITTLE, TCC_CIPHER_DMA_ADDRMODE_SINGLE, TCC_CIPHER_DMA_ADDRMODE_SINGLE);	
	 
		  while(!pHwCIPHER->IRQCTR.nREG & HwCIPHER_IRQCTR_DoneIrqStatus);
		  while(pHwCIPHER->DMACTR.nREG & HwCIPHER_DMACTR_Enable);
		
		tca_cipher_dma_enable_request(FALSE);
		tca_cipher_dma_enable(FALSE, NULL, NULL, NULL);	
	}

	/* Copy Cipher Text to Destination Buffer */
	copy_to_user(pucDstAddr, pDstCpu, uLength);

	#if 0 /* For Debugging */
	{
		int i;
		unsigned int *pDataAddr;
#if 1
		pDataAddr = (unsigned int *)pHwCIPHER;
		printk("\n[ Register Setting ]\n");
		for(i=0; i<=(0x74/4); i+=4)
		{
			printk("0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n", pDataAddr + i, *(pDataAddr+i+0), *(pDataAddr+i+1), *(pDataAddr+i+2), *(pDataAddr+i+3));
		}

		pDataAddr = (unsigned int *)pHwDMAX;
		printk("\n[ Register Setting ]\n");
		for(i=0; i<=(0x74/4); i+=4)
		{
			printk("0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n", pDataAddr + i, *(pDataAddr+i+0), *(pDataAddr+i+1), *(pDataAddr+i+2), *(pDataAddr+i+3));
		}
		
#endif		
		pDataAddr = (unsigned int *)pSrcCpu;
		printk("\n[ Plain Text ]\n");
		for(i=0; i<(uLength/4); i+=4)
		{
			printk("0x%08x 0x%08x 0x%08x 0x%08x\n", pDataAddr[i+0], pDataAddr[i+1], pDataAddr[i+2], pDataAddr[i+3]);
		}

		pDataAddr = (unsigned int *)pDstCpu;
		printk("\n[ Cipher Text ]\n");
		for(i=0; i<(uLength/4); i+=4)
		{
			printk("0x%08x 0x%08x 0x%08x 0x%08x\n", pDataAddr[i+0], pDataAddr[i+1], pDataAddr[i+2], pDataAddr[i+3]);
		}
		printk("\n");
	}
	#endif
	if(uLength> MAX_CIPHER_BUFFER_LENGTH)
	{
#if 0
		/* Release Physical & Virtual Address for DMA */
		dma_free_writecombine(0, uLength, pSrcCpu, SrcDma);
		dma_free_writecombine(0, uLength, pDstCpu, DstDma);
		
		pSrcCpu = dma_alloc_writecombine(0, MAX_CIPHER_BUFFER_LENGTH, &SrcDma, GFP_KERNEL);
		pDstCpu = dma_alloc_writecombine(0, MAX_CIPHER_BUFFER_LENGTH, &DstDma, GFP_KERNEL);
#else
		dma_free_coherent(0, uLength, pSrcCpu, SrcDma);
		dma_free_coherent(0, uLength, pDstCpu, DstDma);
		
		pSrcCpu = dma_alloc_coherent(0, MAX_CIPHER_BUFFER_LENGTH, &SrcDma, GFP_KERNEL);
		pDstCpu = dma_alloc_coherent(0, MAX_CIPHER_BUFFER_LENGTH, &DstDma, GFP_KERNEL);
#endif
	}	
	return 0;
}

int tca_cipher_decrypt(unsigned char *pucSrcAddr, unsigned char *pucDstAddr, unsigned uLength)
{
	PCIPHER pHwCIPHER = (volatile PCIPHER)tcc_p2v(HwCIPHER_BASE);
	PDMAX 	pHwDMAX = (volatile PDMAX)tcc_p2v(HwDMAX_BASE);
	uint /*bucket = 0,*/ nSize, nCount;

	dprintk("%s, Length=%d\n", __func__, uLength);
	
	if(uLength> MAX_CIPHER_BUFFER_LENGTH)
	{
#if 0
		if(pSrcCpu != NULL)
			dma_free_writecombine(0, MAX_CIPHER_BUFFER_LENGTH, pSrcCpu, SrcDma);
		if(pDstCpu != NULL)
			dma_free_writecombine(0, MAX_CIPHER_BUFFER_LENGTH, pDstCpu, DstDma);

		/* Allocate Physical & Virtual Address for DMA */
		pSrcCpu = dma_alloc_writecombine(0, uLength, &SrcDma, GFP_KERNEL);
		pDstCpu = dma_alloc_writecombine(0, uLength, &DstDma, GFP_KERNEL);
#else
		if(pSrcCpu != NULL)
			dma_free_coherent(0, MAX_CIPHER_BUFFER_LENGTH, pSrcCpu, SrcDma);
		if(pDstCpu != NULL)
			dma_free_coherent(0, MAX_CIPHER_BUFFER_LENGTH, pDstCpu, DstDma);

		/* Allocate Physical & Virtual Address for DMA */
		pSrcCpu = dma_alloc_coherent(0, uLength, &SrcDma, GFP_KERNEL);
		pDstCpu = dma_alloc_coherent(0, uLength, &DstDma, GFP_KERNEL);

#endif
	}

	if((pSrcCpu == NULL) || (pDstCpu == NULL))
	{
		dprintk("pSrcCpu = %x, pDstCpu = %x\n", pSrcCpu, pDstCpu)
		return -1;
	}

	/* Init Virtual Address */
	memset(pSrcCpu, 0x00, uLength);
	memset(pDstCpu, 0x00, uLength);

	/* Copy Cipher Text from Source Buffer */
	copy_from_user(pSrcCpu, pucSrcAddr, uLength);
	
 	/* Clear All Conunters */
	tca_cipher_clear_counter(TCC_CIPHER_CLEARCOUNTER_ALL);

	/* Select Decryption */
	BITCLR(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_Encrytion);

	/* Load Key & InitVector Value */
	BITSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_KeyDataLoad);
	BITSET(pHwCIPHER->CTRL.nREG, HwCIPHER_CTRL_InitVectorLoad);

	if(CIPHER_DMA_TYPE == TCC_CIPHER_DMA_DMAX)
	{
		pHwDMAX->SBASE.nREG = SrcDma;
		pHwDMAX->DBASE.nREG = DstDma;

		pHwDMAX->SIZE.bREG.SIZE = uLength;
		pHwDMAX->SIZE.bREG.CIPHER = 1;
		pHwDMAX->CTRL.bREG.CH 	= 0;
		pHwDMAX->CTRL.bREG.TYPE = 0;

		pHwCIPHER->ROUND.bREG.ROUND = 0x33;

		pHwDMAX->CTRL.bREG.EN = 1;

		while(!pHwDMAX->FIFO0.bREG.IDLE) ;

#if 0 /* For Debugging */
		{
			int i;
			unsigned int *pDataAddr;

			pDataAddr = (unsigned int *)pHwDMAX;
			printk("\n[DMAX Register Setting ]\n");
			for(i=0; i<=(0x74/4); i+=4)
			{
				printk("0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n", pDataAddr + i, *(pDataAddr+i+0), *(pDataAddr+i+1), *(pDataAddr+i+2), *(pDataAddr+i+3));
			}
		}		
#endif		
		pHwDMAX->INTSTS = pHwDMAX->INTSTS;
		pHwDMAX->SIZE.bREG.CIPHER		= 0;
	}
	else		/*(CIPHER_DMA_TYPE == TCC_CIPHER_DMA_INTRRENAL)*/
	{
		/* Set the Base Address */
		pHwCIPHER->TXBASE.nREG = SrcDma;
		pHwCIPHER->RXBASE.nREG = DstDma;

        	/* Set the Packet Information */
         	tca_cipher_set_packet(1, uLength);
		/* Configure Interrupt - Receiving */
		tca_cipher_interrupt_config(FALSE, TRUE, FALSE);
		/* Request Enable DMA */
		tca_cipher_dma_enable_request(TRUE);
		/* Clear Packet Counter */
		BITSET(pHwCIPHER->DMACTR.nREG, HwCIPHER_DMACTR_ClearPacketCount);

		/* Enable DMA */ 
		if(uLength > MIN_CIPHER_BLOCK_SIZE)
			tca_cipher_dma_enable(TRUE, TCC_CIPHER_DMA_ENDIAN_LITTLE, TCC_CIPHER_DMA_ADDRMODE_MULTI, TCC_CIPHER_DMA_ADDRMODE_MULTI);	
		else
			tca_cipher_dma_enable(TRUE, TCC_CIPHER_DMA_ENDIAN_LITTLE, TCC_CIPHER_DMA_ADDRMODE_SINGLE, TCC_CIPHER_DMA_ADDRMODE_SINGLE);	
	 
		 while(!pHwCIPHER->IRQCTR.nREG & HwCIPHER_IRQCTR_DoneIrqStatus);
		 while(pHwCIPHER->DMACTR.nREG & HwCIPHER_DMACTR_Enable);
		
		tca_cipher_dma_enable_request(FALSE);
		tca_cipher_dma_enable(FALSE, NULL, NULL, NULL);	
	}

	/* Copy Plain Text to Destination Buffer */
	copy_to_user(pucDstAddr, pDstCpu, uLength);	
	
	#if 0 /* For Debugging */
	{
		int i;
		unsigned int *pDataAddr;
#if 1
		pDataAddr = (unsigned int *)pHwCIPHER;
		printk("\n[Cipher Register Setting ]\n");
		for(i=0; i<=(0x74/4); i+=4)
		{
			printk("0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n", pDataAddr + i, *(pDataAddr+i+0), *(pDataAddr+i+1), *(pDataAddr+i+2), *(pDataAddr+i+3));
		}

		pDataAddr = (unsigned int *)pHwDMAX;
		printk("\n[DMAX Register Setting ]\n");
		for(i=0; i<=(0x74/4); i+=4)
		{
			printk("0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n", pDataAddr + i, *(pDataAddr+i+0), *(pDataAddr+i+1), *(pDataAddr+i+2), *(pDataAddr+i+3));
		}

#endif		
		pDataAddr = (unsigned int *)pSrcCpu;
		printk("\n[ Cipher Text ]\n");
		for(i=0; i<(uLength/4); i+=4)
		{
			printk("0x%08x 0x%08x 0x%08x 0x%08x\n", pDataAddr[i+0], pDataAddr[i+1], pDataAddr[i+2], pDataAddr[i+3]);
		}

		pDataAddr = (unsigned int *)pDstCpu;
		printk("\n[ Plain Text ]\n");
		for(i=0; i<(uLength/4); i+=4)
		{
			printk("0x%08x 0x%08x 0x%08x 0x%08x\n", pDataAddr[i+0], pDataAddr[i+1], pDataAddr[i+2], pDataAddr[i+3]);
		}
		printk("\n");
	}
	#endif
	
	if(uLength> MAX_CIPHER_BUFFER_LENGTH)
	{
#if 0
		/* Release Physical & Virtual Address for DMA */
		dma_free_writecombine(0, uLength, pSrcCpu, SrcDma);
		dma_free_writecombine(0, uLength, pDstCpu, DstDma);
		
		pSrcCpu = dma_alloc_writecombine(0, MAX_CIPHER_BUFFER_LENGTH, &SrcDma, GFP_KERNEL);
		pDstCpu = dma_alloc_writecombine(0, MAX_CIPHER_BUFFER_LENGTH, &DstDma, GFP_KERNEL);
#else
		dma_free_coherent(0, uLength, pSrcCpu, SrcDma);
		dma_free_coherent(0, uLength, pDstCpu, DstDma);
		
		pSrcCpu = dma_alloc_coherent(0, MAX_CIPHER_BUFFER_LENGTH, &SrcDma, GFP_KERNEL);
		pDstCpu = dma_alloc_coherent(0, MAX_CIPHER_BUFFER_LENGTH, &DstDma, GFP_KERNEL);
#endif
	}
	return 0;
}

int tca_cipher_open(struct inode *inode, struct file *filp)
{
	mutex_lock(&tca_cipher_mutex);

#if !defined(CONFIG_ARCH_TCC92XX ) && !defined(CONFIG_ARCH_TCC893X )
	tcc_cpufreq_set_limit_table(&gtHSIOClockLimitTable, TCC_FREQ_LIMIT_CIPHER, 1);
#endif

	/* Cipher block enable */
	cipher_clk = clk_get(NULL, "cipher");
	if(IS_ERR(cipher_clk)) {
		cipher_clk = NULL;
		printk("cipher clock error : cannot get clock\n");
		mutex_unlock(&tca_cipher_mutex);
		return -EINVAL;
	}
	clk_enable(cipher_clk);

#ifndef CONFIG_SOC_TCC8925S
	/* DMAX enable */
	clk_dmax = clk_get(NULL, "dmax");
	if(IS_ERR(clk_dmax)) {
		clk_dmax = NULL;
		printk("dmax clock error : cannot get clock\n");
		mutex_unlock(&tca_cipher_mutex);
		return -EINVAL;
	}
	clk_enable(clk_dmax);
#endif

#if 0
	pSrcCpu = dma_alloc_writecombine(0, MAX_CIPHER_BUFFER_LENGTH, &SrcDma, GFP_KERNEL);
	pDstCpu = dma_alloc_writecombine(0, MAX_CIPHER_BUFFER_LENGTH, &DstDma, GFP_KERNEL);
#else
	pSrcCpu = dma_alloc_coherent(0, MAX_CIPHER_BUFFER_LENGTH, &SrcDma, GFP_KERNEL);
	pDstCpu = dma_alloc_coherent(0, MAX_CIPHER_BUFFER_LENGTH, &DstDma, GFP_KERNEL);
#endif
	return 0;
}

int tca_cipher_release(struct inode *inode, struct file *file)
{
	dprintk("%s\n", __func__);

#if 0
	dma_free_writecombine(0, MAX_CIPHER_BUFFER_LENGTH, pSrcCpu, SrcDma);
	dma_free_writecombine(0, MAX_CIPHER_BUFFER_LENGTH, pDstCpu, DstDma);
#else
	dma_free_coherent(0, MAX_CIPHER_BUFFER_LENGTH, pSrcCpu, SrcDma);
	dma_free_coherent(0, MAX_CIPHER_BUFFER_LENGTH, pDstCpu, DstDma);
#endif	

	/* Enable Cipher Interrupt */
	tca_cipher_interrupt_enable(FALSE);

	if (cipher_clk) {
		clk_disable(cipher_clk);
		clk_put(cipher_clk);
		cipher_clk = NULL;
	}
	
#ifndef CONFIG_SOC_TCC8925S
	if (clk_dmax) {
		clk_disable(clk_dmax);
		clk_put(clk_dmax);
		clk_dmax = NULL;
	}
#endif	

#if !defined(CONFIG_ARCH_TCC92XX) && !defined(CONFIG_ARCH_TCC893X )
	tcc_cpufreq_set_limit_table(&gtHSIOClockLimitTable, TCC_FREQ_LIMIT_CIPHER, 0);
#endif

	mutex_unlock(&tca_cipher_mutex);

	return 0;
}



