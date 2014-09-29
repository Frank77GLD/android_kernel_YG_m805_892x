/****************************************************************************
 *   FileName    : tcc_tsif_hwset.h
 *   Description : 
 ****************************************************************************
 *
 *   TCC Version 1.0
 *   Copyright (c) Telechips, Inc.
 *   ALL RIGHTS RESERVED
 *
 ****************************************************************************/
#ifndef __TCC_TSIF_MODULE_HWSET_H__
#define __TCC_TSIF_MODULE_HWSET_H__

#include <mach/memory.h>
#include <mach/irqs.h>
#define    SUPPORT_STB_TSIF_INTERFACE //Support TSIF interface at STB solution

#define    SUPPORT_PIDFILTER_INTERNAL    //filtering support using internal logic(TSPID reg)
//#define    SUPPORT_PIDFILTER_DMA       //filtering suuport using dma logic

//20111212 koo : GDMA가 not defined 상태면 tsif-dma using으로 변경 됨. 
//#define GDMA 

#define TSIF_CH 					0		//tsif use ch0,1,2

#define TSIF_DMA_CONTROLLER		1		//tsif use gdma controller 1
#define TSIF_DMA_CH				2		//tsif use gdma ch2-n (n:0,1,2)

#define GPIO_D_PORT_TSIFNUM		0
#define GPIO_B0_PORT_TSIFNUM	1
#define GPIO_B1_PORT_TSIFNUM	2
#define GPIO_C_PORT_TSIFNUM		3
#define GPIO_E_PORT_TSIFNUM		4
#define GPIO_F_PORT_TSIFNUM		5


#if defined(CONFIG_MACH_TCC8930ST)
#define TSIF_GPIO_PORT	GPIO_D_PORT_TSIFNUM
#elif defined(CONFIG_MACH_TCC893X) || defined(CONFIG_MACH_TCC8935)
#define TSIF_GPIO_PORT	GPIO_F_PORT_TSIFNUM
#else
#define TSIF_GPIO_PORT	GPIO_F_PORT_TSIFNUM
#endif


#define TSIF_BYTE_SIZE			4		// 1, 2, 4byte
#define TSIF_RXFIFO_THRESHOLD	1		// 0-7 (0=>8 depth)
#define GDMA_WORD_SIZE			4		// 1, 2, 4byte
#define GDMA_BURST_SIZE			4		// 1, 2, 4, 8 = (TSIF_BYTE_SIZE * TSIF_RXFIFO_THRESHOLD) / GDMA_WORD_SIZE

#define MPEG_PACKET_SIZE 		(188)
#if defined(CONFIG_MACH_TCC9300ST)
#define TSIF_DMA_SIZE 				(CONSISTENT_DMA_SIZE / 8)
#elif defined(CONFIG_MACH_TCC8800)
#define TSIF_DMA_SIZE 				(CONSISTENT_DMA_SIZE / 8)
#elif defined(CONFIG_MACH_TCC8800ST)
#define TSIF_DMA_SIZE 				(CONSISTENT_DMA_SIZE / 8)
#elif defined(CONFIG_MACH_TCC8930ST)
#define TSIF_DMA_SIZE 				(CONSISTENT_DMA_SIZE / 8)
#else
#define TSIF_DMA_SIZE 				(CONSISTENT_DMA_SIZE / 8)
#endif
#define TSIF_MASK_SIZE			((0x10000000 - TSIF_DMA_SIZE) >> 4)

#define TSIF_DMA_HOPE_CNT(cnt)  (((cnt) * MPEG_PACKET_SIZE) / (TSIF_BYTE_SIZE * TSIF_RXFIFO_THRESHOLD))
#define TSIF_DMA_INCREASE_SIZE	0x4

#ifdef GDMA
#ifdef SUPPORT_PIDFILTER_INTERNAL
#define TSIF_DMA_PACKET_CNT		4
#else
#define TSIF_DMA_PACKET_CNT		512
#endif
#else
#define TSIF_DMA_PACKET_CNT		1
#endif


#define WAIT_TIME_FOR_DMA_DONE	(1000 * 8)

#pragma pack(push, 4)
struct tcc_tsif_regs {
    volatile unsigned long TSDI, TSRXCR, TSPID[16], TSIC, TSIS, TSISP, TSTXC;
};
#pragma pack(pop)

struct tea_dma_buf {
    void *v_addr;
    unsigned int dma_addr;
    int buf_size; // total size of DMA
    void *v_sec_addr;
    unsigned int dma_sec_addr;
    int buf_sec_size; // total size of DMA
};

typedef struct tcc_tsif_handle tcc_tsif_handle_t;
typedef int (*dma_alloc_f)(struct tea_dma_buf *tdma, unsigned int size);
typedef void (*dma_free_f)(struct tea_dma_buf *tdma);

typedef int (*FN_UPDATECALLBACK) (unsigned int dmxid, unsigned int ftype, unsigned fid, unsigned int value1, unsigned int value2, unsigned int bErrCRC);

struct tcc_tsif_handle {
    struct tea_dma_buf *dma_buffer;
    unsigned int dmx_id;
    unsigned int dma_intr_packet_cnt;
    unsigned int dma_mode;
    unsigned int serial_mode;
    unsigned char *fw_data;
    unsigned int fw_size;
    unsigned int tsif_port_num;
    unsigned int working_mode; //0:tsif for tdmb, 1:tsif mode for dvbt & isdbt, 2:internal mode

#if defined(CONFIG_iTV_PLATFORM)
    unsigned int dma_recv_packet_cnt;
    int cur_q_pos;
#endif
};

struct tcc_tsif_filter {
    unsigned int f_id;
    unsigned int f_type;
    unsigned int f_pid;
    unsigned int f_size;
    unsigned char *f_comp;
    unsigned char *f_mask;
    unsigned char *f_mode;
};

extern int tca_tsif_init(struct tcc_tsif_handle *h);
extern void tca_tsif_clean(struct tcc_tsif_handle *h);

extern int tca_tsif_register_pids(struct tcc_tsif_handle *h, unsigned int *pids, unsigned int count);
extern int tca_tsif_can_support(void);
extern int tca_tsif_buffer_updated_callback(struct tcc_tsif_handle *h, FN_UPDATECALLBACK buffer_updated);
extern int tca_tsif_set_pcrpid(struct tcc_tsif_handle *h, unsigned int pid);
extern long long tca_tsif_get_stc(struct tcc_tsif_handle *h);
extern int tca_tsif_add_filter(struct tcc_tsif_handle *h, struct tcc_tsif_filter *feed);
extern int tca_tsif_remove_filter(struct tcc_tsif_handle *h, struct tcc_tsif_filter *feed);
#endif /*__TCC_TSIF_MODULE_HWSET_H__*/
