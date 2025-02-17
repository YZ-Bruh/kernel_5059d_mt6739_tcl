
#include "tpd.h"
#define GUP_FW_INFO
#include "tpd_custom_gt9xx.h"

#include "mt_boot_common.h"
#include "upmu_common.h"
#include <linux/of_irq.h>
#include <linux/interrupt.h>

#ifdef TPD_PROXIMITY
#include <linux/hwmsensor.h>
#include <linux/hwmsen_dev.h>
#include <linux/sensors_io.h>
#endif
#if GTP_SUPPORT_I2C_DMA
#include <linux/dma-mapping.h>
#endif
#include <linux/device.h>

int touch_irq;
static int tpd_flag;
int tpd_halt = 0;
static int tpd_eint_mode = 1;
static int tpd_polling_time = 50;
static DECLARE_WAIT_QUEUE_HEAD(waiter);
static DEFINE_MUTEX(i2c_access);

#if GTP_HAVE_TOUCH_KEY
const u16 touch_key_array[] = TPD_KEYS;
/* #define GTP_MAX_KEY_NUM ( sizeof( touch_key_array )/sizeof( touch_key_array[0] ) ) */
struct touch_virtual_key_map_t {
	int point_x;
	int point_y;
};
static struct touch_virtual_key_map_t touch_key_point_maping_array[] = GTP_KEY_MAP_ARRAY;
#endif

#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
static int tpd_wb_start_local[TPD_WARP_CNT] = TPD_WARP_START;
static int tpd_wb_end_local[TPD_WARP_CNT] = TPD_WARP_END;
#endif

#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
/* static int tpd_calmat_local[8]     = TPD_CALIBRATION_MATRIX; */
/* static int tpd_def_calmat_local[8] = TPD_CALIBRATION_MATRIX; */
static int tpd_def_calmat_local_normal[8] = TPD_CALIBRATION_MATRIX_ROTATION_NORMAL;
static int tpd_def_calmat_local_factory[8] = TPD_CALIBRATION_MATRIX_ROTATION_FACTORY;
#endif

#if GTP_SUPPORT_I2C_DMA
static u8 *gpDMABuf_va;
static u32 gpDMABuf_pa;
#endif
static irqreturn_t tpd_eint_interrupt_handler(void);
static int touch_event_handler(void *unused);
static int tpd_i2c_probe(struct i2c_client *client,
			 const struct i2c_device_id *id);
static int tpd_i2c_detect(struct i2c_client *client,
			  struct i2c_board_info *info);
static int tpd_i2c_remove(struct i2c_client *client);
static void tpd_on(void);
static void tpd_off(void);

#ifdef GTP_CHARGER_DETECT
#define TPD_CHARGER_CHECK_CIRCLE    50
static struct delayed_work gtp_charger_check_work;
static struct workqueue_struct *gtp_charger_check_workqueue;
static void gtp_charger_check_func(struct work_struct *);
static u8 gtp_charger_mode;
#endif

#if (GTP_ESD_PROTECT || GTP_COMPATIBLE_MODE)
static void force_reset_guitar(void);
#endif

#if GTP_ESD_PROTECT
#define TPD_ESD_CHECK_CIRCLE        2000
static struct delayed_work gtp_esd_check_work;
static struct workqueue_struct *gtp_esd_check_workqueue;
static void gtp_esd_check_func(struct work_struct *);
#endif

#ifdef TPD_PROXIMITY
#define TPD_PROXIMITY_VALID_REG                   0x814E
#define TPD_PROXIMITY_ENABLE_REG                  0x8042
static u8 tpd_proximity_flag;
static u8 tpd_proximity_detect = 1;	/* 0-->close ; 1--> far away */
#endif

#ifndef GTP_REG_REFRESH_RATE
#define GTP_REG_REFRESH_RATE		0x8056
#endif

struct i2c_client *i2c_client_point = NULL;
static const struct i2c_device_id tpd_i2c_id[] = { {"gt9xx", 0}, {} };
static unsigned short force[] = { 0, 0xBA, I2C_CLIENT_END, I2C_CLIENT_END };
static const unsigned short *const forces[] = { force, NULL };

/* static struct i2c_client_address_data addr_data = { .forces = forces,}; */
/* static struct i2c_board_info i2c_tpd __initdata = { I2C_BOARD_INFO("gt9xx", (0xBA >> 1)) }; */
static const struct of_device_id tpd_of_match[] = {
	{.compatible = "mediatek,cap_touch"},
	{},
};

static struct i2c_driver tpd_i2c_driver = {
	.probe = tpd_i2c_probe,
	.remove = tpd_i2c_remove,
	.detect = tpd_i2c_detect,
	.driver.name = "gt9xx",
	.driver = {
		   .name = "gt9xx",
		   .of_match_table = tpd_of_match,
		   },
	.id_table = tpd_i2c_id,
	.address_list = (const unsigned short *)forces,
};

static u8 config[GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH]
= { GTP_REG_CONFIG_DATA >> 8, GTP_REG_CONFIG_DATA & 0xff };

#ifdef GTP_CHARGER_DETECT
static u8 config_charger[GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH]
= { GTP_REG_CONFIG_DATA >> 8, GTP_REG_CONFIG_DATA & 0xff };
#endif
#pragma pack(1)
struct st_tpd_info {
	u16 pid;		/* product id   // */
	u16 vid;		/* version id   // */
};
#pragma pack()

struct st_tpd_info tpd_info;
u8 int_type = 0;
u32 abs_x_max = 0;
u32 abs_y_max = 0;
u8 gtp_rawdiff_mode = 0;
u8 cfg_len = 0;

#if GTP_COMPATIBLE_MODE
u8 driver_num = 0;
u8 sensor_num = 0;
u8 gtp_ref_retries = 0;
u8 gtp_clk_retries = 0;
enum CHIP_TYPE_T gtp_chip_type = CHIP_TYPE_GT9;
u8 gtp_clk_buf[6];
u8 rqst_processing = 0;
u8 is_950 = 0;
u8 esd_resetting = 0;
static u8 gtp_bak_ref_proc(struct i2c_client *client, u8 mode);
static u8 gtp_main_clk_proc(struct i2c_client *client);
static void gtp_recovery_reset(struct i2c_client *client);
#endif
static struct proc_dir_entry *gt91xx_config_proc;

static u8 gtp_set_refresh_rate(u8 rate)
{
	u8 buf[3] = {GTP_REG_REFRESH_RATE >> 8, GTP_REG_REFRESH_RATE & 0xff, rate};

	if (rate > 0xf) {
		GTP_ERROR("Refresh rate is over range (%d)", rate);
		return FAIL;
	}

	GTP_INFO("Refresh rate change to %d", rate);
	return gtp_i2c_write(i2c_client_point, buf, sizeof(buf));
}

static u8 gtp_get_refresh_rate(void)
{
	int ret;

	u8 buf[3] = { GTP_REG_REFRESH_RATE >> 8, GTP_REG_REFRESH_RATE & 0xff };

	ret = gtp_i2c_read(i2c_client_point, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	GTP_INFO("Refresh rate is %d", buf[GTP_ADDR_LENGTH]);
	return buf[GTP_ADDR_LENGTH];
}

/* ============================================================= */
static ssize_t show_refresh_rate(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int ret = gtp_get_refresh_rate();

	if (ret < 0)
		return 0;
	else
		return sprintf(buf, "%d\n", ret);
}

static ssize_t store_refresh_rate(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	unsigned long rate = 0;

	if (kstrtoul(buf, 16, &rate))
		return 0;
	gtp_set_refresh_rate(rate);
	return size;
}

static DEVICE_ATTR(tpd_refresh_rate, 0664, show_refresh_rate,
		   store_refresh_rate);

static struct device_attribute *gt9xx_attrs[] = {

	&dev_attr_tpd_refresh_rate,
};

/* ============================================================= */

static int tpd_i2c_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	strcpy(info->type, "mtk-tpd");
	return 0;
}

#ifdef TPD_PROXIMITY
static s32 tpd_get_ps_value(void)
{
	return tpd_proximity_detect;
}

static s32 tpd_enable_ps(s32 enable)
{
	u8 state;
	s32 ret = -1;

	if (enable) {
		state = 1;
		tpd_proximity_flag = 1;
		GTP_INFO("TPD proximity function to be on.");
	} else {
		state = 0;
		tpd_proximity_flag = 0;
		GTP_INFO("TPD proximity function to be off.");
	}

	ret =
	    i2c_write_bytes(i2c_client_point, TPD_PROXIMITY_ENABLE_REG, &state,
			    1);

	if (ret < 0) {
		GTP_ERROR("TPD %s proximity cmd failed.",
			  state ? "enable" : "disable");
		return ret;
	}

	GTP_INFO("TPD proximity function %s success.",
		 state ? "enable" : "disable");
	return 0;
}

s32 tpd_ps_operate(void *self, u32 command, void *buff_in, s32 size_in,
		   void *buff_out, s32 size_out, s32 *actualout)
{
	s32 err = 0;
	s32 value;
	hwm_sensor_data *sensor_data;

	switch (command) {
	case SENSOR_DELAY:
		if ((buff_in == NULL) || (size_in < sizeof(int))) {
			GTP_ERROR("Set delay parameter error!");
			err = -EINVAL;
		}

		/* Do nothing */
		break;

	case SENSOR_ENABLE:
		if ((buff_in == NULL) || (size_in < sizeof(int))) {
			GTP_ERROR("Enable sensor parameter error!");
			err = -EINVAL;
		} else {
			value = *(int *)buff_in;
			err = tpd_enable_ps(value);
		}

		break;

	case SENSOR_GET_DATA:
		if ((buff_out == NULL) || (size_out < sizeof(hwm_sensor_data))) {
			GTP_ERROR("Get sensor data parameter error!");
			err = -EINVAL;
		} else {
			sensor_data = (hwm_sensor_data *) buff_out;
			sensor_data->values[0] = tpd_get_ps_value();
			sensor_data->value_divide = 1;
			sensor_data->status = SENSOR_STATUS_ACCURACY_MEDIUM;
		}

		break;

	default:
		GTP_ERROR
		    ("proxmy sensor operate function no this parameter %d!",
		     command);
		err = -1;
		break;
	}

	return err;
}
#endif

