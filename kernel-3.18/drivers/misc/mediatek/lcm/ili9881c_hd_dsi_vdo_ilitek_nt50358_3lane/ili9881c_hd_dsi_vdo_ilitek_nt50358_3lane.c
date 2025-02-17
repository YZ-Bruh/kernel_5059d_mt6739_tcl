
#define LOG_TAG "LCM"

#ifndef BUILD_LK
#include <linux/string.h>
#include <linux/kernel.h>
#endif

#include "lcm_drv.h"


#ifdef BUILD_LK
#include <platform/upmu_common.h>
#include <platform/mt_gpio.h>
#include <platform/mt_i2c.h>
#include <platform/mt_pmic.h>
#include <string.h>
#elif defined(BUILD_UBOOT)
#include <asm/arch/mt_gpio.h>
#else
/*#include <mach/mt_pm_ldo.h>*/
#ifdef CONFIG_MTK_LEGACY
#include <mach/mt_gpio.h>
#endif
#endif
#ifdef CONFIG_MTK_LEGACY
#include <cust_gpio_usage.h>
#endif
#ifndef CONFIG_FPGA_EARLY_PORTING
#if defined(CONFIG_MTK_LEGACY)
#include <cust_i2c.h>
#endif
#endif

#ifdef BUILD_LK
#define LCM_LOGI(string, args...)  dprintf(0, "[LK/"LOG_TAG"]"string, ##args)
#define LCM_LOGD(string, args...)  dprintf(1, "[LK/"LOG_TAG"]"string, ##args)
#else
#define LCM_LOGI(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#define LCM_LOGD(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#endif

#define LCM_ID (0x98)

static const unsigned int BL_MIN_LEVEL = 20;
static LCM_UTIL_FUNCS lcm_util;

#define SET_RESET_PIN(v)	(lcm_util.set_reset_pin((v)))
#define MDELAY(n)		(lcm_util.mdelay(n))
#define UDELAY(n)		(lcm_util.udelay(n))


#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update) \
		lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd) lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums) \
		lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd) \
	  lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size) \
		lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

#define set_gpio_lcd_enp(cmd) \
		lcm_util.set_gpio_lcd_enp_bias(cmd)

/* static unsigned char lcd_id_pins_value = 0xFF; */
static const unsigned char LCD_MODULE_ID = 0x01;
#define LCM_DSI_CMD_MODE									0
#define FRAME_WIDTH										(720)
#define FRAME_HEIGHT									(1440)
#define LCM_DENSITY					(320)


#define LCM_PHYSICAL_WIDTH									(64800)
#define LCM_PHYSICAL_HEIGHT									(129600)


#ifndef CONFIG_FPGA_EARLY_PORTING
#define GPIO_65132_EN GPIO_LCD_BIAS_ENP_PIN
#endif

#define REGFLAG_DELAY		0xFFFC
#define REGFLAG_UDELAY	0xFFFB
#define REGFLAG_END_OF_TABLE	0xFFFD
#define REGFLAG_RESET_LOW	0xFFFE
#define REGFLAG_RESET_HIGH	0xFFFF

static LCM_DSI_MODE_SWITCH_CMD lcm_switch_mode_cmd;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[64];
};

static struct LCM_setting_table lcm_suspend_setting[] = {
	{0x28, 0, {} },
	{0x10, 0, {} },
	{REGFLAG_DELAY, 120, {} },
	{0x4F, 1, {0x01} },
	{REGFLAG_DELAY, 120, {} }
};

