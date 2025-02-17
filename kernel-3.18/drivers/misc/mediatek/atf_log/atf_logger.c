
#include <linux/module.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>       /* min() */
#include <linux/uaccess.h>      /* copy_to_user() */
#include <linux/sched.h>        /* TASK_INTERRUPTIBLE/signal_pending/schedule */
#include <linux/syscore_ops.h>
#include <linux/poll.h>
#include <linux/io.h>           /* ioremap() */
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/seq_file.h>
#include <asm/setup.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/atomic.h>
#include <linux/irq.h>
#ifdef MTK_SIP_KERNEL_TIME_SYNC
#include <mach/mt_secure_api.h>
#endif

/*#define ATF_LOGGER_DEBUG*/
#define ATF_LOG_CTRL_BUF_SIZE 256
#define ATF_CRASH_MAGIC_NO	0xdead1abf
#define ATF_LAST_MAGIC_NO	0x41544641
#define atf_log_lock()        spin_lock(&atf_logger_lock)
#define atf_log_unlock()      spin_unlock(&atf_logger_lock)

static DEFINE_SPINLOCK(atf_logger_lock);
#if 0
static unsigned char *atf_crash_log_buf;
#endif
static wait_queue_head_t    atf_log_wq;

#ifdef __aarch64__
static void *_memcpy(void *dest, const void *src, size_t count)
{
	char *tmp = dest;
	const char *s = src;

	while (count--)
		*tmp++ = *s++;
	return dest;
}

#define memcpy _memcpy
#endif

union atf_log_ctl_t {
	struct {
		unsigned int atf_buf_addr;          /*  0x00 */
		unsigned int atf_buf_size;
		unsigned int atf_write_pos;
		unsigned int atf_read_pos;
		/* atf_spinlock_t atf_buf_lock; */
		unsigned int atf_buf_lock;          /*  0x10 */
		unsigned int atf_buf_unread_size;
		unsigned int atf_irq_count;
		unsigned int atf_reader_alive;
		unsigned long long atf_write_seq;   /*  0x20 */
		unsigned long long atf_read_seq;/* useless both in ATF and atf_logger */
		unsigned int atf_aee_dbg_buf_addr;  /*  0x30 */
		unsigned int atf_aee_dbg_buf_size;
		unsigned int atf_crash_log_addr;
		unsigned int atf_crash_log_size;
		unsigned int atf_crash_flag;        /*  0x40 */
		unsigned int padding;  /* padding for next 8 bytes alignment variable */
		unsigned long long atf_except_write_pos_per_cpu[10]; /* 0x48 */
	} info;
	unsigned char data[ATF_LOG_CTRL_BUF_SIZE];
};

struct ipanic_atf_log_rec {
	size_t total_size;
	size_t has_read;
	unsigned long start_idx;
};

union atf_log_ctl_t *atf_buf_vir_ctl;
unsigned long atf_buf_phy_ctl;
unsigned int atf_buf_len;
unsigned char *atf_log_vir_addr;
unsigned int atf_log_len;

#ifdef CONFIG_ARCH_MT6797
phys_addr_t atf_dump_buff_add = 0x44610000;
unsigned int atf_dump_buff_size = 0x30000;
#endif

static unsigned int pos_to_index(unsigned int pos)
{
	return pos - (atf_buf_phy_ctl + ATF_LOG_CTRL_BUF_SIZE);
}

static unsigned int index_to_pos(unsigned int index)
{
	return (atf_buf_phy_ctl + ATF_LOG_CTRL_BUF_SIZE) + index;
}