static ssize_t gt91xx_config_read_proc(struct file *file, char *buffer,
				       size_t count, loff_t *ppos)
{
	char *ptr = NULL;
	char *page = NULL;
	char temp_data[GTP_CONFIG_MAX_LENGTH + 2] = { 0 };
	int i;
	int size;

	page = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!page) {
		kfree(page);
		return -ENOMEM;
	}

	ptr = page;

	ptr += sprintf(ptr, "==== GT9XX config init value====\n");

	for (i = 0; i < GTP_CONFIG_MAX_LENGTH; i++) {
		ptr += sprintf(ptr, "0x%02X ", config[i + 2]);

		if (i % 8 == 7)
			ptr += sprintf(ptr, "\n");
	}

	ptr += sprintf(ptr, "\n");

	ptr += sprintf(ptr, "==== GT9XX config real value====\n");
	i2c_read_bytes(i2c_client_point, GTP_REG_CONFIG_DATA, temp_data,
		       GTP_CONFIG_MAX_LENGTH);

	for (i = 0; i < GTP_CONFIG_MAX_LENGTH; i++) {
		ptr += sprintf(ptr, "0x%02X ", temp_data[i]);

		if (i % 8 == 7)
			ptr += sprintf(ptr, "\n");
	}

	/* Touch PID & VID */
	ptr += sprintf(ptr, "\n");
	ptr += sprintf(ptr, "==== GT9XX Version ID ====\n");
	i2c_read_bytes(i2c_client_point, GTP_REG_VERSION, temp_data, 6);
	ptr +=
	    sprintf(ptr, "Chip PID: %c%c%c  VID: 0x%02X%02X\n", temp_data[0],
		    temp_data[1], temp_data[2], temp_data[5], temp_data[4]);
	ptr +=
	    sprintf(ptr, "Driver VID: 0x%02X%02X\n", gtp_default_FW[12],
		    gtp_default_FW[13]);
	i2c_read_bytes(i2c_client_point, 0x41E4, temp_data, 1);
	ptr += sprintf(ptr, "Boot status 0x%X\n", temp_data[0]);

	/* Touch Status and Clock Gate */
	ptr += sprintf(ptr, "\n");
	ptr += sprintf(ptr, "==== Touch Status and Clock Gate ====\n");
	ptr += sprintf(ptr, "status: 1: on, 0 :off\n");
	ptr += sprintf(ptr, "status:%d\n", (tpd_halt + 1) & 0x1);

	size = ptr - page;
	kfree(page);
	return size;
}

static ssize_t gt91xx_config_write_proc(struct file *file, const char *buffer,
					size_t count, loff_t *ppos)
{
	s32 ret = 0;
	char temp[25] = { 0 };	/* for store special format cmd */
	char mode_str[15] = { 0 };
	unsigned int mode;
	u8 buf[1];

	GTP_DEBUG("write count %ld", (unsigned long int)count);

	if (count > GTP_CONFIG_MAX_LENGTH) {
		GTP_ERROR("size not match [%d:%ld]", GTP_CONFIG_MAX_LENGTH,
			  (unsigned long int)count);
		return -EFAULT;
	}

    /**********************************************/
	/* for store special format cmd  */
	if (copy_from_user(temp, buffer, sizeof(temp))) {
		GTP_ERROR("copy from user fail 2");
		return -EFAULT;
	}
	ret = sscanf(temp, "%s %d", (char *)&mode_str, &mode);

    /***********POLLING/EINT MODE switch****************/
	if (strcmp(mode_str, "polling") == 0) {
		if (mode >= 10 && mode <= 200) {
			GTP_INFO("Switch to polling mode, polling time is %d",
				 mode);
			tpd_eint_mode = 0;
			tpd_polling_time = mode;
			tpd_flag = 1;
			wake_up_interruptible(&waiter);
		} else {
			GTP_INFO
			    ("Wrong polling time, please set between 10~200ms");
		}
		return count;
	}
	if (strcmp(mode_str, "eint") == 0) {
		GTP_INFO("Switch to eint mode");
		tpd_eint_mode = 1;
		return count;
	}
    /**********************************************/
	if (strcmp(mode_str, "switch") == 0) {
		if (mode == 0)	/* turn off */
			tpd_off();
		else if (mode == 1)	/* turn on */
			tpd_on();
		else
			GTP_ERROR("error mode :%d", mode);
		return count;
	}
	/* force clear config */
	if (strcmp(mode_str, "clear_config") == 0) {
		GTP_INFO("Force clear config");
		buf[0] = 0x10;
		ret = i2c_write_bytes(i2c_client_point, GTP_REG_SLEEP, buf, 1);
		return count;
	}

	if (copy_from_user(&config[2], buffer, count)) {
		GTP_ERROR("copy from user fail");
		return -EFAULT;
	}

    /***********clk operate reseved****************/
    /**********************************************/
	ret = gtp_send_cfg(i2c_client_point);
	abs_x_max = (config[RESOLUTION_LOC + 1] << 8) + config[RESOLUTION_LOC];
	abs_y_max =
	    (config[RESOLUTION_LOC + 3] << 8) + config[RESOLUTION_LOC + 2];
	int_type = (config[TRIGGER_LOC]) & 0x03;

	if (ret < 0)
		GTP_ERROR("send config failed.");

	return count;
}

#if GTP_SUPPORT_I2C_DMA
s32 i2c_dma_read(struct i2c_client *client, u16 addr, u8 *rxbuf, s32 len)
{
	int ret;
	s32 retry = 0;
	u8 buffer[2];

	struct i2c_msg msg[2] = {

		{
		 .addr = (client->addr & I2C_MASK_FLAG),
		 .flags = 0,
		 .buf = buffer,
		 .len = 2,
		 .timing = I2C_MASTER_CLOCK},
		{
		 .addr = (client->addr & I2C_MASK_FLAG),
		 .ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
		 .flags = I2C_M_RD,
		 .buf = gpDMABuf_pa,
		 .len = len,
		 .timing = I2C_MASTER_CLOCK},
	};

	buffer[0] = (addr >> 8) & 0xFF;
	buffer[1] = addr & 0xFF;

	if (rxbuf == NULL)
		return -1;

	/* GTP_DEBUG("dma i2c read: 0x%04X, %d bytes(s)", addr, len); */
	for (retry = 0; retry < 20; ++retry) {
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0)
			continue;
		memcpy(rxbuf, gpDMABuf_va, len);
		return 0;
	}
	GTP_ERROR("Dma I2C Read Error: 0x%04X, %d byte(s), err-code: %d", addr,
		  len, ret);
	return ret;
}

s32 i2c_dma_write(struct i2c_client *client, u16 addr, u8 *txbuf, s32 len)
{
	int ret;
	s32 retry = 0;
	u8 *wr_buf = gpDMABuf_va;

	struct i2c_msg msg = {
		.addr = (client->addr & I2C_MASK_FLAG),
		.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
		.flags = 0,
		.buf = gpDMABuf_pa,
		.len = 2 + len,
		.timing = I2C_MASTER_CLOCK
	};

	wr_buf[0] = (u8) ((addr >> 8) & 0xFF);
	wr_buf[1] = (u8) (addr & 0xFF);

	if (txbuf == NULL)
		return -1;

	/* GTP_DEBUG("dma i2c write: 0x%04X, %d bytes(s)", addr, len); */
	memcpy(wr_buf + 2, txbuf, len);
	for (retry = 0; retry < 20; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0)
			continue;
		return 0;
	}
	GTP_ERROR("Dma I2C Write Error: 0x%04X, %d byte(s), err-code: %d", addr,
		  len, ret);
	return ret;
}

s32 i2c_read_bytes_dma(struct i2c_client *client, u16 addr, u8 *rxbuf,
		       s32 len)
{
	s32 left = len;
	s32 read_len = 0;
	u8 *rd_buf = rxbuf;
	s32 ret = 0;

	/* GTP_DEBUG("Read bytes dma: 0x%04X, %d byte(s)", addr, len); */
	while (left > 0) {
		if (left > GTP_DMA_MAX_TRANSACTION_LENGTH)
			read_len = GTP_DMA_MAX_TRANSACTION_LENGTH;
		else
			read_len = left;
		ret = i2c_dma_read(client, addr, rd_buf, read_len);
		if (ret < 0) {
			GTP_ERROR("dma read failed");
			return -1;
		}

		left -= read_len;
		addr += read_len;
		rd_buf += read_len;
	}
	return 0;
}

s32 i2c_write_bytes_dma(struct i2c_client *client, u16 addr, u8 *txbuf,
			s32 len)
{

	s32 ret = 0;
	s32 write_len = 0;
	s32 left = len;
	u8 *wr_buf = txbuf;

	/* GTP_DEBUG("Write bytes dma: 0x%04X, %d byte(s)", addr, len); */
	while (left > 0) {
		if (left > GTP_DMA_MAX_I2C_TRANSFER_SIZE)
			write_len = GTP_DMA_MAX_I2C_TRANSFER_SIZE;
		else
			write_len = left;
		ret = i2c_dma_write(client, addr, wr_buf, write_len);

		if (ret < 0) {
			GTP_ERROR("dma i2c write failed!");
			return -1;
		}

		left -= write_len;
		addr += write_len;
		wr_buf += write_len;
	}
	return 0;
}
#endif /* GTP_SUPPORT_I2C_DMA */

int i2c_read_bytes(struct i2c_client *client, u16 addr, u8 *rxbuf, int len)
{
#if GTP_SUPPORT_I2C_DMA
	return i2c_read_bytes_dma(client, addr, rxbuf, len);
#else

	u8 buffer[GTP_ADDR_LENGTH];
	u16 left = len;
	u16 offset = 0;

	struct i2c_msg msg[2] = {

		{
		 .addr = ((client->addr & I2C_MASK_FLAG) | (I2C_ENEXT_FLAG)),
		 /* .addr = (client->addr &I2C_MASK_FLAG), */
		 /* .ext_flag = I2C_ENEXT_FLAG, */
		 /* .addr = ((client->addr &I2C_MASK_FLAG) | (I2C_PUSHPULL_FLAG)), */
		 .flags = 0,
		 .buf = buffer,
		 .len = GTP_ADDR_LENGTH,
		 .timing = I2C_MASTER_CLOCK},
		{
		 .addr = ((client->addr & I2C_MASK_FLAG) | (I2C_ENEXT_FLAG)),
		 /* .addr = (client->addr &I2C_MASK_FLAG), */
		 /* .ext_flag = I2C_ENEXT_FLAG, */
		 /* .addr = ((client->addr &I2C_MASK_FLAG) | (I2C_PUSHPULL_FLAG)), */
		 .flags = I2C_M_RD,
		 .timing = I2C_MASTER_CLOCK},
	};

	if (rxbuf == NULL)
		return -1;

	GTP_DEBUG("i2c_read_bytes to device %02X address %04X len %d",
		  client->addr, addr, len);

	while (left > 0) {
		buffer[0] = ((addr + offset) >> 8) & 0xFF;
		buffer[1] = (addr + offset) & 0xFF;

		msg[1].buf = &rxbuf[offset];

		if (left > MAX_TRANSACTION_LENGTH) {
			msg[1].len = MAX_TRANSACTION_LENGTH;
			left -= MAX_TRANSACTION_LENGTH;
			offset += MAX_TRANSACTION_LENGTH;
		} else {
			msg[1].len = left;
			left = 0;
		}

		if (i2c_transfer(client->adapter, &msg[0], 2) != 2) {
			GTP_ERROR("I2C read 0x%X length=%d failed",
				  addr + offset, len);
			return -1;
		}
	}

	return 0;
#endif
}

