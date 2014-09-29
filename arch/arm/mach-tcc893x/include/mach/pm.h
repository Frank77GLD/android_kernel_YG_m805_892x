/*
 * arch/arm/mach-tcc893x/pm.h
 *
 * Author:  <linux@telechips.com>
 * Created: November 9, 2012
 * Description: LINUX POWER MANAGEMENT FUNCTIONS
 *
 * Copyright (C) Telechips 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#ifndef __TCC_PM_H__
#define __TCC_PM_H__

/*===========================================================================

                                  MODE

===========================================================================*/

//#define TCC_PM_PMU_CTRL
//#define TCC_PM_SSTLIO_CTRL
#define TCC_PM_CHECK_WAKEUP_SOURCE
#define MEMBUS_CLK_AUTO_RESTORE
#ifdef CONFIG_TCC_MEM_2048MB
#define DRAM_2GB_USED
#endif

#if defined(CONFIG_SHUTDOWN_MODE)
/*===========================================================================

                              Shut-down MAP

===========================================================================*/

/*---------------------------------------------------------------------------
 1) Shut-down (shut down + sram boot + sdram self-refresh)

     << sram >>
     0xF0000000(0x10000000) ------------------
                           |    Boot code     | 0x700
                     0x700  ------------------
                           |      Stack       | 0x100
                     0x800  ------------------
                           |     Shutdown     | 0x600
                     0xE00  ------------------
                           |     Wake-up      | 0x400
                    0x1200  ------------------
                           |    SDRAM Init    | 0xA00
                    0x1C00  ------------------
                           |   GPIO Storage   | 0x300
                    0x2000  ------------------
                           | cpu_reg/mmu data | 0x100
                    0x2100  ------------------
                           |  memclk restore  | 0x100
                    0x2200  ------------------

---------------------------------------------------------------------------*/

#define SRAM_BOOT_ADDR           0xF0000000
#define SRAM_BOOT_SIZE           0x00000700

#define SRAM_STACK_ADDR          0xF0000700
#define SRAM_STACK_SIZE          0x00000100

#define SHUTDOWN_FUNC_ADDR       0xF0000800
#define SHUTDOWN_FUNC_SIZE       0x00000600

#define WAKEUP_FUNC_ADDR         0xF0000E00
#define WAKEUP_FUNC_SIZE         0x00000400

#define SDRAM_INIT_FUNC_ADDR     0xF0001200
#define SDRAM_INIT_FUNC_SIZE     0x00000A00

#define GPIO_REPOSITORY_ADDR     0xF0001C00
#define GPIO_REPOSITORY_SIZE     0x00000300

#define CPU_DATA_REPOSITORY_ADDR 0xF0002000
#define CPU_DATA_REPOSITORY_SIZE 0x00000100

#if defined(MEMBUS_CLK_AUTO_RESTORE)
#define SDRAM_INIT_PARAM_ADDR    0xF0002100
#define SDRAM_INIT_PARAM_APHY    0x00002100
#define SDRAM_INIT_PARAM_SIZE    0x00000010

#define SDRAM_TIME2CYCLE_ADDR    0xF0002110
#define SDRAM_TIME2CYCLE_APHY    0x00002110
#define SDRAM_TIME2CYCLE_SIZE    0x000000F0
#endif

/*-------------------------------------------------------------------------*/


/*===========================================================================

                        Shut-down Backup Registers

===========================================================================*/
#if defined(CONFIG_PM_CONSOLE_NOT_SUSPEND)
typedef struct _BACKUP_UART {
	volatile unsigned int	DLL;	// 0x000  R/W  0x00000000   Divisor Latch (LSB) (DLAB=1)
	volatile unsigned int	IER;		// 0x004  R/W  0x00000000   Interrupt Enable Register (DLAB=0)
	volatile unsigned int	DLM;	// 0x004  R/W  0x00000000   Divisor Latch (MSB) (DLAB=1)
	volatile unsigned int	LCR;	// 0x00C  R/W  0x00000003   Line Control Register
	volatile unsigned int	MCR;	// 0x010  R/W  0x00000040   MODEM Control Register
	volatile unsigned int	AFT;	// 0x020  R/W  0x00000000   AFC Trigger Level Register
	volatile unsigned int	UCR;	// 0x024  R/W  0x00000000   UART Control register
	volatile unsigned int	CFG0;	// R/W  0x00000000   Port Configuration Register 0(PCFG0)
	volatile unsigned int	CFG1;	// R/W  0x00000000   Port Configuration Register 1(PCFG1)
} bkUART;
#endif

