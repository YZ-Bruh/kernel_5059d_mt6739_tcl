

#include "cust_mag.h"
#include "akm09911.h"
#include "mag.h"

#define DEBUG 0
#define AKM09911_DEV_NAME	"akm09911"
#define DRIVER_VERSION	 "1.0.1"
#define AKM09911_DEBUG	1
#define AKM09911_RETRY_COUNT	10
#define AKM09911_DEFAULT_DELAY	100


#if AKM09911_DEBUG
#define MAGN_TAG		 "[AKM09911] "
#define MAGN_ERR(fmt, args...)	pr_err(MAGN_TAG fmt, ##args)
#define MAGN_LOG(fmt, args...)	pr_debug(MAGN_TAG fmt, ##args)
#else
#define MAGN_TAG
#define MAGN_ERR(fmt, args...)	do {} while (0)
#define MAGN_LOG(fmt, args...)	do {} while (0)
#endif

static DECLARE_WAIT_QUEUE_HEAD(open_wq);

static short akmd_delay = AKM09911_DEFAULT_DELAY;
static int factory_mode;
static int akm09911_init_flag;
static struct i2c_client *this_client;
static int8_t akm_device;

static uint8_t akm_fuse[3] = {0};
/*----------------------------------------------------------------------------*/
static const struct i2c_device_id akm09911_i2c_id[] = { {AKM09911_DEV_NAME, 0}, {} };

/*----------------------------------------------------------------------------*/
static int akm09911_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int akm09911_i2c_remove(struct i2c_client *client);
static int akm09911_i2c_detect(struct i2c_client *client, struct i2c_board_info *info);
static int akm09911_suspend(struct i2c_client *client, pm_message_t msg);
static int akm09911_resume(struct i2c_client *client);
static int akm09911_local_init(void);
static int akm09911_remove(void);
static int akm09911_flush(void);

static struct mag_init_info akm09911_init_info = {
	.name = "akm09911",
	.init = akm09911_local_init,
	.uninit = akm09911_remove,
};


/*----------------------------------------------------------------------------*/
enum {
	AMK_FUN_DEBUG = 0x01,
	AMK_DATA_DEBUG = 0X02,
	AMK_HWM_DEBUG = 0X04,
	AMK_CTR_DEBUG = 0X08,
	AMK_I2C_DEBUG = 0x10,
} AMK_TRC;


/*----------------------------------------------------------------------------*/
struct akm09911_i2c_data {
	struct i2c_client *client;
	struct mag_hw hw;
	atomic_t layout;
	atomic_t trace;
	struct hwmsen_convert cvt;
	bool flush;
	bool enable;
};
/*----------------------------------------------------------------------------*/
#ifdef CONFIG_OF
static const struct of_device_id mag_of_match[] = {
	{.compatible = "mediatek,msensor"},
	{},
};
#endif
static struct i2c_driver akm09911_i2c_driver = {
	.driver = {
		   .name = AKM09911_DEV_NAME,
#ifdef CONFIG_OF
		   .of_match_table = mag_of_match,
#endif
		   },
	.probe = akm09911_i2c_probe,
	.remove = akm09911_i2c_remove,
	.detect = akm09911_i2c_detect,
	.suspend = akm09911_suspend,
	.resume = akm09911_resume,
	.id_table = akm09911_i2c_id,
};


/*----------------------------------------------------------------------------*/
static atomic_t dev_open_count;
/*----------------------------------------------------------------------------*/

