

#include "fusion.h"

static struct fusion_context *fusion_context_obj;


static struct fusion_init_info *fusion_init_list[max_fusion_support] = { 0 };	/* modified */

static struct fusion_context *fusion_context_alloc_object(void)
{
	int index = 0;
	struct fusion_context *obj = kzalloc(sizeof(*obj), GFP_KERNEL);

	FUSION_LOG("fusion_context_alloc_object++++\n");
	if (!obj) {
		FUSION_PR_ERR("Alloc fusion object error!\n");
		return NULL;
	}
	mutex_init(&obj->fusion_op_mutex);
	for (index = orientation; index < max_fusion_support; ++index) {
		obj->fusion_context[index].is_first_data_after_enable = false;
		obj->fusion_context[index].is_polling_run = false;
		obj->fusion_context[index].is_batch_enable = false;
		obj->fusion_context[index].power = 0;
		obj->fusion_context[index].enable = 0;
		obj->fusion_context[index].delay_ns = -1;
		obj->fusion_context[index].latency_ns = -1;
	}
	FUSION_LOG("fusion_context_alloc_object----\n");
	return obj;
}

static int handle_to_index(int handle)
{
	int index = -1;

	switch (handle) {
	case ID_ORIENTATION:
		index = orientation;
		break;
	case ID_GAME_ROTATION_VECTOR:
		index = grv;
		break;
	case ID_GEOMAGNETIC_ROTATION_VECTOR:
		index = gmrv;
		break;
	case ID_ROTATION_VECTOR:
		index = rv;
		break;
	case ID_LINEAR_ACCELERATION:
		index = la;
		break;
	case ID_GRAVITY:
		index = grav;
		break;
	case ID_GYROSCOPE_UNCALIBRATED:
		index = ungyro;
		break;
	case ID_MAGNETIC_UNCALIBRATED:
		index = unmag;
		break;
	case ID_PDR:
		index = pdr;
		break;
	default:
		index = -1;
		FUSION_PR_ERR("handle_to_index invalid handle:%d, index:%d\n", handle, index);
		return index;
	}
	FUSION_LOG("handle_to_index handle:%d, index:%d\n", handle, index);
	return index;
}

static int fusion_enable_and_batch(int index)
{
	struct fusion_context *cxt = fusion_context_obj;
	int err;

	/* power on -> power off */
	if (cxt->fusion_context[index].power == 1 && cxt->fusion_context[index].enable == 0) {
		FUSION_LOG("FUSION disable\n");
		/* turn off the power */
		err = cxt->fusion_context[index].fusion_ctl.enable_nodata(0);
		if (err) {
			FUSION_PR_ERR("fusion turn off power err = %d\n", err);
			return -1;
		}
		FUSION_LOG("fusion turn off power done\n");

		cxt->fusion_context[index].power = 0;
		cxt->fusion_context[index].delay_ns = -1;
		FUSION_LOG("FUSION disable done\n");
		return 0;
	}
	/* power off -> power on */
	if (cxt->fusion_context[index].power == 0 && cxt->fusion_context[index].enable == 1) {
		FUSION_LOG("FUSION power on\n");
		err = cxt->fusion_context[index].fusion_ctl.enable_nodata(1);
		if (err) {
			FUSION_PR_ERR("fusion turn on power err = %d\n", err);
			return -1;
		}
		FUSION_LOG("fusion turn on power done\n");

		cxt->fusion_context[index].power = 1;
		FUSION_LOG("FUSION power on done\n");
	}
	/* rate change */
	if (cxt->fusion_context[index].power == 1 && cxt->fusion_context[index].delay_ns >= 0) {
		FUSION_LOG("FUSION set batch\n");
		/* set ODR, fifo timeout latency */
		if (cxt->fusion_context[index].fusion_ctl.is_support_batch)
			err = cxt->fusion_context[index].fusion_ctl.batch(0, cxt->fusion_context[index].delay_ns,
				cxt->fusion_context[index].latency_ns);
		else
			err = cxt->fusion_context[index].fusion_ctl.batch(0, cxt->fusion_context[index].delay_ns, 0);
		if (err) {
			FUSION_PR_ERR("fusion set batch(ODR) err %d\n", err);
			return -1;
		}
		FUSION_LOG("fusion set ODR, fifo latency done\n");
		FUSION_LOG("FUSION batch done\n");
	}
	/* just for debug, remove it when everything is ok */
	if (cxt->fusion_context[index].power == 0 && cxt->fusion_context[index].delay_ns >= 0)
		FUSION_LOG("batch will call firstly in API1.3, do nothing\n");

	return 0;
}
static ssize_t fusion_store_active(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct fusion_context *cxt = fusion_context_obj;
	int err = 0, handle = -1, en = 0, index = -1;

	err = sscanf(buf, "%d,%d", &handle, &en);
	if (err < 0) {
		FUSION_PR_ERR("fusion_store_active param error: err = %d\n", err);
		return err;
	}
	FUSION_LOG("fusion_store_active handle=%d, en=%d\n", handle, en);
	index = handle_to_index(handle);
	if (index < 0) {
		FUSION_PR_ERR("[%s] invalid handle\n", __func__);
		return -1;
	}
	mutex_lock(&fusion_context_obj->fusion_op_mutex);
	if (en == 1)
		cxt->fusion_context[index].enable = 1;
	else if (en == 0)
		cxt->fusion_context[index].enable = 0;
	else {
		FUSION_PR_ERR(" fusion_store_active error !!\n");
		err = -1;
		goto err_out;
	}
	err = fusion_enable_and_batch(index);
err_out:
	mutex_unlock(&fusion_context_obj->fusion_op_mutex);
	return err;
}

