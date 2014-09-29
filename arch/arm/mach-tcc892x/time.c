/*
 * linux/arch/arm/mach-tcc892x/time.c
 *
 * Author:  <linux@telechips.com>
 *
 * Description: TCC Timers
 *
 * Copyright (C) 2011 Telechips, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 *  Returns elapsed usecs since last system timer interrupt
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/cnt32_to_63.h>
#include <linux/spinlock.h>
#include <linux/irq.h>	/* for setup_irq() */
#include <linux/mm.h>	/* for PAGE_ALIGN */
#include <linux/clk.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/mutex.h>

#include <asm/io.h>
#include <asm/leds.h>
#include <asm/mach/time.h>
#include <asm/sched_clock.h>
#include <mach/bsp.h>

#define TCC_TIMER_FREQ (12000000L) /* 12M */

#if (TCC_TIMER_FREQ < (USEC_PER_SEC))
#   define PRESCALE_TO_MICROSEC(X) ((X) * ((USEC_PER_SEC) / (TCC_TIMER_FREQ)))
#else
#   define PRESCALE_TO_MICROSEC(X) ((X) / ((TCC_TIMER_FREQ) / (USEC_PER_SEC)))
#endif

// Global
static volatile PTIMER pTIMER;
static volatile PPIC pPIC;
static struct clk *pTimerClk;

#undef TICKLESS_DEBUG_TCC

//#define TCC_USE_BIT_FIELD


#if defined(TICKLESS_DEBUG_TCC)
static unsigned int  gInt_cnt = 0;
static unsigned int  gTimer_cnt = 0;
static unsigned int  gEvent_cnt = 0;
static unsigned int  gEvent_over_cnt = 0;
extern unsigned long volatile __jiffy_data jiffies;
static unsigned long gdelta_min = 0xFFFFFF;
static unsigned long gdelta_max = 0;
#endif






#define MIN_OSCR_DELTA 2


static int tcc892x_timer_set_next_event(unsigned long cycles, struct clock_event_device *evt)
{
	u32 flags, next, oscr;

#if defined(TICKLESS_DEBUG_TCC)
	static unsigned long jiffies_old;
#endif

    raw_local_irq_save(flags);

	next = pTIMER->TC32MCNT.nREG + cycles;
	pTIMER->TC32CMP0.nREG = next;				/* Load counter value */
	oscr = pTIMER->TC32MCNT.nREG;
	pTIMER->TC32IRQ.bREG.IRQEN0 = 1;			/* Enable interrupt at the end of count */

    raw_local_irq_restore(flags);

#if defined(TICKLESS_DEBUG_TCC)
	if (cycles > 0xEA00)	/* 10ms == 0xEA60 */
		gEvent_over_cnt++;

	if (gdelta_min > cycles)
		gdelta_min = cycles;
	if (gdelta_max < cycles)
		gdelta_max = cycles;

	gEvent_cnt++;
	if (gInt_cnt >= 5000) {
		printk("\nMin Delta: %x \t Max Delta: %x\n", gdelta_min, gdelta_max);
		printk("%s(%d) .. jiffies[%d, %d], int[%4d] event[%4d] delta[%08x] next[%08x]oscr[%08x]\n",
		       __func__,
		       gEvent_over_cnt,
		       jiffies,
		       jiffies - jiffies_old,
		       gInt_cnt,
		       gEvent_cnt,
		       cycles,
		       next,
		       oscr);
		jiffies_old = jiffies;
		gEvent_cnt = 0;
		gInt_cnt = 0;
		gEvent_over_cnt = 0;
		gdelta_min = 0xffffff;
		gdelta_max = 0;
	}
#endif

	return (signed)(next - oscr) <= MIN_OSCR_DELTA ? -ETIME : 0;
}

static void tcc892x_timer_set_mode(enum clock_event_mode mode, struct clock_event_device *evt)
{
	u32 flags;
    u32 period;

#if defined(TICKLESS_DEBUG_TCC)
	printk("%s: mode %s... %d\n", __func__,
	       mode == CLOCK_EVT_MODE_ONESHOT  ? "ONESHOT"   :
	       mode == CLOCK_EVT_MODE_UNUSED   ? "UNUSED"    :
	       mode == CLOCK_EVT_MODE_SHUTDOWN ? "SHUTDOWN"  :
	       mode == CLOCK_EVT_MODE_RESUME   ? "RESUME"    :
	       mode == CLOCK_EVT_MODE_PERIODIC ? "PERIODIC"  : "non",
	       gTimer_cnt++);
#endif

	switch (mode) {
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_ONESHOT:
        raw_local_irq_save(flags);
		pTIMER->TC32IRQ.bREG.IRQEN0 = 0;        /* Disable interrupt when the counter value matched with CMP0 */
		pPIC->CLR0.bREG.TC1 = 1;                /* PIC Interrupt clear */
		if (pTIMER->TC32IRQ.bREG.IRQCLR)        /* IRQ clear */
			pTIMER->TC32IRQ.bREG.IRQCLR = 1;
        raw_local_irq_restore(flags);
		break;
	case CLOCK_EVT_MODE_PERIODIC:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}

}

