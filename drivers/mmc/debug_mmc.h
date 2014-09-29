#define MMC_DEBUG 0
#if MMC_DEBUG
#define MMC_DBG(fmt,args...) \
	do { printk(KERN_DEBUG "[mmc_debug]:%s:%d "fmt"\n", __func__, __LINE__, ##args); } \
	while (0)
#else
#define MMC_DBG(x...) do {} while (0)
#endif


#if MMC_DEBUG
#define MMC_printk(fmt,args...) \
	do { printk(KERN_INFO "[mmc]:%s:%d "fmt"\n", __func__, __LINE__, ##args); } \
	while (0)
#else
#define MMC_printk(x...) do {} while (0)
#endif
