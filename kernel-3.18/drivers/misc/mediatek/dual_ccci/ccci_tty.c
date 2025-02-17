

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/wakelock.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/dma-mapping.h>
#include <ccci.h>
#include <ccci_tty.h>

struct tty_instance_t {
	int m_md_id;
	int count;
	int ready;
	int need_reset;
	int reset_handle;
	int channel;
	int uart_tx;
	int uart_rx_ack;
	int idx;
	struct wake_lock wake_lock;
	char wakelock_name[16];
	wait_queue_head_t write_waitq;
	wait_queue_head_t read_waitq;
	wait_queue_head_t poll_waitq_r;
	wait_queue_head_t poll_waitq_w;
	spinlock_t poll_lock;
	struct shared_mem_tty_t *shared_mem;
	struct mutex ccci_tty_mutex;
	int has_pending_read;
	rwlock_t ccci_tty_rwlock;
	struct timer_list timer;
	struct tasklet_struct ccci_tty_tasklet;
};

struct tty_ctl_block_t {
	int m_md_id;
	struct tty_instance_t ccci_tty_modem;
	struct tty_instance_t ccci_tty_meta;
	struct tty_instance_t ccci_tty_ipc;
#ifdef CONFIG_MTK_ICUSB_SUPPORT
	struct tty_instance_t ccci_tty_icusb;
#endif
	struct shared_mem_tty_t *uart1_shared_mem;
	struct shared_mem_tty_t *uart2_shared_mem;
	struct shared_mem_tty_t *uart3_shared_mem;
#ifdef CONFIG_MTK_ICUSB_SUPPORT
	struct shared_mem_tty_t *uart4_shared_mem;
#endif
	int tty_buf_size;
	struct MD_CALL_BACK_QUEUE tty_notifier;
	char drv_name[32];
	char node_name[16];
	struct cdev ccci_tty_dev;
	int major;
	int minor;
};

static struct tty_ctl_block_t *tty_ctlb[MAX_MD_NUM];

unsigned int tty_debug_enable[MAX_MD_NUM] = { 0 };

/* 1UL<<0, tty_modem; 1UL<<1, tty_meta; 1UL<<2, tty_rpc */

static int ccci_tty_readable(struct tty_instance_t *tty_instance)
{
	int read, write, size;

	read = tty_instance->shared_mem->rx_control.read;
	write = tty_instance->shared_mem->rx_control.write;
	size = write - read;

	if (size < 0)
		size += tty_instance->shared_mem->rx_control.length;

	return size;
}

static int ccci_tty_writeable(struct tty_instance_t *tty_instance)
{
	int read, write, size, length;

	read = tty_instance->shared_mem->tx_control.read;
	write = tty_instance->shared_mem->tx_control.write;
	length = tty_instance->shared_mem->tx_control.length;

	if (read == write) {
		size = length - 1;
	} else if (read < write) {
		size = length - write;
		size += read;
	} else {
		size = read - write - 1;
	}

	if ((size <= 0) || (length <= 0)) {
		CCCI_MSG_INF(tty_instance->m_md_id, "tty",
			     "writeable: read=%d,write=%d,length=%d,size=%d\n",
			     read, write, length, size);
	}
	return size;
}

/*   will be called when modem sends us something. */
/*   we will then copy it to the tty's buffer. */
/*   this is essentially the "read" fops. */
static void ccci_tty_callback(void *private)
{
	struct logic_channel_info_t *ch_info = (struct logic_channel_info_t *) private;
	struct ccci_msg_t msg;
	struct tty_ctl_block_t *ctlb = (struct tty_ctl_block_t *) ch_info->m_owner;

	while (get_logic_ch_data(ch_info, &msg)) {
		switch (msg.channel) {
		case CCCI_UART1_TX_ACK:
			/*  this should be in an interrupt, */
			/*  so no locking required... */
			if (ccci_tty_writeable(&ctlb->ccci_tty_meta)) {
				wake_up_interruptible(&ctlb->
						      ccci_tty_meta.write_waitq);
				wake_up_interruptible_poll(&ctlb->
							   ccci_tty_meta.poll_waitq_w,
							   POLLOUT);
			}
			break;

		case CCCI_UART1_RX:
			if (ccci_tty_readable(&ctlb->ccci_tty_meta)) {
				wake_up_interruptible(&ctlb->
						      ccci_tty_meta.read_waitq);
				wake_up_interruptible_poll(&ctlb->
							   ccci_tty_meta.poll_waitq_r,
							   POLLIN);
				wake_lock_timeout(&ctlb->
						  ccci_tty_meta.wake_lock,
						  HZ / 2);
			}
			break;

		case CCCI_UART2_TX_ACK:
			/*  this should be in an interrupt, */
			/*  so no locking required... */
			if (ccci_tty_writeable(&ctlb->ccci_tty_modem)) {
				wake_up_interruptible(&ctlb->
						      ccci_tty_modem.write_waitq);
				wake_up_interruptible_poll
				    (&ctlb->ccci_tty_modem.poll_waitq_w,
				     POLLOUT);
			}
			break;

		case CCCI_UART2_RX:
			if (ccci_tty_readable(&ctlb->ccci_tty_modem)) {
				ctlb->ccci_tty_modem.has_pending_read = 1;
				wake_up_interruptible(&ctlb->
						      ccci_tty_modem.read_waitq);
				wake_up_interruptible_poll
				    (&ctlb->ccci_tty_modem.poll_waitq_r,
				     POLLIN);
				wake_lock_timeout(&ctlb->
						  ccci_tty_modem.wake_lock,
						  HZ / 2);
			}
			break;

		case CCCI_IPC_UART_TX_ACK:
			if (ccci_tty_writeable(&ctlb->ccci_tty_ipc)) {
				wake_up_interruptible(&ctlb->
						      ccci_tty_ipc.write_waitq);
				wake_up_interruptible_poll(&ctlb->
							   ccci_tty_ipc.poll_waitq_w,
							   POLLOUT);
			}
			break;

		case CCCI_IPC_UART_RX:
			if (ccci_tty_readable(&ctlb->ccci_tty_ipc)) {
				wake_up_interruptible(&ctlb->
						      ccci_tty_ipc.read_waitq);
				wake_up_interruptible_poll(&ctlb->
							   ccci_tty_ipc.poll_waitq_r,
							   POLLIN);
				wake_lock_timeout(&ctlb->ccci_tty_ipc.wake_lock,
						  HZ / 2);
			}
			break;
#ifdef CONFIG_MTK_ICUSB_SUPPORT
		case CCCI_ICUSB_TX_ACK:
			if (ccci_tty_writeable(&ctlb->ccci_tty_icusb)) {
				wake_up_interruptible(&ctlb->
						      ccci_tty_icusb.write_waitq);
				wake_up_interruptible_poll
				    (&ctlb->ccci_tty_icusb.poll_waitq_w,
				     POLLOUT);
			}
			break;

		case CCCI_ICUSB_RX:
			if (ccci_tty_readable(&ctlb->ccci_tty_icusb)) {
				wake_up_interruptible(&ctlb->
						      ccci_tty_icusb.read_waitq);
				wake_up_interruptible_poll
				    (&ctlb->ccci_tty_icusb.poll_waitq_r,
				     POLLIN);
				wake_lock_timeout(&ctlb->
						  ccci_tty_icusb.wake_lock,
						  HZ / 2);
			}
			break;
#endif

		default:
			break;
		}
	}
}