s32 gtp_i2c_read(struct i2c_client *client, u8 *buf, s32 len)
{
	s32 ret = -1;
	u16 addr = (buf[0] << 8) + buf[1];

	ret = i2c_read_bytes(client, addr, &buf[2], len - 2);

	if (!ret)
		return 2;
#if GTP_COMPATIBLE_MODE
	if (CHIP_TYPE_GT9F == gtp_chip_type) {
		if (1 == esd_resetting)
			GTP_INFO("Esd resetting..., no recovery reset");
		else
			gtp_recovery_reset(client);
	}
#endif
	gtp_reset_guitar(client, 20);
	return ret;
}

int i2c_write_bytes(struct i2c_client *client, u16 addr, u8 *txbuf, int len)
{
#if GTP_SUPPORT_I2C_DMA
	return i2c_write_bytes_dma(client, addr, txbuf, len);
#else

	u8 buffer[MAX_TRANSACTION_LENGTH];
	u16 left = len;
	u16 offset = 0;

	struct i2c_msg msg = {

		.addr = ((client->addr & I2C_MASK_FLAG) | (I2C_ENEXT_FLAG)),
		/* .addr = (client->addr &I2C_MASK_FLAG), */
		/* .ext_flag = I2C_ENEXT_FLAG, */
		/* .addr = ((client->addr &I2C_MASK_FLAG) | (I2C_PUSHPULL_FLAG)), */
		.flags = 0,
		.buf = buffer,
		.timing = I2C_MASTER_CLOCK,
	};

	if (txbuf == NULL)
		return -1;

	GTP_DEBUG("i2c_write_bytes to device %02X address %04X len %d",
		  client->addr, addr, len);

	while (left > 0) {
		buffer[0] = ((addr + offset) >> 8) & 0xFF;
		buffer[1] = (addr + offset) & 0xFF;

		if (left > MAX_I2C_TRANSFER_SIZE) {
			memcpy(&buffer[GTP_ADDR_LENGTH], &txbuf[offset],
			       MAX_I2C_TRANSFER_SIZE);
			msg.len = MAX_TRANSACTION_LENGTH;
			left -= MAX_I2C_TRANSFER_SIZE;
			offset += MAX_I2C_TRANSFER_SIZE;
		} else {
			memcpy(&buffer[GTP_ADDR_LENGTH], &txbuf[offset], left);
			msg.len = left + GTP_ADDR_LENGTH;
			left = 0;
		}

		/* GTP_DEBUG("byte left %d offset %d", left, offset); */

		if (i2c_transfer(client->adapter, &msg, 1) != 1) {
			GTP_ERROR("I2C write 0x%X%X length=%d failed",
				  buffer[0], buffer[1], len);
			return -1;
		}
	}

	return 0;
#endif
}

s32 gtp_i2c_write(struct i2c_client *client, u8 *buf, s32 len)
{
	s32 ret = -1;
	u16 addr = (buf[0] << 8) + buf[1];

	ret = i2c_write_bytes(client, addr, &buf[2], len - 2);

	if (!ret)
		return 1;
#if GTP_COMPATIBLE_MODE
	if (CHIP_TYPE_GT9F == gtp_chip_type) {
		if (1 == esd_resetting)
			GTP_INFO("Esd resetting..., no recovery reset");
		else
			gtp_recovery_reset(client);
	}
#endif
	gtp_reset_guitar(client, 20);
	return ret;
}

s32 gtp_i2c_read_dbl_check(struct i2c_client *client, u16 addr, u8 *rxbuf,
			   int len)
{
	u8 buf[16] = { 0 };
	u8 confirm_buf[16] = { 0 };
	u8 retry = 0;

	while (retry++ < 3) {
		memset(buf, 0xAA, 16);
		buf[0] = (u8) (addr >> 8);
		buf[1] = (u8) (addr & 0xFF);
		gtp_i2c_read(client, buf, len + 2);

		memset(confirm_buf, 0xAB, 16);
		confirm_buf[0] = (u8) (addr >> 8);
		confirm_buf[1] = (u8) (addr & 0xFF);
		gtp_i2c_read(client, confirm_buf, len + 2);

		if (!memcmp(buf, confirm_buf, len + 2)) {
			memcpy(rxbuf, confirm_buf + 2, len);
			return SUCCESS;
		}
	}
	GTP_ERROR("i2c read 0x%04X, %d bytes, double check failed!", addr, len);
	return FAIL;
}

s32 gtp_send_cfg(struct i2c_client *client)
{
	s32 ret = 0;
#if GTP_DRIVER_SEND_CFG
	s32 retry = 0;

	for (retry = 0; retry < 5; retry++) {
#ifdef GTP_CHARGER_DETECT

		if (gtp_charger_mode == 1) {
			GTP_DEBUG("Write charger config");
			ret =
			    gtp_i2c_write(client, config_charger,
					  GTP_CONFIG_MAX_LENGTH +
					  GTP_ADDR_LENGTH);
		} else {
			GTP_DEBUG("Write normal config");
			ret =
			    gtp_i2c_write(client, config,
					  GTP_CONFIG_MAX_LENGTH +
					  GTP_ADDR_LENGTH);
		}

#else
		ret =
		    gtp_i2c_write(client, config,
				  GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH);
#endif

		if (ret > 0)
			break;
	}

#endif

	return ret;
}

s32 gtp_read_version(struct i2c_client *client, u16 *version)
{
	s32 ret = -1;
	s32 i;
	u8 buf[8] = { GTP_REG_VERSION >> 8, GTP_REG_VERSION & 0xff };

	GTP_DEBUG_FUNC();

	ret = gtp_i2c_read(client, buf, sizeof(buf));

	if (ret < 0)
		GTP_ERROR("GTP read version failed");
	return ret;

	if (version)
		*version = (buf[7] << 8) | buf[6];

	tpd_info.vid = (buf[7] << 8) | buf[6];
	tpd_info.pid = 0x00;

	/* for gt9xx series */
	for (i = 0; i < 3; i++) {
		if (buf[i + 2] < 0x30)
			break;
		tpd_info.pid |= ((buf[i + 2] - 0x30) << ((2 - i) * 4));
	}

	GTP_INFO("IC VERSION:%c%c%c_%02x%02x", buf[2], buf[3], buf[4], buf[7],
		 buf[6]);

	return ret;
}

