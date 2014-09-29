/* linux/arch/arm/mach-tcc88xx/cpu_early_init.S
 *
 * Copyright (C) 2010 Telechips, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

//=================================================================
// __cpu_early_init
//=================================================================
	.global __cpu_early_init
__cpu_early_init:
	//Bruce, CortexA-5 does not have internal L2 Cache.
	/* Enable L2 cache */
	//mrc	p15, 0, r0, c1, c0, 1	@ read auxiliary control reg
	//orr	r0, r0, #(1<<1)
	//mcr	p15, 0, r0, c1, c0, 1	@ write auxiliary control reg

	//Bruce, Power Control Register
	mrc p15, 0, r0, c15, c0, 0
	orr r0, r0, #0x1 //;EDCG=1
	mcr p15, 0, r0, c15, c0, 0

	//Bruce, for using VFP of the Cortex-A5,
	// 1. Non-Secure mode should be permitted to access to CP10 and CP11.
	// 2. Enable Advenced SIMD extention instructions that are not VFP instructions ard undefined.
	// 3. Enable D16-D31 of the VFP register file.
	MRC p15, 0, r0, c1, c1, 2
	ORR r0, r0, #1<<10
	ORR r0, r0, #1<<11
	MCR p15, 0, r0, c1, c1, 2

	LDR r0, =(0xF << 20)
	MCR p15, 0, r0, c1, c0, 2

	//MOV r0, #0x40000000
	//VMSR FPEXC, r0

#if defined(CONFIG_DRAM_DDR3)
#if (1)
#if (1) //defined(CONFIG_CHIP_TCC8935)
	ldr	r1, =0x00000060
	ldr	r2, =0x00B000B0
	//ldr	r3, =0x05FF05FF
	// CBUS M0
	ldr	r0, =0xf4c42100
	str	r1, [r0, #0x0C]
	str	r2, [r0, #0x10]
	// VBUS
	//ldr	r0, =0xf4c43100
	//str	r1, [r0, #0x0C]
	//str	r3, [r0, #0x10]
#endif
#else
	ldr	r1, =0x00000060
	ldr	r2, =0x06FF06FF
	ldr	r3, =0x00B000B0
	// CBUS M0
	ldr	r0, =0xf4c42100
	str	r1, [r0, #0x0C]
	str	r3, [r0, #0x10]
	// VBUS M0
	ldr	r0, =0xf4c43100
	str	r1, [r0, #0x0C]
	str	r2, [r0, #0x10]
	// GBUS
//	ldr	r0, =0xf4c44100
//	str	r1, [r0, #0x0C]
//	str	r2, [r0, #0x10]
	// DBUS M0
//	ldr	r0, =0xf4c45100
//	str	r1, [r0, #0x0C]
//	str	r2, [r0, #0x10]
	// DBUS M1
//	ldr	r0, =0xf4c46100
//	str	r1, [r0, #0x0C]
//	str	r2, [r0, #0x10]
	// CBUS M1
	ldr	r0, =0xf4c47100
	str	r1, [r0, #0x0C]
	str	r2, [r0, #0x10]
	// VBUS M1 + M3BUS
	ldr	r0, =0xf4c48100
	str	r1, [r0, #0x0C]
	str	r2, [r0, #0x10]
	// IOBUS
//	ldr	r0, =0xf4c49100
//	str	r1, [r0, #0x0C]
//	str	r2, [r0, #0x10]
	// HSBIOBUS
	ldr	r0, =0xf4c4a100
	str	r1, [r0, #0x0C]
	str	r2, [r0, #0x10]
	// G2D
	ldr	r0, =0xf4c4b100
	str	r1, [r0, #0x0C]
	str	r2, [r0, #0x10]
#endif
#endif

	bx	lr