static struct LCM_setting_table init_setting[] = {
	{ 0xFF, 0x03, {0x98, 0x81, 0x03} },
	/* GIP_1 */
	{ 0x01, 0x01, {0x00} },
	{ 0x02, 0x01, {0x00} },
	{ 0x03, 0x01, {0x53} },
	{ 0x04, 0x01, {0x13} },
	{ 0x05, 0x01, {0x00} },
	{ 0x06, 0x01, {0x03} },
	{ 0x07, 0x01, {0x02} },
	{ 0x08, 0x01, {0x00} },
	{ 0x09, 0x01, {0x28} },
	{ 0x0a, 0x01, {0x28} },
	{ 0x0b, 0x01, {0x00} },
	{ 0x0c, 0x01, {0x01} },
	{ 0x0d, 0x01, {0x00} },
	{ 0x0e, 0x01, {0x00} },
	{ 0x0f, 0x01, {0x28} },
	{ 0x10, 0x01, {0x28} },
	{ 0x11, 0x01, {0x00} },
	{ 0x12, 0x01, {0x00} },
	{ 0x13, 0x01, {0x00} },
	{ 0x14, 0x01, {0x00} },
	{ 0x15, 0x01, {0x00} },
	{ 0x16, 0x01, {0x00} },
	{ 0x17, 0x01, {0x00} },
	{ 0x18, 0x01, {0x00} },
	{ 0x19, 0x01, {0x00} },
	{ 0x1a, 0x01, {0x00} },
	{ 0x1b, 0x01, {0x00} },
	{ 0x1c, 0x01, {0x00} },
	{ 0x1d, 0x01, {0x00} },
	{ 0x1e, 0x01, {0x44} },
	{ 0x1f, 0x01, {0x80} },
	{ 0x20, 0x01, {0x01} },
	{ 0x21, 0x01, {0x02} },
	{ 0x22, 0x01, {0x00} },
	{ 0x23, 0x01, {0x00} },
	{ 0x24, 0x01, {0x00} },
	{ 0x25, 0x01, {0x00} },
	{ 0x26, 0x01, {0x00} },
	{ 0x27, 0x01, {0x00} },
	{ 0x28, 0x01, {0x33} },
	{ 0x29, 0x01, {0x03} },
	{ 0x2a, 0x01, {0x00} },
	{ 0x2b, 0x01, {0x00} },
	{ 0x2c, 0x01, {0x00} },
	{ 0x2d, 0x01, {0x00} },
	{ 0x2e, 0x01, {0x00} },
	{ 0x2f, 0x01, {0x00} },
	{ 0x30, 0x01, {0x00} },
	{ 0x31, 0x01, {0x00} },
	{ 0x32, 0x01, {0x00} },
	{ 0x33, 0x01, {0x00} },
	{ 0x34, 0x01, {0x04} },
	{ 0x35, 0x01, {0x00} },
	{ 0x36, 0x01, {0x00} },
	{ 0x37, 0x01, {0x00} },
	{ 0x38, 0x01, {0x3c} },
	{ 0x39, 0x01, {0x00} },
	{ 0x3a, 0x01, {0x40} },
	{ 0x3b, 0x01, {0x00} },
	{ 0x3c, 0x01, {0x00} },
	{ 0x3d, 0x01, {0x00} },
	{ 0x3e, 0x01, {0x00} },
	{ 0x3f, 0x01, {0x00} },
	{ 0x40, 0x01, {0x00} },
	{ 0x41, 0x01, {0x00} },
	{ 0x42, 0x01, {0x00} },
	{ 0x43, 0x01, {0x00} },
	{ 0x44, 0x01, {0x00} },
	{REGFLAG_UDELAY, 100, {} },
	/* GIP_2 */
	{ 0x50, 0x01, {0x01} },
	{ 0x51, 0x01, {0x23} },
	{ 0x52, 0x01, {0x45} },
	{ 0x53, 0x01, {0x67} },
	{ 0x54, 0x01, {0x89} },
	{ 0x55, 0x01, {0xab} },
	{ 0x56, 0x01, {0x01} },
	{ 0x57, 0x01, {0x23} },
	{ 0x58, 0x01, {0x45} },
	{ 0x59, 0x01, {0x67} },
	{ 0x5a, 0x01, {0x89} },
	{ 0x5b, 0x01, {0xab} },
	{ 0x5c, 0x01, {0xcd} },
	{ 0x5d, 0x01, {0xef} },
	{REGFLAG_UDELAY, 100, {} },
	/* GIP_3 */
	{ 0x5e, 0x01, {0x11} },
	{ 0x5f, 0x01, {0x01} },
	{ 0x60, 0x01, {0x00} },
	{ 0x61, 0x01, {0x15} },
	{ 0x62, 0x01, {0x14} },
	{ 0x63, 0x01, {0x0c} },
	{ 0x64, 0x01, {0x0d} },
	{ 0x65, 0x01, {0x0e} },
	{ 0x66, 0x01, {0x0f} },
	{ 0x67, 0x01, {0x06} },
	{ 0x68, 0x01, {0x02} },
	{ 0x69, 0x01, {0x02} },
	{ 0x6a, 0x01, {0x02} },
	{ 0x6b, 0x01, {0x02} },
	{ 0x6c, 0x01, {0x02} },
	{ 0x6d, 0x01, {0x02} },
	{ 0x6e, 0x01, {0x08} },
	{ 0x6f, 0x01, {0x02} },
	{ 0x70, 0x01, {0x02} },
	{ 0x71, 0x01, {0x02} },
	{ 0x72, 0x01, {0x02} },
	{ 0x73, 0x01, {0x02} },
	{ 0x74, 0x01, {0x02} },
	{ 0x75, 0x01, {0x01} },
	{ 0x76, 0x01, {0x00} },
	{ 0x77, 0x01, {0x15} },
	{ 0x78, 0x01, {0x14} },
	{ 0x79, 0x01, {0x0C} },
	{ 0x7a, 0x01, {0x0D} },
	{ 0x7b, 0x01, {0x0e} },
	{ 0x7c, 0x01, {0x0f} },
	{ 0x7d, 0x01, {0x08} },
	{ 0x7e, 0x01, {0x02} },
	{ 0x7f, 0x01, {0x02} },
	{ 0x80, 0x01, {0x02} },
	{ 0x81, 0x01, {0x02} },
	{ 0x82, 0x01, {0x02} },
	{ 0x83, 0x01, {0x02} },
	{ 0x84, 0x01, {0x06} },
	{ 0x85, 0x01, {0x02} },
	{ 0x86, 0x01, {0x02} },
	{ 0x87, 0x01, {0x02} },
	{ 0x88, 0x01, {0x02} },
	{ 0x89, 0x01, {0x02} },
	{ 0x8A, 0x01, {0x02} },
	{REGFLAG_UDELAY, 100, {} },
	/* CMD_Page 4 */
	{ 0xFF, 0x03, {0x98, 0x81, 0x04} },
	{ 0x00, 0x01, {0x00} },
	{ 0x6C, 0x01, {0x15} },
	{ 0x6E, 0x01, {0x2b} },
	{ 0x6F, 0x01, {0x35} },
	{ 0x35, 0x01, {0x1f} },
	{ 0x3A, 0x01, {0x24} },
	{ 0x8D, 0x01, {0x14} },
	{ 0x87, 0x01, {0xBA} },
	{ 0x26, 0x01, {0x76} },
	{ 0xB2, 0x01, {0xD1} },
	{ 0xB5, 0x01, {0x06} },
	{ 0x33, 0x01, {0x14} },
	{ 0x7a, 0x01, {0x10} },
	{REGFLAG_UDELAY, 100, {} },
	/* CMD_Page 1 */
	{ 0xFF, 0x03, {0x98, 0x81, 0x01} },
	{ 0x22, 0x01, {0x0A} },
	{ 0x31, 0x01, {0x00} },
	{ 0x53, 0x01, {0x83} },
	{ 0x55, 0x01, {0x9C} },
	{ 0x50, 0x01, {0xC7} },
	{ 0x51, 0x01, {0xC4} },
	{ 0x60, 0x01, {0x1A} },
	{ 0x62, 0x01, {0x00} },
	{ 0x63, 0x01, {0x00} },
	{ 0x2E, 0x01, {0xF0} },
	/* ============Gamma START============= */
	/* Pos Register */
	{ 0xA0, 0x01, {0x02} },
	{ 0xA1, 0x01, {0x22} },
	{ 0xA2, 0x01, {0x31} },
	{ 0xA3, 0x01, {0x15} },
	{ 0xA4, 0x01, {0x19} },
	{ 0xA5, 0x01, {0x2B} },
	{ 0xA6, 0x01, {0x1F} },
	{ 0xA7, 0x01, {0x20} },
	{ 0xA8, 0x01, {0x92} },
	{ 0xA9, 0x01, {0x1C} },
	{ 0xAA, 0x01, {0x28} },
	{ 0xAB, 0x01, {0x7C} },
	{ 0xAC, 0x01, {0x1B} },
	{ 0xAD, 0x01, {0x1B} },
	{ 0xAE, 0x01, {0x4F} },
	{ 0xAF, 0x01, {0x23} },
	{ 0xB0, 0x01, {0x29} },
	{ 0xB1, 0x01, {0x56} },
	{ 0xB2, 0x01, {0x5F} },
	{ 0xB3, 0x01, {0x3F} },
	/* Neg Register */
	{ 0xC0, 0x01, {0x02} },
	{ 0xC1, 0x01, {0x22} },
	{ 0xC2, 0x01, {0x31} },
	{ 0xC3, 0x01, {0x15} },
	{ 0xC4, 0x01, {0x19} },
	{ 0xC5, 0x01, {0x2B} },
	{ 0xC6, 0x01, {0x1F} },
	{ 0xC7, 0x01, {0x20} },
	{ 0xC8, 0x01, {0x92} },
	{ 0xC9, 0x01, {0x1C} },
	{ 0xCA, 0x01, {0x28} },
	{ 0xCB, 0x01, {0x7C} },
	{ 0xCC, 0x01, {0x1B} },
	{ 0xCD, 0x01, {0x1B} },
	{ 0xCE, 0x01, {0x4F} },
	{ 0xCF, 0x01, {0x23} },
	{ 0xD0, 0x01, {0x29} },
	{ 0xD1, 0x01, {0x56} },
	{ 0xD2, 0x01, {0x5F} },
	{ 0xD3, 0x01, {0x3F} },
	/* ============ Gamma END=========== */
	/* CMD_Page 0 */
	{ 0xFF, 0x03, {0x98, 0x81, 0x00} },
	{ 0x35, 0x01, {0x00} },
	{ 0x11, 0x01, {0x00} },
	{REGFLAG_DELAY, 120, {} },
	{ 0x29, 0x01, {0x00} },
	{REGFLAG_DELAY, 20, {} },

};