static s32 gtp_init_panel(struct i2c_client *client)
{
	s32 ret = 0;

#if GTP_DRIVER_SEND_CFG
	s32 i;
	u8 check_sum = 0;
	u8 rd_cfg_buf[16];

	u8 cfg_info_group1[] = CTP_CFG_GROUP1;
	u8 cfg_info_group2[] = CTP_CFG_GROUP2;
	u8 cfg_info_group3[] = CTP_CFG_GROUP3;
	u8 *send_cfg_buf[3] = {cfg_info_group1, cfg_info_group2, cfg_info_group3};
#ifdef GTP_CHARGER_DETECT
	u8 cfg_info_group1_charger[] = CTP_CFG_GROUP1_CHARGER;
	u8 cfg_info_group2_charger[] = CTP_CFG_GROUP2_CHARGER;
	u8 cfg_info_group3_charger[] = CTP_CFG_GROUP3_CHARGER;
	u8 *send_cfg_buf_charger[3] = {cfg_info_group1_charger, cfg_info_group2_charger,
		cfg_info_group3_charger
	};
#endif
	u8 cfg_info_len[3] = {sizeof(cfg_info_group1) / sizeof(cfg_info_group1[0]),
		sizeof(cfg_info_group2) / sizeof(cfg_info_group2[0]),
		sizeof(cfg_info_group3) / sizeof(cfg_info_group3[0])
	};

	for (i = 0; i < 3; i++) {
		if (cfg_info_len[i] > cfg_len)
			cfg_len = cfg_info_len[i];
	}

	GTP_DEBUG("len1=%d,len2=%d,len3=%d,get_len=%d", cfg_info_len[0],
		  cfg_info_len[1], cfg_info_len[2], cfg_len);

	if ((!cfg_info_len[1]) && (!cfg_info_len[2])) {
		rd_cfg_buf[GTP_ADDR_LENGTH] = 0;
	} else {
		rd_cfg_buf[0] = GTP_REG_SENSOR_ID >> 8;
		rd_cfg_buf[1] = GTP_REG_SENSOR_ID & 0xff;
		ret = gtp_i2c_read(client, rd_cfg_buf, 3);

		if (ret < 0) {
			GTP_ERROR
			    ("Read SENSOR ID failed,default use group1 config!");
			rd_cfg_buf[GTP_ADDR_LENGTH] = 0;
			goto out;
		}

		rd_cfg_buf[GTP_ADDR_LENGTH] &= 0x03;
	}

	GTP_INFO("SENSOR ID:%d", rd_cfg_buf[GTP_ADDR_LENGTH]);
	memset(&config[GTP_ADDR_LENGTH], 0, GTP_CONFIG_MAX_LENGTH);
	memcpy(&config[GTP_ADDR_LENGTH],
	       send_cfg_buf[rd_cfg_buf[GTP_ADDR_LENGTH]], cfg_len);
#ifdef GTP_CHARGER_DETECT
	memset(&config_charger[GTP_ADDR_LENGTH], 0, GTP_CONFIG_MAX_LENGTH);
	memcpy(&config_charger[GTP_ADDR_LENGTH],
	       send_cfg_buf_charger[rd_cfg_buf[GTP_ADDR_LENGTH]], cfg_len);
#endif

#if GTP_CUSTOM_CFG
	config[RESOLUTION_LOC] = (u8) tpd_dts_data.tpd_resolution[0];
	config[RESOLUTION_LOC + 1] = (u8) (tpd_dts_data.tpd_resolution[0] >> 8);
	config[RESOLUTION_LOC + 2] = (u8) tpd_dts_data.tpd_resolution[1];
	config[RESOLUTION_LOC + 3] = (u8) (tpd_dts_data.tpd_resolution[1] >> 8);

	if (GTP_INT_TRIGGER == 0)
		config[TRIGGER_LOC] &= 0xfe;
	else if (GTP_INT_TRIGGER == 1)
		config[TRIGGER_LOC] |= 0x01;
#endif /* endif GTP_CUSTOM_CFG */

	check_sum = 0;

	for (i = GTP_ADDR_LENGTH; i < cfg_len; i++)
		check_sum += config[i];

	config[cfg_len] = (~check_sum) + 1;
#ifdef GTP_CHARGER_DETECT
	check_sum = 0;

	for (i = GTP_ADDR_LENGTH; i < cfg_len; i++)
		check_sum += config_charger[i];

	config_charger[cfg_len] = (~check_sum) + 1;
#endif
#else /* else DRIVER NEED NOT SEND CONFIG */

	if (cfg_len == 0)
		cfg_len = GTP_CONFIG_MAX_LENGTH;

	ret = gtp_i2c_read(client, config, cfg_len + GTP_ADDR_LENGTH);

	if (ret < 0) {
		GTP_ERROR
		    ("GTP read resolution & max_touch_num failed, use default value!");
		abs_x_max = tpd_dts_data.tpd_resolution[0];
		abs_y_max = tpd_dts_data.tpd_resolution[1];
		int_type = GTP_INT_TRIGGER;
		goto out;
	}
#endif /* endif GTP_DRIVER_SEND_CFG */

	abs_x_max = (config[RESOLUTION_LOC + 1] << 8) + config[RESOLUTION_LOC];
	abs_y_max =
	    (config[RESOLUTION_LOC + 3] << 8) + config[RESOLUTION_LOC + 2];
	int_type = (config[TRIGGER_LOC]) & 0x03;

	if ((!abs_x_max) || (!abs_y_max)) {
		GTP_ERROR
		    ("GTP resolution & max_touch_num invalid, use default value!");
		abs_x_max = tpd_dts_data.tpd_resolution[0];
		abs_y_max = tpd_dts_data.tpd_resolution[1];
	}
#if GTP_COMPATIBLE_MODE
	if (CHIP_TYPE_GT9F == gtp_chip_type) {
		u8 have_key = 0;

		if (is_950) {
			driver_num =
			    config[GTP_REG_MATRIX_DRVNUM - GTP_REG_CONFIG_DATA +
				   2];
			sensor_num =
			    config[GTP_REG_MATRIX_SENNUM - GTP_REG_CONFIG_DATA +
				   2];
		} else {
			driver_num =
			    (config[CFG_LOC_DRVA_NUM] & 0x1F) +
			    (config[CFG_LOC_DRVB_NUM] & 0x1F);
			sensor_num =
			    (config[CFG_LOC_SENS_NUM] & 0x0F) +
			    ((config[CFG_LOC_SENS_NUM] >> 4) & 0x0F);
		}

		have_key = config[GTP_REG_HAVE_KEY - GTP_REG_CONFIG_DATA + 2] & 0x01;	/* have key or not */
		if (1 == have_key)
			driver_num--;

		GTP_DEBUG
		    ("Driver * Sensor: %d * %d(Key: %d), X_MAX = %d, Y_MAX = %d, TRIGGER = 0x%02x",
		     driver_num, sensor_num, have_key, abs_x_max, abs_y_max,
		     int_type);
	} else
#endif
	{
		ret = gtp_send_cfg(client);

		if (ret < 0) {
			GTP_ERROR("Send config error.");
			goto out;
		}

		GTP_DEBUG("X_MAX = %d,Y_MAX = %d,TRIGGER = 0x%02x", abs_x_max,
			  abs_y_max, int_type);
	}
	msleep(20);
out:
	return ret;
}

static s8 gtp_i2c_test(struct i2c_client *client)
{

	u8 retry = 0;
	s8 ret = -1;
	u32 hw_info = 0;

	GTP_DEBUG_FUNC();

	while (retry++ < 5) {
		ret = i2c_read_bytes(client, GTP_REG_HW_INFO, (u8 *)&hw_info, sizeof(hw_info));
		if ((!ret) && (hw_info == 0x00900600))
			return ret;

		GTP_ERROR("GTP_REG_HW_INFO : %08X", hw_info);
		GTP_ERROR("GTP i2c test failed time %d.", retry);
		msleep(20);
	}

	return -1;
}

void gtp_int_sync(u32 ms)
{
	GTP_DEBUG("There is pull up resisitor attached on the INT pin~!");
	tpd_gpio_output(GTP_INT_PORT, 0);
	msleep(ms);
	tpd_gpio_as_int(GTP_INT_PORT);
}

void gtp_reset_guitar(struct i2c_client *client, s32 ms)
{
	GTP_INFO("GTP RESET!");
	tpd_gpio_output(GTP_RST_PORT, 0);
	msleep(ms);
	tpd_gpio_output(GTP_INT_PORT, client->addr == 0x14);
	msleep(20);
	tpd_gpio_output(GTP_RST_PORT, 1);
	msleep(20);

#if GTP_COMPATIBLE_MODE
	if (CHIP_TYPE_GT9F == gtp_chip_type)
		return;
#endif

	gtp_int_sync(50);
}