/*----------------------------------------------------------------------------*/
static ssize_t fusion_show_active(struct device *dev, struct device_attribute *attr, char *buf)
{
	int vendor_div[max_fusion_support];
	int index = 0;
	struct fusion_context *cxt = fusion_context_obj;

	for (index = orientation; index < max_fusion_support; ++index) {
		vendor_div[index] = cxt->fusion_context[index].fusion_data.vender_div;
		FUSION_LOG("fusion index:%d vender_div: %d\n", index, vendor_div[index]);
	}

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
		vendor_div[orientation], vendor_div[grv],
		vendor_div[gmrv], vendor_div[rv], vendor_div[la], vendor_div[grav],
		vendor_div[ungyro], vendor_div[unmag], vendor_div[pdr]);
}

static ssize_t fusion_show_sensordevnum(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}


static ssize_t fusion_store_batch(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct fusion_context *cxt = fusion_context_obj;
	int index = -1, handle = 0, flag = 0, err = 0;
	int64_t samplingPeriodNs = 0, maxBatchReportLatencyNs = 0;

	err = sscanf(buf, "%d,%d,%lld,%lld", &handle, &flag, &samplingPeriodNs, &maxBatchReportLatencyNs);
	if (err != 4) {
		FUSION_PR_ERR("fusion_store_batch param error: err = %d\n", err);
		return err;
	}
	index = handle_to_index(handle);
	if (index < 0) {
		FUSION_PR_ERR("[%s] invalid handle\n", __func__);
		return -1;
	}
	FUSION_LOG("handle %d, flag:%d samplingPeriodNs:%lld, maxBatchReportLatencyNs: %lld\n",
			handle, flag, samplingPeriodNs, maxBatchReportLatencyNs);
	cxt->fusion_context[index].delay_ns = samplingPeriodNs;
	cxt->fusion_context[index].latency_ns = maxBatchReportLatencyNs;

	mutex_lock(&fusion_context_obj->fusion_op_mutex);
	err = fusion_enable_and_batch(index);
	mutex_unlock(&fusion_context_obj->fusion_op_mutex);
	return err;
}