#define DUMP_CHANNEL_DATA
#ifdef DUMP_CHANNEL_DATA
static void ccci_ch_dump(int md_id, char *str, void *msg_buf, int len)
{
#define DUMP_BUF_SIZE 32
	unsigned char *char_ptr = (unsigned char *)msg_buf;
	char buf[DUMP_BUF_SIZE];
	int i, j;
	u64 ts_nsec;
	unsigned long rem_nsec;
	char *replace_str;

	for (i = 0, j = 0; i < len && i < DUMP_BUF_SIZE && j + 4 < DUMP_BUF_SIZE; i++) {
		if (((char_ptr[i] >= 32) && (char_ptr[i] <= 126))) {
			buf[j] = char_ptr[i];
			j += 1;
		} else if (char_ptr[i] == '\r' ||
			char_ptr[i] == '\n' ||
			char_ptr[i] == '\t') {
			switch (char_ptr[i]) {
			case '\r':
				replace_str = "\\r";
				break;
			case '\n':
				replace_str = "\\n";
				break;
			case '\t':
				replace_str = "\\t";
				break;
			default:
				replace_str = "";
				break;
			}
			snprintf(buf+j, DUMP_BUF_SIZE - j, "%s", replace_str);
			j += 2;
		} else {
			snprintf(buf+j, DUMP_BUF_SIZE - j, "[%02X]", char_ptr[i]);
			j += 4;
		}
	}

	ts_nsec = local_clock();
	rem_nsec = do_div(ts_nsec, 1000000000);

	buf[j] = '\0';
	ccci_dump_write(md_id, CCCI_DUMP_HISTORY, 0, "[%5lu.%06lu]%s %d>%s\n",
		(unsigned long)ts_nsec, rem_nsec / 1000, str, len, buf);
}
#endif

