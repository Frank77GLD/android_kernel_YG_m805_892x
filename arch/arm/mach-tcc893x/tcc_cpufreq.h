
unsigned int tcc_getspeed(unsigned int cpu);
int tcc_update_cpu_speed(unsigned long rate);
int tcc_cpu_set_speed_cap(unsigned int *speed_cap);
unsigned int tcc_count_slow_cpus(unsigned long speed_limit);
unsigned int tcc_get_slowest_cpu_n(void);
unsigned long tcc_cpu_lowest_speed(void);
unsigned long tcc_cpu_highest_speed(void);

#if defined(CONFIG_AUTO_HOTPLUG)
int auto_hotplug_init(struct mutex *cpu_lock);
void auto_hotplug_exit(void);
void auto_hotplug_governor(unsigned int cpu_freq, bool suspend);
#endif
