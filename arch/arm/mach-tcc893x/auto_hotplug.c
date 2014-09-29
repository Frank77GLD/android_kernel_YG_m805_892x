/*
 * arch/arm/mach-tcc893x/auto_hotplug.c
 *
 * CPU auto-hotplug for Tegra3 CPUs
 *
 * Copyright (c) 2011-2012, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/cpu.h>
#include <linux/clk.h>
#include <linux/seq_file.h>
#include <linux/pm_qos_params.h>

//#include "pm.h"
//#include "cpu-tegra.h"
//#include "clock.h"
#include "tcc_cpufreq.h"


static inline bool is_g_cluster_present(void)   { return true; }
static inline unsigned int is_lp_cluster(void)  { return 0; }
static inline bool tcc_cpu_edp_favor_up(unsigned int n, int mp_overhead)
{ return true; }
static inline bool tcc_cpu_edp_favor_down(unsigned int n, int mp_overhead)
{ return false; }

#define INITIAL_STATE		AUTO_HOTPLUG_IDLE	//AUTO_HOTPLUG_DISABLED
#define UP2G0_DELAY_MS		70
#define UP2Gn_DELAY_MS		100
#define DOWN_DELAY_MS		2000

#define IDLE_TOP_FREQ		850000
#define IDLE_BOTTOM_FREQ	450000

static struct mutex *tcc_cpu_lock;

static struct workqueue_struct *hotplug_wq;
static struct delayed_work hotplug_work;

static bool no_lp;
module_param(no_lp, bool, 0644);

static unsigned long up2gn_delay;
static unsigned long up2g0_delay;
static unsigned long down_delay;
module_param(up2gn_delay, ulong, 0644);
module_param(up2g0_delay, ulong, 0644);
module_param(down_delay, ulong, 0644);

static unsigned int idle_top_freq;
static unsigned int idle_bottom_freq;
module_param(idle_top_freq, uint, 0644);
module_param(idle_bottom_freq, uint, 0644);

static int mp_overhead = 10;
module_param(mp_overhead, int, 0644);

static int balance_level = 75;
module_param(balance_level, int, 0644);

#if (0)
static struct clk *cpu_clk;
static struct clk *cpu_g_clk;
static struct clk *cpu_lp_clk;
#endif

static struct {
	cputime64_t time_up_total;
	u64 last_update;
	unsigned int up_down_count;
} hotplug_stats[CONFIG_NR_CPUS + 1];	/* Append LP CPU entry at the end */

static void hotplug_init_stats(void)
{
	int i;
	u64 cur_jiffies = get_jiffies_64();

	for (i = 0; i <= CONFIG_NR_CPUS; i++) {
		hotplug_stats[i].time_up_total = 0;
		hotplug_stats[i].last_update = cur_jiffies;

		hotplug_stats[i].up_down_count = 0;
		if (is_lp_cluster()) {
			if (i == CONFIG_NR_CPUS)
				hotplug_stats[i].up_down_count = 1;
		} else {
			if ((i < nr_cpu_ids) && cpu_online(i))
				hotplug_stats[i].up_down_count = 1;
		}
	}

}

static void hotplug_stats_update(unsigned int cpu, bool up)
{
	u64 cur_jiffies = get_jiffies_64();
	bool was_up = hotplug_stats[cpu].up_down_count & 0x1;

	if (was_up)
		hotplug_stats[cpu].time_up_total = cputime64_add(
			hotplug_stats[cpu].time_up_total, cputime64_sub(
				cur_jiffies, hotplug_stats[cpu].last_update));

	if (was_up != up) {
		hotplug_stats[cpu].up_down_count++;
		if ((hotplug_stats[cpu].up_down_count & 0x1) != up) {
			/* FIXME: sysfs user space CPU control breaks stats */
			pr_err("hotplug stats out of sync with %s CPU%d",
			       (cpu < CONFIG_NR_CPUS) ? "G" : "LP",
			       (cpu < CONFIG_NR_CPUS) ?  cpu : 0);
			hotplug_stats[cpu].up_down_count ^=  0x1;
		}
	}
	hotplug_stats[cpu].last_update = cur_jiffies;
}