static ssize_t ccci_tty_read(struct file *file, char *buf, size_t count,
			     loff_t *ppos)
{
	struct tty_instance_t *tty_instance = (struct tty_instance_t *) file->private_data;

	int part = 0, size = 0, ret = 0;
	int value;
	unsigned read, write, length;
	struct ccci_msg_t msg;
	char *rx_buffer;
	int md_id = tty_instance->m_md_id;
	int data_be_read;
	char *u_buf = buf;
#ifdef DUMP_CHANNEL_DATA
	char dump_buf[DUMP_BUF_SIZE] = {0};
#endif

	read = tty_instance->shared_mem->rx_control.read;
	write = tty_instance->shared_mem->rx_control.write;
	rx_buffer = tty_instance->shared_mem->buffer;
	length = tty_instance->shared_mem->rx_control.length;

	do {
		size = ccci_tty_readable(tty_instance);

		if (size <= 0) {
			if (file->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				goto out;
			} else {
				/*  Block read */
				value =
				    wait_event_interruptible
				    (tty_instance->read_waitq,
				     ccci_tty_readable(tty_instance));
				if (value == -ERESTARTSYS) {
					CCCI_TTY_MSG(md_id,
						     "Interrupted syscall.signal_pend=0x%llx\n",
						     *(long long *)
						     current->pending.signal.
						     sig);
					ret = -EINTR;
					goto out;
				}
			}
		} else
			break;

	} while (size <= 0);

	data_be_read = (int)count;
	if (tty_debug_enable[md_id] & (1UL << tty_instance->idx))
		CCCI_DBG_MSG(md_id, "tty",
			     "[RX](Before) tty%d read_request=%d write=%04d read=%4d\n",
			     tty_instance->idx, data_be_read,
			     tty_instance->shared_mem->rx_control.write, read);

	if (data_be_read > size)
		data_be_read = size;
	/* copy_to_user may be scheduled,  */
	/* So add 0.5s wake lock to make sure ccci user can be running. */
	wake_lock_timeout(&tty_instance->wake_lock, HZ / 2);
	if ((read + data_be_read) >= length) {
		/*  Need read twice */

		/*  Copy first part */
		part = length - read;
		if (copy_to_user(u_buf, &rx_buffer[read], part)) {
			CCCI_MSG_INF(md_id, "tty",
				     "read: copy_to_user fail:u_buf=%08x,rx_buffer=0x%08x,read=%d,size=%d ret=%d line=%d\n",
				     (unsigned int)u_buf,
				     (unsigned int)rx_buffer, read, part, ret,
				     __LINE__);

			ret = -EFAULT;
			goto out;
		}
#ifdef DUMP_CHANNEL_DATA
		memcpy(dump_buf, &rx_buffer[read],
			(part > DUMP_BUF_SIZE - 1 ? DUMP_BUF_SIZE - 1 : part));
#endif
		/*  Copy second part */
		if (copy_to_user(&u_buf[part], rx_buffer, data_be_read - part)) {
			CCCI_MSG_INF(md_id, "tty",
				     "read: copy_to_user fail:u_buf=%08x,rx_buffer=0x%08x,read=%d,size=%d ret=%d line=%d\n",
				     (unsigned int)u_buf,
				     (unsigned int)rx_buffer, read,
				     data_be_read - part, ret, __LINE__);

			ret = -EFAULT;
			goto out;
		}
#ifdef DUMP_CHANNEL_DATA
		if (part < DUMP_BUF_SIZE)
			memcpy(dump_buf + part, rx_buffer,
				((data_be_read - part) > DUMP_BUF_SIZE - part - 1 ?
					DUMP_BUF_SIZE - part - 1 : data_be_read - part));
#endif
	} else {
		/*  Just need read once */
		if (copy_to_user(u_buf, &rx_buffer[read], data_be_read)) {
			CCCI_MSG_INF(md_id, "tty",
				"read: copy_to_user fail:u_buf=%08x,rx_buffer=0x%08x,read=%d,size=%d ret=%d line=%d\n",
				(unsigned int)u_buf,
				(unsigned int)rx_buffer, read,
				data_be_read, ret, __LINE__);
			ret = -EFAULT;
			goto out;
		}
#ifdef DUMP_CHANNEL_DATA
		memcpy(dump_buf, &rx_buffer[read],
			(data_be_read > DUMP_BUF_SIZE - 1 ? DUMP_BUF_SIZE - 1 : data_be_read));
#endif
	}

	/*  Update read pointer */
	read += data_be_read;
	if (read >= length)
		read -= length;
	tty_instance->shared_mem->rx_control.read = read;

	/*  Ack to MD */
	msg.magic = 0xFFFFFFFF;
	msg.id = tty_instance->channel;
	msg.channel = tty_instance->uart_rx_ack;
	msg.reserved = 0;
	/*wait memory updated*/
	mb();

	ret = ccci_message_send(md_id, &msg, 1);
	if (ret != sizeof(msg)) {
		CCCI_DBG_MSG(md_id, "tty",
			     "ccci_write_mailbox for %d fail: %d\n",
			     tty_instance->channel, ret);
		/* mod_timer(&tty_instance->timer,jiffies + msecs_to_jiffies(10)); */
	} else {
		/* del_timer(&tty_instance->timer): */
	}

	ret = data_be_read;
#ifdef DUMP_CHANNEL_DATA
	if (tty_instance->uart_rx_ack == CCCI_UART2_RX_ACK)
		ccci_ch_dump(md_id, "chr_read", dump_buf,
			(data_be_read > DUMP_BUF_SIZE - 1 ? DUMP_BUF_SIZE - 1 : data_be_read));
#endif
 out:
	if (tty_debug_enable[md_id] & (1UL << tty_instance->idx))
		CCCI_DBG_MSG(md_id, "tty",
			     "[RX](after) tty%d read_len=%d write=%04d read=%4d\n",
			     tty_instance->idx, ret,
			     tty_instance->shared_mem->rx_control.write,
			     tty_instance->shared_mem->rx_control.read);

	return ret;
}

static ssize_t ccci_tty_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct tty_instance_t *tty_instance = (struct tty_instance_t *) file->private_data;
	int ret = 0, part = 0;
	int data_be_write, size, value;
	unsigned read, write, length;
	int xmit_retry = 0;
	struct ccci_msg_t msg;
	char *tx_buffer;
	int md_id = tty_instance->m_md_id;
#ifdef DUMP_CHANNEL_DATA
	char dump_buf[DUMP_BUF_SIZE] = {0};