static size_t atf_log_dump_nolock(unsigned char *buffer, struct ipanic_atf_log_rec *rec, size_t size)
{
	unsigned int len;
	unsigned int least;

	unsigned int local_write_index = 0;

	local_write_index = pos_to_index(atf_buf_vir_ctl->info.atf_write_pos);
	/* find the first letter to read */
	while ((local_write_index + atf_log_len - rec->start_idx) % atf_log_len > 0) {
		if (*(atf_log_vir_addr + rec->start_idx) != 0)
			break;
		rec->start_idx++;
		if (rec->start_idx == atf_log_len)
			rec->start_idx = 0;
	}
	least = (local_write_index + atf_buf_len - rec->start_idx) % atf_buf_len;
	if (size > least)
		size = least;
	len = min(size, (size_t)(atf_log_len - rec->start_idx));
	if (size == len) {
		memcpy(buffer, atf_log_vir_addr + rec->start_idx, size);
	} else {
		size_t right = atf_log_len - rec->start_idx;

		memcpy(buffer, atf_log_vir_addr + rec->start_idx, right);
		memcpy(buffer, atf_log_vir_addr, size - right);
	}
	rec->start_idx += size;
	rec->start_idx %= atf_log_len;
	return size;
}
/* static size_t atf_log_dump(unsigned char *buffer, unsigned int start, size_t size) */
static size_t atf_log_dump(unsigned char *buffer, struct ipanic_atf_log_rec *rec, size_t size)
{
	size_t ret;

	atf_log_lock();
	/* ret = atf_log_dump_nolock(buffer, start, size); */
	ret = atf_log_dump_nolock(buffer, rec, size);
	atf_log_unlock();
	/* show_data(atf_log_vir_addr, 24*1024, "atf_buf"); */
	return ret;
}

size_t ipanic_atflog_buffer(void *data, unsigned char *buffer, size_t sz_buffer)
{
	static bool last_read;
	size_t count;
	struct ipanic_atf_log_rec *rec = (struct ipanic_atf_log_rec *)data;
	unsigned int local_write_index = 0;

	if (atf_buf_len == 0)
		return 0;
	/* pr_notice("ipanic_atf_log: need %d, rec:%d, %d, %lu\n", */
	/* sz_buffer, rec->total_size, rec->has_read, rec->start_idx); */
	if (rec->total_size == rec->has_read || last_read) {
		last_read = false;
		return 0;
	}
	if (rec->has_read == 0) {
		if (atf_buf_vir_ctl->info.atf_write_seq < atf_log_len
				&& atf_buf_vir_ctl->info.atf_write_seq < sz_buffer)
			rec->start_idx = 0;
		else {
			/* atf_log_lock(); */
			local_write_index = pos_to_index(atf_buf_vir_ctl->info.atf_write_pos);
			/* atf_log_unlock(); */
			rec->start_idx = (local_write_index + atf_log_len - rec->total_size) % atf_log_len;
		}
	}
	/* count = atf_log_dump_nolock(buffer, (rec->start_idx + rec->has_read) % atf_log_len, sz_buffer); */
	/* count = atf_log_dump_nolock(buffer, rec, sz_buffer); */
	count = atf_log_dump(buffer, rec, sz_buffer);
	/* pr_notice("ipanic_atf_log: dump %d\n", count); */
	rec->has_read += count;
	if (count != sz_buffer)
		last_read = true;
	return count;
}

static ssize_t atf_log_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	wake_up_interruptible(&atf_log_wq);
	return 1;
}

static ssize_t do_read_log_to_usr(char __user *buf, size_t count)
{
	size_t copy_len = 0;
	size_t right = 0;

	unsigned int local_write_index = 0;
	unsigned int local_read_index = 0;

	local_write_index = pos_to_index(atf_buf_vir_ctl->info.atf_write_pos);
	local_read_index = pos_to_index(atf_buf_vir_ctl->info.atf_read_pos);

	/* check copy length */
	copy_len = (local_write_index + atf_log_len - local_read_index) % atf_log_len;

	/* if copy length < count, just copy the "copy length" */
	if (count > copy_len)
		count = copy_len;

	if (local_write_index > local_read_index) {
		/* write (right) - read (left) */
		/* --------R-------W-----------*/
		if (copy_to_user(buf, atf_log_vir_addr + local_read_index, count))
			return -EFAULT;
	} else {
		/* turn around to the head */
		/* --------W-------R-----------*/
		right = atf_log_len - local_read_index;

		/* check buf space is enough to copy */
		if (count > right) {
			/* if space is enough to copy */
			if (copy_to_user(buf, atf_log_vir_addr + local_read_index, right))
				return -EFAULT;
			if (copy_to_user((buf + right), atf_log_vir_addr, count - right))
				return -EFAULT;
		} else {
			/* if count is only enough to copy right or count, just copy right or count */
			if (copy_to_user(buf, atf_log_vir_addr + local_read_index, count))
				return -EFAULT;
		}
	}

	/* update the read pos */
	local_read_index = (local_read_index + count) % atf_log_len;
	atf_buf_vir_ctl->info.atf_read_pos = index_to_pos(local_read_index);

	return count;
}

