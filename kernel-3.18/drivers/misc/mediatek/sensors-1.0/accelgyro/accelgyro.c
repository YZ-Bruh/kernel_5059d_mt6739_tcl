
#include <linux/vmalloc.h>
#include "inc/accelgyro.h"
#include "inc/accelgyro_factory.h"
#include "sensor_performance.h"

struct acc_context *acc_context_obj /* = NULL */;
struct gyro_context *gyro_context_obj /* = NULL */;
struct accelgyro_timer_context *timer_obj;

static struct accelgyro_init_info *accgyro_sensor_init_list[MAX_CHOOSE_G_NUM] = { 0 };

static int64_t getCurNS(void)
{
	int64_t ns;
	struct timespec time;

	time.tv_sec = time.tv_nsec = 0;
	get_monotonic_boottime(&time);
	ns = time.tv_sec * 1000000000LL + time.tv_nsec;

	return ns;
}

static void initTimer(struct hrtimer *timer, enum hrtimer_restart (*callback) (struct hrtimer *))
{
	hrtimer_init(timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	timer->function = callback;
}

static void startTimer(int delay_ms, bool first)
{
	struct accelgyro_timer_context *obj = timer_obj;

	static int count;

	if (obj == NULL) {
		ACC_PR_ERR("NULL pointer\n");
		return;
	}

	if (first) {
		obj->target_ktime = ktime_add_ns(ktime_get(), (int64_t) delay_ms * 1000000);
#if 0
		ACC_LOG("%d, cur_nt = %lld, delay_ms = %d, target_nt = %lld\n", count,
			getCurNT(), delay_ms, ktime_to_us(obj->target_ktime));
#endif
		count = 0;
	} else {
		do {
			obj->target_ktime = ktime_add_ns(obj->target_ktime, (int64_t) delay_ms * 1000000);
		} while (ktime_to_ns(obj->target_ktime) < ktime_to_ns(ktime_get()));
#if 0
		ACC_LOG("%d, cur_nt = %lld, delay_ms = %d, target_nt = %lld\n", count,
			getCurNT(), delay_ms, ktime_to_us(obj->target_ktime));
#endif
		count++;
	}

	hrtimer_start(&obj->hrTimer, obj->target_ktime, HRTIMER_MODE_ABS);
}

static void acc_work_func(void)
{
	struct acc_context *cxt = NULL;
	int x, y, z, status;
	int64_t pre_ns, cur_ns;
	int64_t delay_ms;
	int err;

	cxt = acc_context_obj;
	delay_ms = atomic_read(&cxt->delay);

	if (cxt->acc_data.get_data == NULL) {
		ACC_PR_ERR("acc driver not register data path\n");
		return;
	}

	cur_ns = getCurNS();

	err = cxt->acc_data.get_data(&x, &y, &z, &status);

	if (err) {
		ACC_PR_ERR("get acc data fails!!\n");
		goto acc_loop;
	} else {
		if (0 == x && 0 == y && 0 == z)
			goto acc_loop;

		cxt->drv_data.x = x;
		cxt->drv_data.y = y;
		cxt->drv_data.z = z;
		cxt->drv_data.status = status;
		pre_ns = cxt->drv_data.timestamp;
		cxt->drv_data.timestamp = cur_ns;
	}

	if (true == cxt->is_first_data_after_enable) {
		pre_ns = cur_ns;
		cxt->is_first_data_after_enable = false;
		/* filter -1 value */
		if (cxt->drv_data.x == ACC_INVALID_VALUE ||
		    cxt->drv_data.y == ACC_INVALID_VALUE || cxt->drv_data.z == ACC_INVALID_VALUE) {
			ACC_LOG(" read invalid data\n");
			goto acc_loop;

		}
	}
	/* report data to input device */
	/* printk("new acc work run....\n"); */
	/* ACC_LOG("acc data[%d,%d,%d]\n" ,cxt->drv_data.acc_data.values[0], */
	/* cxt->drv_data.acc_data.values[1],cxt->drv_data.acc_data.values[2]); */

	while ((cur_ns - pre_ns) >= delay_ms * 1800000LL) {
		struct acc_data tmp_data = cxt->drv_data;

		pre_ns += delay_ms * 1000000LL;
		tmp_data.timestamp = pre_ns;
		acc_data_report(&tmp_data);
	}

	acc_data_report(&cxt->drv_data);
	return;

acc_loop:
	if (true == cxt->is_polling_run)
		startTimer(atomic_read(&timer_obj->delay), false);
}

static void gyro_work_func(void)
{

	struct gyro_context *cxt = NULL;
	int x, y, z, status;
	int64_t pre_ns, cur_ns;
	int64_t delay_ms;
	int err = 0;

	cxt = gyro_context_obj;
	delay_ms = atomic_read(&cxt->delay);

	if (cxt->gyro_data.get_data == NULL)
		GYRO_PR_ERR("gyro driver not register data path\n");


	cur_ns = getCurNS();

	/* add wake lock to make sure data can be read before system suspend */
	cxt->gyro_data.get_data(&x, &y, &z, &status);

	if (err) {
		GYRO_PR_ERR("get gyro data fails!!\n");
		goto gyro_loop;
	} else {
		cxt->drv_data.x = x;
		cxt->drv_data.y = y;
		cxt->drv_data.z = z;
		cxt->drv_data.status = status;
		pre_ns = cxt->drv_data.timestamp;
		cxt->drv_data.timestamp = cur_ns;
	}

	if (true == cxt->is_first_data_after_enable) {
		pre_ns = cur_ns;
		cxt->is_first_data_after_enable = false;
		/* filter -1 value */
		if (cxt->drv_data.x == GYRO_INVALID_VALUE ||
		    cxt->drv_data.y == GYRO_INVALID_VALUE ||
		    cxt->drv_data.z == GYRO_INVALID_VALUE) {
			GYRO_LOG(" read invalid data\n");
			goto gyro_loop;
		}
	}

	/* GYRO_LOG("gyro data[%d,%d,%d]\n" ,cxt->drv_data.gyro_data.values[0], */
	/* cxt->drv_data.gyro_data.values[1],cxt->drv_data.gyro_data.values[2]); */

	while ((cur_ns - pre_ns) >= delay_ms * 1800000LL) {
		struct gyro_data tmp_data = cxt->drv_data;

		pre_ns += delay_ms * 1000000LL;
		tmp_data.timestamp = pre_ns;
		gyro_data_report(&tmp_data);
	}

	gyro_data_report(&cxt->drv_data);
	return;

gyro_loop:
	if (true == cxt->is_polling_run)
		startTimer(atomic_read(&timer_obj->delay), false);
}

static void accelgyro_work_func(struct work_struct *work)
{
	struct accelgyro_timer_context *t_obj = NULL;
	struct acc_context *acc_cxt = NULL;
	struct gyro_context *gyro_cxt = NULL;

	/* if acc enable, poll and report acc data */
	acc_cxt = acc_context_obj;
	if (acc_cxt->power == 1)
		acc_work_func();
	/* if gyro enable, poll and report gyro data */
	gyro_cxt = gyro_context_obj;
	if (gyro_cxt->power == 1)
		gyro_work_func();

	t_obj = timer_obj;

	if ((true == acc_cxt->is_polling_run) || (true == gyro_cxt->is_polling_run))
		startTimer(atomic_read(&t_obj->delay), false);
}

enum hrtimer_restart accelgyro_poll(struct hrtimer *timer)
{
	struct accelgyro_timer_context *obj =
	    (struct accelgyro_timer_context *)container_of(timer, struct accelgyro_timer_context, hrTimer);

	queue_work(obj->accelgyro_workqueue, &obj->report);
	/* ACC_LOG("accelgyro_poll end, queue_work accelworkqueue"); */

	return HRTIMER_NORESTART;
}

static struct acc_context *acc_context_alloc_object(void)
{

	struct acc_context *obj = kzalloc(sizeof(*obj), GFP_KERNEL);

	ACC_LOG("acc_context_alloc_object++++\n");
	if (!obj) {
		ACC_PR_ERR("Alloc accel object error!\n");
		return NULL;
	}
	atomic_set(&obj->delay, 200);	/*5Hz ,  set work queue delay time 200ms */
	atomic_set(&obj->wake, 0);

	obj->is_active_nodata = false;
	obj->is_active_data = false;
	obj->is_first_data_after_enable = false;
	obj->is_polling_run = false;
	mutex_init(&obj->acc_op_mutex);
	obj->is_batch_enable = false;	/* for batch mode init */
	obj->cali_sw[ACC_AXIS_X] = 0;
	obj->cali_sw[ACC_AXIS_Y] = 0;
	obj->cali_sw[ACC_AXIS_Z] = 0;
	obj->power = 0;
	obj->enable = 0;
	obj->delay_ns = -1;
	obj->latency_ns = -1;
	obj->open_sensor = false;	/*add to control stopTimer */
	ACC_LOG("acc_context_alloc_object----\n");
	return obj;
}

static struct gyro_context *gyro_context_alloc_object(void)
{

	struct gyro_context *obj = kzalloc(sizeof(*obj), GFP_KERNEL);

	GYRO_LOG("gyro_context_alloc_object++++\n");
	if (!obj) {
		GYRO_PR_ERR("Alloc gyro object error!\n");
		return NULL;
	}
	atomic_set(&obj->delay, 200);	/*5Hz,  set work queue delay time 200ms */
	atomic_set(&obj->wake, 0);

	obj->is_active_nodata = false;
	obj->is_active_data = false;
	obj->is_first_data_after_enable = false;
	obj->is_polling_run = false;
	obj->is_batch_enable = false;
	obj->cali_sw[GYRO_AXIS_X] = 0;
	obj->cali_sw[GYRO_AXIS_Y] = 0;
	obj->cali_sw[GYRO_AXIS_Z] = 0;
	obj->power = 0;
	obj->enable = 0;
	obj->delay_ns = -1;
	obj->latency_ns = -1;
	obj->open_sensor = false;	/*add to control stopTimer */

	mutex_init(&obj->gyro_op_mutex);
	GYRO_LOG("gyro_context_alloc_object----\n");
	return obj;
}

static void accelgyro_comset_delay(int delay)
{
	struct accelgyro_timer_context *obj = timer_obj;

	ACC_LOG("acc power=%d,gy_power=%d\n", acc_context_obj->power, gyro_context_obj->power);
	if (acc_context_obj->power && gyro_context_obj->power) {	/* acc and gyro both open */
		int acc_delay = (int)acc_context_obj->delay_ns / 1000 / 1000;
		int gyro_delay = (int)gyro_context_obj->delay_ns / 1000 / 1000;

		if (acc_delay > gyro_delay)
			atomic_set(&obj->delay, gyro_delay);
		else
			atomic_set(&obj->delay, acc_delay);
	} else {		/*acc or gyro only one sensor open */
		atomic_set(&obj->delay, delay / 1000 / 1000);
	}
}
#ifndef CONFIG_NANOHUB
static int acc_enable_and_batch(void)
{
	struct acc_context *cxt = acc_context_obj;
	struct accelgyro_timer_context *t_obj = timer_obj;
	int err;

	/* power on -> power off */
	if (cxt->power == 1 && cxt->enable == 0) {
		ACC_LOG("ACC disable\n");
		/* stop polling firstly, if needed */
		if (cxt->is_active_data == false &&
		    cxt->acc_ctl.is_report_input_direct == false && cxt->is_polling_run == true) {
			cxt->open_sensor = false;	/* to control stoptimer */
			/* if gyro also open, set poll rate as gyro */
			if (gyro_context_obj->open_sensor) {
				ACC_LOG("acc will close, gyro also open, swich gyro poll rate and ODR\n");
				atomic_set(&t_obj->delay, (gyro_context_obj->delay_ns) / 1000 / 1000);
				if (gyro_context_obj->gyro_ctl.is_support_batch)
					err = gyro_context_obj->gyro_ctl.batch(0,
						atomic_read(&t_obj->delay) * 1000 * 1000, gyro_context_obj->latency_ns);
				else
					err = gyro_context_obj->gyro_ctl.batch(0,
						atomic_read(&t_obj->delay) * 1000 * 1000, 0);
				if (err) {
					GYRO_PR_ERR("gyro set batch(ODR) err %d\n", err);
					return -1;
				}
				GYRO_LOG("acc close,gyro open set ODR, fifo latency done\n");

			} else {
				ACC_LOG("acc close, stop timer cancel workqueue\n");
				smp_mb();	/* for memory barrier */
				hrtimer_cancel(&t_obj->hrTimer);
				smp_mb();	/* for memory barrier */
				cancel_work_sync(&t_obj->report);	/* cancel when both acc and gyro close */
			}
			cxt->drv_data.x = ACC_INVALID_VALUE;
			cxt->drv_data.y = ACC_INVALID_VALUE;
			cxt->drv_data.z = ACC_INVALID_VALUE;
			cxt->is_polling_run = false;
			ACC_LOG("acc stop polling done\n");
		}
		/* turn off the power */
		if (cxt->is_active_data == false && cxt->is_active_nodata == false) {
			err = cxt->acc_ctl.enable_nodata(0);
			if (err) {
				ACC_PR_ERR("acc turn off power err = %d\n", err);
				return -1;
			}
			ACC_LOG("acc turn off power done\n");
		}

		cxt->power = 0;
		cxt->delay_ns = -1;
		ACC_LOG("ACC disable done\n");
		return 0;
	}
	/* power off -> power on */
	if (cxt->power == 0 && cxt->enable == 1) {
		ACC_LOG("ACC power on\n");
		if (true == cxt->is_active_data || true == cxt->is_active_nodata) {
			err = cxt->acc_ctl.enable_nodata(1);
			if (err) {
				ACC_PR_ERR("acc turn on power err = %d\n", err);
				return -1;
			}
			ACC_LOG("acc turn on power done\n");
		}
		cxt->power = 1;
		cxt->open_sensor = true;
		ACC_LOG("ACC power on done\n");
	}
	/* rate change */
	if (cxt->power == 1 && cxt->delay_ns >= 0) {
		/* set delay to global, compare when acc gyro both open. */
		accelgyro_comset_delay(cxt->delay_ns);

		/* set ODR, fifo timeout latency */
		if (cxt->acc_ctl.is_support_batch) {
			/*set global accgyro delay(min) to bmi160 to set ODR */
			err = cxt->acc_ctl.batch(0, atomic_read(&t_obj->delay) * 1000 * 1000, cxt->latency_ns);
		} else
			/*set global accgyro delay(min) to bmi160 to set ODR */
			err = cxt->acc_ctl.batch(0, atomic_read(&t_obj->delay) * 1000 * 1000, 0);
		if (err) {
			ACC_PR_ERR("acc set batch(ODR) err %d\n", err);
			return -1;
		}
		ACC_LOG("acc set ODR, fifo latency done\n");
		/* start polling, if needed */
		if (cxt->is_active_data == true && cxt->acc_ctl.is_report_input_direct == false) {
			int mdelay = cxt->delay_ns;

			do_div(mdelay, 1000000);
			atomic_set(&cxt->delay, mdelay);
			/* the first sensor start polling timer */
			if (cxt->is_polling_run == false) {
				startTimer(atomic_read(&t_obj->delay), true);
				cxt->is_polling_run = true;
				cxt->is_first_data_after_enable = true;
			}
			ACC_LOG("acc set polling delay %d ms\n", atomic_read(&cxt->delay));
		}
		ACC_LOG("ACC batch done\n");
	}
	/* just for debug, remove it when everything is ok */
	if (cxt->power == 0 && cxt->delay_ns >= 0)
		ACC_LOG("batch will call firstly in API1.3, do nothing\n");

	return 0;
}
#endif
static ssize_t acc_store_enable_nodata(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t count)
{
#if !defined(CONFIG_MTK_SCP_SENSORHUB_V1) && !defined(CONFIG_NANOHUB)
	struct acc_context *cxt = acc_context_obj;
	int err = 0;

	ACC_LOG("acc_store_enable nodata buf=%s\n", buf);
	mutex_lock(&acc_context_obj->acc_op_mutex);
	if (!strncmp(buf, "1", 1)) {
		cxt->enable = 1;
		cxt->is_active_nodata = true;
	} else if (!strncmp(buf, "0", 1)) {
		cxt->enable = 0;
		cxt->is_active_nodata = false;
	} else {
		ACC_PR_ERR(" acc_store enable nodata cmd error !!\n");
		err = -1;
		goto err_out;
	}
	err = acc_enable_and_batch();
err_out:
	mutex_unlock(&acc_context_obj->acc_op_mutex);
	return err;
#endif
	return count;
}

static ssize_t acc_show_enable_nodata(struct device *dev, struct device_attribute *attr, char *buf)
{
	int len = 0;

	ACC_LOG(" not support now\n");
	return len;
}

static ssize_t acc_store_active(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct acc_context *cxt = acc_context_obj;
	int err = 0;

	ACC_LOG("acc_store_active buf=%s\n", buf);
	mutex_lock(&acc_context_obj->acc_op_mutex);
	if (!strncmp(buf, "1", 1)) {
		cxt->enable = 1;
		cxt->is_active_data = true;
	} else if (!strncmp(buf, "0", 1)) {
		cxt->enable = 0;
		cxt->is_active_data = false;
	} else {
		ACC_PR_ERR(" acc_store_active error !!\n");
		err = -1;
		goto err_out;
	}
#ifdef CONFIG_NANOHUB
	if (true == cxt->is_active_data || true == cxt->is_active_nodata) {
		err = cxt->acc_ctl.enable_nodata(1);
		if (err) {
			ACC_PR_ERR("acc turn on power err = %d\n", err);
			goto err_out;
		}
		ACC_LOG("acc turn on power done\n");
	} else {
		err = cxt->acc_ctl.enable_nodata(0);
		if (err) {
			ACC_PR_ERR("acc turn off power err = %d\n", err);
			goto err_out;
		}
		ACC_LOG("acc turn off power done\n");
	}
#else
	err = acc_enable_and_batch();
#endif
err_out:
	mutex_unlock(&acc_context_obj->acc_op_mutex);
	return err;
}

/*----------------------------------------------------------------------------*/
static ssize_t acc_show_active(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct acc_context *cxt = acc_context_obj;
	int div = 0;

	div = cxt->acc_data.vender_div;
	ACC_LOG("acc vender_div value: %d\n", div);
	return snprintf(buf, PAGE_SIZE, "%d\n", div);
}

/* need work around again */
static ssize_t acc_show_sensordevnum(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t acc_store_batch(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct acc_context *cxt = acc_context_obj;
	int handle = 0, flag = 0, err = 0;

	ACC_LOG(" acc_store_batch %s\n", buf);
	err = sscanf(buf, "%d,%d,%lld,%lld", &handle, &flag, &cxt->delay_ns, &cxt->latency_ns);
	if (err != 4) {
		ACC_LOG("acc_store_batch param error: err = %d\n", err);
		return -1;
	}

	mutex_lock(&acc_context_obj->acc_op_mutex);
#ifdef CONFIG_NANOHUB
		if (cxt->acc_ctl.is_support_batch)
			err = cxt->acc_ctl.batch(0, cxt->delay_ns, cxt->latency_ns);
		else
			err = cxt->acc_ctl.batch(0, cxt->delay_ns, 0);
		if (err)
			ACC_PR_ERR("acc set batch(ODR) err %d\n", err);
#else
		err = acc_enable_and_batch();
#endif
	mutex_unlock(&acc_context_obj->acc_op_mutex);
	return err;
}

static ssize_t acc_show_batch(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t acc_store_flush(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct acc_context *cxt = NULL;
	int handle = 0, err = 0;

	err = kstrtoint(buf, 10, &handle);
	if (err != 0)
		ACC_LOG("acc_store_flush param error: err = %d\n", err);

	ACC_LOG("acc_store_flush param: handle %d\n", handle);

	mutex_lock(&acc_context_obj->acc_op_mutex);
	cxt = acc_context_obj;
	if (cxt->acc_ctl.flush != NULL)
		err = cxt->acc_ctl.flush();
	else
		ACC_LOG("ACC DRIVER OLD ARCHITECTURE DON'T SUPPORT ACC COMMON VERSION FLUSH\n");
	if (err < 0)
		ACC_PR_ERR("acc enable flush err %d\n", err);
	mutex_unlock(&acc_context_obj->acc_op_mutex);
	return err;
}

static ssize_t acc_show_flush(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t acc_show_cali(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t acc_store_cali(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct acc_context *cxt = NULL;
	int err = 0;
	uint8_t *cali_buf = NULL;

	cali_buf = vzalloc(count);
	if (cali_buf == NULL) {
		ACC_PR_ERR("kzalloc fail\n");
		return -EFAULT;
	}
	memcpy(cali_buf, buf, count);

	mutex_lock(&acc_context_obj->acc_op_mutex);
	cxt = acc_context_obj;
	if (cxt->acc_ctl.set_cali != NULL)
		err = cxt->acc_ctl.set_cali(cali_buf, count);
	else
		ACC_LOG("ACC DRIVER OLD ARCHITECTURE DON'T SUPPORT ACC COMMON VERSION FLUSH\n");
	if (err < 0)
		ACC_PR_ERR("acc set cali err %d\n", err);
	mutex_unlock(&acc_context_obj->acc_op_mutex);
	vfree(cali_buf);
	return count;
}

#ifndef CONFIG_NANOHUB
static int gyro_enable_and_batch(void)
{
	struct gyro_context *cxt = gyro_context_obj;
	struct accelgyro_timer_context *t_obj = timer_obj;
	int err;

	/* power on -> power off */
	if (cxt->power == 1 && cxt->enable == 0) {
		/* stop polling firstly, if needed */
		if (cxt->is_active_data == false
		    && cxt->gyro_ctl.is_report_input_direct == false
		    && cxt->is_polling_run == true) {
			cxt->open_sensor = false;	/*to control stoptimer */
			/* if acc also open, set poll rate as acc */
			if (acc_context_obj->open_sensor) {
				ACC_LOG("gyro will close, acc also open, swich acc poll rate and ODR\n");
				atomic_set(&t_obj->delay, (acc_context_obj->delay_ns) / 1000 / 1000);
				/* set ODR, fifo timeout latency */
				if (acc_context_obj->acc_ctl.is_support_batch) {
					/*set global accgyro delay(min) to bmi160 to set ODR */
					err = acc_context_obj->acc_ctl.batch(0,
						atomic_read(&t_obj->delay) * 1000 * 1000, acc_context_obj->latency_ns);
				} else
					/*set global accgyro delay(min) to bmi160 to set ODR */
					err = acc_context_obj->acc_ctl.batch(0,
						atomic_read(&t_obj->delay) * 1000 * 1000, 0);
				if (err) {
					ACC_PR_ERR("acc set batch(ODR) err %d\n", err);
					return -1;
				}
				ACC_LOG("gyro close, acc also open set ODR, fifo latency done\n");

			} else {
				GYRO_LOG("gyro close, stop timer and workqueue\n");
				smp_mb();	/* for memory barrier */
				hrtimer_cancel(&t_obj->hrTimer);
				smp_mb();	/* for memory barrier */
				cancel_work_sync(&t_obj->report);
			}

			cxt->drv_data.x = GYRO_INVALID_VALUE;
			cxt->drv_data.y = GYRO_INVALID_VALUE;
			cxt->drv_data.z = GYRO_INVALID_VALUE;
			cxt->is_polling_run = false;
			GYRO_LOG("gyro stop polling done\n");
		}
		/* turn off the power */
		if (cxt->is_active_data == false && cxt->is_active_nodata == false) {
			err = cxt->gyro_ctl.enable_nodata(0);
			if (err) {
				GYRO_PR_ERR("gyro turn off power err = %d\n", err);
				return -1;
			}
			GYRO_LOG("gyro turn off power done\n");
		}

		cxt->power = 0;

		cxt->delay_ns = -1;
		GYRO_LOG("GYRO disable done\n");
		return 0;
	}
	/* power off -> power on */
	if (cxt->power == 0 && cxt->enable == 1) {
		GYRO_LOG("GYRO enable\n");

		if (true == cxt->is_active_data || true == cxt->is_active_nodata) {
			err = cxt->gyro_ctl.enable_nodata(1);
			if (err) {
				GYRO_PR_ERR("gyro turn on power err = %d\n", err);
				return -1;
			}
			GYRO_LOG("gyro turn on power done\n");
		}
		cxt->power = 1;
		cxt->open_sensor = true;	/* add to control stoptimer in future. */
		GYRO_LOG("GYRO enable done\n");
	}
	/* rate change */
	if (cxt->power == 1 && cxt->delay_ns >= 0) {
		GYRO_LOG("GYRO set batch\n");
		/* set ODR, fifo timeout latency */
		accelgyro_comset_delay(cxt->delay_ns);
		if (cxt->gyro_ctl.is_support_batch)
			err = cxt->gyro_ctl.batch(0, atomic_read(&t_obj->delay) * 1000 * 1000, cxt->latency_ns);
		else
			err = cxt->gyro_ctl.batch(0, atomic_read(&t_obj->delay) * 1000 * 1000, 0);
		if (err) {
			GYRO_PR_ERR("gyro set batch(ODR) err %d\n", err);
			return -1;
		}
		GYRO_LOG("gyro set ODR, fifo latency done\n");
		/* start polling, if needed */
		if (cxt->is_active_data == true && cxt->gyro_ctl.is_report_input_direct == false) {
			int mdelay = cxt->delay_ns;

			do_div(mdelay, 1000000);
			atomic_set(&cxt->delay, mdelay);
			/* the first sensor start polling timer */
			if (cxt->is_polling_run == false) {
				startTimer(atomic_read(&t_obj->delay), true);	/*ms */
				cxt->is_polling_run = true;
				cxt->is_first_data_after_enable = true;
			}
			GYRO_LOG("gyro set polling delay %d ms\n", atomic_read(&cxt->delay));
		}
		GYRO_LOG("GYRO batch done\n");
	}
	/* just for debug, remove it when everything is ok */
	if (cxt->power == 0 && cxt->delay_ns >= 0)
		GYRO_LOG("batch will call firstly in API1.3, do nothing\n");

	return 0;
}
#endif

static ssize_t gyro_show_enable_nodata(struct device *dev, struct device_attribute *attr, char *buf)
{
	int len = 0;

	GYRO_LOG(" not support now\n");
	return len;
}

static ssize_t gyro_store_enable_nodata(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct gyro_context *cxt = gyro_context_obj;
	int err = 0;

	GYRO_LOG("gyro_store_enable nodata buf=%s\n", buf);
	mutex_lock(&gyro_context_obj->gyro_op_mutex);
	if (!strncmp(buf, "1", 1)) {
		cxt->enable = 1;
		cxt->is_active_nodata = true;
	} else if (!strncmp(buf, "0", 1)) {
		cxt->enable = 0;
		cxt->is_active_nodata = false;
	} else {
		GYRO_INFO(" gyro_store enable nodata cmd error !!\n");
		err = -1;
		goto err_out;
	}
	err = gyro_enable_and_batch();
err_out:
	mutex_unlock(&gyro_context_obj->gyro_op_mutex);
	return err;
}

static ssize_t gyro_store_active(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct gyro_context *cxt = gyro_context_obj;
	int err = 0;

	GYRO_LOG("gyro_store_active buf=%s\n", buf);
	mutex_lock(&gyro_context_obj->gyro_op_mutex);
	if (!strncmp(buf, "1", 1)) {
		cxt->enable = 1;
		cxt->is_active_data = true;
	} else if (!strncmp(buf, "0", 1)) {
		cxt->enable = 0;
		cxt->is_active_data = false;
	} else {
		GYRO_PR_ERR(" gyro_store_active error !!\n");
		err = -1;
		goto err_out;
	}
#ifdef CONFIG_NANOHUB
	if (true == cxt->is_active_data || true == cxt->is_active_nodata) {
		err = cxt->gyro_ctl.enable_nodata(1);
		if (err) {
			GYRO_PR_ERR("gyro turn on power err = %d\n", err);
			goto err_out;
		}
		GYRO_LOG("gyro turn on power done\n");
	} else {
		err = cxt->gyro_ctl.enable_nodata(0);
		if (err) {
			GYRO_PR_ERR("gyro turn off power err = %d\n", err);
			goto err_out;
		}
		GYRO_LOG("gyro turn off power done\n");
	}
#else
	err = gyro_enable_and_batch();
#endif
err_out:
	mutex_unlock(&gyro_context_obj->gyro_op_mutex);
	GYRO_LOG(" gyro_store_active done\n");
	return err;
}

/*----------------------------------------------------------------------------*/
static ssize_t gyro_show_active(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gyro_context *cxt = NULL;
	int div = 0;

	cxt = gyro_context_obj;

	GYRO_LOG("gyro show active not support now\n");
	div = cxt->gyro_data.vender_div;
	GYRO_LOG("gyro vender_div value: %d\n", div);
	return snprintf(buf, PAGE_SIZE, "%d\n", div);
}


static ssize_t gyro_store_batch(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct gyro_context *cxt = gyro_context_obj;
	int handle = 0, flag = 0, err = 0;

	GYRO_LOG("gyro_store_batch %s\n", buf);
	err = sscanf(buf, "%d,%d,%lld,%lld", &handle, &flag, &cxt->delay_ns, &cxt->latency_ns);
	if (err != 4) {
		GYRO_INFO("gyro_store_batch param error: err = %d\n", err);
		return -1;
	}

	mutex_lock(&gyro_context_obj->gyro_op_mutex);
#ifdef CONFIG_NANOHUB
	if (cxt->gyro_ctl.is_support_batch)
		err = cxt->gyro_ctl.batch(0, atomic_read(&t_obj->delay) * 1000 * 1000, cxt->latency_ns);
	else
		err = cxt->gyro_ctl.batch(0, atomic_read(&t_obj->delay) * 1000 * 1000, 0);
	if (err)
		GYRO_PR_ERR("gyro set batch(ODR) err %d\n", err);
#else
	err = gyro_enable_and_batch();
#endif
	mutex_unlock(&gyro_context_obj->gyro_op_mutex);
	return err;
}

static ssize_t gyro_show_batch(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t gyro_store_flush(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct gyro_context *cxt = NULL;
	int handle = 0, err = 0;

	err = kstrtoint(buf, 10, &handle);
	if (err != 0)
		GYRO_INFO("gyro_store_flush param error: err = %d\n", err);

	GYRO_LOG("gyro_store_flush param: handle %d\n", handle);

	mutex_lock(&gyro_context_obj->gyro_op_mutex);
	cxt = gyro_context_obj;
	if (cxt->gyro_ctl.flush != NULL)
		err = cxt->gyro_ctl.flush();
	else
		GYRO_INFO("GYRO DRIVER OLD ARCHITECTURE DON'T SUPPORT GYRO COMMON VERSION FLUSH\n");
	if (err < 0)
		GYRO_INFO("gyro enable flush err %d\n", err);
	mutex_unlock(&gyro_context_obj->gyro_op_mutex);
	return err;
}

static ssize_t gyro_show_flush(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t gyro_show_cali(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t gyro_store_cali(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct gyro_context *cxt = NULL;
	int err = 0;
	uint8_t *cali_buf = NULL;

	cali_buf = vzalloc(count);
	if (cali_buf == NULL) {
		GYRO_INFO("kzalloc fail\n");
		return -EFAULT;
	}
	memcpy(cali_buf, buf, count);

	mutex_lock(&gyro_context_obj->gyro_op_mutex);
	cxt = gyro_context_obj;
	if (cxt->gyro_ctl.set_cali != NULL)
		err = cxt->gyro_ctl.set_cali(cali_buf, count);
	else
		GYRO_INFO("GYRO DRIVER OLD ARCHITECTURE DON'T SUPPORT GYRO COMMON VERSION FLUSH\n");
	if (err < 0)
		GYRO_INFO("gyro set cali err %d\n", err);
	mutex_unlock(&gyro_context_obj->gyro_op_mutex);
	vfree(cali_buf);
	return count;
}

/* need work around again */
static ssize_t gyro_show_devnum(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static int accelgyro_real_driver_init(void)
{
	int i = 0;
	int err = 0;

	ACC_LOG("accelgyro_real_driver_init +\n");
	for (i = 0; i < MAX_CHOOSE_G_NUM; i++) {
		ACC_LOG(" i=%d\n", i);
		if (accgyro_sensor_init_list[i] != 0) {
			ACC_LOG(" acc try to init driver %s\n", accgyro_sensor_init_list[i]->name);
			err = accgyro_sensor_init_list[i]->init();
			if (err == 0) {
				ACC_LOG(" acc real driver %s probe ok\n",
					accgyro_sensor_init_list[i]->name);
				break;
			}
		}
	}

	if (i == MAX_CHOOSE_G_NUM) {
		ACC_LOG(" acc_real_driver_init fail\n");
		err = -1;
	}
	return err;
}

int accelgyro_driver_add(struct accelgyro_init_info *obj)
{
	int err = 0;
	int i = 0;

	if (!obj) {
		ACC_PR_ERR("ACC driver add fail, acc_init_info is NULL\n");
		return -1;
	}
	for (i = 0; i < MAX_CHOOSE_G_NUM; i++) {
		if (accgyro_sensor_init_list[i] == NULL) {
			accgyro_sensor_init_list[i] = obj;
			break;
		}
	}
	if (i >= MAX_CHOOSE_G_NUM) {
		ACC_PR_ERR("ACC driver add err\n");
		err = -1;
	}

	return err;
}
EXPORT_SYMBOL_GPL(accelgyro_driver_add);

static int accel_open(struct inode *inode, struct file *file)
{
	nonseekable_open(inode, file);
	return 0;
}

static ssize_t accel_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	ssize_t read_cnt = 0;

	read_cnt = sensor_event_read(acc_context_obj->mdev.minor, file, buffer, count, ppos);

	return read_cnt;
}

static unsigned int accel_poll(struct file *file, poll_table *wait)
{
	return sensor_event_poll(acc_context_obj->mdev.minor, file, wait);
}

static const struct file_operations accel_fops = {
	.owner = THIS_MODULE,
	.open = accel_open,
	.read = accel_read,
	.poll = accel_poll,
};

static int acc_misc_init(struct acc_context *cxt)
{

	int err = 0;

	cxt->mdev.minor = ID_ACCELEROMETER;
	cxt->mdev.name = ACC_MISC_DEV_NAME;
	cxt->mdev.fops = &accel_fops;
	err = sensor_attr_register(&cxt->mdev);
	ACC_LOG("accel_fops address: %p!!\n", &accel_fops);
	if (err)
		ACC_PR_ERR("unable to register acc misc device!!\n");
	/* dev_set_drvdata(cxt->mdev.this_device, cxt); */
	return err;
}

DEVICE_ATTR(accenablenodata, S_IWUSR | S_IRUGO, acc_show_enable_nodata, acc_store_enable_nodata);
DEVICE_ATTR(accactive, S_IWUSR | S_IRUGO, acc_show_active, acc_store_active);
DEVICE_ATTR(accbatch, S_IWUSR | S_IRUGO, acc_show_batch, acc_store_batch);
DEVICE_ATTR(accflush, S_IWUSR | S_IRUGO, acc_show_flush, acc_store_flush);
DEVICE_ATTR(acccali, S_IWUSR | S_IRUGO, acc_show_cali, acc_store_cali);
DEVICE_ATTR(accdevnum, S_IWUSR | S_IRUGO, acc_show_sensordevnum, NULL);

static struct attribute *acc_attributes[] = {
	&dev_attr_accenablenodata.attr,
	&dev_attr_accactive.attr,
	&dev_attr_accbatch.attr,
	&dev_attr_accflush.attr,
	&dev_attr_acccali.attr,
	&dev_attr_accdevnum.attr,
	NULL
};

static struct attribute_group acc_attribute_group = {
	.attrs = acc_attributes
};

int acc_register_data_path(struct acc_data_path *data)
{
	struct acc_context *cxt = NULL;

	cxt = acc_context_obj;
	cxt->acc_data.get_data = data->get_data;
	cxt->acc_data.get_raw_data = data->get_raw_data;
	cxt->acc_data.vender_div = data->vender_div;
	ACC_LOG("acc register data path vender_div: %d\n", cxt->acc_data.vender_div);
	if (cxt->acc_data.get_data == NULL) {
		ACC_LOG("acc register data path fail\n");
		return -1;
	}
	return 0;
}

int acc_register_control_path(struct acc_control_path *ctl)
{
	struct acc_context *cxt = NULL;
	int err = 0;

	cxt = acc_context_obj;
	cxt->acc_ctl.enable_nodata = ctl->enable_nodata;
	cxt->acc_ctl.batch = ctl->batch;
	cxt->acc_ctl.flush = ctl->flush;
	cxt->acc_ctl.set_cali = ctl->set_cali;
	cxt->acc_ctl.is_support_batch = ctl->is_support_batch;
	cxt->acc_ctl.is_report_input_direct = ctl->is_report_input_direct;

	if (cxt->acc_ctl.enable_nodata == NULL || cxt->acc_ctl.batch == NULL
	    || cxt->acc_ctl.flush == NULL) {
		ACC_LOG("acc register control path fail\n");
		return -1;
	}
	/* add misc dev for sensor hal control cmd */
	err = acc_misc_init(acc_context_obj);
	if (err) {
		ACC_PR_ERR("unable to register acc misc device!!\n");
		return -2;
	}
	err = sysfs_create_group(&acc_context_obj->mdev.this_device->kobj, &acc_attribute_group);
	if (err < 0) {
		ACC_PR_ERR("unable to create acc attribute file\n");
		return -3;
	}

	kobject_uevent(&acc_context_obj->mdev.this_device->kobj, KOBJ_ADD);
	return 0;
}

int acc_data_report(struct acc_data *data)
{
	struct sensor_event event;
	int err = 0;

	event.time_stamp = data->timestamp;
	event.flush_action = DATA_ACTION;
	event.status = data->status;
	event.word[0] = data->x;
	event.word[1] = data->y;
	event.word[2] = data->z;
	event.reserved = data->reserved[0];
	/* ACC_PR_ERR("x:%d,y:%d,z:%d,time:%lld\n", data->x, data->y, data->z, data->timestamp); */
	if (event.reserved == 1)
		mark_timestamp(ID_ACCELEROMETER, DATA_REPORT, ktime_get_boot_ns(),
			       event.time_stamp);
	err = sensor_input_event(acc_context_obj->mdev.minor, &event);
	if (err < 0)
		pr_err_ratelimited("failed due to event buffer full\n");
	return err;
}

int acc_bias_report(struct acc_data *data)
{
	struct sensor_event event;
	int err = 0;

	event.flush_action = BIAS_ACTION;
	event.word[0] = data->x;
	event.word[1] = data->y;
	event.word[2] = data->z;
	/* ACC_PR_ERR("x:%d,y:%d,z:%d,time:%lld\n", x, y, z, nt); */
	err = sensor_input_event(acc_context_obj->mdev.minor, &event);
	if (err < 0)
		pr_err_ratelimited("failed due to event buffer full\n");
	return err;
}

int acc_flush_report(void)
{
	struct sensor_event event;
	int err = 0;

	ACC_LOG("acc flush\n");
	event.flush_action = FLUSH_ACTION;
	err = sensor_input_event(acc_context_obj->mdev.minor, &event);
	if (err < 0)
		pr_err_ratelimited("failed due to event buffer full\n");
	return err;
}

static int gyroscope_open(struct inode *inode, struct file *file)
{
	nonseekable_open(inode, file);
	return 0;
}

static ssize_t gyroscope_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	ssize_t read_cnt = 0;

	read_cnt = sensor_event_read(gyro_context_obj->mdev.minor, file, buffer, count, ppos);

	return read_cnt;
}

static unsigned int gyroscope_poll(struct file *file, poll_table *wait)
{
	return sensor_event_poll(gyro_context_obj->mdev.minor, file, wait);
}

static const struct file_operations gyroscope_fops = {
	.owner = THIS_MODULE,
	.open = gyroscope_open,
	.read = gyroscope_read,
	.poll = gyroscope_poll,
};

static int gyro_misc_init(struct gyro_context *cxt)
{
	int err = 0;

	cxt->mdev.minor = ID_GYROSCOPE;
	cxt->mdev.name = GYRO_MISC_DEV_NAME;
	cxt->mdev.fops = &gyroscope_fops;
	err = sensor_attr_register(&cxt->mdev);
	if (err)
		GYRO_PR_ERR("unable to register gyro misc device!!\n");

	return err;
}

DEVICE_ATTR(gyroenablenodata, S_IWUSR | S_IRUGO, gyro_show_enable_nodata, gyro_store_enable_nodata);
DEVICE_ATTR(gyroactive, S_IWUSR | S_IRUGO, gyro_show_active, gyro_store_active);
DEVICE_ATTR(gyrobatch, S_IWUSR | S_IRUGO, gyro_show_batch, gyro_store_batch);
DEVICE_ATTR(gyroflush, S_IWUSR | S_IRUGO, gyro_show_flush, gyro_store_flush);
DEVICE_ATTR(gyrocali, S_IWUSR | S_IRUGO, gyro_show_cali, gyro_store_cali);
DEVICE_ATTR(gyrodevnum, S_IWUSR | S_IRUGO, gyro_show_devnum, NULL);

static struct attribute *gyro_attributes[] = {
	&dev_attr_gyroenablenodata.attr,
	&dev_attr_gyroactive.attr,
	&dev_attr_gyrobatch.attr,
	&dev_attr_gyroflush.attr,
	&dev_attr_gyrocali.attr,
	&dev_attr_gyrodevnum.attr,
	NULL
};

static struct attribute_group gyro_attribute_group = {
	.attrs = gyro_attributes
};

int gyro_register_data_path(struct gyro_data_path *data)
{
	struct gyro_context *cxt = NULL;

	cxt = gyro_context_obj;
	cxt->gyro_data.get_data = data->get_data;
	cxt->gyro_data.vender_div = data->vender_div;
	cxt->gyro_data.get_raw_data = data->get_raw_data;
	GYRO_LOG("gyro register data path vender_div: %d\n", cxt->gyro_data.vender_div);
	if (cxt->gyro_data.get_data == NULL) {
		GYRO_LOG("gyro register data path fail\n");
		return -1;
	}
	return 0;
}

int gyro_register_control_path(struct gyro_control_path *ctl)
{
	struct gyro_context *cxt = NULL;
	int err = 0;

	cxt = gyro_context_obj;
	cxt->gyro_ctl.set_delay = ctl->set_delay;
	cxt->gyro_ctl.open_report_data = ctl->open_report_data;
	cxt->gyro_ctl.enable_nodata = ctl->enable_nodata;
	cxt->gyro_ctl.batch = ctl->batch;
	cxt->gyro_ctl.flush = ctl->flush;
	cxt->gyro_ctl.set_cali = ctl->set_cali;
	cxt->gyro_ctl.is_support_batch = ctl->is_support_batch;
	cxt->gyro_ctl.is_use_common_factory = ctl->is_use_common_factory;
	cxt->gyro_ctl.is_report_input_direct = ctl->is_report_input_direct;
	if (NULL == cxt->gyro_ctl.batch || NULL == cxt->gyro_ctl.open_report_data
	    || NULL == cxt->gyro_ctl.enable_nodata) {
		GYRO_LOG("gyro register control path fail\n");
		return -1;
	}

	/* add misc dev for sensor hal control cmd */
	err = gyro_misc_init(gyro_context_obj);
	if (err) {
		GYRO_INFO("unable to register gyro misc device!!\n");
		return -2;
	}
	err = sysfs_create_group(&gyro_context_obj->mdev.this_device->kobj, &gyro_attribute_group);
	if (err < 0) {
		GYRO_INFO("unable to create gyro attribute file\n");
		return -3;
	}

	kobject_uevent(&gyro_context_obj->mdev.this_device->kobj, KOBJ_ADD);

	return 0;
}

int x_t /* = 0 */;
int y_t /* = 0 */;
int z_t /* = 0 */;
long pc /* = 0 */;

static int check_repeat_data(int x, int y, int z)
{
	if ((x_t == x) && (y_t == y) && (z_t == z))
		pc++;
	else
		pc = 0;

	x_t = x;
	y_t = y;
	z_t = z;

	if (pc > 100) {
		GYRO_INFO("Gyro sensor output repeat data\n");
		pc = 0;
	}

	return 0;
}

int gyro_data_report(struct gyro_data *data)
{
	struct sensor_event event;
	int err = 0;

	check_repeat_data(data->x, data->y, data->z);
	event.time_stamp = data->timestamp;
	event.flush_action = DATA_ACTION;
	event.status = data->status;
	event.word[0] = data->x;
	event.word[1] = data->y;
	event.word[2] = data->z;
	event.reserved = data->reserved[0];

	if (event.reserved == 1)
		mark_timestamp(ID_GYROSCOPE, DATA_REPORT, ktime_get_boot_ns(), event.time_stamp);
	err = sensor_input_event(gyro_context_obj->mdev.minor, &event);
	if (err < 0)
		pr_err_ratelimited("gyro_data_report failed due to event buffer full\n");
	return err;
}

int gyro_bias_report(struct gyro_data *data)
{
	struct sensor_event event;
	int err = 0;

	event.flush_action = BIAS_ACTION;
	event.word[0] = data->x;
	event.word[1] = data->y;
	event.word[2] = data->z;

	err = sensor_input_event(gyro_context_obj->mdev.minor, &event);
	if (err < 0)
		pr_err_ratelimited("gyro_bias_report failed due to event buffer full\n");
	return err;
}

int gyro_flush_report(void)
{
	struct sensor_event event;
	int err = 0;

	GYRO_LOG("gyro flush\n");
	event.flush_action = FLUSH_ACTION;
	err = sensor_input_event(gyro_context_obj->mdev.minor, &event);
	if (err < 0)
		pr_err_ratelimited("failed due to event buffer full\n");
	return err;
}

static int accelgyro_probe(void)
{
	int err;

	ACC_LOG("---+++++++++++++accelgyro_probe!!\n");
	/*acc initialize */
	acc_context_obj = acc_context_alloc_object();
	if (!acc_context_obj) {
		err = -ENOMEM;
		ACC_PR_ERR("unable to allocate devobj!\n");
		goto exit_alloc_data_failed;
	}
	/*gyro initialize */
	gyro_context_obj = gyro_context_alloc_object();
	if (!gyro_context_obj) {
		err = -ENOMEM;
		GYRO_PR_ERR("unable to allocate devobj!\n");
		goto exit_alloc_data_failed;
	}

	/*init accelgyro timer, to save timer and min delay between acc,gyro */
	timer_obj = kzalloc(sizeof(*timer_obj), GFP_KERNEL);
	if (!timer_obj) {
		err = -ENOMEM;
		GYRO_PR_ERR("Alloc accelgyro delay object error!\n");
		return err;
	}
	atomic_set(&timer_obj->delay, 200);	/* 5Hz,  set work queue delay time 200ms */

	INIT_WORK(&timer_obj->report, accelgyro_work_func);
	timer_obj->accelgyro_workqueue = NULL;
	timer_obj->accelgyro_workqueue = create_workqueue("accelgyro_polling");
	if (!timer_obj->accelgyro_workqueue) {
		kfree(timer_obj);
		return -1;
	}
	initTimer(&timer_obj->hrTimer, accelgyro_poll);

	/* init real acceleration driver */
	err = accelgyro_real_driver_init();
	if (err) {
		ACC_PR_ERR("acc real driver init fail\n");
		goto real_driver_init_fail;
	}

	ACC_LOG("----accelgyro_probe OK !!\n");
	return 0;

real_driver_init_fail:
	kfree(acc_context_obj);
	kfree(gyro_context_obj);

exit_alloc_data_failed:

	ACC_PR_ERR("----accel_probe fail !!!\n");
	return err;
}



static int accelgyro_remove(void)
{
	int err = 0;

	sysfs_remove_group(&acc_context_obj->mdev.this_device->kobj, &acc_attribute_group);

	err = sensor_attr_deregister(&acc_context_obj->mdev);

	sysfs_remove_group(&gyro_context_obj->mdev.this_device->kobj, &gyro_attribute_group);
	err = sensor_attr_deregister(&gyro_context_obj->mdev);

	if (err)
		ACC_PR_ERR("misc_deregister fail: %d\n", err);
	kfree(acc_context_obj);
	kfree(gyro_context_obj);
	return 0;
}

static int __init accelgyro_init(void)
{
	ACC_LOG("accelgyro_init\n");

	if (accelgyro_probe()) {
		ACC_PR_ERR("failed to register acc driver\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit accelgyro_exit(void)
{
	accelgyro_remove();

}
late_initcall(accelgyro_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ACCELEROMETER device driver");
MODULE_AUTHOR("Mediatek");