static int tpd_power_on(struct i2c_client *client)
{
	int ret = 0;
	int reset_count = 0;

reset_proc:
	tpd_gpio_output(GTP_INT_PORT, 0);
	tpd_gpio_output(GTP_RST_PORT, 0);
	msleep(20);
	/* power on, need confirm with SA */
#ifdef TPD_POWER_SOURCE_CUSTOM
	ret = regulator_set_voltage(tpd->reg, 3300000, 3300000);	/* set 2.8v */
	if (ret)
		GTP_DEBUG("regulator_set_voltage() failed!\n");
	ret = regulator_enable(tpd->reg);	/* enable regulator */
	if (ret)
		GTP_DEBUG("regulator_enable() failed!\n");
#else
	hwPowerOn(MT65XX_POWER_LDO_VGP2, VOL_3300, "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
	hwPowerOn(TPD_POWER_SOURCE_1800, VOL_1800, "TP");
#endif

	gtp_reset_guitar(client, 20);

#if GTP_COMPATIBLE_MODE
	gtp_get_chip_type(client);

	if (CHIP_TYPE_GT9F == gtp_chip_type) {
		ret = gup_fw_download_proc(NULL, GTP_FL_FW_BURN);

		if (FAIL == ret) {
			GTP_ERROR("[tpd_power_on]Download fw failed.");
			if (reset_count++ < TPD_MAX_RESET_COUNT)
				goto reset_proc;
			else
				return ret;
		}

		ret = gtp_fw_startup(client);
		if (FAIL == ret) {
			GTP_ERROR("[tpd_power_on]Startup fw failed.");
			if (reset_count++ < TPD_MAX_RESET_COUNT)
				goto reset_proc;
			else
				return ret;
		}
	} else
#endif
	{
		ret = gtp_i2c_test(client);

		if (ret < 0) {
			GTP_ERROR("I2C communication ERROR!");

			if (reset_count < TPD_MAX_RESET_COUNT) {
				reset_count++;
				goto reset_proc;
			} else {
				goto out;
			}
		}
	}
#if GTP_FW_DOWNLOAD
	ret = gup_init_fw_proc(client);

	if (ret < 0)
		GTP_ERROR("Create fw download thread error.");
#endif
out:
	return ret;
}

/* **************** For GT9XXF Start ********************/
#if GTP_COMPATIBLE_MODE
void gtp_get_chip_type(struct i2c_client *client)
{
	u8 opr_buf[10] = { 0x00 };
	s32 ret = 0;

	msleep(20);

	ret = gtp_i2c_read_dbl_check(client, GTP_REG_CHIP_TYPE, opr_buf, 10);

	if (FAIL == ret) {
		GTP_ERROR
		    ("Failed to get chip-type, set chip type default: GOODIX_GT9");
		gtp_chip_type = CHIP_TYPE_GT9;
		return;
	}

	if (!memcmp(opr_buf, "GOODIX_GT9", 10))
		gtp_chip_type = CHIP_TYPE_GT9;
	else
		gtp_chip_type = CHIP_TYPE_GT9F;
	GTP_INFO("Chip Type: %s",
		 (gtp_chip_type ==
		  CHIP_TYPE_GT9) ? "GOODIX_GT9" : "GOODIX_GT9F");
}

static u8 gtp_bak_ref_proc(struct i2c_client *client, u8 mode)
{
	s32 i = 0;
	s32 j = 0;
	s32 ret = 0;
	struct file *flp = NULL;
	u8 *refp = NULL;
	u32 ref_len = 0;
	u32 ref_seg_len = 0;
	s32 ref_grps = 0;
	s32 ref_chksum = 0;
	u16 tmp = 0;

	GTP_DEBUG("[gtp_bak_ref_proc]Driver:%d,Sensor:%d.", driver_num,
		  sensor_num);

	/* check file-system mounted */
	GTP_DEBUG("[gtp_bak_ref_proc]Waiting for FS %d", gtp_ref_retries);
	if (gup_check_fs_mounted("/data") == FAIL) {
		GTP_DEBUG("[gtp_bak_ref_proc]/data not mounted");
		if (gtp_ref_retries++ < GTP_CHK_FS_MNT_MAX)
			return FAIL;
	} else {
		GTP_DEBUG("[gtp_bak_ref_proc]/data mounted !!!!");
	}

	if (is_950) {
		ref_seg_len = (driver_num * (sensor_num - 1) + 2) * 2;
		ref_grps = 6;
		ref_len = ref_seg_len * 6;	/* for GT950, backup-reference for six segments */
	} else {
		ref_len = driver_num * (sensor_num - 2) * 2 + 4;
		ref_seg_len = ref_len;
		ref_grps = 1;
	}

	refp = kzalloc(ref_len, GFP_KERNEL);
	if (refp == NULL) {
		GTP_ERROR
		    ("[gtp_bak_ref_proc]Alloc memory for ref failed.use default ref");
		return FAIL;
	}
	memset(refp, 0, ref_len);
	if (gtp_ref_retries >= GTP_CHK_FS_MNT_MAX) {
		for (j = 0; j < ref_grps; ++j)
			refp[ref_seg_len + j * ref_seg_len - 1] = 0x01;
		ret = i2c_write_bytes(client, 0x99D0, refp, ref_len);
		if (-1 == ret) {
			GTP_ERROR("[gtp_bak_ref_proc]Write ref i2c error.");
			ret = FAIL;
		}

		GTP_ERROR
		    ("[gtp_bak_ref_proc]Bak file or path is not exist,send default ref.");
		ret = SUCCESS;
		goto exit_ref_proc;
	}

	/* get ref file data */
	flp = filp_open(GTP_BAK_REF_PATH, O_RDWR | O_CREAT, 0660);
	if (IS_ERR(flp)) {
		GTP_ERROR
		    ("[gtp_bak_ref_proc]Ref File not found!Creat ref file.");
		/* flp->f_op->llseek(flp, 0, SEEK_SET); */
		/* flp->f_op->write(flp, (char *)refp, ref_len, &flp->f_pos); */
		gtp_ref_retries++;
		ret = FAIL;
		goto exit_ref_proc;
	} else if (GTP_BAK_REF_SEND == mode) {
		flp->f_op->llseek(flp, 0, SEEK_SET);
		ret = flp->f_op->read(flp, (char *)refp, ref_len, &flp->f_pos);
		if (ret < 0) {
			GTP_ERROR("[gtp_bak_ref_proc]Read ref file failed.");
			memset(refp, 0, ref_len);
		}
	}

	if (GTP_BAK_REF_STORE == mode) {
		ret = i2c_read_bytes(client, 0x99D0, refp, ref_len);
		if (-1 == ret) {
			GTP_ERROR("[gtp_bak_ref_proc]Read ref i2c error.");
			ret = FAIL;
			goto exit_ref_proc;
		}
		flp->f_op->llseek(flp, 0, SEEK_SET);
		flp->f_op->write(flp, (char *)refp, ref_len, &flp->f_pos);
	} else {
		/* checksum ref file */
		for (j = 0; j < ref_grps; ++j) {
			ref_chksum = 0;
			for (i = 0; i < ref_seg_len - 2; i += 2) {
				ref_chksum +=
				    ((refp[i + j * ref_seg_len] << 8) +
				     refp[i + 1 + j * ref_seg_len]);
			}

			GTP_DEBUG("[gtp_bak_ref_proc]Calc ref chksum:0x%04X",
				  ref_chksum & 0xFF);
			tmp =
			    ref_chksum +
			    (refp[ref_seg_len + j * ref_seg_len - 2] << 8) +
			    refp[ref_seg_len + j * ref_seg_len - 1];
			if (1 != tmp) {
				GTP_DEBUG
				    ("[gtp_bak_ref_proc]Ref file chksum error,use default ref");
				memset(&refp[j * ref_seg_len], 0, ref_seg_len);
				refp[ref_seg_len - 1 + j * ref_seg_len] = 0x01;
			} else {
				GTP_DEBUG
				    ("[gtp_bak_ref_proc]Ref file chksum success.");
			}

		}

		ret = i2c_write_bytes(client, 0x99D0, refp, ref_len);
		if (-1 == ret) {
			GTP_ERROR("[gtp_bak_ref_proc]Write ref i2c error.");
			ret = FAIL;
			goto exit_ref_proc;
		}
	}

	ret = SUCCESS;

exit_ref_proc:
	kfree(refp);
	if (!IS_ERR(flp))
		filp_close(flp, NULL);
	return ret;
}

u8 gtp_fw_startup(struct i2c_client *client)
{
	u8 wr_buf[4];

	/* init sw WDT */
	wr_buf[0] = 0xAA;
	wr_buf[1] = 0xAA;
	i2c_write_bytes(client, 0x8040, wr_buf, 2);

	/* release SS51 & DSP */
	wr_buf[0] = 0x00;
	i2c_write_bytes(client, 0x4180, wr_buf, 1);

	/* int sync */
	gtp_int_sync(20);

	/* check fw run status */
	i2c_read_bytes(client, 0x8041, wr_buf, 1);
	if (0xAA == wr_buf[0]) {
		GTP_ERROR("IC works abnormally,startup failed.");
		return FAIL;
	}
	GTP_DEBUG("IC works normally,Startup success.");
	wr_buf[0] = 0xAA;
	wr_buf[1] = 0xAA;
	i2c_write_bytes(client, 0x8040, wr_buf, 2);
	return SUCCESS;
}

static void gtp_recovery_reset(struct i2c_client *client)
{
#if GTP_ESD_PROTECT
	gtp_esd_switch(client, SWITCH_OFF);
#endif
	force_reset_guitar();
#if GTP_ESD_PROTECT
	gtp_esd_switch(client, SWITCH_ON);
#endif
}

static u8 gtp_check_clk_legality(void)
{
	u8 i = 0;
	u8 clk_chksum = gtp_clk_buf[5];

	for (i = 0; i < 5; i++) {
		if ((gtp_clk_buf[i] < 50) || (gtp_clk_buf[i] > 120) ||
		    (gtp_clk_buf[i] != gtp_clk_buf[0])) {
			break;
		}
		clk_chksum += gtp_clk_buf[i];
	}

	if ((i == 5) && (clk_chksum == 0)) {
		GTP_INFO("Clk ram legality check success");
		return SUCCESS;
	}
	GTP_ERROR("main clock freq in clock buf is wrong");
	return FAIL;
}

static u8 gtp_main_clk_proc(struct i2c_client *client)
{
	s32 ret = 0;
	u8 i = 0;
	u8 clk_cal_result = 0;
	u8 clk_chksum = 0;
	struct file *flp = NULL;

	/* check clk legality */
	ret = gtp_check_clk_legality();
	if (SUCCESS == ret)
		goto send_main_clk;

	GTP_DEBUG("[gtp_main_clk_proc]Waiting for FS %d", gtp_ref_retries);
	if (gup_check_fs_mounted("/data") == FAIL) {
		GTP_DEBUG("[gtp_main_clk_proc]/data not mounted");
		if (gtp_clk_retries++ < GTP_CHK_FS_MNT_MAX)
			return FAIL;
		GTP_ERROR
		    ("[gtp_main_clk_proc]Wait for file system timeout,need cal clk");
	} else {
		GTP_DEBUG("[gtp_main_clk_proc]/data mounted !!!!");
		flp = filp_open(GTP_MAIN_CLK_PATH, O_RDWR | O_CREAT, 0660);
		if (!IS_ERR(flp)) {
			flp->f_op->llseek(flp, 0, SEEK_SET);
			ret =
			    flp->f_op->read(flp, (char *)gtp_clk_buf, 6,
					    &flp->f_pos);
			if (ret > 0) {
				ret = gtp_check_clk_legality();
				if (SUCCESS == ret) {
					GTP_DEBUG
					    ("[gtp_main_clk_proc]Open & read & check clk file success.");
					goto send_main_clk;
				}
			}
		}
		GTP_ERROR
		    ("[gtp_main_clk_proc]Check clk file failed,need cal clk");
	}

	/* cal clk */
#if GTP_ESD_PROTECT
	gtp_esd_switch(client, SWITCH_OFF);
#endif
	clk_cal_result = gup_clk_calibration();
	force_reset_guitar();
	GTP_DEBUG("&&&&&&&&&&clk cal result:%d", clk_cal_result);

#if GTP_ESD_PROTECT
	gtp_esd_switch(client, SWITCH_ON);
#endif

	if (clk_cal_result < 50 || clk_cal_result > 120) {
		GTP_ERROR("[gtp_main_clk_proc]cal clk result is illegitimate");
		ret = FAIL;
		goto exit_clk_proc;
	}

	for (i = 0; i < 5; i++) {
		gtp_clk_buf[i] = clk_cal_result;
		clk_chksum += gtp_clk_buf[i];
	}
	gtp_clk_buf[5] = 0 - clk_chksum;

	if (IS_ERR(flp)) {
		flp = filp_open(GTP_MAIN_CLK_PATH, O_RDWR | O_CREAT, 0660);
	} else {
		flp->f_op->llseek(flp, 0, SEEK_SET);
		flp->f_op->write(flp, (char *)gtp_clk_buf, 6, &flp->f_pos);
	}

send_main_clk:

	ret = i2c_write_bytes(client, 0x8020, gtp_clk_buf, 6);
	if (-1 == ret) {
		GTP_ERROR("[gtp_main_clk_proc]send main clk i2c error!");
		ret = FAIL;
		goto exit_clk_proc;
	}
	if (flp && !IS_ERR(flp))
		filp_close(flp, NULL);

	ret = SUCCESS;
	return SUCCESS;
exit_clk_proc:

	if (flp && !IS_ERR(flp)) {	/* RMT add */
		filp_close(flp, NULL);
	}
	return ret;
}

#endif
/* ************* For GT9XXF End **********************/
#ifdef MTK_CTP_RESET_CONFIG
static int tpd_clear_config(void *unused)
{
	int ret = 0, check_sum = 0;
	u8 temp_data = 0, i = 0;
	u8 config_1st[GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH]
	= { GTP_REG_CONFIG_DATA >> 8, GTP_REG_CONFIG_DATA & 0xff };

	GTP_INFO("Clear Config Begin......");
	msleep(10000);		/* wait main thread to be completed */

	ret =
	    i2c_read_bytes(i2c_client_point, GTP_REG_CONFIG_DATA, &temp_data,
			   1);
	if (ret < 0) {
		GTP_ERROR("GTP read config failed!");
		return -1;
	}

	GTP_INFO("IC config version: 0x%x; Driver config version: 0x%x",
		 temp_data, config[GTP_ADDR_LENGTH]);
	if ((temp_data < (u8) 0x5A) && (temp_data > config[GTP_ADDR_LENGTH])) {
		memset(&config_1st[GTP_ADDR_LENGTH], 0, GTP_CONFIG_MAX_LENGTH);
		memcpy(&config_1st[GTP_ADDR_LENGTH], &config[GTP_ADDR_LENGTH],
		       cfg_len);
		config_1st[GTP_ADDR_LENGTH] = 0;
		check_sum = 0;

		for (i = GTP_ADDR_LENGTH; i < cfg_len; i++)
			check_sum += config_1st[i];

		config_1st[cfg_len] = (~check_sum) + 1;
		ret =
		    gtp_i2c_write(i2c_client_point, config_1st,
				  GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH);
		if (ret < 0)
			GTP_ERROR("GTP write 00 config failed!");
		else
			GTP_INFO("Force clear cfg done");
	} else {
		GTP_INFO("No need clear cfg");
	}
	return 0;
}
#endif

static const struct file_operations gt_upgrade_proc_fops = {
	.write = gt91xx_config_write_proc,
	.read = gt91xx_config_read_proc
};

static s32 tpd_i2c_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	s32 err = 0;
	s32 ret = 0;
	struct device_node *node;
	u16 version_info;
	struct task_struct *thread = NULL;
#if 0				/* GTP_HAVE_TOUCH_KEY */
	s32 idx = 0;
#endif
#ifdef TPD_PROXIMITY
	struct hwmsen_object obj_ps;
#endif

	i2c_client_point = client;
	ret = tpd_power_on(client);

	if (ret < 0) {
		GTP_ERROR("I2C communication ERROR!");
		goto out;
	}
#ifdef MTK_CTP_RESET_CONFIG
	thread = kthread_run(tpd_clear_config, 0, "mtk-tpd-clear-config");
	if (IS_ERR(thread)) {
		err = PTR_ERR(thread);
		GTP_INFO(TPD_DEVICE
			 " failed to create kernel thread for clearing config: %d",
			 err);
	}
	thread = NULL;
#endif

#if GTP_AUTO_UPDATE
	ret = gup_init_update_proc(client);

	if (ret < 0) {
		GTP_ERROR("Create update thread error.");
		goto out;
	}
#endif

#ifdef VELOCITY_CUSTOM
	tpd_v_magnify_x = TPD_VELOCITY_CUSTOM_X;
	tpd_v_magnify_y = TPD_VELOCITY_CUSTOM_Y;

#endif

	ret = gtp_read_version(client, &version_info);

	if (ret < 0) {
		GTP_ERROR("Read version failed.");
		goto out;
	}
	GTP_INFO(TPD_DEVICE " read gtp version: %d", version_info);

	ret = gtp_init_panel(client);

	if (ret < 0) {
		GTP_ERROR("GTP init panel failed.");
		goto out;
	}

	gt91xx_config_proc =
	    proc_create(GT91XX_CONFIG_PROC_FILE, 0660, NULL,
			&gt_upgrade_proc_fops);
	if (gt91xx_config_proc == NULL)
		GTP_ERROR("create_proc_entry %s failed\n",
			  GT91XX_CONFIG_PROC_FILE);

#if GTP_CREATE_WR_NODE
	init_wr_node(client);
#endif

	thread = kthread_run(touch_event_handler, 0, TPD_DEVICE);

	if (IS_ERR(thread)) {
		err = PTR_ERR(thread);
		GTP_INFO(TPD_DEVICE " failed to create kernel thread: %d", err);
		goto out;
	}
	msleep(50);
	node = of_find_matching_node(NULL, touch_of_match);
	if (node) {
		touch_irq = irq_of_parse_and_map(node, 0);
		ret = request_irq(touch_irq,
				  (irq_handler_t) tpd_eint_interrupt_handler,
				  !int_type ? IRQF_TRIGGER_RISING :
				  IRQF_TRIGGER_FALLING, "TOUCH_PANEL-eint",
				  NULL);
		if (ret > 0) {
			ret = -1;
			GTP_ERROR("tpd request_irq IRQ LINE NOT AVAILABLE!.");
		}
	} else {
		GTP_ERROR("no irq node!!");
	}
	/* mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM); */
	enable_irq(touch_irq);

#ifdef TPD_PROXIMITY
	/* obj_ps.self = cm3623_obj; */
	obj_ps.polling = 0;	/* 0--interrupt mode;1--polling mode; */
	obj_ps.sensor_operate = tpd_ps_operate;

	err = hwmsen_attach(ID_PROXIMITY, &obj_ps);
	if (err)
		GTP_ERROR("hwmsen attach fail, return:%d.", err);
#endif

#if GTP_ESD_PROTECT
	INIT_DELAYED_WORK(&gtp_esd_check_work, gtp_esd_check_func);
	gtp_esd_check_workqueue = create_workqueue("gtp_esd_check");
	queue_delayed_work(gtp_esd_check_workqueue, &gtp_esd_check_work,
			   TPD_ESD_CHECK_CIRCLE);
#endif

#ifdef GTP_CHARGER_DETECT
	INIT_DELAYED_WORK(&gtp_charger_check_work, gtp_charger_check_func);
	gtp_charger_check_workqueue = create_workqueue("gtp_charger_check");
	queue_delayed_work(gtp_charger_check_workqueue, &gtp_charger_check_work,
			   TPD_CHARGER_CHECK_CIRCLE);
#endif
	tpd_load_status = 1;

	return 0;
out:
	return -1;
}