enum {
	AUTO_HOTPLUG_DISABLED = 0,
	AUTO_HOTPLUG_IDLE,
	AUTO_HOTPLUG_DOWN,
	AUTO_HOTPLUG_UP,
};
static int hotplug_state;

static int hotplug_stats_set(const char *arg, const struct kernel_param *kp)
{
	int ret = 0;
	int old_state;

	if (!tcc_cpu_lock)
		return ret;

	mutex_lock(tcc_cpu_lock);

	old_state = hotplug_state;
	ret = param_set_bool(arg, kp);	/* set idle or disabled only */

	if (ret == 0) {
		if ((hotplug_state == AUTO_HOTPLUG_DISABLED) &&
		    (old_state != AUTO_HOTPLUG_DISABLED))
			pr_info("Tegra auto-hotplug disabled\n");
		else if (hotplug_state != AUTO_HOTPLUG_DISABLED) {
			if (old_state == AUTO_HOTPLUG_DISABLED) {
				pr_info("Tegra auto-hotplug enabled\n");
				hotplug_init_stats();
			}
			/* catch-up with governor target speed */
			tcc_cpu_set_speed_cap(NULL);
		}
	} else
		pr_warn("%s: unable to set hotplug state %s\n",
				__func__, arg);

	mutex_unlock(tcc_cpu_lock);
	return ret;
}

static int hotplug_state_get(char *buffer, const struct kernel_param *kp)
{
	return param_get_int(buffer, kp);
}

static struct kernel_param_ops hotplug_state_ops = {
	.set = hotplug_stats_set,
	.get = hotplug_state_get,
};
module_param_cb(auto_hotplug, &hotplug_state_ops, &hotplug_state, 0644);


enum {
	TCC_CPU_SPEED_BALANCED,
	TCC_CPU_SPEED_BIASED,
	TCC_CPU_SPEED_SKEWED,
};

