
#if defined(CONFIG_MTK_HDMI_SUPPORT)
#include <linux/string.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>

#include <linux/types.h>
#include <mt-plat/mt_gpio.h>


#include "ddp_hal.h"
#include "ddp_reg.h"
#include "ddp_info.h"
#include "ddp_dump.h"
#include "extd_hdmi.h"
#include "external_display.h"

/*extern DDP_MODULE_DRIVER *ddp_modules_driver[DISP_MODULE_NUM];*/
/* --------------------------------------------------------------------------- */
/* External variable declarations */
/* --------------------------------------------------------------------------- */

/* extern LCM_DRIVER *lcm_drv; */
/* --------------------------------------------------------------------------- */
/* Debug Options */
/* --------------------------------------------------------------------------- */


static const char STR_HELP[] = "\n" "USAGE\n" "        echo [ACTION]... > hdmi\n" "\n" "ACTION\n"
		"        hdmitx:[on|off]\n" "             enable hdmi video output\n" "\n";

/* TODO: this is a temp debug solution */
/* extern void hdmi_cable_fake_plug_in(void); */
/* extern int hdmi_drv_init(void); */
static void process_dbg_opt(const char *opt)
{
	if (0 == strncmp(opt, "on", 2))
		hdmi_power_on();
	else if (0 == strncmp(opt, "off", 3))
		hdmi_power_off();
	else if (0 == strncmp(opt, "suspend", 7))
		hdmi_suspend();
	else if (0 == strncmp(opt, "resume", 6))
		hdmi_resume();
	else if (0 == strncmp(opt, "fakecablein:", 12)) {
		if (0 == strncmp(opt + 12, "enable", 6))
			hdmi_cable_fake_plug_in();
		else if (0 == strncmp(opt + 12, "disable", 7))
			hdmi_cable_fake_plug_out();
		else
			goto Error;
	} else if (0 == strncmp(opt, "path_analyze", 12))
		ext_disp_diagnose();
	else if (0 == strncmp(opt, "UART:", 5)) {
		if (0 == strncmp(opt + 5, "on", 2)) {
			pr_debug("[hdmi][Debug] Enable UART,disable DPI D6/D7\n");
		} else if (0 == strncmp(opt + 5, "off", 3)) {
			pr_debug("[hdmi][Debug] Disable UART,enable DPI D6/D7\n");
		}
	} else if (0 == strncmp(opt, "extd_analyze", 12)) {
		ddp_dump_analysis(DISP_MODULE_CONFIG);
		ddp_dump_analysis(DISP_MODULE_MUTEX);
		ddp_dump_analysis(DISP_MODULE_OVL1);
		ddp_dump_analysis(DISP_MODULE_RDMA1);

		ddp_dump_reg(DISP_MODULE_CONFIG);
		ddp_dump_reg(DISP_MODULE_MUTEX);
		ddp_dump_reg(DISP_MODULE_OVL1);
		ddp_dump_reg(DISP_MODULE_RDMA1);
		ddp_dump_reg(DISP_MODULE_DPI);
	} else if (0 == strncmp(opt, "DPI_DUMP", 8))
		ddp_dump_reg(DISP_MODULE_DPI);
	else if (0 == strncmp(opt, "forceon", 7))
		hdmi_force_on(false);
	else if (0 == strncmp(opt, "extd_vsync:", 11)) {
		if (0 == strncmp(opt + 11, "on", 2))
			hdmi_wait_vsync_debug(1);
		else if (0 == strncmp(opt + 11, "off", 3))
			hdmi_wait_vsync_debug(0);
	} else
		goto Error;

	return;

 Error:
	pr_debug("[extd] parse command error!\n\n%s", STR_HELP);
}

static void process_dbg_cmd(char *cmd)
{
	char *tok;

	pr_debug("[extd] %s\n", cmd);

	while ((tok = strsep(&cmd, " ")) != NULL)
		process_dbg_opt(tok);
}

/* --------------------------------------------------------------------------- */
/* Debug FileSystem Routines */
/* --------------------------------------------------------------------------- */
struct dentry *extd_dbgfs = NULL;

static int debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static char debug_buffer[2048];

static ssize_t debug_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	const int debug_bufmax = sizeof(debug_buffer) - 1;
	int n = 0;

	n += scnprintf(debug_buffer + n, debug_bufmax - n, STR_HELP);
	debug_buffer[n++] = 0;

	return simple_read_from_buffer(ubuf, count, ppos, debug_buffer, n);
}

static ssize_t debug_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	const int debug_bufmax = sizeof(debug_buffer) - 1;
	size_t ret;

	ret = count;

	if (count > debug_bufmax)
		count = debug_bufmax;

	if (copy_from_user(&debug_buffer, ubuf, count))
		return -EFAULT;

	debug_buffer[count] = 0;

	process_dbg_cmd(debug_buffer);

	return ret;
}


static const struct file_operations debug_fops = {
	.read = debug_read,
	.write = debug_write,
	.open = debug_open,
};


void Extd_DBG_Init(void)
{
	extd_dbgfs = debugfs_create_file("extd", S_IFREG | S_IRUGO, NULL, (void *)0, &debug_fops);
}


void Extd_DBG_Deinit(void)
{
	debugfs_remove(extd_dbgfs);
}

#endif