static irqreturn_t tpd_eint_interrupt_handler(void)
{
	TPD_DEBUG_PRINT_INT;
	tpd_flag = 1;
	wake_up_interruptible(&waiter);
	return IRQ_HANDLED;
}

static int tpd_i2c_remove(struct i2c_client *client)
{
#if GTP_CREATE_WR_NODE
	uninit_wr_node();
#endif

#if GTP_ESD_PROTECT
	destroy_workqueue(gtp_esd_check_workqueue);
#endif

#if GTP_ESD_PROTECT
	destroy_workqueue(gtp_charger_check_workqueue);
#endif
	return 0;
}

#ifdef GTP_CHARGER_DETECT
static void gtp_charger_check_func(struct work_struct *work)
{
	int cur_charger_state;

	cur_charger_state = upmu_get_pchr_chrdet();

	GTP_DEBUG("Charger mode = %d", cur_charger_state);

	if (gtp_charger_mode != cur_charger_state) {
		GTP_DEBUG("Charger state change detected~!");
		GTP_DEBUG("Charger mode = %d", cur_charger_state);
		gtp_charger_mode = cur_charger_state;
		gtp_send_cfg(i2c_client_point);
	}

	if (!tpd_halt) {
		queue_delayed_work(gtp_charger_check_workqueue,
				   &gtp_charger_check_work,
				   TPD_CHARGER_CHECK_CIRCLE);
	}
}
#endif

#if (GTP_ESD_PROTECT || GTP_COMPATIBLE_MODE)
static void force_reset_guitar(void)
{
	s32 i;
	s32 ret;

	GTP_INFO("force_reset_guitar");

	/* Power off TP */
#ifdef TPD_POWER_SOURCE_CUSTOM
	ret = regulator_disable(tpd->reg);
	if (ret)
		GTP_DEBUG("regulator_disable() failed!\n");
#else
	hwPowerDown(MT65XX_POWER_LDO_VGP2, "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
	hwPowerDown(TPD_POWER_SOURCE_1800, "TP");
#endif
	msleep(30);
	/* Power on TP */
#ifdef TPD_POWER_SOURCE_CUSTOM
	ret = regulator_set_voltage(tpd->reg, 3300000, 3300000);
	if (ret)
		GTP_DEBUG("regulator_set_voltage() failed!\n");
	ret = regulator_enable(tpd->reg);	/* enable regulator */
	if (ret)
		GTP_DEBUG("regulator_enable() failed!\n");
#else
	hwPowerOn(MT65XX_POWER_LDO_VGP2, VOL_3300, "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
	hwPowerOn(TPD_POWER_SOURCE_1800, VOL_1800, "TP");
#endif
	msleep(30);
	for (i = 0; i < 5; i++) {
		/* Reset Guitar */
		gtp_reset_guitar(i2c_client_point, 20);

#if GTP_COMPATIBLE_MODE
		if (CHIP_TYPE_GT9F == gtp_chip_type) {
			/* check code ram */
			ret = gup_fw_download_proc(NULL, GTP_FL_ESD_RECOVERY);
			if (FAIL == ret) {
				GTP_ERROR
				    ("[force_reset_guitar]Check & repair fw failed.");
				continue;
			}
			tpd_halt = 1;
			/* startup fw */
			ret = gtp_fw_startup(i2c_client_point);
			if (FAIL == ret) {
				GTP_ERROR
				    ("[force_reset_guitar]Startup fw failed.");
				continue;
			}
			tpd_halt = 0;
			break;
		}
#endif
		ret = gtp_send_cfg(i2c_client_point);

		if (ret < 0)
			continue;
		break;
	}

}

#endif

#if GTP_ESD_PROTECT
static void gtp_esd_check_func(struct work_struct *work)
{
	int i;
	int ret = -1;
	u8 test[3] = { GTP_REG_CONFIG_DATA >> 8, GTP_REG_CONFIG_DATA & 0xff };

	if (tpd_halt)
		return;

	for (i = 0; i < 3; i++) {
		ret = gtp_i2c_read(i2c_client_point, test, 3);

		if (ret > 0)
			break;
	}

	if (i >= 3) {
#if GTP_COMPATIBLE_MODE
		if (1 == rqst_processing) {
			GTP_INFO("Request Processing, no reset guitar...");
		} else
#endif
		{
#if GTP_COMPATIBLE_MODE
			esd_resetting = 1;
#endif

			force_reset_guitar();

#if GTP_COMPATIBLE_MODE
			esd_resetting = 0;
#endif
		}
	}

	if (!tpd_halt) {
		queue_delayed_work(gtp_esd_check_workqueue, &gtp_esd_check_work,
				   TPD_ESD_CHECK_CIRCLE);
	}
}
#endif
static int tpd_history_x;
static int tpd_history_y;
static void tpd_down(s32 x, s32 y, s32 size, s32 id)
{
	if ((!size) && (!id)) {
		input_report_abs(tpd->dev, ABS_MT_PRESSURE, 100);
		input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 100);
	} else {
		input_report_abs(tpd->dev, ABS_MT_PRESSURE, size);
		input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, size);
		/* track id Start 0 */
		input_report_abs(tpd->dev, ABS_MT_TRACKING_ID, id);
	}

	input_report_key(tpd->dev, BTN_TOUCH, 1);
	input_report_abs(tpd->dev, ABS_MT_POSITION_X, x);
	input_report_abs(tpd->dev, ABS_MT_POSITION_Y, y);
	input_mt_sync(tpd->dev);
	TPD_DEBUG_SET_TIME;
	TPD_EM_PRINT(x, y, x, y, id, 1);
	tpd_history_x = x;
	tpd_history_y = y;

	/*MMProfileLogEx(MMP_TouchPanelEvent, MMProfileFlagPulse, 1, x + y); */
	if (tpd_dts_data.use_tpd_button) {
		if (FACTORY_BOOT == get_boot_mode()
		    || RECOVERY_BOOT == get_boot_mode())
			tpd_button(x, y, 1);
	}
}