static int atf_log_fix_reader(void)
{
	if (atf_buf_vir_ctl->info.atf_write_seq < atf_log_len) {
		atf_buf_vir_ctl->info.atf_read_pos = index_to_pos(0);
	} else {
		atf_buf_vir_ctl->info.atf_read_pos = atf_buf_vir_ctl->info.atf_write_pos;
	}
	return 0;
}

static int atf_log_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = nonseekable_open(inode, file);
	if (unlikely(ret))
		return ret;
	file->private_data = NULL;

	atf_log_lock();
	/* if reader open the file firstly, reset the read position */
	if (!atf_buf_vir_ctl->info.atf_reader_alive)
		atf_log_fix_reader();

	atf_buf_vir_ctl->info.atf_reader_alive++;
	atf_log_unlock();

	return 0;
}

static int atf_log_release(struct inode *ignored, struct file *file)
{
	atf_buf_vir_ctl->info.atf_reader_alive--;
	return 0;
}

static ssize_t atf_log_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos)
{
	ssize_t ret;
	unsigned int write_pos;
	unsigned int read_pos;
	DEFINE_WAIT(wait);

start:
	while (1) {

		/* pr_notice("atf_log_read: wait in wq\n"); */
		prepare_to_wait(&atf_log_wq, &wait, TASK_INTERRUPTIBLE);
		write_pos = atf_buf_vir_ctl->info.atf_write_pos;
		atf_log_lock();
		read_pos = atf_buf_vir_ctl->info.atf_read_pos;
		atf_log_unlock();

		ret = (write_pos == read_pos);

		if (!ret)
			break;
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
		schedule();
	}
	finish_wait(&atf_log_wq, &wait);
	/* pr_notice("atf_log_read: finish wait\n"); */
	if (ret)
		return ret;

	if (unlikely(write_pos == read_pos)) {
		goto start;
	}

	atf_log_lock();
	ret = do_read_log_to_usr(buf, count);
	atf_log_unlock();

	/* update the file pos */
	*f_pos += ret;
	return ret;
}

static unsigned int atf_log_poll(struct file *file, poll_table *wait)
{
	unsigned int ret = POLLOUT | POLLWRNORM;

	if (!(file->f_mode & FMODE_READ))
		return ret;
	poll_wait(file, &atf_log_wq, wait);
	atf_log_lock();
	if (atf_buf_vir_ctl->info.atf_write_pos != atf_buf_vir_ctl->info.atf_read_pos)
		ret |= POLLIN | POLLRDNORM;
	atf_log_unlock();
	return ret;
}

