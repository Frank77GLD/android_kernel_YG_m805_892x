/* 
 * linux/drivers/char/tcc_cm3_ctrl/tcc_avn_proc.h
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

#ifndef     _TCC_AVN_PROC_H_
#define     _TCC_AVN_PROC_H_

#include "tcc_cm3_control.h"

typedef enum
{
    HW_TIMER_TEST = 0x70,
}CM3_AVN_CMD;


extern int CM3_AVN_Proc(unsigned long arg);

#endif	//_TCC_AVN_PROC_H_