static void tpd_up(s32 x, s32 y, s32 id)
{
	/* input_report_abs(tpd->dev, ABS_MT_PRESSURE, 0); */
	input_report_key(tpd->dev, BTN_TOUCH, 0);
	/* input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 0); */
	input_mt_sync(tpd->dev);
	TPD_DEBUG_SET_TIME;
	TPD_EM_PRINT(tpd_history_x, tpd_history_y, tpd_history_x, tpd_history_y,
		     id, 0);
	tpd_history_x = 0;
	tpd_history_y = 0;

	/*MMProfileLogEx(MMP_TouchPanelEvent, MMProfileFlagPulse, 0, x + y); */
	if (tpd_dts_data.use_tpd_button) {
		if (FACTORY_BOOT == get_boot_mode()
		    || RECOVERY_BOOT == get_boot_mode())
			tpd_button(x, y, 0);
	}
}

/*Coordination mapping*/
static void tpd_calibrate_driver(int *x, int *y)
{
	int tx;

	GTP_DEBUG("Call tpd_calibrate of this driver ..\n");

	tx = ((tpd_def_calmat[0] * (*x)) + (tpd_def_calmat[1] * (*y)) +
	      (tpd_def_calmat[2])) >> 12;
	*y = ((tpd_def_calmat[3] * (*x)) + (tpd_def_calmat[4] * (*y)) +
	      (tpd_def_calmat[5])) >> 12;
	*x = tx;
}

static int touch_event_handler(void *unused)
{
	struct sched_param param = {.sched_priority = RTPM_PRIO_TPD};
	u8 end_cmd[3] = {GTP_READ_COOR_ADDR >> 8, GTP_READ_COOR_ADDR & 0xFF, 0};
	u8 point_data[2 + 1 + 8 * GTP_MAX_TOUCH + 1] = {GTP_READ_COOR_ADDR >> 8,
		GTP_READ_COOR_ADDR & 0xFF
	};
	u8 touch_num = 0;
	u8 finger = 0;
	static u8 pre_touch;
	static u8 pre_key;
	u8 key_value = 0;
	u8 *coor_data = NULL;
	s32 input_x = 0;
	s32 input_y = 0;
	s32 input_w = 0;
	s32 id = 0;
	s32 i = 0;
	s32 ret = -1;
#if GTP_COMPATIBLE_MODE
	u8 rqst_data[3] = {(u8)(GTP_REG_RQST >> 8), (u8) (GTP_REG_RQST & 0xFF), 0};
#endif
#ifdef TPD_PROXIMITY
	s32 err = 0;
	hwm_sensor_data sensor_data;
	u8 proximity_status;
#endif
#if GTP_CHANGE_X2Y
	s32 temp;
#endif

	sched_setscheduler(current, SCHED_RR, &param);

	do {
		set_current_state(TASK_INTERRUPTIBLE);
		if (tpd_eint_mode) {
			wait_event_interruptible(waiter, tpd_flag != 0);
			tpd_flag = 0;
		} else {
			msleep(tpd_polling_time);
		}
		set_current_state(TASK_RUNNING);

		mutex_lock(&i2c_access);

		if (tpd_halt) {
			mutex_unlock(&i2c_access);
			GTP_DEBUG("return for interrupt after suspend...  ");
			continue;
		}

		ret = gtp_i2c_read(i2c_client_point, point_data, 12);

		if (ret < 0) {
			GTP_ERROR("I2C transfer error. errno:%d ", ret);
			goto exit_work_func;
		}

		finger = point_data[GTP_ADDR_LENGTH];

#if GTP_COMPATIBLE_MODE
		if ((finger == 0x00) && (CHIP_TYPE_GT9F == gtp_chip_type)) {
			ret = gtp_i2c_read(i2c_client_point, rqst_data, 3);
			if (ret < 0) {
				GTP_ERROR("I2C transfer error. errno:%d\n ",
					  ret);
				goto exit_work_func;
			}
			switch (rqst_data[2] & 0x0F) {
			case GTP_RQST_BAK_REF:
				GTP_INFO("Request Ref.");
				ret =
				    gtp_bak_ref_proc(i2c_client_point,
						     GTP_BAK_REF_SEND);
				if (SUCCESS == ret) {
					GTP_INFO("Send ref success.");
					rqst_data[2] = GTP_RQST_RESPONDED;
					gtp_i2c_write(i2c_client_point,
						      rqst_data, 3);
				}
				goto exit_work_func;
			case GTP_RQST_CONFIG:
				GTP_INFO("Request Config.");
				ret = gtp_send_cfg(i2c_client_point);
				if (ret < 0) {
					GTP_ERROR("Send config error.");
				} else {
					GTP_INFO("Send config success.");
					rqst_data[2] = GTP_RQST_RESPONDED;
					gtp_i2c_write(i2c_client_point,
						      rqst_data, 3);
				}
				goto exit_work_func;
			case GTP_RQST_MAIN_CLOCK:
				GTP_INFO("Request main clock.");
				rqst_processing = 1;
				ret = gtp_main_clk_proc(i2c_client_point);
				if (SUCCESS == ret) {
					GTP_INFO("Send main clk success.");
					rqst_data[2] = GTP_RQST_RESPONDED;
					gtp_i2c_write(i2c_client_point,
						      rqst_data, 3);
					rqst_processing = 0;
				}
				goto exit_work_func;
			case GTP_RQST_RESET:
				GTP_INFO("Request Reset.");
				gtp_recovery_reset(i2c_client_point);
				goto exit_work_func;
			default:
				break;
			}
		}
#endif

		if ((finger & 0x80) == 0) {
			/* mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM); */
			/*enable_irq(touch_irq); */
			mutex_unlock(&i2c_access);
			GTP_ERROR("buffer not ready");
			continue;
		}
#ifdef TPD_PROXIMITY

		if (tpd_proximity_flag == 1) {
			proximity_status = point_data[GTP_ADDR_LENGTH];
			GTP_DEBUG("REG INDEX[0x814E]:0x%02X", proximity_status);

			if (proximity_status & 0x60) {	/* proximity or large touch detect,enable hwm_sensor. */
				tpd_proximity_detect = 0;
				/* sensor_data.values[0] = 0; */
			} else {
				tpd_proximity_detect = 1;
				/* sensor_data.values[0] = 1; */
			}

			/* get raw data */
			GTP_DEBUG(" ps change");
			GTP_DEBUG("PROXIMITY STATUS:0x%02X",
				  tpd_proximity_detect);
			/* map and store data to hwm_sensor_data */
			sensor_data.values[0] = tpd_get_ps_value();
			sensor_data.value_divide = 1;
			sensor_data.status = SENSOR_STATUS_ACCURACY_MEDIUM;
			/* report to the up-layer */
			ret =
			    hwmsen_get_interrupt_data(ID_PROXIMITY,
						      &sensor_data);

			if (ret)
				GTP_ERROR
				    ("Call hwmsen_get_interrupt_data fail = %d",
				     err);
		}
#endif

		touch_num = finger & 0x0f;

		if (touch_num > GTP_MAX_TOUCH) {
			GTP_ERROR("Bad number of fingers!");
			goto exit_work_func;
		}

		if (touch_num > 1) {
			u8 buf[8 * GTP_MAX_TOUCH] = {(GTP_READ_COOR_ADDR + 10) >> 8,
				(GTP_READ_COOR_ADDR + 10) & 0xff
			};

			ret = gtp_i2c_read(i2c_client_point, buf,
					 2 + 8 * (touch_num - 1));
			memcpy(&point_data[12], &buf[2], 8 * (touch_num - 1));
		}
#if GTP_HAVE_TOUCH_KEY
		key_value = point_data[3 + 8 * touch_num];

		if (key_value || pre_key) {
			for (i = 0; i < TPD_KEY_COUNT; i++) {
				/* input_report_key(tpd->dev, touch_key_array[i], key_value & (0x01 << i)); */
				if (key_value & (0x01 << i)) {	/* key=1 menu ;key=2 home; key =4 back; */
					input_x =
					    touch_key_point_maping_array[i].
					    point_x;
					input_y =
					    touch_key_point_maping_array[i].
					    point_y;
					GTP_DEBUG("button =%d %d", input_x,
						  input_y);

					tpd_down(input_x, input_y, 0, 0);
				}
			}

			if ((pre_key != 0) && (key_value == 0))
				tpd_up(0, 0, 0);

			touch_num = 0;
			pre_touch = 0;
		}
#endif
		pre_key = key_value;

		GTP_DEBUG("pre_touch:%02x, finger:%02x.", pre_touch, finger);

		if (touch_num) {
			for (i = 0; i < touch_num; i++) {
				coor_data = &point_data[i * 8 + 3];

				id = coor_data[0] & 0x0F;
				input_x = coor_data[1] | coor_data[2] << 8;
				input_y = coor_data[3] | coor_data[4] << 8;
				input_w = coor_data[5] | coor_data[6] << 8;

				GTP_DEBUG
				    ("Original touch point : [X:%04d, Y:%04d]",
				     input_x, input_y);

				input_x = TPD_WARP_X(abs_x_max, input_x);
				input_y = TPD_WARP_Y(abs_y_max, input_y);
				tpd_calibrate_driver(&input_x, &input_y);

				GTP_DEBUG
				    ("Touch point after calibration: [X:%04d, Y:%04d]",
				     input_x, input_y);

#if GTP_CHANGE_X2Y
				temp = input_x;
				input_x = input_y;
				input_y = temp;
#endif

				tpd_down(input_x, input_y, input_w, id);
			}
		} else if (pre_touch) {
			GTP_DEBUG("Touch Release!");
			tpd_up(0, 0, 0);
		} else {
			GTP_DEBUG("Additional Eint!");
		}
		pre_touch = touch_num;
		/* input_report_key(tpd->dev, BTN_TOUCH, (touch_num || key_value)); */

		if (tpd != NULL && tpd->dev != NULL)
			input_sync(tpd->dev);

exit_work_func:

		if (!gtp_rawdiff_mode) {
			ret = gtp_i2c_write(i2c_client_point, end_cmd, 3);

			if (ret < 0)
				GTP_INFO("I2C write end_cmd  error!");
		}
		/* mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM); */
		/* enable_irq(touch_irq); */
		mutex_unlock(&i2c_access);

	} while (!kthread_should_stop());

	return 0;
}

