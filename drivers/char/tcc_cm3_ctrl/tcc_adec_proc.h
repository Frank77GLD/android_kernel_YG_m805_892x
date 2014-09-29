/* 
 * linux/drivers/char/tcc_cm3_ctrl/tcc_adec_proc.h
 *
 * Author:  <linux@telechips.com>
 * Created: 10th Jun, 2008 
 * Description: Telechips Linux Cortex M3 Audio decoder
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

#ifndef     _TCC_ADEC_PROC_H_
#define     _TCC_ADEC_PROC_H_

#include "tcc_cm3_control.h"

typedef enum
{
    HW_ADEC_INIT = 0xC0,
    HW_ADEC_DECODE,
    HW_ADEC_CLOSE, 
    HW_ADEC_FLUSH, 
}HWADEC_CMD;

typedef struct{
    int iOpCode;
    int* pHandle;
    void* pParam1;
    void* pParam2; 
}t_cm3_adec_cmd;

typedef struct
{
	unsigned int uiChannelCount; 
	unsigned int uiSampleRate; 
	unsigned int uiBitPerSample;
}ADEC_Init_InParam;

typedef struct
{
	unsigned int uiChannelCount; 
	unsigned int uiSampleRate; 
	unsigned int uiSamplesPerChannel;
}ADEC_Init_OutParam;

typedef struct
{
	unsigned int uiStreamSize;
	void*		 pStreamBuff;
	void*		 pPcmBuff;
}ADEC_Decode_InParam;

typedef struct
{
	unsigned int uiRemainedStreamSize;
	unsigned int uiSamplesPerChannel; 
	unsigned int uiNumberOfChannel; 
}ADEC_Decode_OutParam;

typedef union
{
	ARG_CM3_CMD tDefault;	

	struct
	{	
		unsigned int uiCmdId;
		ADEC_Init_InParam inParam;
	}tInit;	

	struct
	{	
		unsigned int uiCmdId;
		ADEC_Decode_InParam inParam;
	}tDecode;	
}ARG_ADEC_CMD_IN;

typedef union
{
	ARG_CM3_CMD tDefault;	

	struct
	{	
		unsigned int uiCmdId;
		unsigned int ret;
		ADEC_Init_OutParam outParam;

	}tInit;	

	struct
	{	
		unsigned int uiCmdId;
		unsigned int ret;
		ADEC_Decode_OutParam outParam;
	}tDecode;	
}ARG_ADEC_CMD_OUT;

extern int ADEC_Proc(unsigned long arg);

#endif	//_TCC_ADEC_PROC_H_