static struct clock_event_device tcc892x_clockevent = {
	.name		= "timer0",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 32,
	.rating		= 250,
	.set_next_event	= tcc892x_timer_set_next_event,
	.set_mode	= tcc892x_timer_set_mode,
};


/*
 * clocksource
 */
static DEFINE_CLOCK_DATA(cd);

static cycle_t tcc892x_read_cycles(struct clocksource *cs)
{
	return pTIMER->TC32MCNT.nREG;
}

static struct clocksource tcc892x_clocksource = {
	.name		= "timer0",
	.rating		= 300,
	.read		= tcc892x_read_cycles,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 20,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int clock_valid = 0;
unsigned long long notrace sched_clock(void)
{
	return (unsigned long long)(jiffies - INITIAL_JIFFIES) * (NSEC_PER_SEC / HZ);
}

static void notrace tcc892x_update_sched_clock(void)
{
    u32 cyc = 0;
    if (likely(clock_valid))
        cyc = pTIMER->TC32MCNT.nREG;
    update_sched_clock(&cd, cyc, (u32)~0);
}

static irqreturn_t tcc892x_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *c = dev_id;

	pTIMER->TC32IRQ.bREG.IRQEN0 = 0;			/* Disable interrupt when the counter value matched with CMP0 */
	if (pTIMER->TC32IRQ.bREG.IRQCLR)			/* IRQ clear */
		pTIMER->TC32IRQ.bREG.IRQCLR = 1;

	if (irq >= 32) {
		pPIC->CLR1.nREG |= (1 << (irq-32));		/* Interrupt clear */
	}
	else {
		pPIC->CLR0.nREG |= (1 << irq);		/* Interrupt clear */
	}

	c->event_handler(c);

#if defined(TICKLESS_DEBUG_TCC)
	gInt_cnt++;
#endif

	return IRQ_HANDLED;
}


static struct irqaction tcc892x_timer_irq = {
	.name		= "timer1",
    .irq        = INT_TC32,
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= tcc892x_timer_interrupt,
	.dev_id		= &tcc892x_clockevent,
};

static void __init tcc892x_timer_init(void)
{
	unsigned long	rate;
	int ret;
	rate = TCC_TIMER_FREQ;

	pTIMER	= (volatile PTIMER) tcc_p2v(HwTMR_BASE);
	pPIC	= (volatile PPIC) tcc_p2v(HwPIC_BASE);

    /*
     * Enable timer clock
     */
	pTimerClk = clk_get(NULL, "timerz");
	BUG_ON(IS_ERR(pTimerClk));
	clk_enable(pTimerClk);
    clk_set_rate(pTimerClk, TCC_TIMER_FREQ);


    /* Initialize the timer */
    pTIMER->TC32EN.nREG = 1;            /* Timer Disable, Prescale is one */
    pTIMER->TC32EN.bREG.EN = 1;         /* Timer Enable */
    if (pTIMER->TC32IRQ.bREG.IRQCLR)    /* Timer IRQ Clear */
        pTIMER->TC32IRQ.bREG.IRQCLR = 1;

    /*
     * Initialize the clocksource device 
     */
    pr_info("Initialize the clocksource device.... rate[%d]", CLOCK_TICK_RATE);
	init_sched_clock(&cd, tcc892x_update_sched_clock, 32, CLOCK_TICK_RATE);
	//tcc892x_clocksource.mult = clocksource_hz2mult(CLOCK_TICK_RATE, tcc892x_clocksource.shift);

    clocksource_register_hz(&tcc892x_clocksource, CLOCK_TICK_RATE);

    pr_info("Initialize the clocksource device.... mult[%x] shift[%x]", cd.mult, cd.shift);

	tcc892x_clockevent.mult =
		div_sc(CLOCK_TICK_RATE, NSEC_PER_SEC, tcc892x_clockevent.shift);
	tcc892x_clockevent.max_delta_ns =
		clockevent_delta2ns(0x7fffffff, &tcc892x_clockevent);
	tcc892x_clockevent.min_delta_ns =
		clockevent_delta2ns(4, &tcc892x_clockevent) + 1;
	tcc892x_clockevent.cpumask = cpumask_of(0);
	tcc892x_clockevent.irq = tcc892x_timer_irq.irq;

	pPIC->SEL0.bREG.TC1 = 1;
	pPIC->IEN0.bREG.TC1 = 1;
	pPIC->INTMSK0.bREG.TC1 = 1;
	pPIC->MODEA0.bREG.TC1 = 1;

	setup_irq(INT_TC32, &tcc892x_timer_irq);

	clockevents_register_device(&tcc892x_clockevent);

	/*
	 * Set scale and timer for sched_clock
	 */
	//setup_sched_clock(CLOCK_TICK_RATE);

	clock_valid = 1;
}

struct sys_timer tcc_timer = {
	.init		= tcc892x_timer_init,
};

