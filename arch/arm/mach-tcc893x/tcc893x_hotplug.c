#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>

#include "gic.h"

#define TCC_SCU_BASE_ADDR 0x77200000
#define SEC_CPU_START_ADDR 0xFC00CDF8
#define SEC_CPU_START_CFG 0xFC00CDFC

#ifdef CONFIG_HAVE_ARM_TWD
extern void tcc893x_twd_disable(void);
extern void tcc893x_twd_enable(void);
#endif

extern volatile int pen_release;

int platform_cpu_kill(unsigned int cpu)
{
	return 1;
}

int platform_cpu_disable(unsigned int cpu)
{
	/*
	 * We don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}

static inline void cpu_enter_lowpower(void)
{
	unsigned int v;

	flush_cache_all();
	asm volatile(
			" mcr p15, 0, %1, c7, c5, 0\n"
			" mcr p15, 0, %1, c7, c10, 4\n"
			/*
			 * Turn off coherency
			 */
			" mrc p15, 0, %0, c1, c0, 1\n"
			" bic %0, %0, %3\n"
			" mcr p15, 0, %0, c1, c0, 1\n"
			" mrc p15, 0, %0, c1, c0, 0\n"
			" bic %0, %0, %2\n"
			" mcr p15, 0, %0, c1, c0, 0\n"
			: "=&r" (v)
			: "r" (0), "Ir" (CR_C), "Ir" (0x40)
			: "cc");
}

static inline void cpu_leave_lowpower(void)
{
	unsigned int v;

	asm volatile(
			"mrc p15, 0, %0, c1, c0, 0\n"
			"orr %0, %0, %1\n"
			"mcr p15, 0, %0, c1, c0, 0\n"
			"mrc p15, 0, %0, c1, c0, 1\n"
			"orr %0, %0, %2\n"
			"mcr p15, 0, %0, c1, c0, 1\n"
			: "=&r" (v)
			: "Ir" (CR_C), "Ir" (0x40)
			: "cc");
}

static inline void platform_do_lowpower(unsigned int cpu, int *spurious)
{
	/*
	 * There is no power-contro hardware on this platform, so all
	 * we can do is put the core into WFE, this is safe as the calling
	 * code will have already disabled interrupts
	 * reference : arch/arm/mach-vexpress/hotplug.c
	 */
	for (;;) {
		/*
		 * here's the WFI
		 */
		asm volatile ("dsb");
		asm volatile ("wfi");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");

		if (pen_release == cpu) {
			/*
			 * OK, proper wakeup, we're done
			 */
			break;
		}
		/*
		 * Getting here, means that we have come out of WFE without
		 * having been woken up - this shouldn't happen
		 *
		 * Just note it happening - when we're woken, we can report
		 * its occurrence.
		 */
		(*spurious)++;
	}
}

void platform_cpu_die(unsigned int cpu)
{
	int spurious = 0;

	/* 
	 * Private Timer disable
	 */
	//tcc893x_twd_disable();
	
	/* 
	 * GIC CPU interface disable
	 */
	//gic_disable_cpu(0);

	/*
	 * we're ready for shutdown now, so do it
	 */
	cpu_enter_lowpower();
	platform_do_lowpower(cpu, &spurious);

	/*
	 * bring this CPU back into the world of cache
	 * coherency, and then restore interrupts
	 */
	cpu_leave_lowpower();

	//gic_enable_cpu(0);
	//tcc893x_twd_enable();

	if (spurious)
		pr_warn("CPU%u: %u spurious wakeup calls\n", cpu, spurious);
}
