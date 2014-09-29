#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static int n_show(struct seq_file *m, void *v)
{
#if defined(CONFIG_PN544_NFC)
	seq_puts(m, "Support\t: true\n");
#else
       seq_puts(m, "Support\t: false\n");
#endif
	return 0;
}


static void *n_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *n_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void n_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations nfcinfo_op = {
	.start	= n_start,
	.next	= n_next,
	.stop	= n_stop,
	.show	= n_show
};


static int nfcinfo_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &nfcinfo_op);
}

static const struct file_operations proc_nfcinfo_operations = {
	.open		= nfcinfo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init proc_nfcinfo_init(void)
{
	proc_create("nfcinfo", 0, NULL, &proc_nfcinfo_operations);
	return 0;
}
module_init(proc_nfcinfo_init);