static noinline int tcc_cpu_speed_balance(void)
{
	unsigned long highest_speed = tcc_cpu_highest_speed();
	unsigned long balanced_speed = highest_speed * balance_level / 100;
	unsigned long skewed_speed = balanced_speed / 2;
	unsigned int nr_cpus = num_online_cpus();
	unsigned int max_cpus = pm_qos_request(PM_QOS_MAX_ONLINE_CPUS) ? : 4;
	unsigned int min_cpus = pm_qos_request(PM_QOS_MIN_ONLINE_CPUS);

	/* balanced: freq targets for all CPUs are above 50% of highest speed
	   biased: freq target for at least one CPU is below 50% threshold
	   skewed: freq targets for at least 2 CPUs are below 25% threshold */
	if (((tcc_count_slow_cpus(skewed_speed) >= 2) ||
	     tcc_cpu_edp_favor_down(nr_cpus, mp_overhead) ||
	     (highest_speed <= idle_bottom_freq) || (nr_cpus > max_cpus)) &&
	    (nr_cpus > min_cpus))
		return TCC_CPU_SPEED_SKEWED;

	if (((tcc_count_slow_cpus(balanced_speed) >= 1) ||
	     (!tcc_cpu_edp_favor_up(nr_cpus, mp_overhead)) ||
	     (highest_speed <= idle_bottom_freq) || (nr_cpus == max_cpus)) &&
	    (nr_cpus >= min_cpus))
		return TCC_CPU_SPEED_BIASED;

	return TCC_CPU_SPEED_BALANCED;
}
void disable_auto_hotplug(void)
{
	hotplug_state=AUTO_HOTPLUG_DISABLED;
	cancel_delayed_work(&hotplug_work);
}
static void hotplug_work_func(struct work_struct *work)
{
	bool up = false;
	unsigned int cpu = nr_cpu_ids;
	unsigned long now = jiffies;
	static unsigned long last_change_time;

	mutex_lock(tcc_cpu_lock);

	switch (hotplug_state) {
	case AUTO_HOTPLUG_DISABLED:
	case AUTO_HOTPLUG_IDLE:
		break;
	case AUTO_HOTPLUG_DOWN:
		cpu = tcc_get_slowest_cpu_n();
		if (cpu < nr_cpu_ids) {
			up = false;
		} else if (!is_lp_cluster() && !no_lp &&
			   !pm_qos_request(PM_QOS_MIN_ONLINE_CPUS)) {
#if (0)
			if(!clk_set_parent(cpu_clk, cpu_lp_clk)) {
				hotplug_stats_update(CONFIG_NR_CPUS, true);
				hotplug_stats_update(0, false);
				/* catch-up with governor target speed */
				tcc_cpu_set_speed_cap(NULL);
				break;
			}
#endif
		}
		queue_delayed_work_on(0, hotplug_wq, &hotplug_work, down_delay);
		break;
	case AUTO_HOTPLUG_UP:
		if (is_lp_cluster() && !no_lp) {
#if (0)
			if(!clk_set_parent(cpu_clk, cpu_g_clk)) {
				hotplug_stats_update(CONFIG_NR_CPUS, false);
				hotplug_stats_update(0, true);
				/* catch-up with governor target speed */
				tcc_cpu_set_speed_cap(NULL);
			}
#endif
		} else {
			switch (tcc_cpu_speed_balance()) {
			/* cpu speed is up and balanced - one more on-line */
			case TCC_CPU_SPEED_BALANCED:
				cpu = cpumask_next_zero(0, cpu_online_mask);
				if (cpu < nr_cpu_ids)
					up = true;
				break;
			/* cpu speed is up, but skewed - remove one core */
			case TCC_CPU_SPEED_SKEWED:
				cpu = tcc_get_slowest_cpu_n();
				if (cpu < nr_cpu_ids)
					up = false;
				break;
			/* cpu speed is up, but under-utilized - do nothing */
			case TCC_CPU_SPEED_BIASED:
			default:
				break;
			}
		}
		queue_delayed_work_on(0, hotplug_wq, &hotplug_work, up2gn_delay);
		break;
	default:
		pr_err("%s: invalid hotplug state %d\n",
		       __func__, hotplug_state);
	}

	if (!up && ((now - last_change_time) < down_delay))
			cpu = nr_cpu_ids;

	if (cpu < nr_cpu_ids) {
		last_change_time = now;
		hotplug_stats_update(cpu, up);
	}
	mutex_unlock(tcc_cpu_lock);

	if (cpu < nr_cpu_ids) {
		if (up){
			cpu_up(cpu);
		}else{
			cpu_down(cpu);
		}
	}
}

static int min_cpus_notify(struct notifier_block *nb, unsigned long n, void *p)
{
	mutex_lock(tcc_cpu_lock);

	if ((n >= 1) && is_lp_cluster()) {
		/* make sure cpu rate is within g-mode range before switching */
#if (1)
		tcc_update_cpu_speed(tcc_getspeed(0));
#else
		unsigned int speed = max(
			tcc_getspeed(0), clk_get_min_rate(cpu_g_clk) / 1000);
		tcc_update_cpu_speed(speed);

		if (!clk_set_parent(cpu_clk, cpu_g_clk)) {
			hotplug_stats_update(CONFIG_NR_CPUS, false);
			hotplug_stats_update(0, true);
		}
#endif
	}
	/* update governor state machine */
	tcc_cpu_set_speed_cap(NULL);
	mutex_unlock(tcc_cpu_lock);
	return NOTIFY_OK;
}

static struct notifier_block min_cpus_notifier = {
	.notifier_call = min_cpus_notify,
};