typedef struct _TCC_REG_{
	CKC ckc;
	PIC pic;
	VIC vic;
	TIMER timer;
#if defined(TCC_PM_PMU_CTRL)
	PMU pmu;
#endif
	SMUCONFIG smuconfig;
	IOBUSCFG iobuscfg;
	MEMBUSCFG membuscfg;
	NFC	nfc;
	GPIO gpio;
#ifdef CONFIG_CACHE_L2X0
	unsigned L2_aux;
#endif
#ifdef CONFIG_ARM_GIC
	unsigned gic_dist_ctrl;
	unsigned gic_dist_enable_set[3]; // 3 (1bit)
	unsigned gic_dist_config[6]; // 6 (2bit)
	unsigned gic_dist_pri[24]; // 24 (8bit)
	unsigned gic_dist_target[24]; // 24 (8bit)
	unsigned gic_cpu_primask;
	unsigned gic_cpu_ctrl;
#endif
#ifdef CONFIG_HAVE_ARM_TWD
	unsigned twd_timer_control;
	unsigned twd_timer_load;
#endif
#ifdef CONFIG_SMP
	unsigned scu_ctrl;
	#ifdef CONFIG_ARM_ERRATA_764369 /* Cortex-A9 only */
	unsigned scu_0x30;
	#endif
#endif
#if defined(CONFIG_PM_CONSOLE_NOT_SUSPEND)
	bkUART	bkuart;
#endif
} TCC_REG, *PTCC_REG;

#elif defined(CONFIG_SLEEP_MODE)

/*===========================================================================

                                  MODE

===========================================================================*/

/*===========================================================================

                              Sleep MAP

===========================================================================*/

/*---------------------------------------------------------------------------
 1) Sleep (Sleep + sdram self-refresh)

     << sram >>
     0xF0000000(0x10000000) ------------------
                           |                  | 0x700
                     0x700  ------------------
                           |      Stack       | 0x100
                     0x800  ------------------
                           |      Sleep       | 0xA00
                    0x1200  ------------------
                           |    SDRAM Init    | 0xA00
                    0x1C00  ------------------
                           |  memclk restore  | 0x100
                    0x1D00  ------------------

---------------------------------------------------------------------------*/

#define SRAM_STACK_ADDR          0xF0000700
#define SRAM_STACK_SIZE          0x00000100

#define SLEEP_FUNC_ADDR          0xF0000800
#define SLEEP_FUNC_SIZE          0x00000A00

#define SDRAM_INIT_FUNC_ADDR     0xF0001200
#define SDRAM_INIT_FUNC_SIZE     0x00000A00

#if defined(MEMBUS_CLK_AUTO_RESTORE)
#define SDRAM_INIT_PARAM_ADDR    0xF0001C00
#define SDRAM_INIT_PARAM_APHY    0xF0001C00
#define SDRAM_INIT_PARAM_SIZE    0x00000010

#define SDRAM_TIME2CYCLE_ADDR    0xF0001C10
#define SDRAM_TIME2CYCLE_APHY    0xF0001C10
#define SDRAM_TIME2CYCLE_SIZE    0x000000F0
#endif

/*-------------------------------------------------------------------------*/


/*===========================================================================

                         Sleep Backup Registers

===========================================================================*/

typedef struct _TCC_REG_{
	CKC ckc;
	PIC pic;
	VIC vic;
	TIMER timer;
#if defined(TCC_PM_PMU_CTRL)
	PMU pmu;
#endif
	SMUCONFIG smuconfig;
	IOBUSCFG iobuscfg;
	MEMBUSCFG membuscfg;
	NFC	nfc;
	GPIO gpio;
#ifdef CONFIG_CACHE_L2X0
	unsigned L2_aux;
#endif
#ifdef CONFIG_ARM_GIC
	unsigned gic_dist_ctrl;
	unsigned gic_dist_enable_set[3]; // 3 (1bit)
	unsigned gic_dist_config[6]; // 6 (2bit)
	unsigned gic_dist_pri[24]; // 24 (8bit)
	unsigned gic_dist_target[24]; // 24 (8bit)
	unsigned gic_cpu_primask;
	unsigned gic_cpu_ctrl;
#endif
#ifdef CONFIG_HAVE_ARM_TWD
	unsigned twd_timer_control;
	unsigned twd_timer_load;
#endif
#ifdef CONFIG_SMP
	unsigned scu_ctrl;
	#ifdef CONFIG_ARM_ERRATA_764369 /* Cortex-A9 only */
	unsigned scu_0x30;
	#endif
#endif
#if defined(CONFIG_PM_CONSOLE_NOT_SUSPEND)
	bkUART	bkuart;
#endif
} TCC_REG, *PTCC_REG;
#endif

/*-------------------------------------------------------------------------*/

#endif  /*__TCC_PM_H__*/
