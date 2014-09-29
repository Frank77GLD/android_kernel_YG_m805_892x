/****************************************************************************
 *   FileName    : TCC893xHWDemux_cmd.h
 *   Description : 
 ****************************************************************************
 *
 *   TCC Version 1.0
 *   Copyright (c) Telechips, Inc.
 *   ALL RIGHTS RESERVED
 *
 ****************************************************************************/
#ifndef __TCC893X_HWDEMUX_COMMAND_MODULE_H__
#define __TCC893X_HWDEMUX_COMMAND_MODULE_H__

typedef enum
{
    HW_DEMUX_INIT = 0xD0,
    HW_DEMUX_DEINIT,
    HW_DEMUX_ADD_FILTER, 
    HW_DEMUX_DELETE_FILTER, 
    HW_DEMUX_GET_STC, 
    HW_DEMUX_SET_PCR_PID,
    HW_DEMUX_GET_VERSION,
    HW_DEMUX_INTERNAL_SET_INPUT
}TSDMX_CMD;

//RESPONSE
typedef enum
{
    HW_DEMUX_BUFFER_UPDATED = 0xE1,
    HW_DEMUX_DEBUG_DATA  = 0xF1,     
}TSDMX_RES;

//FILTER TYPE
typedef enum
{
	HW_DEMUX_SECTION = 0,
	HW_DEMUX_TS,
	HW_DEMUX_PES,
}TSDMX_FILTER_TYPE;

typedef struct _ARG_TSDMX_INIT_
{
    unsigned int uiDMXID;
    unsigned int uiMode;
    unsigned int uiTSRingBufAddr;
    unsigned int uiTSRingBufSize;
    unsigned int uiSECRingBufAddr;
    unsigned int uiSECRingBufSize;
    unsigned int uiTSIFInterface;
    unsigned int uiTSIFCh;
    unsigned int uiTSIFPort;
    unsigned int uiTSIFPol;
}ARG_TSDMXINIT;
#define HW_DEMUX_NORMAL     0 //for uiMODE
#define HW_DEMUX_BYPASS     1 //for uiMODE

#define HW_DEMUX_TSIF_SERIAL       0 //for uiTSIFInterface
#define HW_DEMUX_TSIF_PARALLEL     1 //for uiTSIFInterface
#define HW_DEMUX_SPI               2 //for uiTSIFInterface
#define HW_DEMUX_INTERNAL          3 //for uiTSIFInterface - Demuxer ID should be 1

typedef struct _ARG_TSDMX_ADD_FILTER_
{
    unsigned int uiDMXID;
    unsigned int uiFID;
    unsigned int uiTotalIndex;
    unsigned int uiCurrentIndex;
    unsigned int uiTYPE;
    unsigned int uiPID;
    unsigned int uiFSIZE;
    unsigned int uiVectorData[20];
    unsigned int uiVectorCount;
    unsigned int uiVectorIndex;
}ARG_TSDMX_ADD_FILTER;

typedef struct _ARG_TSDMX_DELETE_FILTER_
{
    unsigned int uiDMXID;
    unsigned int uiFID;
    unsigned int uiTYPE;
    unsigned int uiPID;
}ARG_TSDMX_DELETE_FILTER;

typedef struct _ARG_TSDMX_SET_PCR_PID_
{
    unsigned int uiDMXID;
    unsigned int uiPCRPID;
}ARG_TSDMX_SET_PCR_PID;

typedef struct _ARG_TSDMX_SET_IN_BUFFER_
{
    /* Set input buffer in HW_DEMUX_INTERNAL mode
     */
    unsigned int uiDMXID;
    unsigned int uiInBufferAddr;
    unsigned int uiInBufferSize;
}ARG_TSDMX_SET_IN_BUFFER;

extern int TSDMXCMD_Init(ARG_TSDMXINIT *pARG);
extern int TSDMXCMD_DeInit(unsigned int uiDMXID);
extern int TSDMXCMD_ADD_Filter(ARG_TSDMX_ADD_FILTER *pARG);
extern int TSDMXCMD_DELETE_Filter(ARG_TSDMX_DELETE_FILTER *pARG);
extern long long TSDMXCMD_GET_STC(unsigned int uiDMXID);
extern int TSDMXCMD_SET_PCR_PID(ARG_TSDMX_SET_PCR_PID *pARG);
extern int TSDMXCMD_SET_IN_BUFFER(ARG_TSDMX_SET_IN_BUFFER *pARG);
extern int TSDMXCMD_GET_VERSION(unsigned int uiDMXID);

#endif /*__TCC_TSIF_MODULE_HWSET_H__*/