static struct LCM_setting_table bl_level[] = {
	{0x51, 1, {0xFF} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

static void push_table(void *cmdq, struct LCM_setting_table *table,
	unsigned int count, unsigned char force_update)
{
	unsigned int i;
	unsigned cmd;

	for (i = 0; i < count; i++) {

		cmd = table[i].cmd;

		switch (cmd) {

		case REGFLAG_DELAY:
			if (table[i].count <= 10)
				MDELAY(table[i].count);
			else
				MDELAY(table[i].count);
			break;

		case REGFLAG_UDELAY:
			UDELAY(table[i].count);
			break;

		case REGFLAG_END_OF_TABLE:
			break;

		default:
			dsi_set_cmdq_V22(cmdq, cmd, table[i].count, table[i].para_list, force_update);
		}
	}
}


static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}


static void lcm_get_params(LCM_PARAMS *params)
{
	memset(params, 0, sizeof(LCM_PARAMS));

	params->type = LCM_TYPE_DSI;
	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;
	params->density = LCM_DENSITY;
	params->physical_width = LCM_PHYSICAL_WIDTH/1000;
	params->physical_height = LCM_PHYSICAL_HEIGHT/1000;
	params->physical_width_um = LCM_PHYSICAL_WIDTH;
	params->physical_height_um = LCM_PHYSICAL_HEIGHT;
	params->lcm_if = LCM_INTERFACE_DSI0;


#if (LCM_DSI_CMD_MODE)
	params->dsi.mode = CMD_MODE;
	params->dsi.switch_mode = SYNC_PULSE_VDO_MODE;
	lcm_dsi_mode = CMD_MODE;
#else
	params->dsi.mode = SYNC_PULSE_VDO_MODE;
	params->dsi.switch_mode = CMD_MODE;
	lcm_dsi_mode = SYNC_PULSE_VDO_MODE;
#endif
	LCM_LOGI("lcm_get_params lcm_dsi_mode %d\n", lcm_dsi_mode);
	params->dsi.switch_mode_enable = 0;

	/* DSI */
	/* Command mode setting */
	params->dsi.LANE_NUM = LCM_THREE_LANE;
	/* The following defined the fomat for data coming from LCD engine. */
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;

	/* Highly depends on LCD driver capability. */
	params->dsi.packet_size = 256;
	/* video mode timing */

	params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

	params->dsi.vertical_sync_active = 4;
	params->dsi.vertical_backporch = 14;
	params->dsi.vertical_frontporch = 32;
	params->dsi.vertical_frontporch_for_low_power = 530;
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active = 30;
	params->dsi.horizontal_backporch = 60;
	params->dsi.horizontal_frontporch = 60;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;
	params->dsi.ssc_disable = 1;
#ifndef CONFIG_FPGA_EARLY_PORTING
#if (LCM_DSI_CMD_MODE)
	params->dsi.PLL_CLOCK = 220;	/* this value must be in MTK suggested table */
#else
	params->dsi.PLL_CLOCK = 325;	/* this value must be in MTK suggested table */
#endif
	params->dsi.PLL_CK_CMD = 220;
	params->dsi.PLL_CK_VDO = 325;
#else
	params->dsi.pll_div1 = 0;
	params->dsi.pll_div2 = 0;
	params->dsi.fbk_div = 0x1;
#endif
	params->dsi.clk_lp_per_line_enable = 0;
	params->dsi.esd_check_enable = 1;
	params->dsi.customization_esd_check_enable = 0;
	params->dsi.lcm_esd_check_table[0].cmd = 0x0A;
	params->dsi.lcm_esd_check_table[0].count = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9C;

}


static void lcm_init_power(void)
{

}

static void lcm_suspend_power(void)
{

}

static void lcm_resume_power(void)
{

}

static void lcm_init(void)
{
	SET_RESET_PIN(1);
	SET_RESET_PIN(0);
	MDELAY(10);
	SET_RESET_PIN(1);
	MDELAY(20);

	if (lcm_dsi_mode == CMD_MODE) {
		LCM_LOGI("ili9881c----not support ----lcm mode\n");
	} else {
		push_table(NULL, init_setting, sizeof(init_setting) / sizeof(struct LCM_setting_table), 1);
		LCM_LOGI("ili9881c----tps6132----lcm mode = vdo mode :%d----\n", lcm_dsi_mode);
	}
}

static void lcm_suspend(void)
{
	push_table(NULL, lcm_suspend_setting, sizeof(lcm_suspend_setting) / sizeof(struct LCM_setting_table), 1);
	MDELAY(10);

	/*SET_RESET_PIN(0);*/
}

static void lcm_resume(void)
{
	lcm_init();
}

static void lcm_update(unsigned int x, unsigned int y, unsigned int width, unsigned int height)
{
	unsigned int x0 = x;
	unsigned int y0 = y;
	unsigned int x1 = x0 + width - 1;
	unsigned int y1 = y0 + height - 1;

	unsigned char x0_MSB = ((x0 >> 8) & 0xFF);
	unsigned char x0_LSB = (x0 & 0xFF);
	unsigned char x1_MSB = ((x1 >> 8) & 0xFF);
	unsigned char x1_LSB = (x1 & 0xFF);
	unsigned char y0_MSB = ((y0 >> 8) & 0xFF);
	unsigned char y0_LSB = (y0 & 0xFF);
	unsigned char y1_MSB = ((y1 >> 8) & 0xFF);
	unsigned char y1_LSB = (y1 & 0xFF);

	unsigned int data_array[16];

	data_array[0] = 0x00053902;
	data_array[1] = (x1_MSB << 24) | (x0_LSB << 16) | (x0_MSB << 8) | 0x2a;
	data_array[2] = (x1_LSB);
	dsi_set_cmdq(data_array, 3, 1);

	data_array[0] = 0x00053902;
	data_array[1] = (y1_MSB << 24) | (y0_LSB << 16) | (y0_MSB << 8) | 0x2b;
	data_array[2] = (y1_LSB);
	dsi_set_cmdq(data_array, 3, 1);

	data_array[0] = 0x002c3909;
	dsi_set_cmdq(data_array, 1, 0);
}

static unsigned int lcm_compare_id(void)
{
	unsigned int id = 0, version_id = 0;
	unsigned char buffer[2];
	unsigned int array[16];
	struct LCM_setting_table switch_table_page1[] = {
		{ 0xFF, 0x03, {0x98, 0x81, 0x01} }
	};
	struct LCM_setting_table switch_table_page0[] = {
		{ 0xFF, 0x03, {0x98, 0x81, 0x00} }
	};

	SET_RESET_PIN(1);
	SET_RESET_PIN(0);
	MDELAY(1);

	SET_RESET_PIN(1);
	MDELAY(20);

	push_table(NULL, switch_table_page1, sizeof(switch_table_page1) / sizeof(struct LCM_setting_table), 1);

	array[0] = 0x00023700;	/* read id return two byte,version and id */
	dsi_set_cmdq(array, 1, 1);

	read_reg_v2(0x00, buffer, 1);
	id = buffer[0];

	read_reg_v2(0x01, buffer, 1);
	version_id = buffer[0];

	LCM_LOGI("%s,ili9881c_id=0x%08x,version_id=0x%x\n", __func__, id, version_id);

	push_table(NULL, switch_table_page0, sizeof(switch_table_page0) / sizeof(struct LCM_setting_table), 1);

	if (id == LCM_ID && version_id == 0x81)
		return 1;
	else
		return 0;

}

/* return TRUE: need recovery */
/* return FALSE: No need recovery */
static unsigned int lcm_esd_check(void)
{
#ifndef BUILD_LK
	char buffer[3];
	int array[4];

	array[0] = 0x00013700;
	dsi_set_cmdq(array, 1, 1);

	read_reg_v2(0x53, buffer, 1);

	if (buffer[0] != 0x24) {
		LCM_LOGI("[LCM ERROR] [0x53]=0x%02x\n", buffer[0]);
		return TRUE;
	}
	LCM_LOGI("[LCM NORMAL] [0x53]=0x%02x\n", buffer[0]);
	return FALSE;
#else
	return FALSE;
#endif

}

static unsigned int lcm_ata_check(unsigned char *buffer)
{
#ifndef BUILD_LK
	unsigned int ret = 0;
	unsigned int x0 = FRAME_WIDTH / 4;
	unsigned int x1 = FRAME_WIDTH * 3 / 4;

	unsigned char x0_MSB = ((x0 >> 8) & 0xFF);
	unsigned char x0_LSB = (x0 & 0xFF);
	unsigned char x1_MSB = ((x1 >> 8) & 0xFF);
	unsigned char x1_LSB = (x1 & 0xFF);

	unsigned int data_array[3];
	unsigned char read_buf[4];

	LCM_LOGI("ATA check size = 0x%x,0x%x,0x%x,0x%x\n", x0_MSB, x0_LSB, x1_MSB, x1_LSB);
	data_array[0] = 0x0005390A;	/* HS packet */
	data_array[1] = (x1_MSB << 24) | (x0_LSB << 16) | (x0_MSB << 8) | 0x2a;
	data_array[2] = (x1_LSB);
	dsi_set_cmdq(data_array, 3, 1);

	data_array[0] = 0x00043700;	/* read id return two byte,version and id */
	dsi_set_cmdq(data_array, 1, 1);

	read_reg_v2(0x2A, read_buf, 4);

	if ((read_buf[0] == x0_MSB) && (read_buf[1] == x0_LSB)
	    && (read_buf[2] == x1_MSB) && (read_buf[3] == x1_LSB))
		ret = 1;
	else
		ret = 0;

	x0 = 0;
	x1 = FRAME_WIDTH - 1;

	x0_MSB = ((x0 >> 8) & 0xFF);
	x0_LSB = (x0 & 0xFF);
	x1_MSB = ((x1 >> 8) & 0xFF);
	x1_LSB = (x1 & 0xFF);

	data_array[0] = 0x0005390A;	/* HS packet */
	data_array[1] = (x1_MSB << 24) | (x0_LSB << 16) | (x0_MSB << 8) | 0x2a;
	data_array[2] = (x1_LSB);
	dsi_set_cmdq(data_array, 3, 1);
	return ret;
#else
	return 0;
#endif
}

static void lcm_setbacklight_cmdq(void *handle, unsigned int level)
{

	LCM_LOGI("%s,ili9881c backlight: level = %d\n", __func__, level);

	bl_level[0].para_list[0] = level;

	push_table(handle, bl_level, sizeof(bl_level) / sizeof(struct LCM_setting_table), 1);
}

static void *lcm_switch_mode(int mode)
{
#ifndef BUILD_LK
/* customization: 1. V2C config 2 values, C2V config 1 value; 2. config mode control register */
	if (mode == 0) {	/* V2C */
		lcm_switch_mode_cmd.mode = CMD_MODE;
		lcm_switch_mode_cmd.addr = 0xBB;	/* mode control addr */
		lcm_switch_mode_cmd.val[0] = 0x13;	/* enabel GRAM firstly, ensure writing one frame to GRAM */
		lcm_switch_mode_cmd.val[1] = 0x10;	/* disable video mode secondly */
	} else {		/* C2V */
		lcm_switch_mode_cmd.mode = SYNC_PULSE_VDO_MODE;
		lcm_switch_mode_cmd.addr = 0xBB;
		lcm_switch_mode_cmd.val[0] = 0x03;	/* disable GRAM and enable video mode */
	}
	return (void *)(&lcm_switch_mode_cmd);
#else
	return NULL;
#endif
}


LCM_DRIVER ili9881c_hd_dsi_vdo_ilitek_nt50358_3lane_lcm_drv = {
	.name = "ili9881c_hd_dsi_vdo_ilitek_nt50358_3lane_drv",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
	.compare_id = lcm_compare_id,
	.init_power = lcm_init_power,
	.resume_power = lcm_resume_power,
	.suspend_power = lcm_suspend_power,
	.esd_check = lcm_esd_check,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = lcm_ata_check,
	.update = lcm_update,
	.switch_mode = lcm_switch_mode,
};

