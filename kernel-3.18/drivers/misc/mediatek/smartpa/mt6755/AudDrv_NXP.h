
#ifndef AUDDRV_NXPSPK_H
#define AUDDRV_NXPSPK_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/completion.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/wakelock.h>
#include <linux/semaphore.h>
#include <linux/jiffies.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/div64.h>
#include <linux/i2c.h>





static char const *const kAudioNXPSpkName = "/dev/nxpspk";


typedef struct {
	unsigned char data0;
	unsigned char data1;
	unsigned char data2;
	unsigned char data3;
} Aud_Buffer_Control;

extern int mt_set_gpio_mode(int pin, int mode);
extern int mt_set_gpio_dir(int pin, int mode);
extern int mt_set_gpio_out(int pin, int mode);

/*below is control message*/
#define AUD_NXP_IOC_MAGIC 'C'

#define SET_NXP_REG         _IOWR(AUD_NXP_IOC_MAGIC, 0x00, Aud_Buffer_Control*)
#define GET_NXP_REG         _IOWR(AUD_NXP_IOC_MAGIC, 0x01, Aud_Buffer_Control*)

/* Pre-defined definition */
#define NXP_DEBUG_ON
#define NXP_DEBUG_ARRAY_ON
#define NXP_DEBUG_FUNC_ON


/*Log define*/
#define NXP_INFO(fmt, arg...)           pr_debug("<<-NXP-INFO->> "fmt"\n", ##arg)
#define NXP_ERROR(fmt, arg...)          pr_debug("<<-NXP-ERROR->> "fmt"\n", ##arg)

/****************************PART4:UPDATE define*******************************/
#define TFA9890_DEVICEID   0x0080

/*error no*/
#define ERROR_NO_FILE           2   /*ENOENT*/
#define ERROR_FILE_READ         23  /*ENFILE*/
#define ERROR_FILE_TYPE         21  /*EISDIR*/
#define ERROR_GPIO_REQUEST      4   /*EINTR*/
#define ERROR_I2C_TRANSFER      5   /*EIO*/
#define ERROR_NO_RESPONSE       16  /*EBUSY*/
#define ERROR_TIMEOUT           110 /*ETIMEDOUT*/

#endif