#endif

	if (count == 0)
		return 0;

	mutex_lock(&tty_instance->ccci_tty_mutex);

	data_be_write = (int)count;

	if (tty_debug_enable[md_id] & (1UL << tty_instance->idx))
		CCCI_DBG_MSG(md_id, "tty",
			     "[TX] (Before) tty%d write_request=%d write=%04d read=%4d\n",
			     tty_instance->idx, data_be_write,
			     tty_instance->shared_mem->tx_control.write,
			     tty_instance->shared_mem->tx_control.read);

	size = 0;

	/* Check free space */
	read = tty_instance->shared_mem->tx_control.read;
	write = tty_instance->shared_mem->tx_control.write;
	length = tty_instance->shared_mem->tx_control.length;
	tx_buffer = tty_instance->shared_mem->buffer + length;

	do {
		size = ccci_tty_writeable(tty_instance);

		if (size == 0) {
			if (file->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				goto out;
			} else {
				/*  Block write */
				value =
				    wait_event_interruptible
				    (tty_instance->write_waitq,
				     ccci_tty_writeable(tty_instance));
				if (value == -ERESTARTSYS) {
					CCCI_TTY_MSG(md_id,
						     "(W)Interrupted syscall.signal_pend=0x%llx\n",
						     *(long long *)
						     current->pending.signal.
						     sig);
					ret = -EINTR;
					goto out;
				}
			}
		} else
			break;

	} while (size == 0);

	/*  Calculate write size */
	if (data_be_write >= size)
		data_be_write = size;

	if ((size <= 0) || (length <= 0) || (length - write <= 0)
	    || (data_be_write <= 0)) {
		CCCI_MSG_INF(md_id, "tty",
			     "write: length=%d,size=%d,write=%d,data_be_write=%d,count=%d\n",
			     length, size, write, data_be_write, count);
	}

	if ((write + data_be_write) >= length) {
		/*  Write twice */
		/*  1st write */
		part = length - write;
		ret = copy_from_user(&tx_buffer[write], buf, part);
		if (ret) {
			CCCI_MSG_INF(md_id, "tty",
				     "write: copy from user fail:tx_buffer=0x%08x,write=%d, buf=%08x, part=%d ret=%d line=%d\n",
				     (unsigned int)tx_buffer, write,
				     (unsigned int)buf, part, ret, __LINE__);
			ret = -EFAULT;
			goto out;
		}
		/*  2nd write */
		ret =
		    copy_from_user(tx_buffer, &buf[part], data_be_write - part);
		if (ret) {
			CCCI_MSG_INF(md_id, "tty",
				     "write: copy from user fail:tx_buffer=0x%08x,buf=%08x,part=%d,data_be_write-part=%d ret=%d line=%d\n",
				     (unsigned int)tx_buffer, (unsigned int)buf,
				     part, data_be_write - part, ret, __LINE__);
			ret = -EFAULT;
			goto out;
		}
	} else {
		/*  Write once is OK */
		ret = copy_from_user(&tx_buffer[write], buf, data_be_write);
		if (ret) {
			CCCI_MSG_INF(md_id, "tty",
				     "write: copy from user fail:tx_buffer=0x%08x,write=%d,buf=%08x,data_be_write=%d ret=%d line=%d\n",
				     (unsigned int)tx_buffer, write,
				     (unsigned int)buf, data_be_write, ret,
				     __LINE__);
			ret = -EFAULT;
			goto out;
		}
	}
#ifdef DUMP_CHANNEL_DATA
	ret = copy_from_user(dump_buf, buf, (data_be_write > DUMP_BUF_SIZE - 1 ? DUMP_BUF_SIZE - 1 : data_be_write));
	if (ret) {
		CCCI_MSG_INF(md_id, "tty",
			"write bk temp: copy from user fail:tx_buffer=0x%08x,write=%d, buf=%08x, part=%d ret=%d line=%d\n",
			(unsigned int)tx_buffer, write,
			(unsigned int)buf, part, ret, __LINE__);
		ret = 0;
	}
#endif
	/*  Updata read pointer */
	write += data_be_write;
	if (write >= length)
		write -= length;
	tty_instance->shared_mem->tx_control.write = write;
	/*wait memory updated*/
	mb();

	msg.addr = 0;
	msg.len = data_be_write;
	msg.channel = tty_instance->uart_tx;
	msg.reserved = 0;
	/*wait memory updated*/
	mb();

	do {
		ret = ccci_message_send(md_id, &msg, 1);
		if (ret == sizeof(msg))
			break;

		if (ret == -CCCI_ERR_CCIF_NO_PHYSICAL_CHANNEL) {
			xmit_retry++;
			msleep(20);
		} else
			break;
	} while (1);

	if (ret != sizeof(struct ccci_msg_t)) {

		tty_instance->ready = 1;

		if (ret == CCCI_MD_NOT_READY) {
			CCCI_DBG_MSG(md_id, "tty",
				     "ttyC%d write fail when Modem not ready\n",
				     tty_instance->channel);
		}

	} else {
		ret = data_be_write;
	}
#ifdef DUMP_CHANNEL_DATA
	if (tty_instance->uart_tx == CCCI_UART2_TX)
		ccci_ch_dump(md_id, "chr_write", dump_buf,
			(data_be_write > DUMP_BUF_SIZE - 1 ? DUMP_BUF_SIZE-1 : data_be_write));
#endif
 out:
	if (tty_debug_enable[md_id] & (1UL << tty_instance->idx))
		CCCI_DBG_MSG(md_id, "tty",
			     "[TX] (After) tty%d write_request=%d write=%04d read=%4d\n",
			     tty_instance->idx, ret,
			     tty_instance->shared_mem->tx_control.write,
			     tty_instance->shared_mem->tx_control.read);
	mutex_unlock(&tty_instance->ccci_tty_mutex);
	return ret;
}

static long ccci_tty_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
	default:
		ret = 0;
		break;
	}

	return ret;
}

static unsigned int ccci_tty_poll(struct file *file, poll_table *wait)
{
	struct tty_instance_t *tty_instance = (struct tty_instance_t *) file->private_data;
	int ret = 0;
	unsigned long flags;

	poll_wait(file, &tty_instance->poll_waitq_r, wait);
	poll_wait(file, &tty_instance->poll_waitq_w, wait);
	spin_lock_irqsave(&tty_instance->poll_lock, flags);
	if (ccci_tty_readable(tty_instance))
		ret |= POLLIN | POLLRDNORM;
	if (ccci_tty_writeable(tty_instance))
		ret |= POLLOUT | POLLWRNORM;
	spin_unlock_irqrestore(&tty_instance->poll_lock, flags);
	return ret;
}