long atf_log_ioctl(struct file *flip, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static const struct file_operations atf_log_fops = {
	.owner      = THIS_MODULE,
	.unlocked_ioctl = atf_log_ioctl,
	.compat_ioctl = atf_log_ioctl,
	.poll       = atf_log_poll,
	.read       = atf_log_read,
	.open       = atf_log_open,
	.release    = atf_log_release,
	.write      = atf_log_write,
};

static struct miscdevice atf_log_dev = {
	.minor      = MISC_DYNAMIC_MINOR,
	.name       = "atf_log",
	.fops       = &atf_log_fops,
	.mode       = 0644,
};

static int __init atf_log_dt_scan_memory(unsigned long node, const char *uname, int depth, void *data)
{
	char *type = (char *)of_get_flat_dt_prop(node, "device_type", NULL);
	__be32 *reg, *endp;
	int l;

	/* We are scanning "memory" nodes only */
	if (type == NULL) {
		/*
		 * The longtrail doesn't have a device_type on the
		 * /memory node, so look for the node called /memory@0.
		 */
		if (depth != 1 || strcmp(uname, "memory@0") != 0)
			return 0;
	} else if (strcmp(type, "memory") != 0)
		return 0;

	reg = (__be32 *)of_get_flat_dt_prop(node, "reg", (int *)&l);
	if (reg == NULL)
		return 0;

	endp = reg + (l / sizeof(__be32));

	while ((endp - reg) >= (dt_root_addr_cells + dt_root_size_cells)) {
		u64 base, size;

		base = dt_mem_next_cell(dt_root_addr_cells, (const __be32 **)&reg);
		size = dt_mem_next_cell(dt_root_size_cells, (const __be32 **)&reg);

		if (size == 0)
			continue;
		/* pr_notice( */
		/* "[PHY layout]DRAM size (dt) :  0x%llx - 0x%llx  (0x%llx)\n", */
		/* (unsigned long long)base, */
		/* (unsigned long long)base + (unsigned long long)size - 1, */
		/* (unsigned long long)size); */
	}
	*(unsigned long *)data = node;
	return node;
}

unsigned long long __init atf_log_get_from_dt(unsigned long *phy_addr, unsigned int *len)
{
	unsigned long node = 0;
	struct mem_desc *mem_desc = NULL;

	if (of_scan_flat_dt(atf_log_dt_scan_memory, &node)) {
		mem_desc = (struct mem_desc *)of_get_flat_dt_prop(node, "tee_reserved_mem", NULL);
		if (mem_desc && mem_desc->size) {
			pr_notice("ATF reserved memory: 0x%08llx - 0x%08llx (0x%llx)\n",
					mem_desc->start, mem_desc->start+mem_desc->size - 1,
					mem_desc->size);
		}
	}
	if (mem_desc) {
		*phy_addr = mem_desc->start;
		*len = mem_desc->size;
	}
	return 0;
}

void show_atf_log_ctl(void)
{
	pr_notice("atf_buf_addr(%p) = 0x%x\n", &(atf_buf_vir_ctl->info.atf_buf_addr),
			atf_buf_vir_ctl->info.atf_buf_addr);
	pr_notice("atf_buf_size(%p) = 0x%x\n", &(atf_buf_vir_ctl->info.atf_buf_size),
			atf_buf_vir_ctl->info.atf_buf_size);
	pr_notice("atf_write_pos(%p) = %u\n", &(atf_buf_vir_ctl->info.atf_write_pos),
			atf_buf_vir_ctl->info.atf_write_pos);
	pr_notice("atf_read_pos(%p) = %u\n", &(atf_buf_vir_ctl->info.atf_read_pos),
			atf_buf_vir_ctl->info.atf_read_pos);
	pr_notice("atf_buf_lock(%p) = %u\n", &(atf_buf_vir_ctl->info.atf_buf_lock),
			atf_buf_vir_ctl->info.atf_buf_lock);
	pr_notice("atf_buf_unread_size(%p) = %u\n", &(atf_buf_vir_ctl->info.atf_buf_unread_size),
			atf_buf_vir_ctl->info.atf_buf_unread_size);
	pr_notice("atf_irq_count(%p) = %u\n", &(atf_buf_vir_ctl->info.atf_irq_count),
			atf_buf_vir_ctl->info.atf_irq_count);
	pr_notice("atf_reader_alive(%p) = %u\n", &(atf_buf_vir_ctl->info.atf_reader_alive),
			atf_buf_vir_ctl->info.atf_reader_alive);
	pr_notice("atf_write_seq(%p) = %llu\n", &(atf_buf_vir_ctl->info.atf_write_seq),
			atf_buf_vir_ctl->info.atf_write_seq);
}

#ifdef ATF_LOGGER_DEBUG
static void show_data(unsigned long addr, int nbytes, const char *name)
{
	int	i, j;
	int	nlines;
	u32	*p;

	/*
	 * don't attempt to dump non-kernel addresses or
	 * values that are probably just small negative numbers
	 */
	if (addr < PAGE_OFFSET || addr > -256UL)
		return;

	pr_debug("\n%s: %#lx:\n", name, addr);

	/*
	 * round address down to a 32 bit boundary
	 * and always dump a multiple of 32 bytes
	 */
	p = (u32 *)(addr & ~(sizeof(u32) - 1));
	nbytes += (addr & (sizeof(u32) - 1));
	nlines = (nbytes + 31) / 32;


	for (i = 0; i < nlines; i++) {
		/*
		 * just display low 16 bits of address to keep
		 * each line of the dump < 80 characters
		 */
		pr_debug("%04lx ", (unsigned long)p & 0xffff);
		for (j = 0; j < 8; j++) {
			u32	data;

			if (probe_kernel_address(p, data))
				pr_debug(" ********");
			else
				pr_debug(" %08x", data);
			++p;
		}
		pr_debug("\n");
	}
}
#endif

static irqreturn_t ATF_log_irq_handler(int irq, void *dev_id)
{
	if (!atf_buf_vir_ctl->info.atf_reader_alive)
		pr_err("No alive reader, but still receive irq\n");
	wake_up_interruptible(&atf_log_wq);
	return IRQ_HANDLED;
}

static const struct file_operations proc_atf_log_file_operations = {
	.owner  = THIS_MODULE,
	.open   = atf_log_open,
	.read   = atf_log_read,
	.unlocked_ioctl = atf_log_ioctl,
	.compat_ioctl = atf_log_ioctl,
	.release = atf_log_release,
	.poll   = atf_log_poll,
};

#if 0
static int atf_crash_show(struct seq_file *m, void *v)
{
	seq_write(m, atf_crash_log_buf, atf_buf_vir_ctl->info.atf_crash_log_size);
	return 0;
}

static int atf_crash_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, atf_crash_show, inode->i_private);
}
static const struct file_operations proc_atf_crash_file_operations = {
	.owner = THIS_MODULE,
	.open = atf_crash_file_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif
#ifdef MTK_SIP_KERNEL_TIME_SYNC
static void atf_time_sync_resume(void)
{
	/* Get local_clock and sync to ATF */
	u64 time_to_sync = local_clock();

#ifdef CONFIG_ARM64
	mt_secure_call(MTK_SIP_KERNEL_TIME_SYNC, time_to_sync, 0, 0);
#else
	mt_secure_call(MTK_SIP_KERNEL_TIME_SYNC, (u32)time_to_sync, (u32)(time_to_sync >> 32), 0);
#endif
	pr_notice("atf_time_sync: resume sync");
}

static struct syscore_ops atf_time_sync_syscore_ops = {
	.resume = atf_time_sync_resume,
};
#endif

#ifdef CONFIG_ARCH_MT6797

static int atf_dump_show(struct seq_file *m, void *v)
{
	void *buff;

	buff = ioremap_wc(atf_dump_buff_add, atf_dump_buff_size);
	seq_write(m, buff, atf_dump_buff_size);
	return 0;
}

static int atf_dump_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, atf_dump_show, inode->i_private);
}