static DEFINE_MUTEX(akm09911_i2c_mutex);
#ifndef CONFIG_MTK_I2C_EXTENSION
static int mag_i2c_read_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{
	int err = 0;
	u8 beg = addr;
	struct i2c_msg msgs[2] = { {0}, {0} };

	if (!client) {
		return -EINVAL;
	} else if (len > C_I2C_FIFO_SIZE) {
		MAGN_ERR(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
		return -EINVAL;
	}
	mutex_lock(&akm09911_i2c_mutex);

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &beg;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = data;

	err = i2c_transfer(client->adapter, msgs, sizeof(msgs) / sizeof(msgs[0]));
	if (err != 2) {
		MAGN_ERR("i2c_transfer error: (%d %p %d) %d\n", addr, data, len, err);
		err = -EIO;
	} else {
		err = 0;
	}
	mutex_unlock(&akm09911_i2c_mutex);
	return err;

}

static int mag_i2c_write_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{				/*because address also occupies one byte, the maximum length for write is 7 bytes */
	int err = 0, idx = 0, num = 0;
	char buf[C_I2C_FIFO_SIZE];

	err = 0;
	mutex_lock(&akm09911_i2c_mutex);
	if (!client) {
		mutex_unlock(&akm09911_i2c_mutex);
		return -EINVAL;
	} else if (len >= C_I2C_FIFO_SIZE) {
		mutex_unlock(&akm09911_i2c_mutex);
		MAGN_ERR(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
		return -EINVAL;
	}

	num = 0;
	buf[num++] = addr;
	for (idx = 0; idx < len; idx++)
		buf[num++] = data[idx];

	err = i2c_master_send(client, buf, num);
	if (err < 0) {
		mutex_unlock(&akm09911_i2c_mutex);
		MAGN_ERR("send command error!!\n");
		return -EFAULT;
	}
	mutex_unlock(&akm09911_i2c_mutex);
	return err;
}
#endif

static long AKI2C_RxData(char *rxData, int length)
{
#ifndef CONFIG_MTK_I2C_EXTENSION
	struct i2c_client *client = this_client;
	int res = 0;
	char addr = 0;

	if ((rxData == NULL) || (length < 1))
		return -EINVAL;

	addr = rxData[0];

	res = mag_i2c_read_block(client, addr, rxData, length);
	if (res < 0)
		return -1;
	return 0;
#else
	uint8_t loop_i = 0;
#if DEBUG
	int i = 0;
	struct i2c_client *client = this_client;
	struct akm09911_i2c_data *data = i2c_get_clientdata(client);
	char addr = rxData[0];
#endif

	/* Caller should check parameter validity. */
	if ((rxData == NULL) || (length < 1))
		return -EINVAL;

	mutex_lock(&akm09911_i2c_mutex);
	for (loop_i = 0; loop_i < AKM09911_RETRY_COUNT; loop_i++) {
		this_client->addr = this_client->addr & I2C_MASK_FLAG;
		this_client->addr = this_client->addr | I2C_WR_FLAG;
		if (i2c_master_send(this_client, (const char *)rxData, ((length << 0X08) | 0X01)))
			break;
		mdelay(10);
	}

	if (loop_i >= AKM09911_RETRY_COUNT) {
		mutex_unlock(&akm09911_i2c_mutex);
		MAGN_ERR("%s retry over %d\n", __func__, AKM09911_RETRY_COUNT);
		return -EIO;
	}
	mutex_unlock(&akm09911_i2c_mutex);
#if DEBUG
	if (atomic_read(&data->trace) & AMK_I2C_DEBUG) {
		MAGN_LOG("RxData: len=%02x, addr=%02x\n  data=", length, addr);
		for (i = 0; i < length; i++)
			MAGN_LOG(" %02x", rxData[i]);

		MAGN_LOG("\n");
	}
#endif

	return 0;
#endif
}

static long AKI2C_TxData(char *txData, int length)
{
#ifndef CONFIG_MTK_I2C_EXTENSION
	struct i2c_client *client = this_client;
	int res = 0;
	char addr = 0;
	u8 *buff = NULL;

	if ((txData == NULL) || (length < 2))
		return -EINVAL;

	addr = txData[0];
	buff = &txData[1];

	res = mag_i2c_write_block(client, addr, buff, (length - 1));
	if (res < 0)
		return -1;
	return 0;
#else
	uint8_t loop_i = 0;
#if DEBUG
	int i = 0;
	struct i2c_client *client = this_client;
	struct akm09911_i2c_data *data = i2c_get_clientdata(client);
#endif

	/* Caller should check parameter validity. */
	if ((txData == NULL) || (length < 2))
		return -EINVAL;
	mutex_lock(&akm09911_i2c_mutex);
	this_client->addr = this_client->addr & I2C_MASK_FLAG;
	for (loop_i = 0; loop_i < AKM09911_RETRY_COUNT; loop_i++) {
		if (i2c_master_send(this_client, (const char *)txData, length) > 0)
			break;
		mdelay(10);
	}

	if (loop_i >= AKM09911_RETRY_COUNT) {
		mutex_unlock(&akm09911_i2c_mutex);
		MAGN_ERR("%s retry over %d\n", __func__, AKM09911_RETRY_COUNT);
		return -EIO;
	}
	mutex_unlock(&akm09911_i2c_mutex);
#if DEBUG
	if (atomic_read(&data->trace) & AMK_I2C_DEBUG) {
		MAGN_LOG("TxData: len=%02x, addr=%02x\n  data=", length, txData[0]);
		for (i = 0; i < (length - 1); i++)
			MAGN_LOG(" %02x", txData[i + 1]);

		MAGN_LOG("\n");
	}
#endif

	return 0;
#endif
}

static long AKECS_SetMode_SngMeasure(void)
{
	char buffer[2];
#ifdef AKM_Device_AK8963
	buffer[0] = AK8963_REG_CNTL1;
	buffer[1] = AK8963_MODE_SNG_MEASURE;
#else
	/* Set measure mode */
	buffer[0] = AK09911_REG_CNTL2;
	buffer[1] = AK09911_MODE_SNG_MEASURE;
#endif

	/* Set data */
	return AKI2C_TxData(buffer, 2);
}

static long AKECS_SetMode_SelfTest(void)
{
	char buffer[2];
#ifdef AKM_Device_AK8963
	buffer[0] = AK8963_REG_CNTL1;
	buffer[1] = AK8963_MODE_SELF_TEST;
#else
	/* Set measure mode */
	buffer[0] = AK09911_REG_CNTL2;
	buffer[1] = AK09911_MODE_SELF_TEST;
	/* Set data */
#endif
	return AKI2C_TxData(buffer, 2);
}

static long AKECS_SetMode_FUSEAccess(void)
{
	char buffer[2];

#ifdef AKM_Device_AK8963
	buffer[0] = AK8963_REG_CNTL1;
	buffer[1] = AK8963_MODE_FUSE_ACCESS;
#else
	/* Set measure mode */
	buffer[0] = AK09911_REG_CNTL2;
	buffer[1] = AK09911_MODE_FUSE_ACCESS;
	/* Set data */
#endif
	return AKI2C_TxData(buffer, 2);
}

static int AKECS_SetMode_PowerDown(void)
{
	char buffer[2];
#ifdef AKM_Device_AK8963
	buffer[0] = AK8963_REG_CNTL1;
	buffer[1] = AK8963_MODE_POWERDOWN;
#else
	/* Set powerdown mode */
	buffer[0] = AK09911_REG_CNTL2;
	buffer[1] = AK09911_MODE_POWERDOWN;
	/* Set data */
#endif
	return AKI2C_TxData(buffer, 2);
}

static long AKECS_Reset(int hard)
{
	unsigned char buffer[2];
	long err = 0;

	if (hard != 0) {
		/*TODO change to board setting */
		/* gpio_set_value(akm->rstn, 0); */
		udelay(5);
		/* gpio_set_value(akm->rstn, 1); */
	} else {
		/* Set measure mode */
#ifdef AKM_Device_AK8963
		buffer[0] = AK8963_REG_CNTL2;
		buffer[1] = 0x01;
#else
		buffer[0] = AK09911_REG_CNTL3;
		buffer[1] = 0x01;
#endif
		err = AKI2C_TxData(buffer, 2);
		if (err < 0)
			MAGN_LOG("%s: Can not set SRST bit.", __func__);
		else
			MAGN_LOG("Soft reset is done.");
	}

	/* Device will be accessible 300 us after */
	udelay(300);		/* 100 */

	return err;
}

static long AKECS_SetMode(char mode)
{
	long ret;

	switch (mode & 0x1F) {
	case AK09911_MODE_SNG_MEASURE:
		ret = AKECS_SetMode_SngMeasure();
		break;

	case AK09911_MODE_SELF_TEST:
	case AK8963_MODE_SELF_TEST:
		ret = AKECS_SetMode_SelfTest();
		break;

	case AK09911_MODE_FUSE_ACCESS:
	case AK8963_MODE_FUSE_ACCESS:
		ret = AKECS_SetMode_FUSEAccess();
		break;

	case AK09911_MODE_POWERDOWN:
		ret = AKECS_SetMode_PowerDown();
		break;

	default:
		MAGN_LOG("%s: Unknown mode(%d)", __func__, mode);
		return -EINVAL;
	}

	/* wait at least 100us after changing mode */
	udelay(100);

	return ret;
}

static int AKECS_ReadFuse(void)
{
	int ret = 0;

	ret = AKECS_SetMode_FUSEAccess();
	if (ret < 0) {
		MAGN_LOG("AKM set read fuse mode fail ret:%d\n", ret);
		return ret;
	}
	akm_fuse[0] = AK09911_FUSE_ASAX;
	ret = AKI2C_RxData(akm_fuse, 3);
	if (ret < 0) {
		MAGN_LOG("AKM read fuse fail ret:%d\n", ret);
		return ret;
	}
	ret = AKECS_SetMode_PowerDown();
	return ret;
}

static int AKECS_CheckDevice(void)
{
	char buffer[2];
	int ret;

	/* Set measure mode */
#ifdef AKM_Device_AK8963
	buffer[0] = AK8963_REG_WIA;
#else
	buffer[0] = AK09911_REG_WIA1;
#endif

	/* Read data */
	ret = AKI2C_RxData(buffer, 2);
	if (ret < 0)
		return ret;

	/* Check read data */
	if (buffer[0] != 0x48)
		return -ENXIO;

	akm_device = buffer[1];
	if ((akm_device == 0x05) || (akm_device == 0x04)) {/* ak09911 & ak09912 */
		ret = AKECS_ReadFuse();
		if (ret < 0) {
			MAGN_ERR("AKM09911 akm09911_probe: read fuse fail\n");
			return -ENXIO;
		}
	} else if (akm_device == 0x10) {/* ak09915 & ak09916c & ak09916d & ak09918 */
		akm_fuse[0] = 0x80;
		akm_fuse[1] = 0x80;
		akm_fuse[2] = 0x80;
	}

	return 0;
}

static int AKECS_AxisInfoToPat(
	const uint8_t axis_order[3],
	const uint8_t axis_sign[3],
	int16_t *pat)
{
	/* check invalid input */
	if ((axis_order[0] < 0) || (2 < axis_order[0]) ||
	   (axis_order[1] < 0) || (2 < axis_order[1]) ||
	   (axis_order[2] < 0) || (2 < axis_order[2]) ||
	   (axis_sign[0] < 0) || (1 < axis_sign[0]) ||
	   (axis_sign[1] < 0) || (1 < axis_sign[1]) ||
	   (axis_sign[2] < 0) || (1 < axis_sign[2]) ||
	  ((axis_order[0] * axis_order[1] * axis_order[2]) != 0) ||
	  ((axis_order[0] + axis_order[1] + axis_order[2]) != 3)) {
		*pat = 0;
		return -1;
	}
	/* calculate pat
	 * BIT MAP
	 * [8] = sign_x
	 * [7] = sign_y
	 * [6] = sign_z
	 * [5:4] = order_x
	 * [3:2] = order_y
	 * [1:0] = order_z
	 */
	*pat = ((int16_t)axis_sign[0] << 8);
	*pat += ((int16_t)axis_sign[1] << 7);
	*pat += ((int16_t)axis_sign[2] << 6);
	*pat += ((int16_t)axis_order[0] << 4);
	*pat += ((int16_t)axis_order[1] << 2);
	*pat += ((int16_t)axis_order[2] << 0);
	return 0;
}

static int16_t AKECS_SetCert(void)
{
	struct i2c_client *client = this_client;
	struct akm09911_i2c_data *data = i2c_get_clientdata(client);
	uint8_t axis_sign[3] = {0};
	uint8_t axis_order[3] = {0};
	int16_t ret = 0;
	int i = 0;
	int16_t cert = 0x06;

	for (i = 0; i < 3; i++)
		axis_order[i] = (uint8_t)data->cvt.map[i];

	for (i = 0; i < 3; i++) {
		if (data->cvt.sign[i] > 0)
			axis_sign[i] = 0;
		else if (data->cvt.sign[i] < 0)
			axis_sign[i] = 1;
	}

	ret = AKECS_AxisInfoToPat(axis_order, axis_sign, &cert);
	if (ret != 0)
		return 0;
	return cert;
}
/* M-sensor daemon application have set the sng mode */
static long AKECS_GetData(char *rbuf, int size)
{
	char temp;
	int loop_i, ret;
#if DEBUG
	struct i2c_client *client = this_client;
	struct akm09911_i2c_data *data = i2c_get_clientdata(client);
#endif

	if (size < SENSOR_DATA_SIZE) {
		MAGN_ERR("buff size is too small %d!\n", size);
		return -1;
	}

	memset(rbuf, 0, SENSOR_DATA_SIZE);
#ifdef AKM_Device_AK8963
	rbuf[0] = AK8963_REG_ST1;
#else
	rbuf[0] = AK09911_REG_ST1;
#endif

	for (loop_i = 0; loop_i < AKM09911_RETRY_COUNT; loop_i++) {
		ret = AKI2C_RxData(rbuf, 1);
		if (ret) {
			MAGN_ERR("read ST1 resigster failed!\n");
			return -1;
		}

		if ((rbuf[0] & 0x01) == 0x01)
			break;

		mdelay(2);
#ifdef AKM_Device_AK8963
		rbuf[0] = AK8963_REG_ST1;
#else
		rbuf[0] = AK09911_REG_ST1;
#endif
	}

	if (loop_i >= AKM09911_RETRY_COUNT) {
		MAGN_ERR("Data read retry larger the max count!\n");
		if (0 == factory_mode)
			/* if return we can not get data at factory mode */
			return -1;
	}

	temp = rbuf[0];
#ifdef AKM_Device_AK8963
	rbuf[1] = AK8963_REG_HXL;
	ret = AKI2C_RxData(&rbuf[1], SENSOR_DATA_SIZE - 2);
#else
	rbuf[1] = AK09911_REG_HXL;
	ret = AKI2C_RxData(&rbuf[1], SENSOR_DATA_SIZE - 1);
#endif
	if (ret < 0) {
		MAGN_ERR("AKM8975 akm8975_work_func: I2C failed\n");
		return -1;
	}
	rbuf[0] = temp;
#ifdef AKM_Device_AK8963
	rbuf[8] = rbuf[7];
	rbuf[7] = 0;
#endif

	return 0;
}

/*----------------------------------------------------------------------------*/
static int akm09911_ReadChipInfo(char *buf, int bufsize)
{
	if ((!buf) || (bufsize <= AKM09911_BUFSIZE - 1))
		return -1;

	if (!this_client) {
		*buf = 0;
		return -2;
	}

	sprintf(buf, "akm09911 Chip");
	return 0;
}

/*----------------------------shipment test------------------------------------------------*/
int TEST_DATA(const char testno[], const char testname[], const int testdata,
	      const int lolimit, const int hilimit, int *pf_total)
{
	int pf;			/* Pass;1, Fail;-1 */

	if ((testno == NULL) && (strncmp(testname, "START", 5) == 0)) {
		MAGN_LOG("--------------------------------------------------------------------\n");
		MAGN_LOG(" Test No. Test Name	Fail	Test Data	[	 Low	High]\n");
		MAGN_LOG("--------------------------------------------------------------------\n");
		pf = 1;
	} else if ((testno == NULL) && (strncmp(testname, "END", 3) == 0)) {
		MAGN_LOG("--------------------------------------------------------------------\n");
		if (*pf_total == 1)
			MAGN_LOG("Factory shipment test was passed.\n\n");
		else
			MAGN_LOG("Factory shipment test was failed.\n\n");

		pf = 1;
	} else {
		if ((lolimit <= testdata) && (testdata <= hilimit))
			pf = 1;
		else
			pf = -1;

		/* display result */
		MAGN_LOG(" %7s  %-10s	 %c	%9d	[%9d	%9d]\n",
			 testno, testname, ((pf == 1) ? ('.') : ('F')), testdata, lolimit, hilimit);
	}

	/* Pass/Fail check */
	if (*pf_total != 0) {
		if ((*pf_total == 1) && (pf == 1))
			*pf_total = 1;	/* Pass */
		else
			*pf_total = -1;	/* Fail */
	}
	return pf;
}

int FST_AK8963(void)
{
	int pf_total;		/* p/f flag for this subtest */
	char i2cData[16];
	int hdata[3];
	int asax;
	int asay;
	int asaz;

	/* *********************************************** */
	/* Reset Test Result */
	/* *********************************************** */
	pf_total = 1;

	/* *********************************************** */
	/* Step1 */
	/* *********************************************** */

	/* Set to PowerDown mode */
	/* if (AKECS_SetMode(AK8963_MODE_POWERDOWN) < 0) { */
	/* MAGN_LOG("%s:%d Error.\n", __FUNCTION__, __LINE__); */
	/* return 0; */
	/* } */
	AKECS_Reset(0);
	mdelay(1);

	/* When the serial interface is SPI, */
	/* write "00011011" to I2CDIS register(to disable I2C,). */
	if (CSPEC_SPI_USE == 1) {
		i2cData[0] = AK8963_REG_I2CDIS;
		i2cData[1] = 0x1B;
		if (AKI2C_TxData(i2cData, 2) < 0) {
			MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
			return 0;
		}
	}

	/* Read values from WIA to ASTC. */
	i2cData[0] = AK8963_REG_WIA;
	if (AKI2C_RxData(i2cData, 7) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* TEST */
	TEST_DATA(TLIMIT_NO_RST_WIA, TLIMIT_TN_RST_WIA, (int)i2cData[0], TLIMIT_LO_RST_WIA,
		  TLIMIT_HI_RST_WIA, &pf_total);
	TEST_DATA(TLIMIT_NO_RST_INFO, TLIMIT_TN_RST_INFO, (int)i2cData[1], TLIMIT_LO_RST_INFO,
		  TLIMIT_HI_RST_INFO, &pf_total);
	TEST_DATA(TLIMIT_NO_RST_ST1, TLIMIT_TN_RST_ST1, (int)i2cData[2], TLIMIT_LO_RST_ST1,
		  TLIMIT_HI_RST_ST1, &pf_total);
	TEST_DATA(TLIMIT_NO_RST_HXL, TLIMIT_TN_RST_HXL, (int)i2cData[3], TLIMIT_LO_RST_HXL,
		  TLIMIT_HI_RST_HXL, &pf_total);
	TEST_DATA(TLIMIT_NO_RST_HXH, TLIMIT_TN_RST_HXH, (int)i2cData[4], TLIMIT_LO_RST_HXH,
		  TLIMIT_HI_RST_HXH, &pf_total);
	TEST_DATA(TLIMIT_NO_RST_HYL, TLIMIT_TN_RST_HYL, (int)i2cData[5], TLIMIT_LO_RST_HYL,
		  TLIMIT_HI_RST_HYL, &pf_total);
	TEST_DATA(TLIMIT_NO_RST_HYH, TLIMIT_TN_RST_HYH, (int)i2cData[6], TLIMIT_LO_RST_HYH,
		  TLIMIT_HI_RST_HYH, &pf_total);
	/* our i2c only most can read 8 byte  at one time , */
	i2cData[7] = AK8963_REG_HZL;
	if (AKI2C_RxData((i2cData + 7), 6) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}
	TEST_DATA(TLIMIT_NO_RST_HZL, TLIMIT_TN_RST_HZL, (int)i2cData[7], TLIMIT_LO_RST_HZL,
		  TLIMIT_HI_RST_HZL, &pf_total);
	TEST_DATA(TLIMIT_NO_RST_HZH, TLIMIT_TN_RST_HZH, (int)i2cData[8], TLIMIT_LO_RST_HZH,
		  TLIMIT_HI_RST_HZH, &pf_total);
	TEST_DATA(TLIMIT_NO_RST_ST2, TLIMIT_TN_RST_ST2, (int)i2cData[9], TLIMIT_LO_RST_ST2,
		  TLIMIT_HI_RST_ST2, &pf_total);
	TEST_DATA(TLIMIT_NO_RST_CNTL, TLIMIT_TN_RST_CNTL, (int)i2cData[10], TLIMIT_LO_RST_CNTL,
		  TLIMIT_HI_RST_CNTL, &pf_total);
	/* i2cData[11] is BLANK. */
	TEST_DATA(TLIMIT_NO_RST_ASTC, TLIMIT_TN_RST_ASTC, (int)i2cData[12], TLIMIT_LO_RST_ASTC,
		  TLIMIT_HI_RST_ASTC, &pf_total);

	/* Read values from I2CDIS. */
	i2cData[0] = AK8963_REG_I2CDIS;
	if (AKI2C_RxData(i2cData, 1) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}
	if (CSPEC_SPI_USE == 1)
		TEST_DATA(TLIMIT_NO_RST_I2CDIS, TLIMIT_TN_RST_I2CDIS, (int)i2cData[0],
			  TLIMIT_LO_RST_I2CDIS_USESPI, TLIMIT_HI_RST_I2CDIS_USESPI, &pf_total);
	else
		TEST_DATA(TLIMIT_NO_RST_I2CDIS, TLIMIT_TN_RST_I2CDIS, (int)i2cData[0],
			  TLIMIT_LO_RST_I2CDIS_USEI2C, TLIMIT_HI_RST_I2CDIS_USEI2C, &pf_total);

	/* Set to FUSE ROM access mode */
	if (AKECS_SetMode(AK8963_MODE_FUSE_ACCESS) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* Read values from ASAX to ASAZ */
	i2cData[0] = AK8963_FUSE_ASAX;
	if (AKI2C_RxData(i2cData, 3) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}
	asax = (int)i2cData[0];
	asay = (int)i2cData[1];
	asaz = (int)i2cData[2];

	/* TEST */
	TEST_DATA(TLIMIT_NO_ASAX, TLIMIT_TN_ASAX, asax, TLIMIT_LO_ASAX, TLIMIT_HI_ASAX, &pf_total);
	TEST_DATA(TLIMIT_NO_ASAY, TLIMIT_TN_ASAY, asay, TLIMIT_LO_ASAY, TLIMIT_HI_ASAY, &pf_total);
	TEST_DATA(TLIMIT_NO_ASAZ, TLIMIT_TN_ASAZ, asaz, TLIMIT_LO_ASAZ, TLIMIT_HI_ASAZ, &pf_total);

	/* Read values. CNTL */
	i2cData[0] = AK8963_REG_CNTL1;
	if (AKI2C_RxData(i2cData, 1) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* Set to PowerDown mode */
	if (AKECS_SetMode(AK8963_MODE_POWERDOWN) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* TEST */
	TEST_DATA(TLIMIT_NO_WR_CNTL, TLIMIT_TN_WR_CNTL, (int)i2cData[0], TLIMIT_LO_WR_CNTL,
		  TLIMIT_HI_WR_CNTL, &pf_total);


	/* *********************************************** */
	/* Step2 */
	/* *********************************************** */

	/* Set to SNG measurement pattern (Set CNTL register) */
	if (AKECS_SetMode(AK8963_MODE_SNG_MEASURE) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* Wait for DRDY pin changes to HIGH. */
	mdelay(10);
	/* Get measurement data from AK8963 */
	/* ST1 + (HXL + HXH) + (HYL + HYH) + (HZL + HZH) + ST2 */
	/* = 1 + (1 + 1) + (1 + 1) + (1 + 1) + 1 = 8 bytes */
	if (AKECS_GetData(i2cData, SENSOR_DATA_SIZE) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	hdata[0] = (int16_t) (i2cData[1] | (i2cData[2] << 8));
	hdata[1] = (int16_t) (i2cData[3] | (i2cData[4] << 8));
	hdata[2] = (int16_t) (i2cData[5] | (i2cData[6] << 8));
	/* AK8963 @ 14 BIT */
	hdata[0] <<= 2;
	hdata[1] <<= 2;
	hdata[2] <<= 2;


	/* TEST */
	TEST_DATA(TLIMIT_NO_SNG_ST1, TLIMIT_TN_SNG_ST1, (int)i2cData[0], TLIMIT_LO_SNG_ST1,
		  TLIMIT_HI_SNG_ST1, &pf_total);
	TEST_DATA(TLIMIT_NO_SNG_HX, TLIMIT_TN_SNG_HX, hdata[0], TLIMIT_LO_SNG_HX, TLIMIT_HI_SNG_HX,
		  &pf_total);
	TEST_DATA(TLIMIT_NO_SNG_HY, TLIMIT_TN_SNG_HY, hdata[1], TLIMIT_LO_SNG_HY, TLIMIT_HI_SNG_HY,
		  &pf_total);
	TEST_DATA(TLIMIT_NO_SNG_HZ, TLIMIT_TN_SNG_HZ, hdata[2], TLIMIT_LO_SNG_HZ, TLIMIT_HI_SNG_HZ,
		  &pf_total);
	TEST_DATA(TLIMIT_NO_SNG_ST2, TLIMIT_TN_SNG_ST2, (int)i2cData[8], TLIMIT_LO_SNG_ST2,
		  TLIMIT_HI_SNG_ST2, &pf_total);

	/* Generate magnetic field for self-test (Set ASTC register) */
	i2cData[0] = AK8963_REG_ASTC;
	i2cData[1] = 0x40;
	if (AKI2C_TxData(i2cData, 2) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* Set to Self-test mode (Set CNTL register) */
	if (AKECS_SetMode(AK8963_MODE_SELF_TEST) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* Wait for DRDY pin changes to HIGH. */
	mdelay(10);
	/* Get measurement data from AK8963 */
	/* ST1 + (HXL + HXH) + (HYL + HYH) + (HZL + HZH) + ST2 */
	/* = 1 + (1 + 1) + (1 + 1) + (1 + 1) + 1 = 8Byte */
	if (AKECS_GetData(i2cData, SENSOR_DATA_SIZE) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* TEST */
	TEST_DATA(TLIMIT_NO_SLF_ST1, TLIMIT_TN_SLF_ST1, (int)i2cData[0], TLIMIT_LO_SLF_ST1,
		  TLIMIT_HI_SLF_ST1, &pf_total);

	hdata[0] = (int16_t) (i2cData[1] | (i2cData[2] << 8));
	hdata[1] = (int16_t) (i2cData[3] | (i2cData[4] << 8));
	hdata[2] = (int16_t) (i2cData[5] | (i2cData[6] << 8));

	/* AK8963 @ 14 BIT */
	hdata[0] <<= 2;
	hdata[1] <<= 2;
	hdata[2] <<= 2;

	MAGN_LOG("hdata[0] = %d\n", hdata[0]);
	MAGN_LOG("asax = %d\n", asax);
	TEST_DATA(TLIMIT_NO_SLF_RVHX,
		  TLIMIT_TN_SLF_RVHX,
		  (hdata[0]) * ((asax - 128) / 2 / 128 + 1),
		  TLIMIT_LO_SLF_RVHX, TLIMIT_HI_SLF_RVHX, &pf_total);

	TEST_DATA(TLIMIT_NO_SLF_RVHY,
		  TLIMIT_TN_SLF_RVHY,
		  (hdata[1]) * ((asay - 128) / 2 / 128 + 1),
		  TLIMIT_LO_SLF_RVHY, TLIMIT_HI_SLF_RVHY, &pf_total);

	TEST_DATA(TLIMIT_NO_SLF_RVHZ,
		  TLIMIT_TN_SLF_RVHZ,
		  (hdata[2]) * ((asaz - 128) / 2 / 128 + 1),
		  TLIMIT_LO_SLF_RVHZ, TLIMIT_HI_SLF_RVHZ, &pf_total);
	/* TEST */
	TEST_DATA(TLIMIT_NO_SLF_ST2, TLIMIT_TN_SLF_ST2, (int)i2cData[8], TLIMIT_LO_SLF_ST2,
		  TLIMIT_HI_SLF_ST2, &pf_total);

	/* Set to Normal mode for self-test. */
	i2cData[0] = AK8963_REG_ASTC;
	i2cData[1] = 0x00;
	if (AKI2C_TxData(i2cData, 2) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}
	MAGN_LOG("pf_total = %d\n", pf_total);
	return pf_total;
}



int FST_AK09911(void)
{
	int pf_total;		/* p/f flag for this subtest */
	char i2cData[16];
	int hdata[3];
	int asax;
	int asay;
	int asaz;

	/* *********************************************** */
	/* Reset Test Result */
	/* *********************************************** */
	pf_total = 1;

	/* *********************************************** */
	/* Step1 */
	/* *********************************************** */

	/* Reset device. */
	if (AKECS_Reset(0) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* Read values from WIA. */
	i2cData[0] = AK09911_REG_WIA1;
	if (AKI2C_RxData(i2cData, 2) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* TEST */
	TEST_DATA(TLIMIT_NO_RST_WIA1_09911, TLIMIT_TN_RST_WIA1_09911, (int)i2cData[0],
		  TLIMIT_LO_RST_WIA1_09911, TLIMIT_HI_RST_WIA1_09911, &pf_total);
	TEST_DATA(TLIMIT_NO_RST_WIA2_09911, TLIMIT_TN_RST_WIA2_09911, (int)i2cData[1],
		  TLIMIT_LO_RST_WIA2_09911, TLIMIT_HI_RST_WIA2_09911, &pf_total);

	/* Set to FUSE ROM access mode */
	if (AKECS_SetMode(AK09911_MODE_FUSE_ACCESS) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* Read values from ASAX to ASAZ */
	i2cData[0] = AK09911_FUSE_ASAX;
	if (AKI2C_RxData(i2cData, 3) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}
	asax = (int)i2cData[0];
	asay = (int)i2cData[1];
	asaz = (int)i2cData[2];

	/* TEST */
	TEST_DATA(TLIMIT_NO_ASAX_09911, TLIMIT_TN_ASAX_09911, asax, TLIMIT_LO_ASAX_09911,
		  TLIMIT_HI_ASAX_09911, &pf_total);
	TEST_DATA(TLIMIT_NO_ASAY_09911, TLIMIT_TN_ASAY_09911, asay, TLIMIT_LO_ASAY_09911,
		  TLIMIT_HI_ASAY_09911, &pf_total);
	TEST_DATA(TLIMIT_NO_ASAZ_09911, TLIMIT_TN_ASAZ_09911, asaz, TLIMIT_LO_ASAZ_09911,
		  TLIMIT_HI_ASAZ_09911, &pf_total);

	/* Set to PowerDown mode */
	if (AKECS_SetMode(AK09911_MODE_POWERDOWN) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* *********************************************** */
	/* Step2 */
	/* *********************************************** */

	/* Set to SNG measurement pattern (Set CNTL register) */
	if (AKECS_SetMode(AK09911_MODE_SNG_MEASURE) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* Wait for DRDY pin changes to HIGH. */
	/* usleep(AKM_MEASURE_TIME_US); */
	/* Get measurement data from AK09911 */
	/* ST1 + (HXL + HXH) + (HYL + HYH) + (HZL + HZH) + TEMP + ST2 */
	/* = 1 + (1 + 1) + (1 + 1) + (1 + 1) + 1 + 1 = 9yte */
	/* if (AKD_GetMagneticData(i2cData) != AKD_SUCCESS) { */
	if (AKECS_GetData(i2cData, SENSOR_DATA_SIZE) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* hdata[0] = (int)((((uint)(i2cData[2]))<<8)+(uint)(i2cData[1])); */
	/* hdata[1] = (int)((((uint)(i2cData[4]))<<8)+(uint)(i2cData[3])); */
	/* hdata[2] = (int)((((uint)(i2cData[6]))<<8)+(uint)(i2cData[5])); */

	hdata[0] = (int16_t) (i2cData[1] | (i2cData[2] << 8));
	hdata[1] = (int16_t) (i2cData[3] | (i2cData[4] << 8));
	hdata[2] = (int16_t) (i2cData[5] | (i2cData[6] << 8));

	/* TEST */
	i2cData[0] &= 0x7F;
	TEST_DATA(TLIMIT_NO_SNG_ST1_09911, TLIMIT_TN_SNG_ST1_09911, (int)i2cData[0],
		  TLIMIT_LO_SNG_ST1_09911, TLIMIT_HI_SNG_ST1_09911, &pf_total);

	/* TEST */
	TEST_DATA(TLIMIT_NO_SNG_HX_09911, TLIMIT_TN_SNG_HX_09911, hdata[0], TLIMIT_LO_SNG_HX_09911,
		  TLIMIT_HI_SNG_HX_09911, &pf_total);
	TEST_DATA(TLIMIT_NO_SNG_HY_09911, TLIMIT_TN_SNG_HY_09911, hdata[1], TLIMIT_LO_SNG_HY_09911,
		  TLIMIT_HI_SNG_HY_09911, &pf_total);
	TEST_DATA(TLIMIT_NO_SNG_HZ_09911, TLIMIT_TN_SNG_HZ_09911, hdata[2], TLIMIT_LO_SNG_HZ_09911,
		  TLIMIT_HI_SNG_HZ_09911, &pf_total);
	TEST_DATA(TLIMIT_NO_SNG_ST2_09911, TLIMIT_TN_SNG_ST2_09911, (int)i2cData[8],
		  TLIMIT_LO_SNG_ST2_09911, TLIMIT_HI_SNG_ST2_09911, &pf_total);

	/* Set to Self-test mode (Set CNTL register) */
	if (AKECS_SetMode(AK09911_MODE_SELF_TEST) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* Wait for DRDY pin changes to HIGH. */
	/* usleep(AKM_MEASURE_TIME_US); */
	/* Get measurement data from AK09911 */
	/* ST1 + (HXL + HXH) + (HYL + HYH) + (HZL + HZH) + TEMP + ST2 */
	/* = 1 + (1 + 1) + (1 + 1) + (1 + 1) + 1 + 1 = 9byte */
	/* if (AKD_GetMagneticData(i2cData) != AKD_SUCCESS) { */
	if (AKECS_GetData(i2cData, SENSOR_DATA_SIZE) < 0) {
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		return 0;
	}

	/* TEST */
	i2cData[0] &= 0x7F;
	TEST_DATA(TLIMIT_NO_SLF_ST1_09911, TLIMIT_TN_SLF_ST1_09911, (int)i2cData[0],
		  TLIMIT_LO_SLF_ST1_09911, TLIMIT_HI_SLF_ST1_09911, &pf_total);

	/* hdata[0] = (int)((((uint)(i2cData[2]))<<8)+(uint)(i2cData[1])); */
	/* hdata[1] = (int)((((uint)(i2cData[4]))<<8)+(uint)(i2cData[3])); */
	/* hdata[2] = (int)((((uint)(i2cData[6]))<<8)+(uint)(i2cData[5])); */

	hdata[0] = (int16_t) (i2cData[1] | (i2cData[2] << 8));
	hdata[1] = (int16_t) (i2cData[3] | (i2cData[4] << 8));
	hdata[2] = (int16_t) (i2cData[5] | (i2cData[6] << 8));

	/* TEST */
	TEST_DATA(TLIMIT_NO_SLF_RVHX_09911,
		  TLIMIT_TN_SLF_RVHX_09911,
		  (hdata[0]) * (asax / 128 + 1),
		  TLIMIT_LO_SLF_RVHX_09911, TLIMIT_HI_SLF_RVHX_09911, &pf_total);

	TEST_DATA(TLIMIT_NO_SLF_RVHY_09911,
		  TLIMIT_TN_SLF_RVHY_09911,
		  (hdata[1]) * (asay / 128 + 1),
		  TLIMIT_LO_SLF_RVHY_09911, TLIMIT_HI_SLF_RVHY_09911, &pf_total);

	TEST_DATA(TLIMIT_NO_SLF_RVHZ_09911,
		  TLIMIT_TN_SLF_RVHZ_09911,
		  (hdata[2]) * (asaz / 128 + 1),
		  TLIMIT_LO_SLF_RVHZ_09911, TLIMIT_HI_SLF_RVHZ_09911, &pf_total);

	TEST_DATA(TLIMIT_NO_SLF_ST2_09911,
		  TLIMIT_TN_SLF_ST2_09911,
		  (int)i2cData[8], TLIMIT_LO_SLF_ST2_09911, TLIMIT_HI_SLF_ST2_09911, &pf_total);

	return pf_total;
}

int FctShipmntTestProcess_Body(void)
{
	int pf_total = 1;

	/* *********************************************** */
	/* Reset Test Result */
	/* *********************************************** */
	TEST_DATA(NULL, "START", 0, 0, 0, &pf_total);

	/* *********************************************** */
	/* Step 1 to 2 */
	/* *********************************************** */
#ifdef AKM_Device_AK8963
	pf_total = FST_AK8963();
#else
	pf_total = FST_AK09911();
#endif

	/* *********************************************** */
	/* Judge Test Result */
	/* *********************************************** */
	TEST_DATA(NULL, "END", 0, 0, 0, &pf_total);

	return pf_total;
}

static ssize_t store_shipment_test(struct device_driver *ddri, const char *buf, size_t count)
{
	/* struct i2c_client *client = this_client; */
	/* struct akm09911_i2c_data *data = i2c_get_clientdata(client); */
	/* int layout = 0; */


	return count;
}

static ssize_t show_shipment_test(struct device_driver *ddri, char *buf)
{
	char result[10];
	int res = 0;

	res = FctShipmntTestProcess_Body();
	if (1 == res) {
		MAGN_LOG("shipment_test pass\n");
		strlcpy(result, "y", sizeof(result));
	} else if (-1 == res) {
		MAGN_LOG("shipment_test fail\n");
		strlcpy(result, "n", sizeof(result));
	} else {
		MAGN_LOG("shipment_test NaN\n");
		strlcpy(result, "NaN", sizeof(result));
	}

	return sprintf(buf, "%s\n", result);
}

static ssize_t show_daemon_name(struct device_driver *ddri, char *buf)
{
	char strbuf[AKM09911_BUFSIZE];

	sprintf(strbuf, "akmd09911");
	return sprintf(buf, "%s", strbuf);
}

static ssize_t show_chipinfo_value(struct device_driver *ddri, char *buf)
{
	char strbuf[AKM09911_BUFSIZE];

	akm09911_ReadChipInfo(strbuf, AKM09911_BUFSIZE);
	return sprintf(buf, "%s\n", strbuf);
}

/*----------------------------------------------------------------------------*/
static ssize_t show_sensordata_value(struct device_driver *ddri, char *buf)
{

	char sensordata[SENSOR_DATA_SIZE];
	char strbuf[AKM09911_BUFSIZE];

	AKECS_SetMode_SngMeasure();
	mdelay(10);
	AKECS_GetData(sensordata, SENSOR_DATA_SIZE);

	sprintf(strbuf, "%d %d %d %d %d %d %d %d %d\n", sensordata[0], sensordata[1], sensordata[2],
		sensordata[3], sensordata[4], sensordata[5], sensordata[6], sensordata[7],
		sensordata[8]);

	return sprintf(buf, "%s\n", strbuf);
}

/*----------------------------------------------------------------------------*/
static ssize_t show_layout_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = this_client;
	struct akm09911_i2c_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "(%d, %d)\n[%+2d %+2d %+2d]\n[%+2d %+2d %+2d]\n",
		       data->hw.direction, atomic_read(&data->layout), data->cvt.sign[0],
		       data->cvt.sign[1], data->cvt.sign[2], data->cvt.map[0], data->cvt.map[1],
		       data->cvt.map[2]);
}

/*----------------------------------------------------------------------------*/
static ssize_t store_layout_value(struct device_driver *ddri, const char *buf, size_t count)
{
	struct i2c_client *client = this_client;
	struct akm09911_i2c_data *data = i2c_get_clientdata(client);
	int layout = 0;
	int ret = 0;

	ret = kstrtoint(buf, 10, &layout);
	if (ret != 0) {
		atomic_set(&data->layout, layout);
		if (!hwmsen_get_convert(layout, &data->cvt))
			MAGN_ERR("HWMSEN_GET_CONVERT function error!\r\n");
		else if (!hwmsen_get_convert(data->hw.direction, &data->cvt))
			MAGN_ERR("invalid layout: %d, restore to %d\n", layout,
				 data->hw.direction);
		else {
			MAGN_ERR("invalid layout: (%d, %d)\n", layout, data->hw.direction);
			ret = hwmsen_get_convert(0, &data->cvt);
			if (!ret)
				MAGN_ERR("HWMSEN_GET_CONVERT function error!\r\n");
		}
	} else
		MAGN_ERR("invalid format = '%s'\n", buf);

	return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t show_status_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = this_client;
	struct akm09911_i2c_data *data = i2c_get_clientdata(client);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "CUST: %d %d (%d %d)\n",
		data->hw.i2c_num, data->hw.direction, data->hw.power_id,
		data->hw.power_vol);

	len += snprintf(buf + len, PAGE_SIZE - len, "OPEN: %d\n", atomic_read(&dev_open_count));
	return len;
}

/*----------------------------------------------------------------------------*/
static ssize_t show_trace_value(struct device_driver *ddri, char *buf)
{
	ssize_t res;
	struct akm09911_i2c_data *obj = i2c_get_clientdata(this_client);

	if (NULL == obj) {
		MAGN_ERR("akm09911_i2c_data is null!!\n");
		return 0;
	}

	res = snprintf(buf, PAGE_SIZE, "0x%04X\n", atomic_read(&obj->trace));
	return res;
}

/*----------------------------------------------------------------------------*/
static ssize_t store_trace_value(struct device_driver *ddri, const char *buf, size_t count)
{
	struct akm09911_i2c_data *obj = i2c_get_clientdata(this_client);
	int trace;

	if (NULL == obj) {
		MAGN_ERR("akm09911_i2c_data is null!!\n");
		return 0;
	}

	if (1 == sscanf(buf, "0x%x", &trace))
		atomic_set(&obj->trace, trace);
	else
		MAGN_ERR("invalid content: '%s', length = %zu\n", buf, count);

	return count;
}

static ssize_t show_chip_orientation(struct device_driver *ddri, char *buf)
{
	ssize_t _tLength = 0;
	struct akm09911_i2c_data *_pt_i2c_obj = i2c_get_clientdata(this_client);

	if (NULL == _pt_i2c_obj)
		return 0;

	MAGN_LOG("[%s] default direction: %d\n", __func__, _pt_i2c_obj->hw.direction);

	_tLength = snprintf(buf, PAGE_SIZE, "default direction = %d\n",
		_pt_i2c_obj->hw.direction);

	return _tLength;
}

static ssize_t store_chip_orientation(struct device_driver *ddri, const char *buf, size_t tCount)
{
	int _nDirection = 0;
	int ret = 0;
	struct akm09911_i2c_data *_pt_i2c_obj = i2c_get_clientdata(this_client);

	if (NULL == _pt_i2c_obj)
		return 0;

	ret = kstrtoint(buf, 10, &_nDirection);
	if (ret != 0) {
		if (hwmsen_get_convert(_nDirection, &_pt_i2c_obj->cvt))
			MAGN_ERR("ERR: fail to set direction\n");
	}

	MAGN_LOG("[%s] set direction: %d\n", __func__, _nDirection);

	return tCount;
}

static ssize_t show_power_status(struct device_driver *ddri, char *buf)
{
	int ret = 0;
	ssize_t res = 0;
	u8 uData = AK09911_REG_CNTL2;
	struct akm09911_i2c_data *obj = i2c_get_clientdata(this_client);

	if (obj == NULL) {
		MAGN_ERR("i2c_data obj is null!!\n");
		return 0;
	}
	ret = AKI2C_RxData(&uData, 1);
	if (ret < 0)
		MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
	res = snprintf(buf, PAGE_SIZE, "0x%04X\n", uData);
	return res;
}

static ssize_t show_regiter_map(struct device_driver *ddri, char *buf)
{
	u8 _bIndex = 0;
	u8 _baRegMap[] = {
	    0x00, 0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
		0x30, 0x31, 0x32, 0x33, 0x60, 0x61, 0x62
	};
	/* u8  _baRegValue[20]; */
	ssize_t _tLength = 0;
	char tmp[2] = { 0 };
	int ret = 0;

	for (_bIndex = 0; _bIndex < 20; _bIndex++) {
		tmp[0] = _baRegMap[_bIndex];
		ret = AKI2C_RxData(tmp, 1);
		if (ret < 0)
			MAGN_LOG("%s:%d Error.\n", __func__, __LINE__);
		_tLength +=
		    snprintf((buf + _tLength), (PAGE_SIZE - _tLength), "Reg[0x%02X]: 0x%02X\n",
			     _baRegMap[_bIndex], tmp[0]);
	}

	return _tLength;
}


/*----------------------------------------------------------------------------*/
static DRIVER_ATTR(daemon, S_IRUGO, show_daemon_name, NULL);
static DRIVER_ATTR(shipmenttest, S_IRUGO | S_IWUSR, show_shipment_test, store_shipment_test);
static DRIVER_ATTR(chipinfo, S_IRUGO, show_chipinfo_value, NULL);
static DRIVER_ATTR(sensordata, S_IRUGO, show_sensordata_value, NULL);
static DRIVER_ATTR(layout, S_IRUGO | S_IWUSR, show_layout_value, store_layout_value);
static DRIVER_ATTR(status, S_IRUGO, show_status_value, NULL);
static DRIVER_ATTR(trace, S_IRUGO | S_IWUSR, show_trace_value, store_trace_value);
static DRIVER_ATTR(orientation, S_IWUSR | S_IRUGO, show_chip_orientation, store_chip_orientation);
static DRIVER_ATTR(power, S_IRUGO, show_power_status, NULL);
static DRIVER_ATTR(regmap, S_IRUGO, show_regiter_map, NULL);

/*----------------------------------------------------------------------------*/
static struct driver_attribute *akm09911_attr_list[] = {
	&driver_attr_daemon,
	&driver_attr_shipmenttest,
	&driver_attr_chipinfo,
	&driver_attr_sensordata,
	&driver_attr_layout,
	&driver_attr_status,
	&driver_attr_trace,
	&driver_attr_orientation,
	&driver_attr_power,
	&driver_attr_regmap,
};

/*----------------------------------------------------------------------------*/
static int akm09911_create_attr(struct device_driver *driver)
{
	int idx, err = 0;
	int num = (int)(sizeof(akm09911_attr_list) / sizeof(akm09911_attr_list[0]));

	if (driver == NULL)
		return -EINVAL;

	for (idx = 0; idx < num; idx++) {
		err = driver_create_file(driver, akm09911_attr_list[idx]);
		if (err) {
			MAGN_ERR("driver_create_file (%s) = %d\n",
				 akm09911_attr_list[idx]->attr.name, err);
			break;
		}
	}
	return err;
}

/*----------------------------------------------------------------------------*/
static int akm09911_delete_attr(struct device_driver *driver)
{
	int idx , err = 0;
	int num = (int)(sizeof(akm09911_attr_list)/sizeof(akm09911_attr_list[0]));

	if (driver == NULL)
		return -EINVAL;

	for (idx = 0; idx < num; idx++)
		driver_remove_file(driver, akm09911_attr_list[idx]);

	return err;
}

/*----------------------------------------------------------------------------*/
static int akm09911_suspend(struct i2c_client *client, pm_message_t msg)
{
	return 0;
}

/*----------------------------------------------------------------------------*/
static int akm09911_resume(struct i2c_client *client)
{
	return 0;
}

/*----------------------------------------------------------------------------*/
static int akm09911_i2c_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	strlcpy(info->type, AKM09911_DEV_NAME, sizeof(info->type));
	return 0;
}

static int akm09911_enable(int en)
{
	int value = 0;
	int err = 0;
	struct akm09911_i2c_data *f_obj = i2c_get_clientdata(this_client);

	if (NULL == f_obj)
		return -1;

	value = en;
	factory_mode = 1;
	if (value == 1) {
		f_obj->enable = true;
		err = AKECS_SetMode(AK09911_MODE_SNG_MEASURE);
		if (err < 0) {
			MAGN_ERR("%s:AKECS_SetMode Error.\n", __func__);
			return err;
		}
	} else {
		f_obj->enable = false;
		err = AKECS_SetMode(AK09911_MODE_POWERDOWN);
		if (err < 0) {
			MAGN_ERR("%s:AKECS_SetMode Error.\n", __func__);
			return err;
		}
	}
	if (f_obj->flush) {
		if (value == 1) {
			MAGN_LOG("will call akm09911_flush in akm09911_enable\n");
			akm09911_flush();
		} else
			f_obj->flush = false;
	}
	wake_up(&open_wq);
	return err;
}

static int akm09911_set_delay(u64 ns)
{
	int value = 0;

	value = (int)ns / 1000 / 1000;

	if (value <= 10)
		akmd_delay = 10;
	else
		akmd_delay = value;

	return 0;
}

static int akm09911_open_report_data(int open)
{
	return 0;
}

static int akm09911_coordinate_convert(int16_t *mag_data)
{
	struct i2c_client *client = this_client;
	struct akm09911_i2c_data *data = i2c_get_clientdata(client);
	int16_t temp_data[3];
	int i = 0;

	for (i = 0; i < 3; i++)
		temp_data[i] = mag_data[i];
	/* remap coordinate */
	mag_data[data->cvt.map[AKM099XX_AXIS_X]] =
		data->cvt.sign[AKM099XX_AXIS_X] * temp_data[AKM099XX_AXIS_X];
	mag_data[data->cvt.map[AKM099XX_AXIS_Y]] =
		data->cvt.sign[AKM099XX_AXIS_Y] * temp_data[AKM099XX_AXIS_Y];
	mag_data[data->cvt.map[AKM099XX_AXIS_Z]] =
		data->cvt.sign[AKM099XX_AXIS_Z] * temp_data[AKM099XX_AXIS_Z];
	return 0;
}
static int akm09911_get_data(int *x, int *y, int *z, int *status)
{
	char strbuf[SENSOR_DATA_SIZE];
	int16_t data[3];

	AKECS_SetMode_SngMeasure();
	mdelay(10);

	AKECS_GetData(strbuf, SENSOR_DATA_SIZE);
	data[0] = (int16_t)(strbuf[1] | (strbuf[2] << 8));
	data[1] = (int16_t)(strbuf[3] | (strbuf[4] << 8));
	data[2] = (int16_t)(strbuf[5] | (strbuf[6] << 8));

	akm09911_coordinate_convert(data);

	if (akm_device == 0x04) {/* ak09912 */
		*x = data[0] * CONVERT_M_DIV * AKECS_ASA_CACULATE_AK09912(akm_fuse[0]);
		*y = data[1] * CONVERT_M_DIV * AKECS_ASA_CACULATE_AK09912(akm_fuse[1]);
		*z = data[2] * CONVERT_M_DIV * AKECS_ASA_CACULATE_AK09912(akm_fuse[2]);
	} else if (akm_device == 0x05) {
		*x = data[0] * CONVERT_M_DIV * AKECS_ASA_CACULATE_AK09911(akm_fuse[0]);
		*y = data[1] * CONVERT_M_DIV * AKECS_ASA_CACULATE_AK09911(akm_fuse[1]);
		*z = data[2] * CONVERT_M_DIV * AKECS_ASA_CACULATE_AK09911(akm_fuse[2]);
	} else if ((akm_device == 0x10) || (akm_device == 0x09) ||
		(akm_device == 0x0b) || (akm_device == 0x0c)) {
		*x = data[0] * CONVERT_M_DIV;
		*y = data[1] * CONVERT_M_DIV;
		*z = data[2] * CONVERT_M_DIV;
	}
	*status = strbuf[8];
	return 0;
}

static int akm09911_batch(int flag, int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
	int value = 0;

	value = (int)samplingPeriodNs / 1000 / 1000;

	if (value <= 10)
		akmd_delay = 10;
	else
		akmd_delay = value;

	MAGN_LOG("akm09911 mag set delay = (%d) ok.\n", value);
	return 0;
}

static int akm09911_flush(void)
{
	/*Only flush after sensor was enabled*/
	int err = 0;
	struct akm09911_i2c_data *f_obj = i2c_get_clientdata(this_client);

	if (NULL == f_obj)
		return -1;

	if (!f_obj->enable) {
		f_obj->flush = true;
		return 0;
	}
	err = mag_flush_report();
	if (err >= 0)
		f_obj->flush = false;
	return err;
}

static int akm09911_factory_enable_sensor(bool enabledisable, int64_t sample_periods_ms)
{
	int err;

	err = akm09911_enable(enabledisable == true ? 1 : 0);
	if (err) {
		MAGN_ERR("%s enable sensor failed!\n", __func__);
		return -1;
	}
	err = akm09911_batch(0, sample_periods_ms * 1000000, 0);
	if (err) {
		MAGN_ERR("%s enable set batch failed!\n", __func__);
		return -1;
	}
	return 0;
}
static int akm09911_factory_get_data(int32_t data[3], int *status)
{
	/* get raw data */
	return  akm09911_get_data(&data[0], &data[1], &data[2], status);
}
static int akm09911_factory_get_raw_data(int32_t data[3])
{
	MAGN_LOG("do not support akm09911_factory_get_raw_data!\n");
	return 0;
}
static int akm09911_factory_enable_calibration(void)
{
	return 0;
}
static int akm09911_factory_clear_cali(void)
{
	return 0;
}
static int akm09911_factory_set_cali(int32_t data[3])
{
	return 0;
}
static int akm09911_factory_get_cali(int32_t data[3])
{
	return 0;
}
static int akm09911_factory_do_self_test(void)
{
	return 0;
}

static struct mag_factory_fops akm09911_factory_fops = {
	.enable_sensor = akm09911_factory_enable_sensor,
	.get_data = akm09911_factory_get_data,
	.get_raw_data = akm09911_factory_get_raw_data,
	.enable_calibration = akm09911_factory_enable_calibration,
	.clear_cali = akm09911_factory_clear_cali,
	.set_cali = akm09911_factory_set_cali,
	.get_cali = akm09911_factory_get_cali,
	.do_self_test = akm09911_factory_do_self_test,
};

static struct mag_factory_public akm09911_factory_device = {
	.gain = 1,
	.sensitivity = 1,
	.fops = &akm09911_factory_fops,
};

/*----------------------------------------------------------------------------*/
static int akm09911_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int err = 0;
	struct i2c_client *new_client = NULL;
	struct akm09911_i2c_data *data = NULL;
	struct mag_control_path ctl = { 0 };
	struct mag_data_path mag_data = { 0 };

	MAGN_LOG("akm09911_i2c_probe\n");
	data = kzalloc(sizeof(struct akm09911_i2c_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	err = get_mag_dts_func(client->dev.of_node, &data->hw);
	if (err < 0) {
		MAGN_ERR("get dts info fail\n");
		err = -EFAULT;
		goto exit_kfree;
	}

	/*data->hw.direction from dts is for AKMD, rang is 1-8*/
	/*now use akm09911_coordinate_convert api, so the rang is 0-7 */
	data->hw.direction--;
	err = hwmsen_get_convert(data->hw.direction, &data->cvt);
	if (err) {
		MAGN_ERR("invalid direction: %d\n", data->hw.direction);
		goto exit_kfree;
	}
	atomic_set(&data->layout, data->hw.direction);
	atomic_set(&data->trace, 0);
	/* init_waitqueue_head(&data_ready_wq); */
	init_waitqueue_head(&open_wq);
	data->client = client;
	new_client = data->client;
	i2c_set_clientdata(new_client, data);
	this_client = new_client;

	/* Check connection */
	err = AKECS_CheckDevice();
	if (err < 0) {
		MAGN_ERR("AKM09911 akm09911_probe: check device connect error\n");
		goto exit_init_failed;
	}

	err = mag_factory_device_register(&akm09911_factory_device);
	if (err) {
		MAGN_ERR("misc device register failed, err = %d\n", err);
		goto exit_misc_device_register_failed;
	}


	/* Register sysfs attribute */
	err = akm09911_create_attr(&(akm09911_init_info.platform_diver_addr->driver));
	if (err) {
		MAGN_ERR("create attribute err = %d\n", err);
		goto exit_sysfs_create_group_failed;
	}

	ctl.is_use_common_factory = false;
	ctl.enable = akm09911_enable;
	ctl.set_delay = akm09911_set_delay;
	ctl.open_report_data = akm09911_open_report_data;
	ctl.batch = akm09911_batch;
	ctl.flush = akm09911_flush;
	ctl.is_report_input_direct = false;
	ctl.is_support_batch = data->hw.is_batch_supported;
	strlcpy(ctl.libinfo.libname, "akl", sizeof(ctl.libinfo.libname));
	ctl.libinfo.layout = AKECS_SetCert();
	ctl.libinfo.deviceid = akm_device;

	err = mag_register_control_path(&ctl);
	if (err) {
		MAGN_ERR("register mag control path err\n");
		goto exit_kfree;
	}

	mag_data.div = CONVERT_M_DIV;
	mag_data.get_data = akm09911_get_data;

	err = mag_register_data_path(&mag_data);
	if (err) {
		MAGN_ERR("register data control path err\n");
		goto exit_kfree;
	}

	MAGN_ERR("%s: OK\n", __func__);
	akm09911_init_flag = 1;
	return 0;

exit_sysfs_create_group_failed:
exit_init_failed:
exit_misc_device_register_failed:
exit_kfree:
	kfree(data);
exit:
	MAGN_ERR("%s: err = %d\n", __func__, err);
	akm09911_init_flag = -1;
	data = NULL;
	new_client = NULL;
	this_client = NULL;
	return err;
}

/*----------------------------------------------------------------------------*/
static int akm09911_i2c_remove(struct i2c_client *client)
{
	int err;

	err = akm09911_delete_attr(&(akm09911_init_info.platform_diver_addr->driver));
	if (err)
		MAGN_ERR("akm09911_delete_attr fail: %d\n", err);

	this_client = NULL;
	i2c_unregister_device(client);
	mag_factory_device_deregister(&akm09911_factory_device);
	kfree(i2c_get_clientdata(client));
	return 0;
}

/*----------------------------------------------------------------------------*/
static int akm09911_remove(void)
{
	atomic_set(&dev_open_count, 0);
	i2c_del_driver(&akm09911_i2c_driver);
	return 0;
}

static int akm09911_local_init(void)
{
	if (i2c_add_driver(&akm09911_i2c_driver)) {
		MAGN_ERR("i2c_add_driver error\n");
		return -1;
	}
	if (-1 == akm09911_init_flag)
		return -1;
	return 0;
}

/*----------------------------------------------------------------------------*/
static int __init akm09911_init(void)
{
	mag_driver_add(&akm09911_init_info);
	return 0;
}

/*----------------------------------------------------------------------------*/
static void __exit akm09911_exit(void)
{
#ifdef CONFIG_CUSTOM_KERNEL_MAGNETOMETER_MODULE
	mag_success_Flag = false;
#endif
}

/*----------------------------------------------------------------------------*/
module_init(akm09911_init);
module_exit(akm09911_exit);

MODULE_AUTHOR("MTK");
MODULE_DESCRIPTION("AKM09911 compass driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);