static int ccci_tty_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	int major = imajor(inode);
	int index, minor_start;
	int md_id;
	int ret = 0;
	char *name;

	struct tty_ctl_block_t *ctlb;
	struct tty_instance_t *tty_instance = NULL;

	md_id = get_md_id_by_dev_major(major);
	if (md_id < 0) {
		CCCI_MSG_INF(md_id, "tty", "get_md_id_by_dev_major(%d)=%d\n",
			     major, md_id);
		return -ENODEV;
	}
	ret = get_dev_id_by_md_id(md_id, "tty", NULL, &minor_start);
	if (ret < 0) {
		CCCI_MSG_INF(md_id, "tty", "get minor start fail(%d)\n", ret);
		return -ENODEV;
	}
	index = minor - minor_start;

	ctlb = tty_ctlb[md_id];
	switch (index) {
	case CCCI_TTY_MODEM:
		name = "ccci_modem";
		tty_instance = &ctlb->ccci_tty_modem;
		break;

	case CCCI_TTY_META:
		name = "ccci_meta";
		tty_instance = &ctlb->ccci_tty_meta;
		break;

	case CCCI_TTY_IPC:
		name = "ccci_ipc";
		tty_instance = &ctlb->ccci_tty_ipc;
		break;
#ifdef CONFIG_MTK_ICUSB_SUPPORT
	case CCCI_TTY_ICUSB:
		name = "ccci_icusb";
		tty_instance = &ctlb->ccci_tty_icusb;
		break;
#endif

	default:
		return -ENODEV;
	}

	mutex_lock(&tty_instance->ccci_tty_mutex);

	tty_instance->count++;
	tty_instance->m_md_id = md_id;
	if (tty_instance->count > 1) {
		CCCI_MSG_INF(md_id, "tty",
			     "[tty_open]Multi-Open! %s open %s%d, %s, count:%d\n",
			     current->comm, ctlb->node_name, index, name,
			     tty_instance->count);

		mutex_unlock(&tty_instance->ccci_tty_mutex);
		return -EMFILE;
	}
	CCCI_MSG_INF(md_id, "tty",
		     "[tty_open]%s open %s%d, %s, nb_flag:%x\n",
		     current->comm, ctlb->node_name, index, name,
		     file->f_flags & O_NONBLOCK);
	write_lock_bh(&tty_instance->ccci_tty_rwlock);

	tty_instance->ready = 1;

	write_unlock_bh(&tty_instance->ccci_tty_rwlock);

	file->private_data = tty_instance;
	nonseekable_open(inode, file);

	mutex_unlock(&tty_instance->ccci_tty_mutex);

	/* Note: reset handle must be set after make sure open tty instance successfully */
	if (tty_instance->need_reset) {
		/* CCCI_MSG("<tty>%s opening ttyC%d, %s\n", current->comm, index, name); */
		tty_instance->reset_handle =
			ccci_reset_register(md_id, name);
		if (tty_instance->reset_handle < 0)
			CCCI_ERR_INF(md_id, "fs ", "tty_instance->reset_handle %d< 0\n",
				tty_instance->reset_handle);
	}
	return ret;
}

static int ccci_tty_release(struct inode *inode, struct file *file)
{
	struct tty_instance_t *tty_instance = (struct tty_instance_t *) file->private_data;
	int md_id;
	struct tty_ctl_block_t *ctlb = NULL;

	md_id = tty_instance->m_md_id;
	ctlb = tty_ctlb[md_id];

	mutex_lock(&tty_instance->ccci_tty_mutex);

	tty_instance->count--;

	CCCI_MSG_INF(md_id, "tty", "[tty_close]Port%d count %d\n",
		     tty_instance->idx, tty_instance->count);

	if (tty_instance->count == 0) {
		/*  keep tty_instance->tty cannot be used by ccci_tty_read() */
		write_lock_bh(&tty_instance->ccci_tty_rwlock);
		tty_instance->ready = 0;
		write_unlock_bh(&tty_instance->ccci_tty_rwlock);

		ccci_reset_buffers(tty_instance->shared_mem,
				   ctlb->tty_buf_size);

		if (tty_instance->need_reset) {
			if (tty_instance->reset_handle >= 0) {
				ccci_user_ready_to_reset(md_id,
							 tty_instance->reset_handle);
			} else {
				CCCI_MSG_INF(md_id, "tty",
					     "[tty_close] fail, Invalid reset handle(port%d): %d\n",
					     tty_instance->idx,
					     tty_instance->reset_handle);
			}
		}
	}

	mutex_unlock(&tty_instance->ccci_tty_mutex);

	return 0;
}

void ccci_reset_buffers(struct shared_mem_tty_t *shared_mem, int size)
{
	shared_mem->tx_control.length = size;
	shared_mem->tx_control.read = 0;
	shared_mem->tx_control.write = 0;

	shared_mem->rx_control.length = size;
	shared_mem->rx_control.read = 0;
	shared_mem->rx_control.write = 0;

	memset(shared_mem->buffer, 0, size * 2);
}

int ccci_uart_ipo_h_restore(int md_id)
{
	struct tty_ctl_block_t *ctlb = NULL;

	ctlb = tty_ctlb[md_id];
	ccci_reset_buffers(ctlb->uart1_shared_mem, ctlb->tty_buf_size);
	ccci_reset_buffers(ctlb->uart2_shared_mem, ctlb->tty_buf_size);
	ccci_reset_buffers(ctlb->uart3_shared_mem, ctlb->tty_buf_size);
#ifdef CONFIG_MTK_ICUSB_SUPPORT
	ccci_reset_buffers(ctlb->uart4_shared_mem, ctlb->tty_buf_size);
#endif
	return 0;
}

static void tty_call_back_func(struct MD_CALL_BACK_QUEUE *notifier,
			       unsigned long data)
{
	struct tty_ctl_block_t *ctl_b =
	    container_of(notifier, struct tty_ctl_block_t, tty_notifier);
	int md_id = ctl_b->m_md_id;

	switch (data) {
	case CCCI_MD_RESET:
		CCCI_TTY_MSG(md_id, "tty_call_back_func: reset tty buffers\n");
		ccci_reset_buffers(ctl_b->ccci_tty_meta.shared_mem,
				   ctl_b->tty_buf_size);
		ccci_reset_buffers(ctl_b->ccci_tty_modem.shared_mem,
				   ctl_b->tty_buf_size);
		ccci_reset_buffers(ctl_b->ccci_tty_ipc.shared_mem,
				   ctl_b->tty_buf_size);
#ifdef CONFIG_MTK_ICUSB_SUPPORT
		ccci_reset_buffers(ctl_b->ccci_tty_icusb.shared_mem,
				   ctl_b->tty_buf_size);
#endif
		break;

	default:
		break;
	}
}