static ssize_t fusion_show_batch(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t fusion_store_flush(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct fusion_context *cxt = NULL;
	int index = -1, handle = 0, err = 0;

	err = kstrtoint(buf, 10, &handle);
	if (err != 0)
		FUSION_PR_ERR("fusion_store_flush param error: err = %d\n", err);

	FUSION_PR_ERR("fusion_store_flush param: handle %d\n", handle);

	mutex_lock(&fusion_context_obj->fusion_op_mutex);
	cxt = fusion_context_obj;
	index = handle_to_index(handle);
	if (index < 0) {
		FUSION_PR_ERR("[%s] invalid index\n", __func__);
		mutex_unlock(&fusion_context_obj->fusion_op_mutex);
		return  -1;
	}
	if (NULL != cxt->fusion_context[index].fusion_ctl.flush)
		err = cxt->fusion_context[index].fusion_ctl.flush();
	else
		FUSION_PR_ERR("FUSION DRIVER OLD ARCHITECTURE DON'T SUPPORT FUSION COMMON VERSION FLUSH\n");
	if (err < 0)
		FUSION_PR_ERR("fusion enable flush err %d\n", err);
	mutex_unlock(&fusion_context_obj->fusion_op_mutex);
	return err;
}

static ssize_t fusion_show_flush(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}
static int fusion_real_driver_init(void)
{
	int index = 0;
	int err = 0;

	FUSION_LOG("fusion_real_driver_init +\n");
	for (index = 0; index < max_fusion_support; index++) {
		FUSION_LOG("index = %d\n", index);
		if (NULL != fusion_init_list[index]) {
			FUSION_LOG("fusion try to init driver %s\n", fusion_init_list[index]->name);
			err = fusion_init_list[index]->init();
			if (0 == err)
				FUSION_LOG("fusion real driver %s probe ok\n",
					fusion_init_list[index]->name);
		}
	}
	return err;
}

static int fusion_open(struct inode *inode, struct file *file)
{
	nonseekable_open(inode, file);
	return 0;
}

static ssize_t fusion_read(struct file *file, char __user *buffer,
			  size_t count, loff_t *ppos)
{
	ssize_t read_cnt = 0;

	read_cnt = sensor_event_read(fusion_context_obj->mdev.minor, file, buffer, count, ppos);

	return read_cnt;
}

static unsigned int fusion_poll(struct file *file, poll_table *wait)
{
	return sensor_event_poll(fusion_context_obj->mdev.minor, file, wait);
}

static const struct file_operations fusion_fops = {
	.owner = THIS_MODULE,
	.open = fusion_open,
	.read = fusion_read,
	.poll = fusion_poll,
};

static int fusion_misc_init(struct fusion_context *cxt)
{

	int err = 0;

	cxt->mdev.minor = ID_GAME_ROTATION_VECTOR;
	cxt->mdev.name = FUSION_MISC_DEV_NAME;
	cxt->mdev.fops = &fusion_fops;
	err = sensor_attr_register(&cxt->mdev);
	if (err)
		FUSION_PR_ERR("unable to register fusion misc device!!\n");

	/* dev_set_drvdata(cxt->mdev.this_device, cxt); */
	return err;
}

DEVICE_ATTR(fusionactive, S_IWUSR | S_IRUGO, fusion_show_active, fusion_store_active);
DEVICE_ATTR(fusionbatch, S_IWUSR | S_IRUGO, fusion_show_batch, fusion_store_batch);
DEVICE_ATTR(fusionflush, S_IWUSR | S_IRUGO, fusion_show_flush, fusion_store_flush);
DEVICE_ATTR(fusiondevnum, S_IWUSR | S_IRUGO, fusion_show_sensordevnum, NULL);

static struct attribute *fusion_attributes[] = {
	&dev_attr_fusionactive.attr,
	&dev_attr_fusionbatch.attr,
	&dev_attr_fusionflush.attr,
	&dev_attr_fusiondevnum.attr,
	NULL
};

static struct attribute_group fusion_attribute_group = {
	.attrs = fusion_attributes
};

int fusion_register_data_path(struct fusion_data_path *data, int handle)
{
	int index = -1;
	struct fusion_context *cxt = NULL;

	if (NULL == data) {
		FUSION_PR_ERR("fail\n");
		return -1;
	}

	index = handle_to_index(handle);
	if (index < 0) {
		FUSION_PR_ERR("[%s] invalid handle\n", __func__);
		return -1;
	}
	cxt = fusion_context_obj;
	cxt->fusion_context[index].fusion_data.get_data = data->get_data;
	cxt->fusion_context[index].fusion_data.vender_div = data->vender_div;
	FUSION_LOG("fusion handle:%d vender_div: %d\n", handle, cxt->fusion_context[index].fusion_data.vender_div);
	return 0;
}

int fusion_register_control_path(struct fusion_control_path *ctl, int handle)
{
	struct fusion_context *cxt = NULL;
	int index = -1;

	if (NULL == ctl || NULL == ctl->set_delay || NULL == ctl->open_report_data
	    || NULL == ctl->enable_nodata || NULL == ctl->batch || NULL == ctl->flush) {
		FUSION_PR_ERR("fusion handle:%d register control path fail\n", handle);
		return -1;
	}

	index = handle_to_index(handle);
	if (index < 0) {
		FUSION_PR_ERR("[%s] invalid handle\n", __func__);
		return -1;
	}

	cxt = fusion_context_obj;
	cxt->fusion_context[index].fusion_ctl.set_delay = ctl->set_delay;
	cxt->fusion_context[index].fusion_ctl.open_report_data = ctl->open_report_data;
	cxt->fusion_context[index].fusion_ctl.enable_nodata = ctl->enable_nodata;
	cxt->fusion_context[index].fusion_ctl.batch = ctl->batch;
	cxt->fusion_context[index].fusion_ctl.flush = ctl->flush;
	cxt->fusion_context[index].fusion_ctl.is_support_batch = ctl->is_support_batch;
	cxt->fusion_context[index].fusion_ctl.is_report_input_direct = ctl->is_report_input_direct;
	return 0;
}

static int fusion_data_report(int x, int y, int z, int scalar, int status, int64_t nt, int handle)
{
	/* FUSION_LOG("+fusion_data_report! %d, %d, %d, %d\n",x,y,z,status); */
	struct sensor_event event;
	int err = 0;

	event.handle = handle;
	event.flush_action = DATA_ACTION;
	event.time_stamp = nt;
	event.status = status;
	event.word[0] = x;
	event.word[1] = y;
	event.word[2] = z;
	event.word[3] = scalar;

	err = sensor_input_event(fusion_context_obj->mdev.minor, &event);
	if (err < 0)
		pr_err_ratelimited("failed due to event buffer full\n");
	return err;
}

static int fusion_flush_report(int handle)
{
	struct sensor_event event;
	int err = 0;

	FUSION_LOG("flush\n");
	event.handle = handle;
	event.flush_action = FLUSH_ACTION;
	err = sensor_input_event(fusion_context_obj->mdev.minor, &event);
	if (err < 0)
		pr_err_ratelimited("failed due to event buffer full\n");
	return err;
}
static int uncali_sensor_data_report(int *data, int status, int64_t nt, int handle)
{
	struct sensor_event event;
	int err = 0;

	event.handle = handle;
	event.flush_action = DATA_ACTION;
	event.time_stamp = nt;
	event.status = status;
	event.word[0] = data[0];
	event.word[1] = data[1];
	event.word[2] = data[2];
	event.word[3] = data[3];
	event.word[4] = data[4];
	event.word[5] = data[5];
	err = sensor_input_event(fusion_context_obj->mdev.minor, &event);
	if (err < 0)
		pr_err_ratelimited("failed due to event buffer full\n");
	return err;
}

static int uncali_sensor_flush_report(int handle)
{
	struct sensor_event event;
	int err = 0;

	FUSION_LOG("flush handle:%d\n", handle);
	event.handle = handle;
	event.flush_action = FLUSH_ACTION;
	err = sensor_input_event(fusion_context_obj->mdev.minor, &event);
	if (err < 0)
		pr_err_ratelimited("failed due to event buffer full\n");
	return err;
}

int rv_data_report(int x, int y, int z, int scalar, int status, int64_t nt)
{
	return fusion_data_report(x, y, z, scalar, status, nt, ID_ROTATION_VECTOR);
}
int rv_flush_report(void)
{
	return fusion_flush_report(ID_ROTATION_VECTOR);
}
int grv_data_report(int x, int y, int z, int scalar, int status, int64_t nt)
{
	return fusion_data_report(x, y, z, scalar, status, nt, ID_GAME_ROTATION_VECTOR);
}
int grv_flush_report(void)
{
	return fusion_flush_report(ID_GAME_ROTATION_VECTOR);
}
int gmrv_data_report(int x, int y, int z, int scalar, int status, int64_t nt)
{
	return fusion_data_report(x, y, z, scalar, status, nt, ID_GEOMAGNETIC_ROTATION_VECTOR);
}
int gmrv_flush_report(void)
{
	return fusion_flush_report(ID_GEOMAGNETIC_ROTATION_VECTOR);
}
int grav_data_report(int x, int y, int z, int status, int64_t nt)
{
	return fusion_data_report(x, y, z, 0, status, nt, ID_GRAVITY);
}
int grav_flush_report(void)
{
	return fusion_flush_report(ID_GRAVITY);
}
int la_data_report(int x, int y, int z, int status, int64_t nt)
{
	return fusion_data_report(x, y, z, 0, status, nt, ID_LINEAR_ACCELERATION);
}
int la_flush_report(void)
{
	return fusion_flush_report(ID_LINEAR_ACCELERATION);
}
int orientation_data_report(int x, int y, int z, int status, int64_t nt)
{
	return fusion_data_report(x, y, z, 0, status, nt, ID_ORIENTATION);
}
int orientation_flush_report(void)
{
	return fusion_flush_report(ID_ORIENTATION);
}
int uncali_gyro_data_report(int *data, int status, int64_t nt)
{
	return uncali_sensor_data_report(data, status, nt, ID_GYROSCOPE_UNCALIBRATED);
}

int uncali_gyro_flush_report(void)
{
	return uncali_sensor_flush_report(ID_GYROSCOPE_UNCALIBRATED);
}
int uncali_mag_data_report(int *data, int status, int64_t nt)
{
	return uncali_sensor_data_report(data, status, nt, ID_MAGNETIC_UNCALIBRATED);
}

int uncali_mag_flush_report(void)
{
	return uncali_sensor_flush_report(ID_MAGNETIC_UNCALIBRATED);
}
static int fusion_probe(void)
{

	int err;

	FUSION_LOG("+++++++++++++fusion_probe!!\n");

	fusion_context_obj = fusion_context_alloc_object();
	if (!fusion_context_obj) {
		err = -ENOMEM;
		FUSION_PR_ERR("unable to allocate devobj!\n");
		goto exit_alloc_data_failed;
	}
	/* init real fusioneleration driver */
	err = fusion_real_driver_init();
	if (err) {
		FUSION_PR_ERR("fusion real driver init fail\n");
		goto real_driver_init_fail;
	}
	/* add misc dev for sensor hal control cmd */
	err = fusion_misc_init(fusion_context_obj);
	if (err) {
		FUSION_PR_ERR("unable to register fusion misc device!!\n");
		goto real_driver_init_fail;
	}
	err = sysfs_create_group(&fusion_context_obj->mdev.this_device->kobj, &fusion_attribute_group);
	if (err < 0) {
		FUSION_PR_ERR("unable to create fusion attribute file\n");
		goto real_driver_init_fail;
	}
	kobject_uevent(&fusion_context_obj->mdev.this_device->kobj, KOBJ_ADD);

	FUSION_LOG("----fusion_probe OK !!\n");
	return 0;

real_driver_init_fail:
	kfree(fusion_context_obj);
exit_alloc_data_failed:
	FUSION_LOG("----fusion_probe fail !!!\n");
	return err;
}



static int fusion_remove(void)
{
	int err = 0;

	FUSION_FUN(f);

	sysfs_remove_group(&fusion_context_obj->mdev.this_device->kobj, &fusion_attribute_group);
	err = sensor_attr_deregister(&fusion_context_obj->mdev);
	if (err)
		FUSION_PR_ERR("misc_deregister fail: %d\n", err);

	kfree(fusion_context_obj);

	return 0;
}
int fusion_driver_add(struct fusion_init_info *obj, int handle)
{
	int err = 0;
	int index = 0;

	FUSION_LOG("handle:%d\n", handle);
	if (!obj) {
		FUSION_PR_ERR("FUSION handle: %d, driver add fail, fusion_init_info is NULL\n", handle);
		return -1;
	}

	index = handle_to_index(handle);
	if (index < 0) {
		FUSION_PR_ERR("[%s] invalid index\n", __func__);
		return  -1;
	}

	if (NULL == fusion_init_list[index])
		fusion_init_list[index] = obj;
	else
		FUSION_PR_ERR("fusion_init_list handle:%d already exist\n", handle);
	return err;
}
static int __init fusion_init(void)
{
	FUSION_FUN();

	if (fusion_probe()) {
		FUSION_PR_ERR("failed to register fusion driver\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit fusion_exit(void)
{
	fusion_remove();
}

late_initcall(fusion_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FUSION device driver");
MODULE_AUTHOR("Mediatek");