static int tpd_local_init(void)
{
#ifdef TPD_POWER_SOURCE_CUSTOM
	tpd->reg = regulator_get(tpd->tpd_dev, "vtouch");
	if (IS_ERR(tpd->reg))
		GTP_ERROR("regulator_get() failed!\n");
#endif

#if GTP_SUPPORT_I2C_DMA
	gpDMABuf_va =
	    (u8 *) dma_alloc_coherent(NULL, GTP_DMA_MAX_TRANSACTION_LENGTH,
				      &gpDMABuf_pa, GFP_KERNEL);
	if (!gpDMABuf_va)
		GTP_INFO("[Error] Allocate DMA I2C Buffer failed!\n");
#endif

	if (i2c_add_driver(&tpd_i2c_driver) != 0) {
		GTP_INFO("unable to add i2c driver.");
		return -1;
	}

	if (tpd_load_status == 0) {
		GTP_INFO("add error touch panel driver.");
		i2c_del_driver(&tpd_i2c_driver);
		return -1;
	}
	input_set_abs_params(tpd->dev, ABS_MT_TRACKING_ID, 0,
			     (GTP_MAX_TOUCH - 1), 0, 0);
	if (tpd_dts_data.use_tpd_button) {
		/*initialize tpd button data */
		tpd_button_setting(tpd_dts_data.tpd_key_num,
				   tpd_dts_data.tpd_key_local,
				   tpd_dts_data.tpd_key_dim_local);
	}
#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
	TPD_DO_WARP = 1;
	memcpy(tpd_wb_start, tpd_wb_start_local, TPD_WARP_CNT * 4);
	memcpy(tpd_wb_end, tpd_wb_start_local, TPD_WARP_CNT * 4);
#endif

#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
	/* memcpy(tpd_calmat, tpd_def_calmat_local, 8 * 4); */
	/* memcpy(tpd_def_calmat, tpd_def_calmat_local, 8 * 4); */
	if (FACTORY_BOOT == get_boot_mode()) {
		TPD_DEBUG("Factory mode is detected!\n");
		memcpy(tpd_calmat, tpd_def_calmat_local_factory, 8 * 4);
		memcpy(tpd_def_calmat, tpd_def_calmat_local_factory, 8 * 4);
	} else {
		TPD_DEBUG("Normal mode is detected!\n");
		memcpy(tpd_calmat, tpd_def_calmat_local_normal, 8 * 4);
		memcpy(tpd_def_calmat, tpd_def_calmat_local_normal, 8 * 4);
	}
#endif

	/* set vendor string */
	tpd->dev->id.vendor = 0x00;
	tpd->dev->id.product = tpd_info.pid;
	tpd->dev->id.version = tpd_info.vid;

	GTP_INFO("end %s, %d", __func__, __LINE__);
	tpd_type_cap = 1;

	return 0;
}

static s8 gtp_enter_sleep(struct i2c_client *client)
{
	s8 ret = -1;
#if !GTP_POWER_CTRL_SLEEP
	s8 retry = 0;
	u8 i2c_control_buf[3] = { (u8) (GTP_REG_SLEEP >> 8),
		(u8) GTP_REG_SLEEP, 5
	};

	tpd_gpio_output(GTP_INT_PORT, 0);
	msleep(20);

	while (retry++ < 5) {
		ret = gtp_i2c_write(client, i2c_control_buf, 3);

		if (ret > 0) {
			GTP_INFO("GTP enter sleep!");
			return ret;
		}

		msleep(20);
	}

#else

	tpd_gpio_output(GTP_RST_PORT, 0);
	msleep(20);

#ifdef TPD_POWER_SOURCE_CUSTOM
	hwPowerDown(TPD_POWER_SOURCE_CUSTOM, "TP");
#else
	hwPowerDown(MT65XX_POWER_LDO_VGP2, "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
	hwPowerDown(TPD_POWER_SOURCE_1800, "TP");
#endif

	GTP_INFO("GTP enter sleep!");
	return 0;

#endif
	GTP_ERROR("GTP send sleep cmd failed.");
	return ret;
}

static s8 gtp_wakeup_sleep(struct i2c_client *client)
{
	u8 retry = 0;
	s8 ret = -1;

	GTP_INFO("GTP wakeup begin.");

#if GTP_POWER_CTRL_SLEEP

#if GTP_COMPATIBLE_MODE
	if (CHIP_TYPE_GT9F == gtp_chip_type) {
		force_reset_guitar();
		GTP_INFO("Esd recovery wakeup.");
		return 0;
	}
#endif

	while (retry++ < 5) {
		ret = tpd_power_on(client);

		if (ret < 0)
			GTP_ERROR("I2C Power on ERROR!");

		ret = gtp_send_cfg(client);

		if (ret > 0) {
			GTP_DEBUG("Wakeup sleep send config success.");
			return ret;
		}
	}

#else
#if GTP_COMPATIBLE_MODE
	if (CHIP_TYPE_GT9F == gtp_chip_type) {
		u8 opr_buf[2] = { 0 };

		while (retry++ < 10) {
			tpd_gpio_output(GTP_INT_PORT, 1);
			msleep(20);

			ret = gtp_i2c_test(client);

			if (ret >= 0) {
				/* Hold ss51 & dsp */
				opr_buf[0] = 0x0C;
				ret =
				    i2c_write_bytes(client, 0x4180, opr_buf, 1);
				if (ret < 0) {
					GTP_DEBUG
					    ("Hold ss51 & dsp I2C error,retry:%d",
					     retry);
					continue;
				}

				/* Confirm hold */
				opr_buf[0] = 0x00;
				ret =
				    i2c_read_bytes(client, 0x4180, opr_buf, 1);
				if (ret < 0) {
					GTP_DEBUG
					    ("confirm ss51 & dsp hold, I2C error,retry:%d",
					     retry);
					continue;
				}
				if (0x0C != opr_buf[0]) {
					GTP_DEBUG
					    ("ss51 & dsp not hold, val: %d, retry: %d",
					     opr_buf[0], retry);
					continue;
				}
				GTP_DEBUG("ss51 & dsp has been hold");

				ret = gtp_fw_startup(client);
				if (FAIL == ret) {
					GTP_ERROR
					    ("[gtp_wakeup_sleep]Startup fw failed.");
					continue;
				}
				GTP_INFO("flashless wakeup sleep success");
				return ret;
			}
			force_reset_guitar();
		}
		if (retry >= 10) {
			GTP_ERROR("wakeup retry timeout, process esd reset");
			force_reset_guitar();
		}
		GTP_ERROR("GTP wakeup sleep failed.");
		return ret;
	}
#endif
	while (retry++ < 10) {
		tpd_gpio_output(GTP_INT_PORT, 1);
		msleep(20);
		tpd_gpio_output(GTP_INT_PORT, 0);
		msleep(20);
		ret = gtp_i2c_test(client);

		if (ret >= 0) {
			gtp_int_sync(50);
			return ret;
		}

		gtp_reset_guitar(client, 20);
	}

#endif

	GTP_ERROR("GTP wakeup sleep failed.");
	return ret;
}

/* Function to manage low power suspend */
static void tpd_suspend(struct device *h)
{
	s32 ret = -1;

	/* return; */
	mutex_lock(&i2c_access);
	/* mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM); */
	disable_irq(touch_irq);
	tpd_halt = 1;
	mutex_unlock(&i2c_access);

	ret = gtp_enter_sleep(i2c_client_point);
	if (ret < 0)
		GTP_ERROR("GTP early suspend failed.");
#if GTP_ESD_PROTECT
	cancel_delayed_work_sync(&gtp_esd_check_work);
#endif

#ifdef GTP_CHARGER_DETECT
	cancel_delayed_work_sync(&gtp_charger_check_work);
#endif
#ifdef TPD_PROXIMITY

	if (tpd_proximity_flag == 1)
		return;
#endif
}

/* Function to manage power-on resume */
static void tpd_resume(struct device *h)
{
	s32 ret = -1;

	/*return; */
	ret = gtp_wakeup_sleep(i2c_client_point);

	if (ret < 0)
		GTP_ERROR("GTP later resume failed.");

	GTP_INFO("GTP wakeup sleep.");

	mutex_lock(&i2c_access);
	tpd_halt = 0;
	/* mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM); */
	enable_irq(touch_irq);
	mutex_unlock(&i2c_access);

#ifdef TPD_PROXIMITY
	if (tpd_proximity_flag == 1)
		return;
#endif

#if GTP_ESD_PROTECT
	queue_delayed_work(gtp_esd_check_workqueue, &gtp_esd_check_work,
			   TPD_ESD_CHECK_CIRCLE);
#endif

#ifdef GTP_CHARGER_DETECT
	queue_delayed_work(gtp_charger_check_workqueue, &gtp_charger_check_work,
			   TPD_CHARGER_CHECK_CIRCLE);
#endif

}

static void tpd_off(void)
{

#ifdef TPD_POWER_SOURCE_CUSTOM
	hwPowerDown(TPD_POWER_SOURCE_CUSTOM, "TP");
#else
	hwPowerDown(MT65XX_POWER_LDO_VGP2, "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
	hwPowerDown(TPD_POWER_SOURCE_1800, "TP");
#endif
	GTP_INFO("GTP enter sleep!");

	tpd_halt = 1;
	/* mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM); */
	disable_irq(touch_irq);
}

static void tpd_on(void)
{
	s32 ret = -1, retry = 0;

	while (retry++ < 5) {
		ret = tpd_power_on(i2c_client_point);

		if (ret < 0)
			GTP_ERROR("I2C Power on ERROR!");

		ret = gtp_send_cfg(i2c_client_point);

		if (ret > 0)
			GTP_DEBUG("Wakeup sleep send config success.");
	}
	if (ret < 0)
		GTP_ERROR("GTP later resume failed.");
	/* mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM); */
	enable_irq(touch_irq);
	tpd_halt = 0;
}

static struct tpd_driver_t tpd_device_driver = {
	.tpd_device_name = "gt9xx",
	.tpd_local_init = tpd_local_init,
	.suspend = tpd_suspend,
	.resume = tpd_resume,
	.attrs = {
		  .attr = gt9xx_attrs,
		  .num = ARRAY_SIZE(gt9xx_attrs),
		  },
};

/* called when loaded into kernel */
static int __init tpd_driver_init(void)
{
	GTP_INFO("MediaTek gt91xx touch panel driver init");
	tpd_get_dts_info();
	if (tpd_driver_add(&tpd_device_driver) < 0)
		GTP_INFO("add generic driver failed");
	return 0;
}

/* should never be called */
static void __exit tpd_driver_exit(void)
{
	GTP_INFO("MediaTek gt91xx touch panel driver exit");
	/* input_unregister_device(tpd->dev); */
	tpd_driver_remove(&tpd_device_driver);
}

module_init(tpd_driver_init);
module_exit(tpd_driver_exit);
