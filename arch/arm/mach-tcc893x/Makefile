#
# Makefile for Telechips TCC88xx processors
#

# Common support

obj-$(CONFIG_ARCH_TCC893X)      += cpu_early_init.o
obj-$(CONFIG_ARCH_TCC893X)	+= pmu.o
obj-$(CONFIG_ARCH_TCC893X)      += irq.o io.o clock.o tcc_ddr.o

obj-$(CONFIG_ARCH_TCC893X)      += gic.o timer.o
obj-$(CONFIG_LOCAL_TIMERS)      += localtimer.o

obj-$(CONFIG_ARCH_TCC893X)      += pm.o gpio.o 
obj-$(CONFIG_ARCH_TCC893X)      += devices.o
obj-$(CONFIG_ARCH_TCC893X)      += tca_ckc.o
obj-$(CONFIG_ARCH_TCC893X)      += tca_adconv.o tcc_adc.o
obj-$(CONFIG_ARCH_TCC893X)      += tcc_asm.o
obj-$(CONFIG_ARCH_TCC893X)      += tca_serial.o
obj-$(CONFIG_ARCH_TCC893X)      += tca_i2c.o
obj-$(CONFIG_ARCH_TCC893X)      += tca_spi.o tca_tco.o
obj-$(CONFIG_ARCH_TCC893X)      += tca_tsif.o TCC893xHWDemux_cmd.o
obj-$(CONFIG_ARCH_TCC893X)      += tca_intr.o
obj-$(CONFIG_ARCH_TCC893X)      += tccfb_address.o
obj-$(CONFIG_ARCH_TCC893X)      += vioc/vioc_disp.o vioc/vioc_outcfg.o vioc/vioc_rdma.o vioc/vioc_wdma.o vioc/vioc_wmix.o vioc/vioc_viqe.o \
                                                           vioc/vioc_scaler.o vioc/vioc_ireq.o vioc/vioc_api.o vioc/vioc_config.o vioc/vioc_vin.o

obj-$(CONFIG_TCC_CIPHER)		+= tca_cipher.o
obj-$(CONFIG_CPU_FREQ)          += cpufreq.o
obj-$(CONFIG_SERIAL_TCC_SC) += tca_sc.o 

obj-$(CONFIG_TCC_ECID_SUPPORT)  += tcc_ecid.o

# Specific board support
obj-$(CONFIG_MACH_TCC893X)      += board-tcc8930.o board-tcc8930-gpio.o board-tcc8930-keypad.o board-tcc8930-power.o
obj-$(CONFIG_MACH_TCC893X)      += board-tcc8930-panel.o
obj-$(CONFIG_MACH_TCC893X)      += board-tcc8930-mmc.o
obj-$(CONFIG_MACH_TCC893X)      += board-tcc8930-dxb.o
obj-$(CONFIG_MACH_TCC893X)      += board-tcc8930-hdmi.o
obj-$(CONFIG_MACH_TCC893X)      += board-tcc8930-bluetooth.o
obj-$(CONFIG_MACH_TCC893X)      += board-tcc8930-camera.o
obj-$(CONFIG_MACH_TCC893X)      += board-tcc8930-pmic.o
obj-$(CONFIG_MACH_TCC893X)      += board-tcc8930-uart.o
obj-$(CONFIG_INPUT_TCC_REMOTE) += board-tcc8930-remocon.o
obj-$(CONFIG_MACH_TCC8930ST)     += board-tcc8930st.o board-tcc8930st-keypad.o board-tcc8930st-power.o
obj-$(CONFIG_MACH_TCC8930ST)     += board-tcc8930st-panel.o
obj-$(CONFIG_MACH_TCC8930ST)    += board-tcc8930st-mmc.o
obj-$(CONFIG_MACH_TCC8930ST)    += board-tcc8930st-dxb.o
obj-$(CONFIG_MACH_TCC8930ST)    += board-tcc8930st-hdmi.o
obj-$(CONFIG_MACH_TCC8930ST)      += board-tcc8930st-uart.o
obj-$(CONFIG_MACH_TCC8930ST)      += board-tcc8930st-gpio.o
obj-$(CONFIG_MACH_TCC8930ST)    += tcc_display.o
obj-$(CONFIG_MACH_M805_893X)    += board-m805_893x.o board-m805_893x-gpio.o board-m805_893x-keypad.o board-m805_893x-power.o
obj-$(CONFIG_MACH_M805_893X)    += board-m805_893x-panel.o
obj-$(CONFIG_MACH_M805_893X)    += board-m805_893x-mmc.o
obj-$(CONFIG_MACH_M805_893X)    += board-m805_893x-dxb.o
obj-$(CONFIG_MACH_M805_893X)    += board-m805_893x-hdmi.o
obj-$(CONFIG_MACH_M805_893X)    += board-m805_893x-camera.o
obj-$(CONFIG_MACH_M805_893X)    += board-m805_893x-pmic.o
obj-$(CONFIG_MACH_M805_893X)    += board-m805_893x-uart.o
obj-$(CONFIG_SMP)               += tcc893x_headsmp.o
obj-$(CONFIG_SMP)               += tcc893x_smp.o
obj-$(CONFIG_SMP)               += tcc893x_hotplug.o
obj-$(CONFIG_CPU_IDLE)          += cpuidle.o
obj-$(CONFIG_AUTO_HOTPLUG)      += auto_hotplug.o