void auto_hotplug_governor(unsigned int cpu_freq, bool suspend)
{
	unsigned long up_delay, top_freq, bottom_freq;

	if (!is_g_cluster_present())
		return;

	if (hotplug_state == AUTO_HOTPLUG_DISABLED)
		return;

	if (suspend) {
		hotplug_state = AUTO_HOTPLUG_IDLE;

		/* Switch to G-mode if suspend rate is high enough */
		if (is_lp_cluster() && (cpu_freq >= idle_bottom_freq)) {
#if (0)
			if (!clk_set_parent(cpu_clk, cpu_g_clk)) {
				hotplug_stats_update(CONFIG_NR_CPUS, false);
				hotplug_stats_update(0, true);
			}
#endif
		}
		return;
	}

	if (is_lp_cluster()) {
		up_delay = up2g0_delay;
		top_freq = idle_top_freq;
		bottom_freq = 0;
	} else {
		up_delay = up2gn_delay;
		top_freq = idle_bottom_freq;
		bottom_freq = idle_bottom_freq;
	}

	if (pm_qos_request(PM_QOS_MIN_ONLINE_CPUS) >= 2) {
		if (hotplug_state != AUTO_HOTPLUG_UP) {
			hotplug_state = AUTO_HOTPLUG_UP;
			queue_delayed_work_on(0, hotplug_wq, &hotplug_work, up_delay);
		}
		return;
	}

	switch (hotplug_state) {
	case AUTO_HOTPLUG_IDLE:
		if (cpu_freq > top_freq) {
			hotplug_state = AUTO_HOTPLUG_UP;
			queue_delayed_work_on(0, hotplug_wq, &hotplug_work, up_delay);
		} else if (cpu_freq <= bottom_freq) {
			hotplug_state = AUTO_HOTPLUG_DOWN;
			queue_delayed_work_on(0, hotplug_wq, &hotplug_work, down_delay);
		}
		break;
	case AUTO_HOTPLUG_DOWN:
		if (cpu_freq > top_freq) {
			hotplug_state = AUTO_HOTPLUG_UP;
			queue_delayed_work_on(0, hotplug_wq, &hotplug_work, up_delay);
		} else if (cpu_freq > bottom_freq) {
			hotplug_state = AUTO_HOTPLUG_IDLE;
		}
		break;
	case AUTO_HOTPLUG_UP:
		if (cpu_freq <= bottom_freq) {
			hotplug_state = AUTO_HOTPLUG_DOWN;
			queue_delayed_work_on(0, hotplug_wq, &hotplug_work, down_delay);
		} else if (cpu_freq <= top_freq) {
			hotplug_state = AUTO_HOTPLUG_IDLE;
		}
		break;
	default:
		pr_err("%s: invalid hotplug state %d\n",
		       __func__, hotplug_state);
		BUG();
	}
}

int auto_hotplug_init(struct mutex *cpu_lock)
{
	/*
	 * Not bound to the issuer CPU (=> high-priority), has rescue worker
	 * task, single-threaded, freezable.
	 */
	hotplug_wq = alloc_workqueue(
		"auto-hotplug", WQ_UNBOUND | WQ_RESCUER | WQ_FREEZABLE, 1);
	if (!hotplug_wq)
		return -ENOMEM;
	INIT_DELAYED_WORK(&hotplug_work, hotplug_work_func);
#if (0)
	cpu_clk = clk_get_sys(NULL, "cpu");
	cpu_g_clk = clk_get_sys(NULL, "cpu_g");
	cpu_lp_clk = clk_get_sys(NULL, "cpu_lp");
	if (IS_ERR(cpu_clk) || IS_ERR(cpu_g_clk) || IS_ERR(cpu_lp_clk))
		return -ENOENT;

	idle_top_freq = clk_get_max_rate(cpu_lp_clk) / 1000;
	idle_bottom_freq = clk_get_min_rate(cpu_g_clk) / 1000;
#else
	idle_top_freq = IDLE_TOP_FREQ;
	idle_bottom_freq = IDLE_BOTTOM_FREQ;
#endif
	up2g0_delay = msecs_to_jiffies(UP2G0_DELAY_MS);
	up2gn_delay = msecs_to_jiffies(UP2Gn_DELAY_MS);
	down_delay = msecs_to_jiffies(DOWN_DELAY_MS);

	tcc_cpu_lock = cpu_lock;
	hotplug_state = INITIAL_STATE;
	hotplug_init_stats();
	pr_info("Tegra auto-hotplug initialized: %s\n",
		(hotplug_state == AUTO_HOTPLUG_DISABLED) ? "disabled" : "enabled");

	if (pm_qos_add_notifier(PM_QOS_MIN_ONLINE_CPUS, &min_cpus_notifier))
		pr_err("%s: Failed to register min cpus PM QoS notifier\n",
			__func__);

	return 0;
}

void auto_hotplug_exit(void)
{
	destroy_workqueue(hotplug_wq);
}