static const struct file_operations proc_atf_dump_log_file_operations = {
	.owner = THIS_MODULE,
	.open = atf_dump_file_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif
static int __init atf_log_init(void)
{
	/* register module driver */
	int err;
	struct proc_dir_entry *atf_log_proc_dir;
	struct proc_dir_entry *atf_log_proc_file;
	struct device_node *node = NULL;
	int irq_num;
#ifdef CONFIG_ARCH_MT6797
	struct proc_dir_entry *atf_log_dump_proc_file;
#endif
#if 0
	struct proc_dir_entry *atf_crash_proc_file;
	struct proc_dir_entry *atf_last_proc_file;
#endif
#ifdef MTK_SIP_KERNEL_TIME_SYNC
	u64 time_to_sync;
#endif

	err = misc_register(&atf_log_dev);
	if (unlikely(err)) {
		pr_err("atf_log: failed to register device");
		return -1;
	}
	pr_notice("atf_log: inited");
	/* get atf reserved memory(atf_buf_phy_ctl) from device tree */
	/* pass from preloader to LK, then create the dt node in LK */
	atf_log_get_from_dt(&atf_buf_phy_ctl, &atf_buf_len);    /* TODO */
	if (atf_buf_len == 0) {
		pr_err("No atf_log_buffer!\n");
		return -1;
	}
	/* map control header */
	atf_buf_vir_ctl = ioremap_wc(atf_buf_phy_ctl, ATF_LOG_CTRL_BUF_SIZE);
	atf_log_len = atf_buf_vir_ctl->info.atf_buf_size;
	/* map log buffer */
	atf_log_vir_addr = ioremap_wc(atf_buf_phy_ctl + ATF_LOG_CTRL_BUF_SIZE, atf_log_len);
	pr_notice("atf_buf_phy_ctl: 0x%lu\n", atf_buf_phy_ctl);
	pr_notice("atf_buf_len: %u\n", atf_buf_len);
	pr_notice("atf_buf_vir_ctl: %p\n", atf_buf_vir_ctl);
	pr_notice("atf_log_vir_addr: %p\n", atf_log_vir_addr);
	pr_notice("atf_log_len: %u\n", atf_log_len);
	/* show_atf_log_ctl(); */
	/* show_data(atf_buf_vir_ctl, 512, "atf_buf"); */
	atf_buf_vir_ctl->info.atf_reader_alive = 0;
	/* initial wait queue */
	init_waitqueue_head(&atf_log_wq);

	node = of_find_compatible_node(NULL, NULL, "mediatek,atf_logger");
	if (!node) {
		pr_err("[SCP] Can't find node:mediatek,atf_logger.n");
		return -1;
	}

	irq_num = irq_of_parse_and_map(node, 0);
	pr_notice("atf irq num %d.\n", irq_num);

	if (request_irq(irq_num, (irq_handler_t)ATF_log_irq_handler, IRQF_TRIGGER_NONE, "ATF_irq", NULL) != 0) {
		pr_crit("Fail to request ATF_log_irq interrupt!\n");
		return -1;
	}

	/* create /proc/atf_log */
	atf_log_proc_dir = proc_mkdir("atf_log", NULL);
	if (atf_log_proc_dir == NULL) {
		pr_err("atf_log proc_mkdir failed\n");
		return -ENOMEM;
	}
	/* create /proc/atf_log/atf_log */
	atf_log_proc_file = proc_create("atf_log", 0444, atf_log_proc_dir, &proc_atf_log_file_operations);
	if (atf_log_proc_file == NULL) {
		pr_err("atf_log proc_create failed at atf_log\n");
		return -ENOMEM;
	}
#ifdef CONFIG_ARCH_MT6797
/* create /proc/atf_log/atf_dump_log */
	atf_log_dump_proc_file = proc_create("atf_dump_log", 0444, atf_log_proc_dir,
	&proc_atf_dump_log_file_operations);
	if (atf_log_dump_proc_file == NULL) {
		pr_err("atf_log proc_create failed at atf_dump_log\n");
		return -ENOMEM;
	}
#endif
#if 0
	if (atf_buf_vir_ctl->info.atf_crash_flag == ATF_CRASH_MAGIC_NO) {
		atf_crash_proc_file = proc_create("atf_crash", 0444, atf_log_proc_dir, &proc_atf_crash_file_operations);
		if (atf_crash_proc_file == NULL) {
			pr_err("atf_log proc_create failed at atf_crash\n");
			return -ENOMEM;
		}
		atf_buf_vir_ctl->info.atf_crash_flag = ATF_LAST_MAGIC_NO;
		atf_crash_log_buf = ioremap_wc(atf_buf_vir_ctl->info.atf_crash_log_addr,
				atf_buf_vir_ctl->info.atf_crash_log_size);
	} else if (atf_buf_vir_ctl->info.atf_crash_flag == ATF_LAST_MAGIC_NO) {
		atf_last_proc_file = proc_create("atf_last", 0444, atf_log_proc_dir, &proc_atf_crash_file_operations);
		if (atf_last_proc_file == NULL) {
			pr_err("atf_log proc_create failed at atf_last\n");
			return -ENOMEM;
		}
		atf_crash_log_buf = ioremap_wc(atf_buf_vir_ctl->info.atf_crash_log_addr,
				atf_buf_vir_ctl->info.atf_crash_log_size);
	}
#endif
#ifdef MTK_SIP_KERNEL_TIME_SYNC
	register_syscore_ops(&atf_time_sync_syscore_ops);

	/* Get local_clock and sync to ATF */
	time_to_sync = local_clock();

#ifdef CONFIG_ARM64
	mt_secure_call(MTK_SIP_KERNEL_TIME_SYNC, time_to_sync, 0, 0);
#else
	mt_secure_call(MTK_SIP_KERNEL_TIME_SYNC, (u32)time_to_sync, (u32)(time_to_sync >> 32), 0);
#endif
	pr_notice("atf_time_sync: inited");
#endif

	return 0;
}

static void __exit atf_log_exit(void)
{
	/* deregister module driver */
	int err;

	err = misc_deregister(&atf_log_dev);
	if (unlikely(err))
		pr_err("atf_log: failed to unregister device");
	pr_notice("atf_log: exited");
}

module_init(atf_log_init);
module_exit(atf_log_exit);

MODULE_DESCRIPTION("MEDIATEK Module ATF Logging Driver");
MODULE_AUTHOR("Chun Fan<chun.fan@mediatek.com>");

