

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "kd_flashlight_type.h"
#ifdef CONFIG_COMPAT
#include <linux/fs.h>
#include <linux/compat.h>
#endif
#include "kd_flashlight.h"

int strobe_getPartId(int sensorDev, int strobeId)
{
	/* return 1 or 2 (backup flash part). Other numbers are invalid. */
	if (sensorDev == e_CAMERA_MAIN_SENSOR && strobeId == 1)
		return 1;
	else if (sensorDev == e_CAMERA_MAIN_SENSOR && strobeId == 2)
		return 1;
	else if (sensorDev == e_CAMERA_SUB_SENSOR && strobeId == 1)
		return 1;
	else if (sensorDev == e_CAMERA_SUB_SENSOR && strobeId == 2)
		return 1;
	/*  else  sensorDev == e_CAMERA_MAIN_2_SENSOR */
	return 200;
}