static const struct file_operations ccci_tty_ops = {
	.owner = THIS_MODULE,
	.open = ccci_tty_open,
	.read = ccci_tty_read,
	.write = ccci_tty_write,
	.release = ccci_tty_release,
	.unlocked_ioctl = ccci_tty_ioctl,
	.poll = ccci_tty_poll,

};

void tty_timer_func(unsigned long data)
{
#if 0
	struct tty_instance_t *tty_instance = (struct tty_instance_t *) data;
	struct tty_ctl_block_t *ctlb = NULL;
	int md_id = tty_instance->m_md_id;

	CCCI_DBG_MSG(md_id, "tty", "timer func running\n");
	switch (tty_instance->idx) {
	case CCCI_TTY_MODEM:
		ctlb =
		    container_of(tty_instance, struct tty_ctl_block_t, ccci_tty_modem);
		tasklet_schedule(&ctlb->ccci_tty_modem_read_tasklet);
		break;
	case CCCI_TTY_META:
		CCCI_DBG_MSG(md_id, "tty", "timer: META\n");
		ctlb =
		    container_of(tty_instance, struct tty_ctl_block_t, ccci_tty_meta);
		tasklet_schedule(&ctlb->ccci_tty_meta_read_tasklet);
		break;
	case CCCI_TTY_IPC:
		ctlb =
		    container_of(tty_instance, struct tty_ctl_block_t, ccci_tty_ipc);
		tasklet_schedule(&ctlb->ccci_tty_ipc_read_tasklet);
		break;
	}
#endif
}

#define CCCI_TTY_NAME    "ccci_tty_drv"
#ifdef CONFIG_MTK_ICUSB_SUPPORT
#define CCCI_TTY_DEV_NUM    (4)
#else
#define CCCI_TTY_DEV_NUM    (3)
#endif

void ccci_tty_instance_init(struct tty_instance_t *instance)
{
	rwlock_init(&instance->ccci_tty_rwlock);
	spin_lock_init(&instance->poll_lock);
	wake_lock_init(&instance->wake_lock, WAKE_LOCK_SUSPEND,
		       instance->wakelock_name);
	/* setup_timer(&instance->timer, tty_timer_func, instance); */
	mutex_init(&instance->ccci_tty_mutex);
	init_waitqueue_head(&instance->read_waitq);
	init_waitqueue_head(&instance->write_waitq);
	init_waitqueue_head(&instance->poll_waitq_r);
	init_waitqueue_head(&instance->poll_waitq_w);
	/* tasklet_init(&instance->ccci_tty_tasklet, ccci_tty_read, instance); */
}

