
#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/types.h>

#define PFX "IMX258_pdafotp"
#define LOG_INF(format, args...)    pr_debug(PFX "[%s] " format, __func__, ##args)

#include "kd_camera_typedef.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"
#include "imx258mipiraw_Sensor.h"



#define USHORT		unsigned short
#define BYTE		unsigned char
#define Sleep(ms) mdelay(ms)

#define IMX258_EEPROM_READ_ID  0xA0
#define IMX258_EEPROM_WRITE_ID   0xA1
#define IMX258_I2C_SPEED		100
#define IMX258_MAX_OFFSET		0xFFFF

#define DATA_SIZE 2048
BYTE imx258_eeprom_data[DATA_SIZE] = { 0 };

/* static bool get_done = 0; */
/* static int last_size = 0; */
/* static int last_offset = 0; */


static bool selective_read_eeprom(kal_uint16 addr, BYTE *data)
{
	char pu_send_cmd[2] = { (char)(addr >> 8), (char)(addr & 0xFF) };

	if (addr > IMX258_MAX_OFFSET)
		return false;
/* kdSetI2CSpeed(IMX258_I2C_SPEED); */

	if (iReadRegI2C(pu_send_cmd, 2, (u8 *) data, 1, IMX258_EEPROM_READ_ID) < 0)
		return false;
	return true;
}

static bool _read_imx258_eeprom(kal_uint16 addr, BYTE *data, kal_uint32 size)
{
	int i = 0;
	int offset = addr;

	for (i = 0; i < size; i++) {
		if (!selective_read_eeprom(offset, &data[i]))
			return false;

		LOG_INF("read_eeprom 0x%0x %d\n", offset, data[i]);
		offset++;
	}
/* get_done = true; */
/* last_size = size; */
/* last_offset = addr; */
	return true;
}

bool read_imx258_eeprom(kal_uint16 addr, BYTE *data, kal_uint32 size)
{
	bool ret;

	addr = 0x0763;
	size = 1404;

	LOG_INF("read imx258 eeprom, size = %d\n", size);

	ret = _read_imx258_eeprom(addr, imx258_eeprom_data, size);
	if (!ret)
		return false;

	memcpy(data, imx258_eeprom_data, size);
	return true;
}

bool read_imx258_eeprom_SPC(kal_uint16 addr, BYTE *data, kal_uint32 size)
{
	bool ret;

	addr = 0x0F73;		/* 0x0F73; */
	size = 126;

	LOG_INF("read imx258 eeprom, size = %d\n", size);

	ret = _read_imx258_eeprom(addr, imx258_eeprom_data, size);
	if (!ret)
		return false;

	memcpy(data, imx258_eeprom_data, size);
	return true;
}
