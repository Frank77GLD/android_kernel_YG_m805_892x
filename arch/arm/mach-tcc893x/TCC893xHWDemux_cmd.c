/****************************************************************************
 *   FileName    :  TCC893xHWDemux_cmd.c
 *   Description : 
 ****************************************************************************
 *
 *   TCC Version 1.0
 *   Copyright (c) Telechips, Inc.
 *   ALL RIGHTS RESERVED
 *
 ****************************************************************************/

#include <linux/kernel.h>
#include <mach/bsp.h>
#include <mach/io.h>

#include <mach/TCC893xHWDemux_cmd.h>

int TSDMXCMD_Init(ARG_TSDMXINIT *pARG)
{
    volatile PMAILBOX pMailBox = (volatile PMAILBOX)tcc_p2v(HwCORTEXM3_MAILBOX0_BASE);
    BITCLR( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN low
    pMailBox->uMBOX_TX0.nREG = HW_DEMUX_INIT << 24 | (pARG->uiDMXID & 0xff) << 16 | (pARG->uiMode & 0xff) <<8;
    pMailBox->uMBOX_TX1.nREG = pARG->uiTSRingBufAddr;
    pMailBox->uMBOX_TX2.nREG = pARG->uiTSRingBufSize;
    pMailBox->uMBOX_TX3.nREG = pARG->uiSECRingBufAddr;
    pMailBox->uMBOX_TX4.nREG = pARG->uiSECRingBufSize;
    pMailBox->uMBOX_TX5.nREG = pARG->uiTSIFInterface << 24 | (pARG->uiTSIFCh & 0xff) <<16 | (pARG->uiTSIFPort & 0xff) <<8 | (pARG->uiTSIFPol & 0xff);
    pMailBox->uMBOX_TX6.nREG = 0;
    pMailBox->uMBOX_TX7.nREG = 0;
    BITSET( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN high

    return 0;
}

int TSDMXCMD_DeInit(unsigned int uiDMXID)
{
    volatile PMAILBOX pMailBox = (volatile PMAILBOX)tcc_p2v(HwCORTEXM3_MAILBOX0_BASE);
    BITCLR( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN low
    pMailBox->uMBOX_TX0.nREG = HW_DEMUX_DEINIT << 24 | (uiDMXID & 0xff) << 16;
    pMailBox->uMBOX_TX1.nREG = 0;
    pMailBox->uMBOX_TX2.nREG = 0;
    pMailBox->uMBOX_TX3.nREG = 0;
    pMailBox->uMBOX_TX4.nREG = 0;
    pMailBox->uMBOX_TX5.nREG = 0;
    pMailBox->uMBOX_TX6.nREG = 0;
    pMailBox->uMBOX_TX7.nREG = 0;
    BITSET( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN high

    return 0;
}

int TSDMXCMD_ADD_Filter(ARG_TSDMX_ADD_FILTER *pARG)
{
    volatile PMAILBOX pMailBox = (volatile PMAILBOX)tcc_p2v(HwCORTEXM3_MAILBOX0_BASE);
    BITCLR( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN low

    if(pARG->uiTYPE == HW_DEMUX_SECTION) {
        pMailBox->uMBOX_TX0.nREG = HW_DEMUX_ADD_FILTER << 24 | (pARG->uiDMXID & 0xff) << 16 | (pARG->uiFID & 0xff) << 8 | (pARG->uiTotalIndex & 0xf) << 4 | (pARG->uiCurrentIndex & 0xf);
        if(pARG->uiCurrentIndex == 0) {
            pMailBox->uMBOX_TX1.nREG = (pARG->uiTYPE & 0xff) << 24 | (pARG->uiPID & 0xffff) << 8 | (pARG->uiFSIZE & 0xff);
            pMailBox->uMBOX_TX2.nREG = (pARG->uiVectorData[pARG->uiVectorIndex++]);
            pMailBox->uMBOX_TX3.nREG = (pARG->uiVectorData[pARG->uiVectorIndex++]);
            pMailBox->uMBOX_TX4.nREG = (pARG->uiVectorData[pARG->uiVectorIndex++]);
            pMailBox->uMBOX_TX5.nREG = (pARG->uiVectorData[pARG->uiVectorIndex++]);
            pMailBox->uMBOX_TX6.nREG = (pARG->uiVectorData[pARG->uiVectorIndex++]);
            pMailBox->uMBOX_TX7.nREG = (pARG->uiVectorData[pARG->uiVectorIndex++]);
        } else {
            pMailBox->uMBOX_TX1.nREG = (pARG->uiVectorData[pARG->uiVectorIndex++]);
            pMailBox->uMBOX_TX2.nREG = (pARG->uiVectorData[pARG->uiVectorIndex++]);
            pMailBox->uMBOX_TX3.nREG = (pARG->uiVectorData[pARG->uiVectorIndex++]);
            pMailBox->uMBOX_TX4.nREG = (pARG->uiVectorData[pARG->uiVectorIndex++]);
            pMailBox->uMBOX_TX5.nREG = (pARG->uiVectorData[pARG->uiVectorIndex++]);
            pMailBox->uMBOX_TX6.nREG = (pARG->uiVectorData[pARG->uiVectorIndex++]);
            pMailBox->uMBOX_TX7.nREG = (pARG->uiVectorData[pARG->uiVectorIndex++]);
        }
    } else {
        pMailBox->uMBOX_TX0.nREG = HW_DEMUX_ADD_FILTER << 24 | (pARG->uiDMXID & 0xff) << 16;
        pMailBox->uMBOX_TX1.nREG = pARG->uiTYPE << 24 | (pARG->uiPID & 0xffff) << 8;
        pMailBox->uMBOX_TX2.nREG = 0;
        pMailBox->uMBOX_TX3.nREG = 0;
        pMailBox->uMBOX_TX4.nREG = 0;
        pMailBox->uMBOX_TX5.nREG = 0;
        pMailBox->uMBOX_TX6.nREG = 0;
        pMailBox->uMBOX_TX7.nREG = 0;
    }

    BITSET( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN high

    return 0;
}

int TSDMXCMD_DELETE_Filter(ARG_TSDMX_DELETE_FILTER *pARG)
{
    volatile PMAILBOX pMailBox = (volatile PMAILBOX)tcc_p2v(HwCORTEXM3_MAILBOX0_BASE);
    BITCLR( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN low
    pMailBox->uMBOX_TX0.nREG = HW_DEMUX_DELETE_FILTER << 24 | (pARG->uiDMXID & 0xff) << 16 | (pARG->uiFID & 0xff) << 8 | (pARG->uiTYPE & 0xff);
    pMailBox->uMBOX_TX1.nREG = (pARG->uiPID & 0xffff) << 16;
    pMailBox->uMBOX_TX2.nREG = 0;
    pMailBox->uMBOX_TX3.nREG = 0;
    pMailBox->uMBOX_TX4.nREG = 0;
    pMailBox->uMBOX_TX5.nREG = 0;
    pMailBox->uMBOX_TX6.nREG = 0;
    pMailBox->uMBOX_TX7.nREG = 0;
    BITSET( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN high

    return 0;
}

long long TSDMXCMD_GET_STC(unsigned int uiDMXID)
{
#ifndef     SUPPORT_DEBUG_CM3HWDEMUX    
    volatile PMAILBOX pMailBox = (volatile PMAILBOX)tcc_p2v(HwCORTEXM3_MAILBOX0_BASE);
    BITCLR( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN low
    pMailBox->uMBOX_TX0.nREG = HW_DEMUX_GET_STC << 24 | (uiDMXID & 0xff) << 16;
    pMailBox->uMBOX_TX1.nREG = 0;
    pMailBox->uMBOX_TX2.nREG = 0;
    pMailBox->uMBOX_TX3.nREG = 0;
    pMailBox->uMBOX_TX4.nREG = 0;
    pMailBox->uMBOX_TX5.nREG = 0;
    pMailBox->uMBOX_TX6.nREG = 0;
    pMailBox->uMBOX_TX7.nREG = 0;
    BITSET( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN high
#endif

    return 0;
}

int TSDMXCMD_SET_PCR_PID(ARG_TSDMX_SET_PCR_PID *pARG)
{
    volatile PMAILBOX pMailBox = (volatile PMAILBOX)tcc_p2v(HwCORTEXM3_MAILBOX0_BASE);
    BITCLR( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN low
    pMailBox->uMBOX_TX0.nREG = HW_DEMUX_SET_PCR_PID << 24 | (pARG->uiDMXID & 0xff) << 16 | (pARG->uiPCRPID & 0xffff);
    pMailBox->uMBOX_TX1.nREG = 0;
    pMailBox->uMBOX_TX2.nREG = 0;
    pMailBox->uMBOX_TX3.nREG = 0;
    pMailBox->uMBOX_TX4.nREG = 0;
    pMailBox->uMBOX_TX5.nREG = 0;
    pMailBox->uMBOX_TX6.nREG = 0;
    pMailBox->uMBOX_TX7.nREG = 0;
    BITSET( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN high

    return 0;
}

int TSDMXCMD_SET_IN_BUFFER(ARG_TSDMX_SET_IN_BUFFER *pARG)
{
    volatile PMAILBOX pMailBox = (volatile PMAILBOX)tcc_p2v(HwCORTEXM3_MAILBOX0_BASE);
    BITCLR( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN low
    pMailBox->uMBOX_TX0.nREG = HW_DEMUX_INTERNAL_SET_INPUT << 24 | (pARG->uiDMXID & 0xff) << 16;
    pMailBox->uMBOX_TX1.nREG = pARG->uiInBufferAddr;
    pMailBox->uMBOX_TX2.nREG = pARG->uiInBufferSize;
    pMailBox->uMBOX_TX3.nREG = 0;
    pMailBox->uMBOX_TX4.nREG = 0;
    pMailBox->uMBOX_TX5.nREG = 0;
    pMailBox->uMBOX_TX6.nREG = 0;
    pMailBox->uMBOX_TX7.nREG = 0;
    BITSET( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN high

    return 0;
}

int TSDMXCMD_GET_VERSION(unsigned int uiDMXID)
{
    volatile PMAILBOX pMailBox = (volatile PMAILBOX)tcc_p2v(HwCORTEXM3_MAILBOX0_BASE);
    BITCLR( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN low
    pMailBox->uMBOX_TX0.nREG = HW_DEMUX_GET_VERSION << 24 | (uiDMXID & 0xff) << 16;
    pMailBox->uMBOX_TX1.nREG = 0;
    pMailBox->uMBOX_TX2.nREG = 0;
    pMailBox->uMBOX_TX3.nREG = 0;
    pMailBox->uMBOX_TX4.nREG = 0;
    pMailBox->uMBOX_TX5.nREG = 0;
    pMailBox->uMBOX_TX6.nREG = 0;
    pMailBox->uMBOX_TX7.nREG = 0;
    BITSET( pMailBox->uMBOX_CTL_016.nREG, Hw5); //OEN high

    return 0;
}