int ccci_tty_init(int md_id)
{
	int ret = 0;
	int smem_phy = 0;
	int smem_size = 0;
	int tty_buf_len;
	int major, minor;
	struct tty_ctl_block_t *ctlb;
	char name[16];

	if (md_id < 0 || md_id >= MAX_MD_NUM)
		return -CCCI_ERR_INVALID_PARAM;
	/*  Create control block structure */
	ctlb = kmalloc(sizeof(struct tty_ctl_block_t), GFP_KERNEL);
	if (ctlb == NULL)
		return -CCCI_ERR_GET_MEM_FAIL;

	memset(ctlb, 0, sizeof(struct tty_ctl_block_t));
	tty_ctlb[md_id] = ctlb;

	/*  Init ctlb */
	ctlb->m_md_id = md_id;
	ctlb->tty_notifier.call = tty_call_back_func;
	ctlb->tty_notifier.next = NULL;
	ret = md_register_call_chain(md_id, &ctlb->tty_notifier);
	if (ret) {
		CCCI_MSG_INF(md_id, "tty", "md_register_call_chain fail: %d\n",
			     ret);
		goto _RELEASE_CTL_MEMORY;
	}

	ret = get_dev_id_by_md_id(md_id, "tty", &major, &minor);
	if (ret < 0)
		goto _RELEASE_CTL_MEMORY;

	snprintf(name, 16, "%s%d", CCCI_TTY_NAME, md_id);
	if (register_chrdev_region(MKDEV(major, minor), CCCI_TTY_DEV_NUM, name)
	    != 0) {
		CCCI_MSG_INF(md_id, "tty", "regsiter CCCI_TTY fail\n");
		ret = -1;
		goto _RELEASE_CTL_MEMORY;
	}

	cdev_init(&ctlb->ccci_tty_dev, &ccci_tty_ops);
	ctlb->ccci_tty_dev.owner = THIS_MODULE;
	ret =
	    cdev_add(&ctlb->ccci_tty_dev, MKDEV(major, minor),
		     CCCI_TTY_DEV_NUM);
	if (ret) {
		CCCI_MSG_INF(md_id, "tty", "cdev_add fail\n");
		goto _DEL_TTY_DRV;
	}

	snprintf(ctlb->drv_name, 32, CCCI_TTY_NAME "_%d", md_id);
	if (md_id == MD_SYS1)
		snprintf(ctlb->node_name, 16, "ttyC");
	else if (md_id == MD_SYS2)
		snprintf(ctlb->node_name, 16, "ccci%d_tty", md_id + 1);

	ret = ccci_uart_base_req(md_id, 0, (int *)&ctlb->uart1_shared_mem,
		&smem_phy, &smem_size);
	if (ret) {
		CCCI_MSG_INF(md_id, "tty", "[Error]ccci_uart_base_req ret=%d\n", ret);
		goto _DEL_TTY_DRV;
	}
	/* CCCI_DBG_MSG(md_id, "tty", "TTY0 %x:%x:%d\n", (unsigned int)ctlb->uart1_shared_mem,  */
	/*             (unsigned int)smem_phy, smem_size); */

	/*  Get tty config information */
	if (ccci_get_sub_module_cfg(md_id, "tty",
		(char *)&tty_buf_len, sizeof(int)) != sizeof(int)) {
		CCCI_MSG_INF(md_id, "tty", "[Error]get tty version fail\n");
		goto _DEL_TTY_DRV;
	}
	tty_buf_len = (tty_buf_len - sizeof(struct shared_mem_tty_t)) / 2;
	ctlb->tty_buf_size = tty_buf_len;

	/*  Meta section */
	ctlb->uart1_shared_mem->tx_control.length = tty_buf_len;
	ctlb->uart1_shared_mem->tx_control.read = 0;
	ctlb->uart1_shared_mem->tx_control.write = 0;
	ctlb->uart1_shared_mem->rx_control.length = tty_buf_len;
	ctlb->uart1_shared_mem->rx_control.read = 0;
	ctlb->uart1_shared_mem->rx_control.write = 0;

	/*  meta related channel register */
	register_to_logic_ch(md_id, CCCI_UART1_RX, ccci_tty_callback, ctlb);
	register_to_logic_ch(md_id, CCCI_UART1_TX_ACK, ccci_tty_callback, ctlb);

	ctlb->ccci_tty_meta.need_reset = 0;
	ctlb->ccci_tty_meta.reset_handle = -1;
	ctlb->ccci_tty_meta.count = 0;
	ctlb->ccci_tty_meta.channel = 0;
	ctlb->ccci_tty_meta.shared_mem = ctlb->uart1_shared_mem;
	ctlb->ccci_tty_meta.uart_tx = CCCI_UART1_TX;
	ctlb->ccci_tty_meta.uart_rx_ack = CCCI_UART1_RX_ACK;
	ctlb->ccci_tty_meta.ready = 1;
	ctlb->ccci_tty_meta.idx = 1;
	snprintf(ctlb->ccci_tty_meta.wakelock_name,
		 sizeof(ctlb->ccci_tty_meta.wakelock_name), "%s%d", "ccci_meta",
		 (md_id + 1));
	ccci_tty_instance_init(&ctlb->ccci_tty_meta);

	/*  MUX section */
	ccci_uart_base_req(md_id, 1, (int *)&ctlb->uart2_shared_mem, &smem_phy, &smem_size);
	/* CCCI_DBG_MSG(md_id, "tty", "TTY1 %x:%x:%d\n", (unsigned int)ctlb->uart2_shared_mem,  */
	/*             (unsigned int)smem_phy, smem_size); */

	ctlb->uart2_shared_mem->tx_control.length = tty_buf_len;
	ctlb->uart2_shared_mem->tx_control.read = 0;
	ctlb->uart2_shared_mem->tx_control.write = 0;

	ctlb->uart2_shared_mem->rx_control.length = tty_buf_len;
	ctlb->uart2_shared_mem->rx_control.read = 0;
	ctlb->uart2_shared_mem->rx_control.write = 0;

	/*  modem related channel registration. */
	register_to_logic_ch(md_id, CCCI_UART2_RX, ccci_tty_callback, ctlb);
	register_to_logic_ch(md_id, CCCI_UART2_TX_ACK, ccci_tty_callback, ctlb);

	/*  modem reset registration. */
	ctlb->ccci_tty_modem.need_reset = 1;
	ctlb->ccci_tty_modem.reset_handle = -1;
	ctlb->ccci_tty_modem.count = 0;
	ctlb->ccci_tty_modem.channel = 1;
	ctlb->ccci_tty_modem.shared_mem = ctlb->uart2_shared_mem;
	ctlb->ccci_tty_modem.uart_tx = CCCI_UART2_TX;
	ctlb->ccci_tty_modem.uart_rx_ack = CCCI_UART2_RX_ACK;
	ctlb->ccci_tty_modem.ready = 1;
	ctlb->ccci_tty_modem.idx = 0;
	snprintf(ctlb->ccci_tty_modem.wakelock_name,
		 sizeof(ctlb->ccci_tty_modem.wakelock_name), "%s%d",
		 "ccci_modem", (md_id + 1));
	ccci_tty_instance_init(&ctlb->ccci_tty_modem);

	/*  for IPC uart */
	ccci_uart_base_req(md_id, 5, (int *)&ctlb->uart3_shared_mem, &smem_phy, &smem_size);
	/* CCCI_DBG_MSG(md_id, "tty", "TTY2 %x:%x:%d\n", (unsigned int)ctlb->uart3_shared_mem,  */
	/*             (unsigned int)smem_phy, smem_size); */

	ctlb->uart3_shared_mem->tx_control.length = tty_buf_len;
	ctlb->uart3_shared_mem->tx_control.read = 0;
	ctlb->uart3_shared_mem->tx_control.write = 0;
	ctlb->uart3_shared_mem->rx_control.length = tty_buf_len;
	ctlb->uart3_shared_mem->rx_control.read = 0;
	ctlb->uart3_shared_mem->rx_control.write = 0;
	/*  IPC related channel register */
	register_to_logic_ch(md_id, CCCI_IPC_UART_RX, ccci_tty_callback, ctlb);
	register_to_logic_ch(md_id, CCCI_IPC_UART_TX_ACK, ccci_tty_callback, ctlb);

	/*  IPC reset register */
	ctlb->ccci_tty_ipc.need_reset = 0;
	ctlb->ccci_tty_ipc.reset_handle = -1;
	ctlb->ccci_tty_ipc.count = 0;
	ctlb->ccci_tty_ipc.channel = 1;
	ctlb->ccci_tty_ipc.shared_mem = ctlb->uart3_shared_mem;
	ctlb->ccci_tty_ipc.uart_tx = CCCI_IPC_UART_TX;
	ctlb->ccci_tty_ipc.uart_rx_ack = CCCI_IPC_UART_RX_ACK;
	ctlb->ccci_tty_ipc.ready = 1;
	ctlb->ccci_tty_ipc.idx = 2;
	snprintf(ctlb->ccci_tty_ipc.wakelock_name,
		 sizeof(ctlb->ccci_tty_ipc.wakelock_name), "%s%d", "ccci_ipc",
		 (md_id + 1));
	ccci_tty_instance_init(&ctlb->ccci_tty_ipc);

#ifdef CONFIG_MTK_ICUSB_SUPPORT
	/*  for ICUSB uart */
	ccci_uart_base_req(md_id, 6, (int *)&ctlb->uart4_shared_mem, &smem_phy,	&smem_size);
	/* CCCI_DBG_MSG(md_id, "tty", "TTY2 %x:%x:%d\n", (unsigned int)ctlb->uart3_shared_mem,  */
	/*             (unsigned int)smem_phy, smem_size); */
	ctlb->uart4_shared_mem->tx_control.length = tty_buf_len;
	ctlb->uart4_shared_mem->tx_control.read = 0;
	ctlb->uart4_shared_mem->tx_control.write = 0;
	ctlb->uart4_shared_mem->rx_control.length = tty_buf_len;
	ctlb->uart4_shared_mem->rx_control.read = 0;
	ctlb->uart4_shared_mem->rx_control.write = 0;
	/*  IPC related channel register */
	register_to_logic_ch(md_id, CCCI_ICUSB_RX, ccci_tty_callback, ctlb);
	register_to_logic_ch(md_id, CCCI_ICUSB_TX_ACK, ccci_tty_callback, ctlb);

	/*  IPC reset register */
	ctlb->ccci_tty_icusb.need_reset = 0;
	ctlb->ccci_tty_icusb.reset_handle = -1;
	ctlb->ccci_tty_icusb.count = 0;
	ctlb->ccci_tty_icusb.channel = 1;
	ctlb->ccci_tty_icusb.shared_mem = ctlb->uart4_shared_mem;
	ctlb->ccci_tty_icusb.uart_tx = CCCI_ICUSB_TX;
	ctlb->ccci_tty_icusb.uart_rx_ack = CCCI_ICUSB_RX_ACK;
	ctlb->ccci_tty_icusb.ready = 1;
	ctlb->ccci_tty_icusb.idx = 3;
	snprintf(ctlb->ccci_tty_icusb.wakelock_name,
		 sizeof(ctlb->ccci_tty_icusb.wakelock_name), "%s%d",
		 "ccci_icusb", (md_id + 1));
	ccci_tty_instance_init(&ctlb->ccci_tty_icusb);
#endif

	return 0;

 _DEL_TTY_DRV:
	unregister_chrdev_region(MKDEV(major, minor), CCCI_TTY_DEV_NUM);

 _RELEASE_CTL_MEMORY:
	kfree(ctlb);
	tty_ctlb[md_id] = NULL;

	return ret;
}

