//-------------------------------------------------------------------
// Copyright SAMSUNG Electronics Co., Ltd
// All right reserved.
//
// This software is the confidential and proprietary information
// of Samsung Electronics, Inc. ("Confidential Information").  You
// shall not disclose such Confidential Information and shall use
// it only in accordance with the terms of the license agreement
// you entered into with Samsung Electronics.
//-------------------------------------------------------------------
/**
 * @file  regs-vwrapper.h
 * @brief This file defines video wrapper.\n
 *        ONLY FOR SAMSUNG FPGA TEST \n  
 *
 * @author   Digital IP Development Team (js13.park@samsung.com) \n
 *           SystemLSI, Samsung Electronics
 */

#ifndef __REGS_VIDEO_WRAPPER_H
#define __REGS_VIDEO_WRAPPER_H

#define HDMIDP_VIDEO_WRAPPER_REG(x)    (0x00A50000 + x)

//@{
/**
 * @name Video Wrapper config registers
 */
#define VIDEO_WRAPPER_SYNC_MODE         HDMIDP_VIDEO_WRAPPER_REG(0x0000)
#define VIDEO_WRAPPER_HV_LINE_0         HDMIDP_VIDEO_WRAPPER_REG(0x0004)
#define VIDEO_WRAPPER_HV_LINE_1         HDMIDP_VIDEO_WRAPPER_REG(0x0008)
#define VIDEO_WRAPPER_VSYNC_GEN         HDMIDP_VIDEO_WRAPPER_REG(0x000C)
#define VIDEO_WRAPPER_HSYNC_GEN         HDMIDP_VIDEO_WRAPPER_REG(0x0010)
#define VIDEO_WRAPPER_H_BLANK           HDMIDP_VIDEO_WRAPPER_REG(0x0014)
#define VIDEO_WRAPPER_V_PATTERN         HDMIDP_VIDEO_WRAPPER_REG(0x0018)
#define VIDEO_WRAPPER_SYS_EN            HDMIDP_VIDEO_WRAPPER_REG(0x001C)
#define VIDEO_WRAPPER_VCLK_SEL		HDMIDP_VIDEO_WRAPPER_REG(0x0020)
#define VIDEO_WRAPPER_3D_SEL		HDMIDP_VIDEO_WRAPPER_REG(0x0030)
//@}

#define V_PATTERN_PASS_TO_CORE		0x1
#define VCLK_SEL_TO_EXTERNAL		0x0
#define VCLK_SEL_TO_INTERNAL		0x1
#define WRAPPER_3D_ENABLE		0x1
#define WRAPPER_2D_ENABLE		0x0


#endif /* __REGS_VIDEO_WRAPPER_H */