void ccci_tty_exit(int md_id)
{
	struct tty_ctl_block_t *ctlb = tty_ctlb[md_id];

	if (ctlb != NULL) {
		unregister_chrdev_region(MKDEV(ctlb->major, ctlb->minor),
					 CCCI_TTY_DEV_NUM);
		cdev_del(&ctlb->ccci_tty_dev);
		un_register_to_logic_ch(md_id, CCCI_UART1_RX);
		/* ccci_unregister(CCCI_UART1_RX_ACK); */
		/* ccci_unregister(CCCI_UART1_TX); */
		un_register_to_logic_ch(md_id, CCCI_UART1_TX_ACK);

		un_register_to_logic_ch(md_id, CCCI_UART2_RX);
		/* ccci_unregister(CCCI_UART2_RX_ACK); */
		/* ccci_unregister(CCCI_UART2_TX); */
		un_register_to_logic_ch(md_id, CCCI_UART2_TX_ACK);

		un_register_to_logic_ch(md_id, CCCI_IPC_UART_RX);
		/* ccci_unregister(CCCI_IPC_UART_RX_ACK); */
		/* ccci_unregister(CCCI_IPC_UART_TX); */
		un_register_to_logic_ch(md_id, CCCI_IPC_UART_TX_ACK);

#ifdef CONFIG_MTK_ICUSB_SUPPORT
		un_register_to_logic_ch(md_id, CCCI_ICUSB_RX);
		un_register_to_logic_ch(md_id, CCCI_ICUSB_TX_ACK);
#endif
		ctlb->uart1_shared_mem = NULL;
		ctlb->uart2_shared_mem = NULL;
		ctlb->uart3_shared_mem = NULL;
#ifdef CONFIG_MTK_ICUSB_SUPPORT
		ctlb->uart4_shared_mem = NULL;
#endif

		wake_lock_destroy(&ctlb->ccci_tty_modem.wake_lock);
		wake_lock_destroy(&ctlb->ccci_tty_meta.wake_lock);
		wake_lock_destroy(&ctlb->ccci_tty_ipc.wake_lock);
#ifdef CONFIG_MTK_ICUSB_SUPPORT
		wake_lock_destroy(&ctlb->ccci_tty_icusb.wake_lock);
#endif

		kfree(ctlb);
		tty_ctlb[md_id] = NULL;
	}
}
