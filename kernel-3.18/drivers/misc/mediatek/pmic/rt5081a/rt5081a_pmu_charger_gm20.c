
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/switch.h>
#include <linux/math64.h>

#include <mtk_charger_intf.h>
#include <mt-plat/aee.h>
#include <mt-plat/charging.h>
#include <mt-plat/battery_common.h>
#include <mt-plat/mt_boot_common.h>
#include <mach/mt_pe.h>

#include "inc/rt5081a_pmu_fled.h"
#include "inc/rt5081a_pmu_charger.h"
#include "inc/rt5081a_pmu.h"

#define RT5081A_PMU_CHARGER_DRV_VERSION	"1.1.22_MTK"


/* ======================= */
/* RT5081A Charger Variable */
/* ======================= */

enum rt5081a_pmu_charger_irqidx {
	RT5081A_CHG_IRQIDX_CHGIRQ1 = 0,
	RT5081A_CHG_IRQIDX_CHGIRQ2,
	RT5081A_CHG_IRQIDX_CHGIRQ3,
	RT5081A_CHG_IRQIDX_CHGIRQ4,
	RT5081A_CHG_IRQIDX_CHGIRQ5,
	RT5081A_CHG_IRQIDX_CHGIRQ6,
	RT5081A_CHG_IRQIDX_QCIRQ,
	RT5081A_CHG_IRQIDX_DICHGIRQ7,
	RT5081A_CHG_IRQIDX_OVPCTRLIRQ,
	RT5081A_CHG_IRQIDX_MAX,
};

enum rt5081a_pmu_chg_type {
	RT5081A_CHG_TYPE_NOVBUS = 0,
	RT5081A_CHG_TYPE_UNDER_GOING,
	RT5081A_CHG_TYPE_SDP,
	RT5081A_CHG_TYPE_SDPNSTD,
	RT5081A_CHG_TYPE_DCP,
	RT5081A_CHG_TYPE_CDP,
	RT5081A_CHG_TYPE_MAX,
};

enum rt5081a_usbsw_state {
	RT5081A_USBSW_CHG = 0,
	RT5081A_USBSW_USB,
};

struct rt5081a_pmu_charger_desc {
	u32 ichg;
	u32 aicr;
	u32 mivr;
	u32 cv;
	u32 ieoc;
	u32 safety_timer;
	u32 ircmp_resistor;
	u32 ircmp_vclamp;
	u32 dc_wdt;
	bool en_te;
	bool en_wdt;
	bool en_polling;
	const char *chg_dev_name;
	const char *ls_dev_name;
};

struct rt5081a_pmu_charger_data {
	/* Inherited from mtk_charger_info */
	struct mtk_charger_info mchr_info;
	struct rt5081a_pmu_charger_desc *chg_desc;
	struct rt5081a_pmu_chip *chip;
	struct mutex adc_access_lock;
	struct mutex irq_access_lock;
	struct mutex aicr_access_lock;
	struct mutex ichg_access_lock;
	struct mutex pe_access_lock;
	struct mutex bc12_access_lock;
	struct mutex hidden_mode_lock;
	struct mutex chgdet_lock;
	struct device *dev;
	wait_queue_head_t wait_queue;
	bool err_state;
	CHARGER_TYPE chg_type;
	bool pwr_rdy;
	u8 irq_flag[RT5081A_CHG_IRQIDX_MAX];
	int aicr_limit;
	u32 zcv;
	bool adc_hang;
	struct switch_dev *usb_switch;
	bool bc12_en;
	u32 hidden_mode_cnt;
	u32 ieoc;
	u32 ichg;
	bool ieoc_wkard;
	atomic_t bc12_cnt;
	atomic_t bc12_wkard;
#ifdef CONFIG_TCPC_CLASS
	atomic_t tcpc_usb_connected;
#else
	struct work_struct chgdet_work;
#endif /* CONFIG_TCPC_CLASS */
};

/* These default values will be used if there's no property in dts */
static struct rt5081a_pmu_charger_desc rt5081a_default_chg_desc = {
	.ichg = 2000000,		/* uA */
	.aicr = 500000,			/* uA */
	.mivr = 4400000,		/* uV */
	.cv = 4350000,			/* uA */
	.ieoc = 250000,			/* uA */
	.safety_timer = 12,		/* hour */
#ifdef CONFIG_MTK_BIF_SUPPORT
	.ircmp_resistor = 0,		/* uohm */
	.ircmp_vclamp = 0,		/* uV */
#else
	.ircmp_resistor = 25000,	/* uohm */
	.ircmp_vclamp = 32000,		/* uV */
#endif
	.dc_wdt = 4000000,		/* us */
	.en_te = true,
	.en_wdt = true,
};


static const u32 rt5081a_otg_oc_threshold[] = {
	500000, 700000, 1100000, 1300000, 1800000, 2100000, 2400000, 3000000,
}; /* uA */

static const u32 rt5081a_dc_vbatov_lvl[] = {
	104, 108, 119,
}; /* % * VOREG */

static const u32 rt5081a_dc_wdt[] = {
	0, 125000, 250000, 500000, 1000000, 2000000, 4000000, 8000000,
}; /* us */

enum rt5081a_charging_status {
	RT5081A_CHG_STATUS_READY = 0,
	RT5081A_CHG_STATUS_PROGRESS,
	RT5081A_CHG_STATUS_DONE,
	RT5081A_CHG_STATUS_FAULT,
	RT5081A_CHG_STATUS_MAX,
};

/* Charging status name */
static const char *rt5081a_chg_status_name[RT5081A_CHG_STATUS_MAX] = {
	"ready", "progress", "done", "fault",
};

static const unsigned char rt5081a_reg_en_hidden_mode[] = {
	RT5081A_PMU_REG_HIDDENPASCODE1,
	RT5081A_PMU_REG_HIDDENPASCODE2,
	RT5081A_PMU_REG_HIDDENPASCODE3,
	RT5081A_PMU_REG_HIDDENPASCODE4,
};

static const unsigned char rt5081a_val_en_hidden_mode[] = {
	0x96, 0x69, 0xC3, 0x3C,
};

enum rt5081a_iin_limit_sel {
	RT5081A_IIMLMTSEL_AICR_3250 = 0,
	RT5081A_IIMLMTSEL_CHG_TYPE,
	RT5081A_IINLMTSEL_AICR,
	RT5081A_IINLMTSEL_LOWER_LEVEL, /* lower of above three */
};

enum rt5081a_adc_sel {
	RT5081A_ADC_VBUS_DIV5 = 1,
	RT5081A_ADC_VBUS_DIV2,
	RT5081A_ADC_VSYS,
	RT5081A_ADC_VBAT,
	RT5081A_ADC_TS_BAT = 6,
	RT5081A_ADC_IBUS = 8,
	RT5081A_ADC_IBAT,
	RT5081A_ADC_CHG_VDDP = 11,
	RT5081A_ADC_TEMP_JC,
	RT5081A_ADC_MAX,
};

static const int rt5081a_adc_unit[RT5081A_ADC_MAX] = {
	0,
	RT5081A_ADC_UNIT_VBUS_DIV5,
	RT5081A_ADC_UNIT_VBUS_DIV2,
	RT5081A_ADC_UNIT_VSYS,
	RT5081A_ADC_UNIT_VBAT,
	0,
	RT5081A_ADC_UNIT_TS_BAT,
	0,
	RT5081A_ADC_UNIT_IBUS,
	RT5081A_ADC_UNIT_IBAT,
	0,
	RT5081A_ADC_UNIT_CHG_VDDP,
	RT5081A_ADC_UNIT_TEMP_JC,
};

static const int rt5081a_adc_offset[RT5081A_ADC_MAX] = {
	0,
	RT5081A_ADC_OFFSET_VBUS_DIV5,
	RT5081A_ADC_OFFSET_VBUS_DIV2,
	RT5081A_ADC_OFFSET_VSYS,
	RT5081A_ADC_OFFSET_VBAT,
	0,
	RT5081A_ADC_OFFSET_TS_BAT,
	0,
	RT5081A_ADC_OFFSET_IBUS,
	RT5081A_ADC_OFFSET_IBAT,
	0,
	RT5081A_ADC_OFFSET_CHG_VDDP,
	RT5081A_ADC_OFFSET_TEMP_JC,
};


/* =============================== */
/* rt5081a Charger Register Address */
/* =============================== */

static const unsigned char rt5081a_chg_reg_addr[] = {
	RT5081A_PMU_REG_CHGCTRL1,
	RT5081A_PMU_REG_CHGCTRL2,
	RT5081A_PMU_REG_CHGCTRL3,
	RT5081A_PMU_REG_CHGCTRL4,
	RT5081A_PMU_REG_CHGCTRL5,
	RT5081A_PMU_REG_CHGCTRL6,
	RT5081A_PMU_REG_CHGCTRL7,
	RT5081A_PMU_REG_CHGCTRL8,
	RT5081A_PMU_REG_CHGCTRL9,
	RT5081A_PMU_REG_CHGCTRL10,
	RT5081A_PMU_REG_CHGCTRL11,
	RT5081A_PMU_REG_CHGCTRL12,
	RT5081A_PMU_REG_CHGCTRL13,
	RT5081A_PMU_REG_CHGCTRL14,
	RT5081A_PMU_REG_CHGCTRL15,
	RT5081A_PMU_REG_CHGCTRL16,
	RT5081A_PMU_REG_CHGADC,
	RT5081A_PMU_REG_DEVICETYPE,
	RT5081A_PMU_REG_QCCTRL1,
	RT5081A_PMU_REG_QCCTRL2,
	RT5081A_PMU_REG_QC3P0CTRL1,
	RT5081A_PMU_REG_QC3P0CTRL2,
	RT5081A_PMU_REG_USBSTATUS1,
	RT5081A_PMU_REG_QCSTATUS1,
	RT5081A_PMU_REG_QCSTATUS2,
	RT5081A_PMU_REG_CHGPUMP,
	RT5081A_PMU_REG_CHGCTRL17,
	RT5081A_PMU_REG_CHGCTRL18,
	RT5081A_PMU_REG_CHGDIRCHG1,
	RT5081A_PMU_REG_CHGDIRCHG2,
	RT5081A_PMU_REG_CHGDIRCHG3,
	RT5081A_PMU_REG_CHGSTAT,
	RT5081A_PMU_REG_CHGNTC,
	RT5081A_PMU_REG_ADCDATAH,
	RT5081A_PMU_REG_ADCDATAL,
	RT5081A_PMU_REG_CHGCTRL19,
	RT5081A_PMU_REG_CHGSTAT1,
	RT5081A_PMU_REG_CHGSTAT2,
	RT5081A_PMU_REG_CHGSTAT3,
	RT5081A_PMU_REG_CHGSTAT4,
	RT5081A_PMU_REG_CHGSTAT5,
	RT5081A_PMU_REG_CHGSTAT6,
	RT5081A_PMU_REG_QCSTAT,
	RT5081A_PMU_REG_DICHGSTAT,
	RT5081A_PMU_REG_OVPCTRLSTAT,
};

/* ===================================================================== */
/* Internal Functions                                                    */
/* ===================================================================== */
static int rt5081a_set_aicr(struct mtk_charger_info *mchr_info, void *data);
static int rt5081a_get_aicr(struct mtk_charger_info *mchr_info, void *data);
static int rt5081a_set_ichg(struct mtk_charger_info *mchr_info, void *data);
static int rt5081a_get_ichg(struct mtk_charger_info *mchr_info, void *data);
static int rt5081a_enable_charging(struct mtk_charger_info *mchr_info, void *data);

static inline void rt5081a_chg_irq_set_flag(
	struct rt5081a_pmu_charger_data *chg_data, u8 *irq, u8 mask)
{
	mutex_lock(&chg_data->irq_access_lock);
	*irq |= mask;
	mutex_unlock(&chg_data->irq_access_lock);
}

static inline void rt5081a_chg_irq_clr_flag(
	struct rt5081a_pmu_charger_data *chg_data, u8 *irq, u8 mask)
{
	mutex_lock(&chg_data->irq_access_lock);
	*irq &= ~mask;
	mutex_unlock(&chg_data->irq_access_lock);
}

static inline int rt5081a_pmu_reg_test_bit(
	struct rt5081a_pmu_chip *chip, u8 cmd, u8 shift, bool *is_one)
{
	int ret = 0;
	u8 data = 0;

	ret = rt5081a_pmu_reg_read(chip, cmd);
	if (ret < 0) {
		*is_one = false;
		return ret;
	}

	data = ret & (1 << shift);
	*is_one = (data == 0 ? false : true);

	return ret;
}

static u8 rt5081a_find_closest_reg_value(u32 min, u32 max, u32 step, u32 num,
	u32 target)
{
	u32 i = 0, cur_val = 0, next_val = 0;

	/* Smaller than minimum supported value, use minimum one */
	if (target < min)
		return 0;

	for (i = 0; i < num - 1; i++) {
		cur_val = min + i * step;
		next_val = cur_val + step;

		if (cur_val > max)
			cur_val = max;

		if (next_val > max)
			next_val = max;

		if (target >= cur_val && target < next_val)
			return i;
	}

	/* Greater than maximum supported value, use maximum one */
	return num - 1;
}

static u8 rt5081a_find_closest_reg_value_via_table(const u32 *value_table,
	u32 table_size, u32 target_value)
{
	u32 i = 0;

	/* Smaller than minimum supported value, use minimum one */
	if (target_value < value_table[0])
		return 0;

	for (i = 0; i < table_size - 1; i++) {
		if (target_value >= value_table[i] &&
		    target_value < value_table[i + 1])
			return i;
	}

	/* Greater than maximum supported value, use maximum one */
	return table_size - 1;
}

static u32 rt5081a_find_closest_real_value(u32 min, u32 max, u32 step,
	u8 reg_val)
{
	u32 ret_val = 0;

	ret_val = min + reg_val * step;
	if (ret_val > max)
		ret_val = max;

	return ret_val;
}

static inline void rt5081a_enable_irq(struct rt5081a_pmu_charger_data *chg_data,
	const char *name, bool en)
{
	struct resource *res = NULL;
	struct platform_device *pdev = to_platform_device(chg_data->dev);

	dev_info(chg_data->dev, "%s: (%s) en = %d", __func__, name, en);

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, name);
	if (res)
		(en ? enable_irq : disable_irq)(res->start);
	else
		dev_err(chg_data->dev, "%s: get plat res fail\n", __func__);
}

static int rt5081a_set_fast_charge_timer(
	struct rt5081a_pmu_charger_data *chg_data, u32 hour)
{
	int ret = 0;
	u8 reg_fct = 0;

	reg_fct = rt5081a_find_closest_reg_value(
		RT5081A_WT_FC_MIN,
		RT5081A_WT_FC_MAX,
		RT5081A_WT_FC_STEP,
		RT5081A_WT_FC_NUM,
		hour
	);

	dev_info(chg_data->dev, "%s: timer = %d (0x%02X)\n", __func__, hour,
		reg_fct);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL12,
		RT5081A_MASK_WT_FC,
		reg_fct << RT5081A_SHIFT_WT_FC
	);

	return ret;
}

static int rt5081a_set_usbsw_state(struct rt5081a_pmu_charger_data *chg_data,
	int state)
{
#ifdef CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT
	dev_info(chg_data->dev, "%s: state = %d\n", __func__, state);

	if (chg_data->usb_switch)
		switch_set_state(chg_data->usb_switch, state);
#ifdef CONFIG_PROJECT_PHY
	else {
		if (state == RT5081A_USBSW_CHG)
			Charger_Detect_Init();
		else
			Charger_Detect_Release();
	}
#endif
#endif /* CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT */

	return 0;
}

static int rt5081a_enable_hidden_mode(struct rt5081a_pmu_charger_data *chg_data,
	bool en)
{
	int ret = 0;

	mutex_lock(&chg_data->hidden_mode_lock);

	if (en) {
		if (chg_data->hidden_mode_cnt == 0) {
			ret = rt5081a_pmu_reg_block_write(chg_data->chip,
				rt5081a_reg_en_hidden_mode[0],
				ARRAY_SIZE(rt5081a_val_en_hidden_mode),
				rt5081a_val_en_hidden_mode);
			if (ret < 0)
				goto err;
		}
		chg_data->hidden_mode_cnt++;
	} else {
		if (chg_data->hidden_mode_cnt == 1) /* last one */
			ret = rt5081a_pmu_reg_write(chg_data->chip,
				rt5081a_reg_en_hidden_mode[0], 0x00);
		chg_data->hidden_mode_cnt--;
		if (ret < 0)
			goto err;
	}
	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);
	goto out;

err:
	dev_err(chg_data->dev, "%s: en = %d fail(%d)\n", __func__, en, ret);
out:
	mutex_unlock(&chg_data->hidden_mode_lock);
	return ret;
}

static int rt5081a_get_adc(struct rt5081a_pmu_charger_data *chg_data,
	enum rt5081a_adc_sel adc_sel, int *adc_val)
{
	int ret = 0, i = 0;
	u8 adc_data[6] = {0};
	bool adc_start = false;
	u32 aicr = 0, ichg = 0;
	s64 adc_result = 0;
	const int max_wait_times = 6;

	mutex_lock(&chg_data->adc_access_lock);
	rt5081a_enable_hidden_mode(chg_data, true);

	/* Select ADC to desired channel */
	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGADC,
		RT5081A_MASK_ADC_IN_SEL,
		adc_sel << RT5081A_SHIFT_ADC_IN_SEL
	);

	if (ret < 0) {
		dev_err(chg_data->dev, "%s: select ch to %d failed, ret = %d\n",
			__func__, adc_sel, ret);
		goto out;
	}

	/* Workaround for IBUS & IBAT */
	if (adc_sel == RT5081A_ADC_IBUS) {
		mutex_lock(&chg_data->aicr_access_lock);
		ret = rt5081a_get_aicr(&chg_data->mchr_info, &aicr);
		if (ret < 0) {
			dev_err(chg_data->dev, "%s: get aicr failed\n",
				__func__);
			goto out_unlock_all;
		}
	} else if (adc_sel == RT5081A_ADC_IBAT) {
		mutex_lock(&chg_data->ichg_access_lock);
		ret = rt5081a_get_ichg(&chg_data->mchr_info, &ichg);
		if (ret < 0) {
			dev_err(chg_data->dev, "%s: get ichg failed\n",
				__func__);
			goto out_unlock_all;
		}
	}

	/* Start ADC conversation */
	ret = rt5081a_pmu_reg_set_bit(chg_data->chip, RT5081A_PMU_REG_CHGADC,
		RT5081A_MASK_ADC_START);
	if (ret < 0) {
		dev_err(chg_data->dev,
			"%s: start conversation failed, sel = %d, ret = %d\n",
			__func__, adc_sel, ret);
		goto out_unlock_all;
	}

	for (i = 0; i < max_wait_times; i++) {
		msleep(35);
		ret = rt5081a_pmu_reg_test_bit(chg_data->chip,
			RT5081A_PMU_REG_CHGADC, RT5081A_SHIFT_ADC_START,
			&adc_start);
		if (!adc_start && ret >= 0)
			break;
	}
	if (i == max_wait_times) {
		dev_err(chg_data->dev,
			"%s: wait conversation failed, sel = %d, ret = %d\n",
			__func__, adc_sel, ret);

		if (!chg_data->adc_hang) {
			for (i = 0; i < ARRAY_SIZE(rt5081a_chg_reg_addr); i++) {
				ret = rt5081a_pmu_reg_read(chg_data->chip,
						rt5081a_chg_reg_addr[i]);

				dev_err(chg_data->dev, "%s: reg[0x%02X] = 0x%02X\n",
					__func__, rt5081a_chg_reg_addr[i], ret);
			}


			chg_data->adc_hang = true;
		}

		/* Add for debug */
		/* ZCV, reg0x10 */
		ret = rt5081a_pmu_reg_read(chg_data->chip, RT5081A_PMU_REG_OSCCTRL);
		if (ret < 0)
			dev_err(chg_data->dev, "%s: read reg0x10 failed\n", __func__);
		else
			dev_err(chg_data->dev, "%s: reg0x10 = 0x%02X\n", __func__, ret);

		/* TS auto sensing */
		ret = rt5081a_pmu_reg_read(chg_data->chip, RT5081A_PMU_REG_CHGHIDDENCTRL15);
		if (ret < 0)
			dev_err(chg_data->dev, "%s: read reg0x3E failed\n", __func__);
		else
			dev_err(chg_data->dev, "%s: reg0x3E = 0x%02X\n", __func__, ret);

	}

	mdelay(1);

	/* Read ADC data */
	ret = rt5081a_pmu_reg_block_read(chg_data->chip, RT5081A_PMU_REG_ADCDATAH,
		6, adc_data);
	if (ret < 0) {
		dev_err(chg_data->dev,
			"%s: read ADC data failed, ret = %d\n", __func__, ret);
		goto out_unlock_all;
	}

	dev_dbg(chg_data->dev,
		"%s: adc_sel = %d, adc_h = 0x%02X, adc_l = 0x%02X\n",
		__func__, adc_sel, adc_data[0], adc_data[1]);

	dev_dbg(chg_data->dev,
		"%s: 0x4e~51 = (0x%02X, 0x%02X, 0x%02X, 0x%02X)\n", __func__,
		adc_data[2], adc_data[3], adc_data[4], adc_data[5]);

	/* Calculate ADC value */
	adc_result = ((s64)adc_data[0] * 256 + adc_data[1]) * rt5081a_adc_unit[adc_sel]
		+ rt5081a_adc_offset[adc_sel];

out_unlock_all:
	/* Coefficient of IBUS & IBAT, current unit: 10uA */
#if defined(__LP64__) || defined(_LP64)
	if (adc_sel == RT5081A_ADC_IBUS) {
		if (aicr < 40000) /* 400mA */
			adc_result = adc_result * 67 / 100;
		mutex_unlock(&chg_data->aicr_access_lock);
	} else if (adc_sel == RT5081A_ADC_IBAT) {
		if (ichg >= 10000 && ichg <= 45000) /* 100~450mA */
			adc_result = adc_result * 475 / 1000;
		else if (ichg >= 50000 && ichg <= 85000) /* 500~850mA */
			adc_result = adc_result * 536 / 1000;
		mutex_unlock(&chg_data->ichg_access_lock);
	}
#else
	if (adc_sel == RT5081A_ADC_IBUS) {
		if (aicr < 40000) /* 400mA */
			adc_result = div_s64(adc_result * 67, 100);
		mutex_unlock(&chg_data->aicr_access_lock);
	} else if (adc_sel == RT5081A_ADC_IBAT) {
		if (ichg >= 10000 && ichg <= 45000) /* 100~450mA */
			adc_result = div_s64(adc_result * 475, 1000);
		else if (ichg >= 50000 && ichg <= 85000) /* 500~850mA */
			adc_result = div_s64(adc_result * 536, 1000);
		mutex_unlock(&chg_data->ichg_access_lock);
	}
#endif
out:
	*adc_val = adc_result;
	rt5081a_enable_hidden_mode(chg_data, false);
	mutex_unlock(&chg_data->adc_access_lock);
	return ret;
}

static int rt5081a_enable_chgdet_flow(struct rt5081a_pmu_charger_data *chg_data,
	bool en)
{
	int ret = 0;
	int i = 0, vbus = 0;
	const int max_wait_cnt = 200;
	enum rt5081a_usbsw_state usbsw =
		en ? RT5081A_USBSW_CHG : RT5081A_USBSW_USB;

	if (en) {
		/* Workaround for CDP port */
		for (i = 0; i < max_wait_cnt; i++) {
			if (is_usb_rdy())
				break;
			dev_info(chg_data->dev, "%s: CDP block\n", __func__);
			ret = rt5081a_get_adc(chg_data, RT5081A_ADC_VBUS_DIV5,
				&vbus);
			if (ret >= 0 && vbus < 4300000) {
				dev_info(chg_data->dev,
					"%s: plug out, vbus = %dmV\n",
					__func__, vbus / 1000);
				return 0;
			}
			msleep(100);
		}
		if (i == max_wait_cnt)
			dev_err(chg_data->dev, "%s: CDP timeout\n", __func__);
		else
			dev_info(chg_data->dev, "%s: CDP free\n", __func__);
	}

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);
	rt5081a_set_usbsw_state(chg_data, usbsw);
	mutex_lock(&chg_data->bc12_access_lock);
	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_DEVICETYPE, RT5081A_MASK_USBCHGEN);
	if (ret >= 0)
		chg_data->bc12_en = en;
	mutex_unlock(&chg_data->bc12_access_lock);

	return ret;
}

/* Hardware pin current limit */
static int rt5081a_enable_ilim(struct rt5081a_pmu_charger_data *chg_data, bool en)
{
	int ret = 0;

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);

	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGCTRL3, RT5081A_MASK_ILIM_EN);

	return ret;
}

#ifdef CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT
static inline int rt5081a_toggle_chgdet_flow(
	struct rt5081a_pmu_charger_data *chg_data)
{
	int ret = 0;
	u8 data = 0;

	/* read data */
	ret = i2c_smbus_read_i2c_block_data(chg_data->chip->i2c,
		RT5081A_PMU_REG_DEVICETYPE, 1, &data);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: read usbd fail\n", __func__);
		goto out;
	}

	/* usbd off */
	data &= ~RT5081A_MASK_USBCHGEN;
	ret = i2c_smbus_write_i2c_block_data(chg_data->chip->i2c,
		RT5081A_PMU_REG_DEVICETYPE, 1, &data);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: usbd off fail\n", __func__);
		goto out;
	}

	udelay(40);

	/* usbd on */
	data |= RT5081A_MASK_USBCHGEN;
	ret = i2c_smbus_write_i2c_block_data(chg_data->chip->i2c,
		RT5081A_PMU_REG_DEVICETYPE, 1, &data);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: usbd on fail\n", __func__);
out:

	return ret;
}

static int rt5081a_bc12_workaround(struct rt5081a_pmu_charger_data *chg_data)
{
	int ret = 0;

	dev_info(chg_data->dev, "%s\n", __func__);

	rt_mutex_lock(&chg_data->chip->io_lock);

	ret = rt5081a_toggle_chgdet_flow(chg_data);
	if (ret < 0)
		goto err;

	mdelay(10);

	ret = rt5081a_toggle_chgdet_flow(chg_data);
	if (ret < 0)
		goto err;

	goto out;
err:
	dev_err(chg_data->dev, "%s: fail\n", __func__);
out:
	rt_mutex_unlock(&chg_data->chip->io_lock);
	return ret;
}

static int __rt5081a_chgdet_handler(struct rt5081a_pmu_charger_data *chg_data)
{
	int ret = 0;
	bool pwr_rdy = false;
	u8 usb_status = 0;

	dev_info(chg_data->dev, "%s\n", __func__);
#ifdef CONFIG_TCPC_CLASS
	pwr_rdy = atomic_read(&chg_data->tcpc_usb_connected);
#else
	/* Check UVP_D_STAT & OTG mode */
	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_OVPCTRLSTAT,
		RT5081A_SHIFT_OVPCTRL_UVP_D_STAT, &pwr_rdy);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: read uvp_d_stat fail\n", __func__);
		return ret;
	}
	pwr_rdy = !pwr_rdy;
#endif
	if (chg_data->pwr_rdy == pwr_rdy &&
		atomic_read(&chg_data->bc12_wkard) == 0) {
		dev_info(chg_data->dev, "%s: pwr rdy(%d) is the same\n",
			__func__, pwr_rdy);
		if (!pwr_rdy)
			goto out;
		return 0;
	}
	chg_data->pwr_rdy = pwr_rdy;

	/* plug out */
	if (!pwr_rdy) {
		chg_data->chg_type = CHARGER_UNKNOWN;
		atomic_set(&chg_data->bc12_cnt, 0);
		goto out;
	}
	atomic_inc(&chg_data->bc12_cnt);

	/* plug in */
	ret = rt5081a_pmu_reg_read(chg_data->chip, RT5081A_PMU_REG_USBSTATUS1);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: read chg type fail\n", __func__);
		return ret;
	}
	usb_status = (ret & RT5081A_MASK_USB_STATUS) >> RT5081A_SHIFT_USB_STATUS;

	switch (usb_status) {
	case RT5081A_CHG_TYPE_UNDER_GOING:
		dev_info(chg_data->dev, "%s: under going...\n", __func__);
		return ret;
	case RT5081A_CHG_TYPE_SDP:
		chg_data->chg_type = STANDARD_HOST;
		break;
	case RT5081A_CHG_TYPE_SDPNSTD:
		chg_data->chg_type = NONSTANDARD_CHARGER;
		break;
	case RT5081A_CHG_TYPE_CDP:
		chg_data->chg_type = CHARGING_HOST;
		break;
	case RT5081A_CHG_TYPE_DCP:
		chg_data->chg_type = STANDARD_CHARGER;
		break;
	default:
		chg_data->chg_type = CHARGER_UNKNOWN;
		break;
	}

	/* BC12 workaround (NONSTD -> STD) */
	if (atomic_read(&chg_data->bc12_cnt) < 3 &&
		chg_data->chg_type == STANDARD_HOST) {
		ret = rt5081a_bc12_workaround(chg_data);
		/* Workaround success, wait for next event */
		if (ret >= 0) {
			atomic_set(&chg_data->bc12_wkard, 1);
			return ret;
		}
		goto out;
	}

#ifdef RT5081A_APPLE_SAMSUNG_TA_SUPPORT
	ret = rt5081a_detect_apple_samsung_ta(chg_data);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: detect apple/samsung ta fail(%d)\n",
			__func__, ret);
#endif

out:
	atomic_set(&chg_data->bc12_wkard, 0);

	/* Turn off USB charger detection */
	ret = rt5081a_enable_chgdet_flow(chg_data, false);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: disable chrdet fail\n", __func__);

	return ret;
}

static int rt5081a_chgdet_handler(struct rt5081a_pmu_charger_data *chg_data)
{
	int ret = 0;

	mutex_lock(&chg_data->chgdet_lock);
	ret = __rt5081a_chgdet_handler(chg_data);
	mutex_unlock(&chg_data->chgdet_lock);
	return ret;
}
#endif /* CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT */

/* Select IINLMTSEL */
static int rt5081a_select_input_current_limit(
	struct rt5081a_pmu_charger_data *chg_data, enum rt5081a_iin_limit_sel sel)
{
	int ret = 0;

	dev_info(chg_data->dev, "%s: select input current limit = %d\n",
		__func__, sel);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL2,
		RT5081A_MASK_IINLMTSEL,
		sel << RT5081A_SHIFT_IINLMTSEL
	);

	return ret;
}

static int rt5081a_chg_sw_workaround(struct rt5081a_pmu_charger_data *chg_data)
{
	int ret = 0;
	u8 zcv_data[2] = {0};

	dev_info(chg_data->dev, "%s\n", __func__);

	rt5081a_enable_hidden_mode(chg_data, true);

	/* Read ZCV data */
	ret = rt5081a_pmu_reg_block_read(chg_data->chip,
		RT5081A_PMU_REG_ADCBATDATAH, 2, zcv_data);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: read zcv data failed\n", __func__);
	else {
		chg_data->zcv = 5000 * (zcv_data[0] * 256 + zcv_data[1]);

		dev_info(chg_data->dev, "%s: zcv = (0x%02X, 0x%02X, %dmV)\n",
			__func__, zcv_data[0], zcv_data[1], chg_data->zcv / 1000);
	}

	/* Trigger any ADC before disabling ZCV */
	ret = rt5081a_pmu_reg_write(chg_data->chip, RT5081A_PMU_REG_CHGADC,
		0x11);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: trigger ADC failed\n", __func__);

	/* Disable ZCV */
	ret = rt5081a_pmu_reg_set_bit(chg_data->chip, RT5081A_PMU_REG_OSCCTRL,
		0x04);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: disable ZCV failed\n", __func__);

	/* Disable TS auto sensing */
	ret = rt5081a_pmu_reg_clr_bit(chg_data->chip,
		RT5081A_PMU_REG_CHGHIDDENCTRL15, 0x01);

	/* Disable SEN_DCP for charging mode */
	ret = rt5081a_pmu_reg_clr_bit(chg_data->chip,
		RT5081A_PMU_REG_QCCTRL2, RT5081A_MASK_EN_DCP);

	rt5081a_enable_hidden_mode(chg_data, false);

	return ret;
}

static int rt5081a_enable_wdt(struct rt5081a_pmu_charger_data *chg_data,
	bool en)
{
	int ret = 0;

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);
	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGCTRL13, RT5081A_MASK_WDT_EN);

	return ret;
}

static int rt5081a_is_charging_enable(struct rt5081a_pmu_charger_data *chg_data,
	bool *en)
{
	int ret = 0;

	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_CHGCTRL2,
		RT5081A_SHIFT_CHG_EN, en);

	return ret;
}

static int __rt5081a_enable_te(struct rt5081a_pmu_charger_data *chg_data, bool en)
{
	int ret = 0;

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);
	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGCTRL2, RT5081A_MASK_TE_EN);

	return ret;
}

static int rt5081a_enable_pump_express(struct rt5081a_pmu_charger_data *chg_data,
	bool en)
{
	int ret = 0, i = 0;
	const int max_wait_times = 5;
	bool pumpx_en = false;
	bool chg_en = true;
	u32 aicr = 80000;	/* 10uA */
	u32 ichg = 200000;	/* 10uA */

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);

	ret = rt5081a_set_aicr(&chg_data->mchr_info, &aicr);
	if (ret < 0)
		return ret;

	ret = rt5081a_set_ichg(&chg_data->mchr_info, &ichg);
	if (ret < 0)
		return ret;

	ret = rt5081a_enable_charging(&chg_data->mchr_info, &chg_en);
	if (ret < 0)
		return ret;

	rt5081a_enable_hidden_mode(chg_data, true);

	ret = rt5081a_pmu_reg_clr_bit(chg_data->chip,
		RT5081A_PMU_REG_CHGHIDDENCTRL9, 0x80);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: disable psk mode fail\n", __func__);

	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGCTRL17, RT5081A_MASK_PUMPX_EN);
	if (ret < 0)
		goto out;

	for (i = 0; i < max_wait_times; i++) {
		msleep(2500);
		ret = rt5081a_pmu_reg_test_bit(chg_data->chip,
			RT5081A_PMU_REG_CHGCTRL17, RT5081A_SHIFT_PUMPX_EN,
			&pumpx_en);
		if (!pumpx_en && ret >= 0)
			break;
	}
	if (i == max_wait_times) {
		dev_err(chg_data->dev, "%s: wait failed, ret = %d\n", __func__,
			ret);
		ret = -EIO;
		goto out;
	}
	ret = 0;
out:
	rt5081a_pmu_reg_set_bit(chg_data->chip, RT5081A_PMU_REG_CHGHIDDENCTRL9,
		0x80);
	rt5081a_enable_hidden_mode(chg_data, false);
	return ret;
}

static int rt5081a_get_ieoc(struct rt5081a_pmu_charger_data *chg_data, u32 *ieoc)
{
	int ret = 0;
	u8 reg_ieoc = 0;

	ret = rt5081a_pmu_reg_read(chg_data->chip, RT5081A_PMU_REG_CHGCTRL9);
	if (ret < 0)
		return ret;

	reg_ieoc = (ret & RT5081A_MASK_IEOC) >> RT5081A_SHIFT_IEOC;
	*ieoc = rt5081a_find_closest_real_value(
		RT5081A_IEOC_MIN,
		RT5081A_IEOC_MAX,
		RT5081A_IEOC_STEP,
		reg_ieoc
	);

	return ret;
}

static int rt5081a_get_mivr(struct rt5081a_pmu_charger_data *chg_data, u32 *mivr)
{
	int ret = 0;
	u8 reg_mivr = 0;

	ret = rt5081a_pmu_reg_read(chg_data->chip, RT5081A_PMU_REG_CHGCTRL6);
	if (ret < 0)
		return ret;

	reg_mivr = ((ret & RT5081A_MASK_MIVR) >> RT5081A_SHIFT_MIVR) & 0xFF;
	*mivr = rt5081a_find_closest_real_value(
		RT5081A_MIVR_MIN,
		RT5081A_MIVR_MAX,
		RT5081A_MIVR_STEP,
		reg_mivr
	);

	return ret;
}

static int __rt5081a_set_ieoc(struct rt5081a_pmu_charger_data *chg_data, u32 ieoc)
{
	int ret = 0;
	u8 reg_ieoc = 0;

	/* IEOC workaround */
	if (chg_data->ieoc_wkard)
		ieoc += 100000; /* 100mA */

	/* Find corresponding reg value */
	reg_ieoc = rt5081a_find_closest_reg_value(
		RT5081A_IEOC_MIN,
		RT5081A_IEOC_MAX,
		RT5081A_IEOC_STEP,
		RT5081A_IEOC_NUM,
		ieoc
	);

	dev_info(chg_data->dev, "%s: ieoc = %d (0x%02X)\n", __func__, ieoc,
		reg_ieoc);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL9,
		RT5081A_MASK_IEOC,
		reg_ieoc << RT5081A_SHIFT_IEOC
	);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: set ieoc fail\n", __func__);

	/* Store IEOC */
	ret = rt5081a_get_ieoc(chg_data, &chg_data->ieoc);

	return ret;
}

static int rt5081a_get_charging_status(struct rt5081a_pmu_charger_data *chg_data,
	enum rt5081a_charging_status *chg_stat)
{
	int ret = 0;

	ret = rt5081a_pmu_reg_read(chg_data->chip, RT5081A_PMU_REG_CHGSTAT);
	if (ret < 0)
		return ret;

	*chg_stat = (ret & RT5081A_MASK_CHG_STAT) >> RT5081A_SHIFT_CHG_STAT;

	return ret;
}

static int rt5081a_set_dc_wdt(struct rt5081a_pmu_charger_data *chg_data, u32 us)
{
	int ret = 0;
	u8 reg_wdt = 0;

	reg_wdt = rt5081a_find_closest_reg_value_via_table(
		rt5081a_dc_wdt,
		ARRAY_SIZE(rt5081a_dc_wdt),
		us
	);

	dev_info(chg_data->dev, "%s: wdt = %dms(0x%02X)\n", __func__, us / 1000,
		reg_wdt);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGDIRCHG2,
		RT5081A_MASK_DC_WDT,
		reg_wdt << RT5081A_SHIFT_DC_WDT
	);

	return ret;
}

static int rt5081a_enable_jeita(struct rt5081a_pmu_charger_data *chg_data,
	bool en)
{
	int ret = 0;

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);

	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGCTRL16, RT5081A_MASK_JEITA_EN);

	return ret;
}

static int rt5081a_set_aicl_vth(struct rt5081a_pmu_charger_data *chg_data,
	u32 aicl_vth)
{
	int ret = 0;
	u8 reg_aicl_vth = 0;

	reg_aicl_vth = rt5081a_find_closest_reg_value(
		RT5081A_AICL_VTH_MIN,
		RT5081A_AICL_VTH_MAX,
		RT5081A_AICL_VTH_STEP,
		RT5081A_AICL_VTH_NUM,
		aicl_vth
	);

	dev_info(chg_data->dev, "%s: vth = %d (0x%02X)\n", __func__, aicl_vth,
		reg_aicl_vth);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL14,
		RT5081A_MASK_AICL_VTH,
		reg_aicl_vth << RT5081A_SHIFT_AICL_VTH
	);

	if (ret < 0)
		dev_err(chg_data->dev, "%s: set aicl vth failed, ret = %d\n",
			__func__, ret);

	return ret;
}

static int __rt5081a_set_mivr(struct rt5081a_pmu_charger_data *chg_data, u32 uV)
{
	int ret = 0;
	u8 reg_mivr = 0;

	/* Find corresponding reg value */
	reg_mivr = rt5081a_find_closest_reg_value(
		RT5081A_MIVR_MIN,
		RT5081A_MIVR_MAX,
		RT5081A_MIVR_STEP,
		RT5081A_MIVR_NUM,
		uV
	);

	dev_info(chg_data->dev, "%s: mivr = %d (0x%02X)\n", __func__, uV,
		reg_mivr);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL6,
		RT5081A_MASK_MIVR,
		reg_mivr << RT5081A_SHIFT_MIVR
	);

	return ret;

}

static int __rt5081a_set_aicr(struct rt5081a_pmu_charger_data *chg_data, u32 uA)
{
	int ret = 0;
	u8 reg_aicr = 0;

	/* Find corresponding reg value */
	reg_aicr = rt5081a_find_closest_reg_value(
		RT5081A_AICR_MIN,
		RT5081A_AICR_MAX,
		RT5081A_AICR_STEP,
		RT5081A_AICR_NUM,
		uA
	);

	dev_info(chg_data->dev, "%s: aicr = %d (0x%02X)\n", __func__, uA,
		reg_aicr);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL3,
		RT5081A_MASK_AICR,
		reg_aicr << RT5081A_SHIFT_AICR
	);

	return ret;
}

static int __rt5081a_run_aicl(struct rt5081a_pmu_charger_data *chg_data)
{
	int ret = 0;
	u32 mivr = 0, aicl_vth = 0, aicr = 0;

	dev_info(chg_data->dev, "%s\n", __func__);

	ret = rt5081a_get_mivr(chg_data, &mivr);
	if (ret < 0)
		goto out;

	/* Check if there's a suitable AICL_VTH */
	aicl_vth = mivr + 200000;
	if (aicl_vth > RT5081A_AICL_VTH_MAX) {
		dev_info(chg_data->dev, "%s: no suitable VTH, vth = %d\n",
			__func__, aicl_vth);
		ret = -EINVAL;
		goto out;
	}

	ret = rt5081a_set_aicl_vth(chg_data, aicl_vth);
	if (ret < 0)
		goto out;

	/* Clear AICL measurement IRQ */
	rt5081a_chg_irq_clr_flag(chg_data,
		&chg_data->irq_flag[RT5081A_CHG_IRQIDX_CHGIRQ5],
		RT5081A_MASK_CHG_AICLMEASI);

	mutex_lock(&chg_data->pe_access_lock);
	mutex_lock(&chg_data->aicr_access_lock);

	ret = rt5081a_pmu_reg_set_bit(chg_data->chip, RT5081A_PMU_REG_CHGCTRL14,
		RT5081A_MASK_AICL_MEAS);
	if (ret < 0)
		goto unlock_out;

	ret = wait_event_interruptible_timeout(chg_data->wait_queue,
		chg_data->irq_flag[RT5081A_CHG_IRQIDX_CHGIRQ5] & RT5081A_MASK_CHG_AICLMEASI,
		msecs_to_jiffies(2500));
	if (ret <= 0) {
		dev_err(chg_data->dev, "%s: wait AICL time out, ret = %d\n",
			__func__, ret);
		ret = -EIO;
		goto unlock_out;
	}

	ret = rt5081a_get_aicr(&chg_data->mchr_info, &aicr);
	if (ret < 0)
		goto unlock_out;

	chg_data->aicr_limit = aicr * 10;
	dev_info(chg_data->dev, "%s: OK, aicr upper bound = %dmA\n", __func__,
		aicr / 1000);

unlock_out:
	mutex_unlock(&chg_data->aicr_access_lock);
	mutex_unlock(&chg_data->pe_access_lock);
out:
	return ret;
}

#if defined(CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT) && !defined(CONFIG_TCPC_CLASS)
static void rt5081a_chgdet_work_handler(struct work_struct *work)
{
	int ret = 0;
	bool uvp_d = false, otg_mode = false;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)container_of(work,
		struct rt5081a_pmu_charger_data, chgdet_work);

	/* Check UVP_D_STAT & OTG mode */
	ret = rt5081a_pmu_reg_test_bit(chg_data->chip,
		RT5081A_PMU_REG_OVPCTRLSTAT, RT5081A_SHIFT_OVPCTRL_UVP_D_STAT,
		&uvp_d);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: read uvp_d_stat fail\n", __func__);
		return;
	}

	/* power not good */
	if (uvp_d)
		return;

	/* power good */
	ret = rt5081a_pmu_reg_test_bit(chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL1, RT5081A_SHIFT_OPA_MODE,
		&otg_mode);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: read otg mode fail\n", __func__);
		return;
	}

	/* In OTG mode skip this event */
	if (otg_mode) {
		dev_info(chg_data->dev, "%s: triggered by OTG\n", __func__);
		return;
	}

	/* Turn on USB charger detection */
	ret = rt5081a_enable_chgdet_flow(chg_data, true);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: en bc12 fail\n", __func__);
}
#endif /* CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT && !CONFIG_TCPC_CLASS */

static int __rt5081a_get_ichg(struct rt5081a_pmu_charger_data *chg_data,
	u32 *ichg)
{
	int ret = 0;
	u8 reg_ichg = 0;

	ret = rt5081a_pmu_reg_read(chg_data->chip, RT5081A_PMU_REG_CHGCTRL7);
	if (ret < 0)
		return ret;

	reg_ichg = (ret & RT5081A_MASK_ICHG) >> RT5081A_SHIFT_ICHG;
	*ichg = rt5081a_find_closest_real_value(RT5081A_ICHG_MIN,
		RT5081A_ICHG_MAX, RT5081A_ICHG_STEP, reg_ichg);

	return ret;
}

static inline int rt5081a_ichg_workaround(
	struct rt5081a_pmu_charger_data *chg_data, u32 uA)
{
	int ret = 0;

	/* Vsys short protection */
	rt5081a_enable_hidden_mode(chg_data, true);

	if (chg_data->ichg >= 900000 && uA < 900000)
		ret = rt5081a_pmu_reg_update_bits(chg_data->chip,
			RT5081A_PMU_REG_CHGHIDDENCTRL7, 0x60, 0x00);
	else if (uA >= 900000 && chg_data->ichg < 900000)
		ret = rt5081a_pmu_reg_update_bits(chg_data->chip,
			RT5081A_PMU_REG_CHGHIDDENCTRL7, 0x60, 0x40);

	rt5081a_enable_hidden_mode(chg_data, false);
	return ret;
}

static int __rt5081a_set_ichg(struct rt5081a_pmu_charger_data *chg_data, u32 uA)
{
	int ret = 0;
	u8 reg_ichg = 0;

	uA = (uA < 500000) ? 500000 : uA;

	ret = rt5081a_ichg_workaround(chg_data, uA);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: workaround fail\n", __func__);

	/* Find corresponding reg value */
	reg_ichg = rt5081a_find_closest_reg_value(
		RT5081A_ICHG_MIN,
		RT5081A_ICHG_MAX,
		RT5081A_ICHG_STEP,
		RT5081A_ICHG_NUM,
		uA
	);

	dev_info(chg_data->dev, "%s: ichg = %d (0x%02X)\n", __func__, uA,
		reg_ichg);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL7,
		RT5081A_MASK_ICHG,
		reg_ichg << RT5081A_SHIFT_ICHG
	);
	if (ret < 0)
		return ret;

	/* Store Ichg setting */
	__rt5081a_get_ichg(chg_data, &chg_data->ichg);

	/* Workaround to make IEOC accurate */
	if (uA < 900000 && !chg_data->ieoc_wkard) { /* 900mA */
		ret = __rt5081a_set_ieoc(chg_data, chg_data->ieoc + 100000);
		chg_data->ieoc_wkard = true;
	} else if (uA >= 900000 && chg_data->ieoc_wkard) {
		chg_data->ieoc_wkard = false;
		ret = __rt5081a_set_ieoc(chg_data, chg_data->ieoc - 100000);
	}

	return ret;
}

static int __rt5081a_set_cv(struct rt5081a_pmu_charger_data *chg_data, u32 uV)
{
	int ret = 0;
	u8 reg_cv = 0;

	reg_cv = rt5081a_find_closest_reg_value(
		RT5081A_BAT_VOREG_MIN,
		RT5081A_BAT_VOREG_MAX,
		RT5081A_BAT_VOREG_STEP,
		RT5081A_BAT_VOREG_NUM,
		uV
	);

	dev_info(chg_data->dev, "%s: bat voreg = %d (0x%02X)\n", __func__, uV,
		reg_cv);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL4,
		RT5081A_MASK_BAT_VOREG,
		reg_cv << RT5081A_SHIFT_BAT_VOREG
	);

	return ret;
}

static int __rt5081a_enable_safety_timer(struct rt5081a_pmu_charger_data *chg_data,
	bool en)
{
	int ret = 0;

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);
	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGCTRL12, RT5081A_MASK_TMR_EN);

	return ret;
}

static int __rt5081a_enable_hz(struct rt5081a_pmu_charger_data *chg_data, bool en)
{
	int ret = 0;

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);
	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGCTRL1, RT5081A_MASK_HZ_EN);

	return ret;
}

static int __rt5081a_set_ircmp_resistor(struct rt5081a_pmu_charger_data *chg_data,
	u32 uohm)
{
	int ret = 0;
	u8 reg_resistor = 0;

	reg_resistor = rt5081a_find_closest_reg_value(
		RT5081A_IRCMP_RES_MIN,
		RT5081A_IRCMP_RES_MAX,
		RT5081A_IRCMP_RES_STEP,
		RT5081A_IRCMP_RES_NUM,
		uohm
	);

	dev_info(chg_data->dev, "%s: resistor = %d (0x%02X)\n", __func__, uohm,
		reg_resistor);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL18,
		RT5081A_MASK_IRCMP_RES,
		reg_resistor << RT5081A_SHIFT_IRCMP_RES
	);

	return ret;
}

static int __rt5081a_set_ircmp_vclamp(struct rt5081a_pmu_charger_data *chg_data,
	u32 uV)
{
	int ret = 0;
	u8 reg_vclamp = 0;

	reg_vclamp = rt5081a_find_closest_reg_value(
		RT5081A_IRCMP_VCLAMP_MIN,
		RT5081A_IRCMP_VCLAMP_MAX,
		RT5081A_IRCMP_VCLAMP_STEP,
		RT5081A_IRCMP_VCLAMP_NUM,
		uV
	);

	dev_info(chg_data->dev, "%s: vclamp = %d (0x%02X)\n", __func__, uV,
		reg_vclamp);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL18,
		RT5081A_MASK_IRCMP_VCLAMP,
		reg_vclamp << RT5081A_SHIFT_IRCMP_VCLAMP
	);

	return ret;
}



/* =================== */
/* Released interfaces */
/* =================== */

/* This is for GM20's PE20 */
static int rt5081a_hw_init(struct mtk_charger_info *mchr_info, void *data)
{
	return 0;
}

static int rt5081a_enable_hz(struct mtk_charger_info *mchr_info, void *data)
{
	bool en = *((bool *)data);
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	return __rt5081a_enable_hz(chg_data, en);
}

static int rt5081a_run_aicl(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	bool mivr_stat = false;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_CHGSTAT1,
		RT5081A_SHIFT_MIVR_STAT, &mivr_stat);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: read mivr stat failed\n", __func__);
		goto out;
	}

	if (!mivr_stat) {
		dev_info(chg_data->dev, "%s: mivr stat not act\n", __func__);
		goto out;
	}

	ret = __rt5081a_run_aicl(chg_data);
	if (ret < 0)
		goto out;

	*((u32 *)data) = chg_data->aicr_limit / 1000;
	return	ret;

out:
	*((u32 *)data) = 0;
	return ret;
}

static int rt5081a_set_ircmp_resistor(struct mtk_charger_info *mchr_info, void *data)
{
	u32 uV = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	uV = *((u32 *)data) * 1000;

	return __rt5081a_set_ircmp_resistor(chg_data, uV);
}

static int rt5081a_set_ircmp_vclamp(struct mtk_charger_info *mchr_info, void *data)
{
	u32 uV = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	uV = *((u32 *)data) * 1000;

	return __rt5081a_set_ircmp_vclamp(chg_data, uV);
}

static int rt5081a_set_error_state(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	chg_data->err_state = *((bool *)data);
	rt5081a_enable_hz(mchr_info, &chg_data->err_state);

	return ret;
}

#if 0 /* Uncomment this if you need it */
static int rt5081a_reset_eoc_state(struct mtk_charger_info *mchr_info)
{
	int ret = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		dev_get_drvdata(&chg_dev->dev);

	dev_info(chg_data->dev, "%s\n", __func__);

	rt5081a_enable_hidden_mode(chg_data, true);

	ret = rt5081a_pmu_reg_set_bit(chg_data->chip,
			RT5081A_PMU_REG_CHGHIDDENCTRL0, 0x80);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: set failed, ret = %d\n",
			__func__, ret);
		goto err;
	}

	udelay(100);
	ret = rt5081a_pmu_reg_clr_bit(chg_data->chip,
			RT5081A_PMU_REG_CHGHIDDENCTRL0, 0x80);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: clear failed, ret = %d\n",
			__func__, ret);
		goto err;
	}

err:
	rt5081a_enable_hidden_mode(chg_data, false);

	return ret;
}
#endif

#ifdef CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT
static int rt5081a_get_charger_type(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	dev_info(chg_data->dev, "%s\n", __func__);

	*(CHARGER_TYPE *)data = chg_data->chg_type;
	pr_info("%s: chg_type = %d\n", __func__, chg_data->chg_type);
	return ret;
}
#endif

static int rt5081a_enable_charging(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;
	bool en = *((bool *)data);

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);

	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGCTRL2, RT5081A_MASK_CHG_EN);

	return ret;
}

static int rt5081a_enable_safety_timer(struct mtk_charger_info *mchr_info, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;
	bool en = *((bool *)data);

	return __rt5081a_enable_safety_timer(chg_data, en);
}

static int rt5081a_is_safety_timer_enable(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_CHGCTRL12,
		RT5081A_SHIFT_TMR_EN, data);

	return ret;
}

static int rt5081a_enable_power_path(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;
	bool en = *((bool *)data);
	u32 mivr = en ? 4500000 : RT5081A_MIVR_MAX;

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);

	/*
	 * enable power path -> unmask mivr irq
	 * mask mivr irq -> disable power path
	 */
	if (!en)
		rt5081a_enable_irq(chg_data, "chg_mivr", false);

	ret = __rt5081a_set_mivr(chg_data, mivr);

	if (en)
		rt5081a_enable_irq(chg_data, "chg_mivr", true);

	return ret;
}

static int rt5081a_is_power_path_enable(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;
	u32 mivr = 0;

	ret = rt5081a_get_mivr(chg_data, &mivr);
	*((bool *)data) = (mivr == RT5081A_MIVR_MAX ? false : true);

	return ret;
}

static int rt5081a_get_ichg(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	u32 ichg = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	ret = __rt5081a_get_ichg(chg_data, &ichg);
	if (ret < 0)
		return ret;

	/* MTK's current unit : 10uA */
	/* Our current unit : uA */
	ichg /= 10;
	*((u32 *)data) = ichg;

	return ret;
}

static int rt5081a_set_ichg(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	u32 uA = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	/* MTK's current unit : 10uA */
	/* Our current unit : uA */
	uA = *((u32 *)data) * 10;

	mutex_lock(&chg_data->ichg_access_lock);
	ret = __rt5081a_set_ichg(chg_data, uA);
	mutex_unlock(&chg_data->ichg_access_lock);

	return ret;
}

static int rt5081a_get_aicr(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	u8 reg_aicr = 0;
	u32 aicr = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	ret = rt5081a_pmu_reg_read(chg_data->chip, RT5081A_PMU_REG_CHGCTRL3);
	if (ret < 0)
		return ret;

	reg_aicr = (ret & RT5081A_MASK_AICR) >> RT5081A_SHIFT_AICR;
	aicr = rt5081a_find_closest_real_value(RT5081A_AICR_MIN, RT5081A_AICR_MAX,
		RT5081A_AICR_STEP, reg_aicr);

	/* MTK's current unit : 10uA */
	/* Our current unit : uA */
	aicr /= 10;
	*((u32 *)data) = aicr;

	return ret;
}

static int rt5081a_set_aicr(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	u32 uA = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	/* MTK's current unit : 10uA */
	/* Our current unit : uA */
	uA = *((u32 *)data) * 10;

	mutex_lock(&chg_data->aicr_access_lock);
	ret = __rt5081a_set_aicr(chg_data, uA);
	mutex_unlock(&chg_data->aicr_access_lock);

	return ret;
}

static int rt5081a_set_mivr(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	u32 uV = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;
	bool en = true;

	ret = rt5081a_is_power_path_enable(mchr_info, &en);
	if (!en) {
		dev_err(chg_data->dev, "%s: power path is disabled\n",
			__func__);
		return -EINVAL;
	}

	/* MTK's current unit : mV */
	/* Our current unit : uV */
	uV = *((u32 *)data) * 1000;

	ret = __rt5081a_set_mivr(chg_data, uV);

	return ret;
}

static int rt5081a_get_cv(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	u8 reg_cv = 0;
	u32 cv = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	ret = rt5081a_pmu_reg_read(chg_data->chip, RT5081A_PMU_REG_CHGCTRL4);
	if (ret < 0)
		return ret;

	reg_cv = (ret & RT5081A_MASK_BAT_VOREG) >> RT5081A_SHIFT_BAT_VOREG;

	cv = rt5081a_find_closest_real_value(
		RT5081A_BAT_VOREG_MIN,
		RT5081A_BAT_VOREG_MAX,
		RT5081A_BAT_VOREG_STEP,
		reg_cv
	);

	*((u32 *)data) = cv;

	return ret;
}

static int rt5081a_set_cv(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	u32 uV = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	uV = *((u32 *)data);

	ret = __rt5081a_set_cv(chg_data, uV);

	return ret;
}

static int rt5081a_set_otg_current_limit(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	u8 reg_ilimit = 0;
	u32 uA = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	/* MTK's current unit : mA */
	/* Our current unit : uA */
	uA = *((u32 *)data) * 1000;

	reg_ilimit = rt5081a_find_closest_reg_value_via_table(
		rt5081a_otg_oc_threshold,
		ARRAY_SIZE(rt5081a_otg_oc_threshold),
		uA
	);

	dev_info(chg_data->dev, "%s: ilimit = %d (0x%02X)\n", __func__, uA,
		reg_ilimit);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL10,
		RT5081A_MASK_BOOST_OC,
		reg_ilimit << RT5081A_SHIFT_BOOST_OC
	);

	return ret;
}

static int rt5081a_enable_otg(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	bool en = *((bool *)data);
	bool en_otg = false;
	u32 current_limit = 500; /* mA */
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;
	u8 hidden_val = en ? 0x00 : 0x0F;
	u8 lg_slew_rate = en ? 0x7c : 0x73;

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);

	rt5081a_enable_hidden_mode(chg_data, true);

	/* Set OTG_OC to 500mA */
	ret = rt5081a_set_otg_current_limit(mchr_info, &current_limit);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: set otg oc failed\n", __func__);
		goto out;
	}

	/*
	 * Woraround : slow Low side mos Gate driver slew rate for decline VBUS noise
	 * reg[0x33] = 0x7c after entering OTG mode
	 * reg[0x33] = 0x73 after leaving OTG mode
	 */
	ret = rt5081a_pmu_reg_write(chg_data->chip, RT5081A_PMU_REG_LG_CONTROL,
		lg_slew_rate);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: recover Low side mos Gate drive speed fail(%d)\n",
			__func__, ret);
		goto out;
	}

	/* Turn off USB charger detection/Enable WDT */
	if (en) {
#ifdef CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT
		ret = rt5081a_enable_chgdet_flow(chg_data, false);
		if (ret < 0)
			dev_err(chg_data->dev, "%s: disable usb chrdet fail\n",
				__func__);
#endif /* CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT */

		if (chg_data->chg_desc->en_wdt) {
			ret = rt5081a_enable_wdt(chg_data, true);
			if (ret < 0)
				dev_err(chg_data->dev, "%s: en wdt fail\n",
					__func__);
		}
	}

	/* Switch OPA mode to boost mode */
	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGCTRL1, RT5081A_MASK_OPA_MODE);

	msleep(20);

	if (en) {
		ret = rt5081a_pmu_reg_test_bit(chg_data->chip,
			RT5081A_PMU_REG_CHGCTRL1, RT5081A_SHIFT_OPA_MODE, &en_otg);
		if (ret < 0 || !en_otg) {
			dev_err(chg_data->dev, "%s: fail(%d)\n", __func__, ret);
			goto err_en_otg;
		}
#if defined(CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT) && defined(CONFIG_TCPC_CLASS)
		rt5081a_set_usbsw_state(chg_data, RT5081A_USBSW_USB);
#endif /* CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT && CONFIG_TCPC_CLASS */
	}

	/*
	 * Woraround reg[0x35] = 0x00 after entering OTG mode
	 * reg[0x35] = 0x0F after leaving OTG mode
	 */
	ret = rt5081a_pmu_reg_write(chg_data->chip,
		RT5081A_PMU_REG_CHGHIDDENCTRL6, hidden_val);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: workaroud failed, ret = %d\n",
			__func__, ret);

	/* Disable WDT */
	if (!en) {
		ret = rt5081a_enable_wdt(chg_data, false);
		if (ret < 0)
			dev_err(chg_data->dev, "%s: disable wdt failed\n",
				__func__);
	}
	goto out;

err_en_otg:
	/* Disable OTG */
	rt5081a_pmu_reg_clr_bit(chg_data->chip, RT5081A_PMU_REG_CHGCTRL1,
		RT5081A_MASK_OPA_MODE);

	/* Disable WDT */
	ret = rt5081a_enable_wdt(chg_data, false);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: disable wdt failed\n", __func__);

	/* Recover Low side mos Gate slew rate */
	ret = rt5081a_pmu_reg_write(chg_data->chip,
			RT5081A_PMU_REG_LG_CONTROL, 0x73);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: recover Low side mos Gate drive speed fail(%d)\n",
			__func__, ret);
	ret = -EIO;
out:
	rt5081a_enable_hidden_mode(chg_data, false);
	return ret;
}

#if 0
static int rt5081a_enable_discharge(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0, i = 0;
	const u32 check_dischg_max = 3;
	bool is_dischg = true;
	bool en = *((bool *)data);
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);

	ret = rt5081a_enable_hidden_mode(chg_data, true);
	if (ret < 0)
		goto out;

	/* Set bit2 of reg[0x31] to 1/0 to enable/disable discharging */
	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGHIDDENCTRL1, 0x04);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: en = %d failed, ret = %d\n",
			__func__, en, ret);
		return ret;
	}

	if (!en) {
		for (i = 0; i < check_dischg_max; i++) {
			ret = rt5081a_pmu_reg_test_bit(chg_data->chip,
				RT5081A_PMU_REG_CHGHIDDENCTRL1, 2, &is_dischg);
			if (!is_dischg)
				break;
			ret = rt5081a_pmu_reg_clr_bit(chg_data->chip,
				RT5081A_PMU_REG_CHGHIDDENCTRL1, 0x04);
		}
		if (i == check_dischg_max)
			dev_err(chg_data->dev,
				"%s: disable discharg failed, ret = %d\n",
				__func__, ret);
	}

out:
	rt5081a_enable_hidden_mode(chg_data, false);
	return ret;
}
#endif

static int rt5081a_set_pep_current_pattern(struct mtk_charger_info *mchr_info,
	void *data)
{
	int ret = 0;
	bool is_increase = *((bool *)data);
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	dev_info(chg_data->dev, "%s: pe1.0 pump_up = %d\n", __func__,
		is_increase);

	mutex_lock(&chg_data->pe_access_lock);

	/* Set to PE1.0 */
	ret = rt5081a_pmu_reg_clr_bit(chg_data->chip, RT5081A_PMU_REG_CHGCTRL17,
		RT5081A_MASK_PUMPX_20_10);

	/* Set Pump Up/Down */
	ret = (is_increase ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGCTRL17,
		RT5081A_MASK_PUMPX_UP_DN);
	if (ret < 0)
		goto out;

	/* Enable PumpX */
	ret = rt5081a_enable_pump_express(chg_data, true);

out:
	mutex_unlock(&chg_data->pe_access_lock);
	return ret;
}

static int rt5081a_set_pep20_reset(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	u32 mivr = 4500;	/* mA */
	u32 ichg = 51200;	/* 10uA */
	u32 aicr = 10000;	/* 10uA */
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	mutex_lock(&chg_data->pe_access_lock);
	ret = rt5081a_set_mivr(mchr_info, &mivr);
	if (ret < 0)
		goto out;

	ret = rt5081a_set_ichg(mchr_info, &ichg);
	if (ret < 0)
		goto out;

	/* disable skip mode */
	rt5081a_enable_hidden_mode(chg_data, true);

	ret = rt5081a_pmu_reg_clr_bit(chg_data->chip,
		RT5081A_PMU_REG_CHGHIDDENCTRL9, 0x80);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: disable psk mode fail\n", __func__);

	ret = rt5081a_set_aicr(mchr_info, &aicr);
	if (ret < 0)
		goto out;

	msleep(250);

	aicr = 70000;	/* 10uA */
	ret = rt5081a_set_aicr(mchr_info, &aicr);

out:
	rt5081a_pmu_reg_set_bit(chg_data->chip, RT5081A_PMU_REG_CHGHIDDENCTRL9,
		0x80);
	rt5081a_enable_hidden_mode(chg_data, false);
	mutex_unlock(&chg_data->pe_access_lock);
	return ret;
}

static int rt5081a_set_pep20_current_pattern(struct mtk_charger_info *mchr_info,
	void *data)
{
	int ret = 0;
	u8 reg_volt = 0;
	u32 uV = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	/* MTK's current unit : mA */
	/* Our current unit : uA */
	uV = *((u32 *)data) * 1000;

	dev_info(chg_data->dev, "%s: pep2.0  = %d\n", __func__, uV);

	mutex_lock(&chg_data->pe_access_lock);
	/* Set to PEP2.0 */
	ret = rt5081a_pmu_reg_set_bit(chg_data->chip, RT5081A_PMU_REG_CHGCTRL17,
		RT5081A_MASK_PUMPX_20_10);
	if (ret < 0)
		goto out;

	/* Find register value of target voltage */
	reg_volt = rt5081a_find_closest_reg_value(
		RT5081A_PEP20_VOLT_MIN,
		RT5081A_PEP20_VOLT_MAX,
		RT5081A_PEP20_VOLT_STEP,
		RT5081A_PEP20_VOLT_NUM,
		uV
	);

	/* Set Voltage */
	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL17,
		RT5081A_MASK_PUMPX_DEC,
		reg_volt << RT5081A_SHIFT_PUMPX_DEC
	);
	if (ret < 0)
		goto out;

	/* Enable PumpX */
	ret = rt5081a_enable_pump_express(chg_data, true);
	ret = (ret >= 0) ? 0 : ret;

out:
	mutex_unlock(&chg_data->pe_access_lock);
	return ret;
}

static int rt5081a_set_pep20_efficiency_table(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	struct pep20_profile_t *profile = (struct pep20_profile_t *)data;

	profile[0].vchr = 8000;
	profile[1].vchr = 8000;
	profile[2].vchr = 8000;
	profile[3].vchr = 8500;
	profile[4].vchr = 8500;
	profile[5].vchr = 8500;
	profile[6].vchr = 9000;
	profile[7].vchr = 9000;
	profile[8].vchr = 9500;
	profile[9].vchr = 9500;

	return ret;
}

#if 0
static int rt5081a_enable_cable_drop_comp(struct mtk_charger_info *mchr_info,
	void *data)
{
	int ret = 0;
	bool en = *((bool *)data);
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);

	mutex_lock(&chg_data->pe_access_lock);
	/* Set to PEP2.0 */
	ret = rt5081a_pmu_reg_set_bit(chg_data->chip, RT5081A_PMU_REG_CHGCTRL17,
		RT5081A_MASK_PUMPX_20_10);
	if (ret < 0)
		goto out;

	/* Set Voltage */
	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGCTRL17,
		RT5081A_MASK_PUMPX_DEC,
		0x1F << RT5081A_SHIFT_PUMPX_DEC
	);
	if (ret < 0)
		goto out;

	/* Enable PumpX */
	ret = rt5081a_enable_pump_express(chg_data, true);

out:
	mutex_unlock(&chg_data->pe_access_lock);
	return ret;
}
#endif

static int rt5081a_is_charging_done(struct mtk_charger_info *mchr_info,
	void *data)
{
	int ret = 0;
	enum rt5081a_charging_status chg_stat = RT5081A_CHG_STATUS_READY;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	ret = rt5081a_get_charging_status(chg_data, &chg_stat);
	if (ret < 0)
		return ret;

	/* Return is charging done or not */
	switch (chg_stat) {
	case RT5081A_CHG_STATUS_READY:
	case RT5081A_CHG_STATUS_PROGRESS:
	case RT5081A_CHG_STATUS_FAULT:
		*((u32 *)data) = false;
		break;
	case RT5081A_CHG_STATUS_DONE:
		*((u32 *)data) = true;
		break;
	default:
		*((u32 *)data) = false;
		break;
	}

	return 0;
}

static int rt5081a_kick_wdt(struct mtk_charger_info *mchr_info, void *data)
{
	/* Any I2C communication can kick watchdog timer */
	int ret = 0;
	enum rt5081a_charging_status chg_status;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	ret = rt5081a_get_charging_status(chg_data, &chg_status);

	return ret;
}

static int rt5081a_enable_direct_charge(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	bool en = *((bool *)data);
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);

	if (en) {
		rt5081a_enable_irq(chg_data, "chg_mivr", false);

		/* Enable bypass mode */
		ret = rt5081a_pmu_reg_set_bit(chg_data->chip,
			RT5081A_PMU_REG_CHGCTRL2, RT5081A_MASK_BYPASS_MODE);
		if (ret < 0) {
			dev_err(chg_data->dev, "%s: en bypass mode failed\n",
				__func__);
			goto out;
		}

		/* VG_EN = 1 */
		ret = rt5081a_pmu_reg_set_bit(chg_data->chip,
			RT5081A_PMU_REG_CHGPUMP, RT5081A_MASK_VG_EN);
		if (ret < 0) {
			dev_err(chg_data->dev, "%s: en VG_EN failed\n",
				__func__);
			goto disable_bypass;
		}

		return ret;
	}

	/* Disable direct charge */
	/* VG_EN = 0 */
	ret = rt5081a_pmu_reg_clr_bit(chg_data->chip, RT5081A_PMU_REG_CHGPUMP,
		RT5081A_MASK_VG_EN);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: disable VG_EN failed\n", __func__);

disable_bypass:
	/* Disable bypass mode */
	ret = rt5081a_pmu_reg_clr_bit(chg_data->chip, RT5081A_PMU_REG_CHGCTRL2,
		RT5081A_MASK_BYPASS_MODE);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: disable bypass mode failed\n",
			__func__);

out:
	rt5081a_enable_irq(chg_data, "chg_mivr", true);
	return ret;
}

static int rt5081a_enable_dc_vbusov(struct mtk_charger_info *mchr_info,
	void *data)
{
	int ret = 0;
	bool en = *((bool *)data);
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGDIRCHG3,
		RT5081A_MASK_DC_VBUSOV_EN);

	return ret;
}

static int rt5081a_set_dc_vbusov(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	u8 reg_vbusov = 0;
	u32 uV = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	/* MTK's current unit : mV */
	/* Our current unit : uV */
	uV = *((u32 *)data) * 1000;

	reg_vbusov = rt5081a_find_closest_reg_value(
		RT5081A_DC_VBUSOV_LVL_MIN,
		RT5081A_DC_VBUSOV_LVL_MAX,
		RT5081A_DC_VBUSOV_LVL_STEP,
		RT5081A_DC_VBUSOV_LVL_NUM,
		uV
	);

	dev_info(chg_data->dev, "%s: vbusov = %d (0x%02X)\n", __func__, uV,
		reg_vbusov);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGDIRCHG3,
		RT5081A_MASK_DC_VBUSOV_LVL,
		reg_vbusov << RT5081A_SHIFT_DC_VBUSOV_LVL
	);

	return ret;
}

static int rt5081a_enable_dc_ibusoc(struct mtk_charger_info *mchr_info,
	void *data)
{
	int ret = 0;
	bool en = *((bool *)data);
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);
	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGDIRCHG1,
		RT5081A_MASK_DC_IBUSOC_EN);

	return ret;
}

static int rt5081a_set_dc_ibusoc(struct mtk_charger_info *mchr_info,
	void *data)
{
	int ret = 0;
	u8 reg_ibusoc = 0;
	u32 uA = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	/* MTK's current unit : mA */
	/* Our current unit : uA */
	uA = *((u32 *)data) * 1000;

	reg_ibusoc = rt5081a_find_closest_reg_value(
		RT5081A_DC_IBUSOC_LVL_MIN,
		RT5081A_DC_IBUSOC_LVL_MAX,
		RT5081A_DC_IBUSOC_LVL_STEP,
		RT5081A_DC_IBUSOC_LVL_NUM,
		uA
	);

	dev_info(chg_data->dev, "%s: ibusoc = %d (0x%02X)\n", __func__, uA,
		reg_ibusoc);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGDIRCHG1,
		RT5081A_MASK_DC_IBUSOC_LVL,
		reg_ibusoc << RT5081A_SHIFT_DC_IBUSOC_LVL
	);

	return ret;
}

static int rt5081a_enable_dc_vbatov(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0;
	bool en = *((bool *)data);
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);

	ret = (en ? rt5081a_pmu_reg_set_bit : rt5081a_pmu_reg_clr_bit)
		(chg_data->chip, RT5081A_PMU_REG_CHGDIRCHG1,
		RT5081A_MASK_DC_VBATOV_EN);

	return ret;
}

static int rt5081a_set_dc_vbatov(struct mtk_charger_info *mchr_info,
	void *data)
{
	int ret = 0, i = 0;
	u8 reg_vbatov = 0;
	u32 cv = 0, vbatov = 0;
	u32 uV = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	/* MTK's current unit : mV */
	/* Our current unit : uV */
	uV = *((u32 *)data) * 1000;

	ret = rt5081a_get_cv(mchr_info, &cv);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: get voreg failed\n", __func__);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(rt5081a_dc_vbatov_lvl); i++) {
		vbatov = (rt5081a_dc_vbatov_lvl[i] * cv) / 100;

		/* Choose closest level */
		if (uV <= vbatov) {
			reg_vbatov = i;
			break;
		}
	}
	if (i == ARRAY_SIZE(rt5081a_dc_vbatov_lvl))
		reg_vbatov = i;

	dev_info(chg_data->dev, "%s: vbatov = %dmV (0x%02X)\n", __func__, uV / 1000,
		reg_vbatov);

	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_CHGDIRCHG1,
		RT5081A_MASK_DC_VBATOV_LVL,
		reg_vbatov << RT5081A_SHIFT_DC_VBATOV_LVL
	);

	return ret;
}

static int rt5081a_is_dc_enable(struct mtk_charger_info *mchr_info,
	void *data)
{
	int ret = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_CHGPUMP,
		RT5081A_SHIFT_VG_EN, data);

	return ret;
}

static int rt5081a_kick_dc_wdt(struct mtk_charger_info *mchr_info, void *data)
{
	/* Any I2C communication can reset watchdog timer */
	int ret = 0;
	enum rt5081a_charging_status chg_status;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	ret = rt5081a_get_charging_status(chg_data, &chg_status);

	return ret;
}

static int rt5081a_get_tchg(struct mtk_charger_info *mchr_info,
	void *data)
{
	int ret = 0, adc_temp = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	/* Get value from ADC */
	ret = rt5081a_get_adc(chg_data, RT5081A_ADC_TEMP_JC, &adc_temp);
	if (ret < 0)
		return ret;

	((int *)data)[0] = adc_temp;
	((int *)data)[1] = adc_temp;

	dev_info(chg_data->dev, "%s: tchg = %d\n", __func__, adc_temp);

	return ret;
}

static int rt5081a_get_ibus(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0, adc_ibus = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	/* Get value from ADC */
	ret = rt5081a_get_adc(chg_data, RT5081A_ADC_IBUS, &adc_ibus);
	if (ret < 0)
		return ret;

	*((u32 *)data) = adc_ibus / 1000;

	dev_info(chg_data->dev, "%s: ibus = %dmA\n", __func__, adc_ibus / 1000);
	return ret;
}

static int rt5081a_get_vbus(struct mtk_charger_info *mchr_info, void *data)
{
	int ret = 0, adc_vbus = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	/* Get value from ADC */
	ret = rt5081a_get_adc(chg_data, RT5081A_ADC_VBUS_DIV2, &adc_vbus);
	if (ret < 0)
		return ret;

	*((u32 *)data) = adc_vbus /  1000;

	dev_info(chg_data->dev, "%s: vbus = %dmV\n", __func__, adc_vbus / 1000);
	return ret;
}

static int rt5081a_dump_register(struct mtk_charger_info *mchr_info,
	void *data)
{
	int i = 0, ret = 0;
	u32 ichg = 0, aicr = 0, mivr = 0, ieoc = 0, cv = 0;
	bool chg_en = 0;
	int adc_vsys = 0, adc_vbat = 0, adc_ibat = 0, adc_ibus = 0, adc_vbus = 0;
	enum rt5081a_charging_status chg_status = RT5081A_CHG_STATUS_READY;
	u8 chg_stat = 0, chg_ctrl[2] = {0};
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;

	ret = rt5081a_get_ichg(mchr_info, &ichg); /* 10uA */
	ret = rt5081a_get_aicr(mchr_info, &aicr); /* 10uA */
	ret = rt5081a_get_charging_status(chg_data, &chg_status);
	ret = rt5081a_get_ieoc(chg_data, &ieoc);
	ret = rt5081a_get_mivr(chg_data, &mivr);
	ret = rt5081a_get_cv(mchr_info, &cv);
	ret = rt5081a_is_charging_enable(chg_data, &chg_en);
	ret = rt5081a_get_adc(chg_data, RT5081A_ADC_VSYS, &adc_vsys);
	ret = rt5081a_get_adc(chg_data, RT5081A_ADC_VBAT, &adc_vbat);
	ret = rt5081a_get_adc(chg_data, RT5081A_ADC_IBAT, &adc_ibat);
	ret = rt5081a_get_adc(chg_data, RT5081A_ADC_IBUS, &adc_ibus);
	ret = rt5081a_get_adc(chg_data, RT5081A_ADC_VBUS_DIV5, &adc_vbus);

	chg_stat = rt5081a_pmu_reg_read(chg_data->chip, RT5081A_PMU_REG_CHGSTAT1);
	ret = rt5081a_pmu_reg_block_read(chg_data->chip, RT5081A_PMU_REG_CHGCTRL1,
		2, chg_ctrl);

	if (chg_status == RT5081A_CHG_STATUS_FAULT) {
		for (i = 0; i < ARRAY_SIZE(rt5081a_chg_reg_addr); i++) {
			ret = rt5081a_pmu_reg_read(chg_data->chip,
				rt5081a_chg_reg_addr[i]);
			if (ret < 0)
				return ret;

			dev_dbg(chg_data->dev, "%s: reg[0x%02X] = 0x%02X\n",
				__func__, rt5081a_chg_reg_addr[i], ret);
		}
	}

	dev_info(chg_data->dev,
		"%s: ICHG = %dmA, AICR = %dmA, MIVR = %dmV, IEOC = %dmA, CV = %dmV\n",
		__func__, ichg / 100, aicr / 100, mivr / 1000, ieoc / 1000, cv / 1000);

	dev_info(chg_data->dev,
		"%s: VSYS = %dmV, VBAT = %dmV, IBAT = %dmA, IBUS = %dmA, VBUS = %dmV\n",
		__func__, adc_vsys / 1000, adc_vbat / 1000, adc_ibat / 1000,
		adc_ibus / 1000, adc_vbus / 1000);

	dev_info(chg_data->dev, "%s: CHG_EN = %d, CHG_STATUS = %s, CHG_STAT = 0x%02X\n",
		__func__, chg_en, rt5081a_chg_status_name[chg_status], chg_stat);

	dev_info(chg_data->dev, "%s: CHG_CTRL1 = 0x%02X, CHG_CTRL2 = 0x%02X\n",
		__func__, chg_ctrl[0], chg_ctrl[1]);

	ret = 0;
	return ret;
}

#if defined(CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT) && defined(CONFIG_TCPC_CLASS)
static int rt5081a_enable_chg_type_det(struct mtk_charger_info *mchr_info,
	void *data)
{
	int ret = 0;

	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)mchr_info;
	bool en = *((bool *)data);

	dev_info(chg_data->dev, "%s: en = %d\n", __func__, en);

	atomic_set(&chg_data->tcpc_usb_connected, en);

	/* TypeC detach */
	if (!en) {
		ret = rt5081a_chgdet_handler(chg_data);
		return ret;
	}

	/* TypeC attach */
	ret = rt5081a_enable_chgdet_flow(chg_data, true);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: en bc12 fail(%d)\n", __func__, ret);

	return ret;
}
#endif /* CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT && CONFIG_TCPC_CLASS */

#ifdef RT5081A_APPLE_SAMSUNG_TA_SUPPORT
static int rt5081a_detect_apple_samsung_ta(
	struct rt5081a_pmu_charger_data *chg_data)
{
	int ret = 0;
	bool dcd_timeout = false;
	bool dp_0_9v = false, dp_1_5v = false, dp_2_3v = false, dm_2_3v = false;

	/* Only SDP/CDP/DCP could possibly be Apple/Samsung TA */
	if (chg_data->chg_type != STANDARD_HOST &&
	    chg_data->chg_type != CHARGING_HOST &&
	    chg_data->chg_type != STANDARD_CHARGER)
		return -EINVAL;

	if (chg_data->chg_type == STANDARD_HOST ||
	    chg_data->chg_type == CHARGING_HOST) {
		ret = rt5081a_pmu_reg_test_bit(chg_data->chip,
			RT5081A_PMU_REG_QCSTAT, RT5081A_SHIFT_DCDTI_STAT,
			&dcd_timeout);
		if (ret < 0) {
			dev_err(chg_data->dev, "%s: read dcd timeout failed\n",
				__func__);
			return ret;
		}

		if (!dcd_timeout) {
			dev_info(chg_data->dev, "%s: dcd is not timeout\n",
				__func__);
			return 0;
		}
	}

	/* Check DP > 0.9V */
	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_QCSTATUS2,
		0x0F,
		0x03
	);

	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_QCSTATUS2,
		4, &dp_0_9v);
	if (ret < 0)
		return ret;

	if (!dp_0_9v) {
		dev_info(chg_data->dev, "%s: DP < 0.9V\n", __func__);
		return ret;
	}

	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_QCSTATUS2,
		5, &dp_1_5v);
	if (ret < 0)
		return ret;

	/* Samsung charger */
	if (!dp_1_5v) {
		dev_info(chg_data->dev, "%s: 0.9V < DP < 1.5V\n", __func__);
		chg_data->chg_type = SAMSUNG_CHARGER;
		return ret;
	}

	/* Check DP > 2.3 V */
	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_QCSTATUS2,
		0x0F,
		0x0B
	);
	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_QCSTATUS2,
		5, &dp_2_3v);
	if (ret < 0)
		return ret;

	/* Check DM > 2.3V */
	ret = rt5081a_pmu_reg_update_bits(
		chg_data->chip,
		RT5081A_PMU_REG_QCSTATUS2,
		0x0F,
		0x0F
	);
	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_QCSTATUS2,
		5, &dm_2_3v);
	if (ret < 0)
		return ret;

	/* Apple charger */
	if (!dp_2_3v && !dm_2_3v) {
		dev_info(chg_data->dev, "%s: 1.5V < DP < 2.3V && DM < 2.3V\n",
			__func__);
		chg_data->chg_type = APPLE_0_5A_CHARGER;
	} else if (!dp_2_3v && dm_2_3v) {
		dev_info(chg_data->dev, "%s: 1.5V < DP < 2.3V && 2.3V < DM\n",
			__func__);
		chg_data->chg_type = APPLE_1_0A_CHARGER;
	} else if (dp_2_3v && !dm_2_3v) {
		dev_info(chg_data->dev, "%s: 2.3V < DP && DM < 2.3V\n",
			__func__);
		chg_data->chg_type = APPLE_2_1A_CHARGER;
	} else {
		dev_info(chg_data->dev, "%s: 2.3V < DP && 2.3V < DM\n",
			__func__);
		chg_data->chg_type = APPLE_2_4A_CHARGER;
	}

	return 0;
}
#endif

static int rt5081a_toggle_cfo(struct rt5081a_pmu_charger_data *chg_data)
{
	int ret = 0;
	u8 data = 0;

	rt_mutex_lock(&chg_data->chip->io_lock);

	/* check if strobe mode */
	ret = i2c_smbus_read_i2c_block_data(chg_data->chip->i2c,
		RT5081A_PMU_REG_FLEDEN, 1, &data);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: check strobe fail\n", __func__);
		goto out;
	}
	if (data & RT5081A_STROBE_EN_MASK) {
		dev_err(chg_data->dev, "%s: in strobe mode\n", __func__);
		goto out;
	}

	/* read data */
	ret = i2c_smbus_read_i2c_block_data(chg_data->chip->i2c,
		RT5081A_PMU_REG_CHGCTRL2, 1, &data);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: read cfo fail\n", __func__);
		goto out;
	}

	/* cfo off */
	data &= ~RT5081A_MASK_CFO_EN;
	ret = i2c_smbus_write_i2c_block_data(chg_data->chip->i2c,
		RT5081A_PMU_REG_CHGCTRL2, 1, &data);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: cfo off fail\n", __func__);
		goto out;
	}

	/* cfo on */
	data |= RT5081A_MASK_CFO_EN;
	ret = i2c_smbus_write_i2c_block_data(chg_data->chip->i2c,
		RT5081A_PMU_REG_CHGCTRL2, 1, &data);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: cfo on fail\n", __func__);

out:
	rt_mutex_unlock(&chg_data->chip->io_lock);
	return ret;
}


static irqreturn_t rt5081a_pmu_chg_treg_irq_handler(int irq, void *data)
{
	int ret = 0;
	bool treg_stat = false;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);

	/* Read treg status */
	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_CHGSTAT1,
		RT5081A_SHIFT_CHG_TREG, &treg_stat);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: read treg stat failed\n", __func__);
	else
		dev_err(chg_data->dev, "%s: treg stat = %d\n", __func__,
			treg_stat);

	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_aicr_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_mivr_irq_handler(int irq, void *data)
{
	int ret = 0, ibus = 0;
	bool mivr_stat = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_info(chg_data->dev, "%s\n", __func__);
	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_CHGSTAT1,
		RT5081A_SHIFT_MIVR_STAT, &mivr_stat);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: read mivr stat failed\n", __func__);
		goto out;
	}

	if (!mivr_stat) {
		dev_info(chg_data->dev, "%s: mivr stat not act\n", __func__);
		goto out;
	}

	ret = rt5081a_get_adc(chg_data, RT5081A_ADC_IBUS, &ibus);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: get ibus fail\n", __func__);
		goto out;
	}

	if (ibus < 100000) { /* 100mA */
		ret = rt5081a_toggle_cfo(chg_data);
		if (ret < 0)
			dev_err(chg_data->dev, "%s: toggle cfo fail\n",
				__func__);
		goto out;
	}

out:
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_pwr_rdy_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_vinovp_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_vsysuv_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_vsysov_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_vbatov_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_vbusov_irq_handler(int irq, void *data)
{
	int ret = 0;
	bool vbusov_stat = false;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_CHGSTAT2,
		RT5081A_SHIFT_CHG_VBUSOV_STAT, &vbusov_stat);
	if (ret < 0)
		return IRQ_HANDLED;
	dev_info(chg_data->dev, "%s: stat = %d\n", __func__, vbusov_stat);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_ts_bat_cold_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_ts_bat_cool_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_ts_bat_warm_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_ts_bat_hot_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_tmri_irq_handler(int irq, void *data)
{
	int ret = 0;
	bool tmr_stat = false;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_CHGSTAT4,
		RT5081A_SHIFT_CHG_TMRI_STAT, &tmr_stat);
	if (ret < 0)
		return IRQ_HANDLED;

	dev_info(chg_data->dev, "%s: stat = %d\n", __func__, tmr_stat);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_batabsi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_adpbadi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_rvpi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_otpi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_aiclmeasi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	rt5081a_chg_irq_set_flag(chg_data, &chg_data->irq_flag[RT5081A_CHG_IRQIDX_CHGIRQ5],
		RT5081A_MASK_CHG_AICLMEASI);

	wake_up_interruptible(&chg_data->wait_queue);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_ichgmeasi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chgdet_donei_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_wdtmri_irq_handler(int irq, void *data)
{
	int ret = 0;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	ret = rt5081a_kick_wdt(&chg_data->mchr_info, NULL);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: kick wdt failed\n", __func__);

	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_ssfinishi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_info(chg_data->dev, "%s\n", __func__);

	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_rechgi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_termi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chg_ieoci_irq_handler(int irq, void *data)
{
	int ret = 0;
	bool ieoc_stat = false;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	ret = rt5081a_pmu_reg_test_bit(chg_data->chip, RT5081A_PMU_REG_CHGSTAT5,
		RT5081A_SHIFT_CHG_IEOCI_STAT, &ieoc_stat);
	if (ret < 0)
		return IRQ_HANDLED;

	dev_info(chg_data->dev, "%s: stat = %d\n", __func__, ieoc_stat);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_adc_donei_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_pumpx_donei_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_bst_batuvi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_bst_vbusovi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_bst_olpi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_attachi_irq_handler(int irq, void *data)
{
#ifdef CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_info(chg_data->dev, "%s\n", __func__);

	/* Check bc12 enable flag */
	mutex_lock(&chg_data->bc12_access_lock);
	if (!chg_data->bc12_en) {
		dev_err(chg_data->dev, "%s: bc12 disabled, ignore irq\n",
			__func__);
		mutex_unlock(&chg_data->bc12_access_lock);
		return IRQ_HANDLED;
	}
	mutex_unlock(&chg_data->bc12_access_lock);
	rt5081a_chgdet_handler(chg_data);
#endif /* CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT */

	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_detachi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_qc30stpdone_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_qc_vbusdet_done_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_hvdcp_det_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_chgdeti_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_dcdti_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_dirchg_vgoki_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_dirchg_wdtmri_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_dirchg_uci_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_dirchg_oci_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_dirchg_ovi_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_ovpctrl_swon_evt_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_ovpctrl_uvp_d_evt_irq_handler(int irq, void *data)
{
#if defined(CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT) && !defined(CONFIG_TCPC_CLASS)
	int ret = 0;
	bool uvp_d = false, otg_mode = false;
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_info(chg_data->dev, "%s\n", __func__);

	/* Check UVP_D_STAT & OTG mode */
	ret = rt5081a_pmu_reg_test_bit(chg_data->chip,
		RT5081A_PMU_REG_OVPCTRLSTAT, RT5081A_SHIFT_OVPCTRL_UVP_D_STAT,
		&uvp_d);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: read uvp_d_stat fail\n", __func__);
		goto out;
	}

	/* power good */
	if (!uvp_d) {
		ret = rt5081a_pmu_reg_test_bit(chg_data->chip,
			RT5081A_PMU_REG_CHGCTRL1, RT5081A_SHIFT_OPA_MODE,
			&otg_mode);
		if (ret < 0) {
			dev_err(chg_data->dev, "%s: read otg mode fail\n",
				__func__);
			goto out;
		}

		/* In OTG mode skip this event */
		if (otg_mode) {
			dev_info(chg_data->dev, "%s: triggered by OTG\n",
				__func__);
			goto out;
		}

		/* Turn on USB charger detection */
		ret = rt5081a_enable_chgdet_flow(chg_data, true);
		if (ret < 0)
			dev_err(chg_data->dev, "%s: en bc12 fail\n", __func__);

		goto out;
	}

	/* not power good */
	ret = rt5081a_chgdet_handler(chg_data);

out:
#endif /* CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT && !CONFIG_TCPC_CLASS */

	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_ovpctrl_uvp_evt_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_ovpctrl_ovp_d_evt_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static irqreturn_t rt5081a_pmu_ovpctrl_ovp_evt_irq_handler(int irq, void *data)
{
	struct rt5081a_pmu_charger_data *chg_data =
		(struct rt5081a_pmu_charger_data *)data;

	dev_err(chg_data->dev, "%s\n", __func__);
	return IRQ_HANDLED;
}

static struct rt5081a_pmu_irq_desc rt5081a_chg_irq_desc[] = {
	RT5081A_PMU_IRQDESC(chg_treg),
	RT5081A_PMU_IRQDESC(chg_aicr),
	RT5081A_PMU_IRQDESC(chg_mivr),
	RT5081A_PMU_IRQDESC(pwr_rdy),
	RT5081A_PMU_IRQDESC(chg_vinovp),
	RT5081A_PMU_IRQDESC(chg_vsysuv),
	RT5081A_PMU_IRQDESC(chg_vsysov),
	RT5081A_PMU_IRQDESC(chg_vbatov),
	RT5081A_PMU_IRQDESC(chg_vbusov),
	RT5081A_PMU_IRQDESC(ts_bat_cold),
	RT5081A_PMU_IRQDESC(ts_bat_cool),
	RT5081A_PMU_IRQDESC(ts_bat_warm),
	RT5081A_PMU_IRQDESC(ts_bat_hot),
	RT5081A_PMU_IRQDESC(chg_tmri),
	RT5081A_PMU_IRQDESC(chg_batabsi),
	RT5081A_PMU_IRQDESC(chg_adpbadi),
	RT5081A_PMU_IRQDESC(chg_rvpi),
	RT5081A_PMU_IRQDESC(otpi),
	RT5081A_PMU_IRQDESC(chg_aiclmeasi),
	RT5081A_PMU_IRQDESC(chg_ichgmeasi),
	RT5081A_PMU_IRQDESC(chgdet_donei),
	RT5081A_PMU_IRQDESC(chg_wdtmri),
	RT5081A_PMU_IRQDESC(ssfinishi),
	RT5081A_PMU_IRQDESC(chg_rechgi),
	RT5081A_PMU_IRQDESC(chg_termi),
	RT5081A_PMU_IRQDESC(chg_ieoci),
	RT5081A_PMU_IRQDESC(adc_donei),
	RT5081A_PMU_IRQDESC(pumpx_donei),
	RT5081A_PMU_IRQDESC(bst_batuvi),
	RT5081A_PMU_IRQDESC(bst_vbusovi),
	RT5081A_PMU_IRQDESC(bst_olpi),
	RT5081A_PMU_IRQDESC(attachi),
	RT5081A_PMU_IRQDESC(detachi),
	RT5081A_PMU_IRQDESC(qc30stpdone),
	RT5081A_PMU_IRQDESC(qc_vbusdet_done),
	RT5081A_PMU_IRQDESC(hvdcp_det),
	RT5081A_PMU_IRQDESC(chgdeti),
	RT5081A_PMU_IRQDESC(dcdti),
	RT5081A_PMU_IRQDESC(dirchg_vgoki),
	RT5081A_PMU_IRQDESC(dirchg_wdtmri),
	RT5081A_PMU_IRQDESC(dirchg_uci),
	RT5081A_PMU_IRQDESC(dirchg_oci),
	RT5081A_PMU_IRQDESC(dirchg_ovi),
	RT5081A_PMU_IRQDESC(ovpctrl_swon_evt),
	RT5081A_PMU_IRQDESC(ovpctrl_uvp_d_evt),
	RT5081A_PMU_IRQDESC(ovpctrl_uvp_evt),
	RT5081A_PMU_IRQDESC(ovpctrl_ovp_d_evt),
	RT5081A_PMU_IRQDESC(ovpctrl_ovp_evt),
};

static void rt5081a_pmu_charger_irq_register(struct platform_device *pdev)
{
	struct resource *res;
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(rt5081a_chg_irq_desc); i++) {
		if (!rt5081a_chg_irq_desc[i].name)
			continue;
		res = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
					rt5081a_chg_irq_desc[i].name);
		if (!res)
			continue;
		ret = devm_request_threaded_irq(&pdev->dev, res->start, NULL,
					rt5081a_chg_irq_desc[i].irq_handler,
					IRQF_TRIGGER_FALLING,
					rt5081a_chg_irq_desc[i].name,
					platform_get_drvdata(pdev));
		if (ret < 0) {
			dev_err(&pdev->dev, "request %s irq fail\n", res->name);
			continue;
		}
		rt5081a_chg_irq_desc[i].irq = res->start;
	}
}

static inline int rt_parse_dt(struct device *dev,
	struct rt5081a_pmu_charger_data *chg_data)
{
	struct rt5081a_pmu_charger_desc *chg_desc = NULL;
	struct device_node *np = dev->of_node;
	struct i2c_client *i2c = chg_data->chip->i2c;

	dev_info(chg_data->dev, "%s\n", __func__);
	chg_data->chg_desc = &rt5081a_default_chg_desc;

	chg_desc = devm_kzalloc(&i2c->dev,
		sizeof(struct rt5081a_pmu_charger_desc), GFP_KERNEL);
	if (!chg_desc)
		return -ENOMEM;

	memcpy(chg_desc, &rt5081a_default_chg_desc,
		sizeof(struct rt5081a_pmu_charger_desc));

	if (of_property_read_string(np, "charger_name",
		&(chg_data->mchr_info.name)) < 0) {
		dev_err(chg_data->dev, "%s: no charger name\n", __func__);
		chg_data->mchr_info.name = "primary_charger";
	}

	if (of_property_read_u32(np, "ichg", &chg_desc->ichg) < 0)
		dev_err(chg_data->dev, "%s: no ichg\n", __func__);

	if (of_property_read_u32(np, "aicr", &chg_desc->aicr) < 0)
		dev_err(chg_data->dev, "%s: no aicr\n", __func__);

	if (of_property_read_u32(np, "mivr", &chg_desc->mivr) < 0)
		dev_err(chg_data->dev, "%s: no mivr\n", __func__);

	if (of_property_read_u32(np, "cv", &chg_desc->cv) < 0)
		dev_err(chg_data->dev, "%s: no cv\n", __func__);

	if (of_property_read_u32(np, "ieoc", &chg_desc->ieoc) < 0)
		dev_err(chg_data->dev, "%s: no ieoc\n", __func__);

	if (of_property_read_u32(np, "safety_timer",
		&chg_desc->safety_timer) < 0)
		dev_err(chg_data->dev, "%s: no safety timer\n", __func__);

	if (of_property_read_u32(np, "dc_wdt", &chg_desc->dc_wdt) < 0)
		dev_err(chg_data->dev, "%s: no dc wdt\n", __func__);

	if (of_property_read_u32(np, "ircmp_resistor",
		&chg_desc->ircmp_resistor) < 0)
		dev_err(chg_data->dev, "%s: no ircmp resistor\n", __func__);

	if (of_property_read_u32(np, "ircmp_vclamp",
		&chg_desc->ircmp_vclamp) < 0)
		dev_err(chg_data->dev, "%s: no ircmp vclamp\n", __func__);

	chg_desc->en_te = of_property_read_bool(np, "enable_te");
	chg_desc->en_wdt = of_property_read_bool(np, "enable_wdt");

	chg_data->chg_desc = chg_desc;

	return 0;
}

static int rt5081a_chg_init_setting(struct rt5081a_pmu_charger_data *chg_data)
{
	int ret = 0;
	struct rt5081a_pmu_charger_desc *chg_desc = chg_data->chg_desc;

	dev_info(chg_data->dev, "%s\n", __func__);

	/* Select IINLMTSEL to use AICR */
	ret = rt5081a_select_input_current_limit(chg_data,
		RT5081A_IINLMTSEL_AICR);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: select iinlmtsel failed\n",
			__func__);

	mdelay(5);

	/* Disable hardware ILIM */
	ret = rt5081a_enable_ilim(chg_data, false);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: disable ilim failed\n", __func__);

	ret = __rt5081a_set_ichg(chg_data, chg_desc->ichg);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: set ichg failed\n", __func__);

	ret = __rt5081a_set_aicr(chg_data, chg_desc->aicr);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: set aicr failed\n", __func__);

	ret = __rt5081a_set_mivr(chg_data, chg_desc->mivr);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: set mivr failed\n", __func__);

	ret = __rt5081a_set_cv(chg_data, chg_desc->cv);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: set voreg failed\n", __func__);

	ret = __rt5081a_set_ieoc(chg_data, chg_desc->ieoc);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: set ieoc failed\n", __func__);

	ret = __rt5081a_enable_te(chg_data, chg_desc->en_te);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: set te failed\n", __func__);

	ret = rt5081a_set_fast_charge_timer(chg_data, chg_desc->safety_timer);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: set fast timer failed\n", __func__);

	ret = rt5081a_set_dc_wdt(chg_data, chg_desc->dc_wdt);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: set dc watch dog timer failed\n",
			__func__);

	ret = __rt5081a_enable_safety_timer(chg_data, true);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: enable charger timer failed\n",
			__func__);

	/* Initially disable WDT to prevent 1mA power consumption */
	ret = rt5081a_enable_wdt(chg_data, false);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: disable watchdog timer failed\n",
			__func__);

	/* Disable JEITA */
	ret = rt5081a_enable_jeita(chg_data, false);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: disable jeita failed\n", __func__);

	/* Disable HZ */
	ret = __rt5081a_enable_hz(chg_data, false);
	if (ret < 0)
		dev_err(chg_data->dev, "%s: disable hz failed\n", __func__);

	ret = __rt5081a_set_ircmp_resistor(chg_data, chg_desc->ircmp_resistor);
	if (ret < 0)
		dev_err(chg_data->dev,
			"%s: set IR compensation resistor failed\n", __func__);

	ret = __rt5081a_set_ircmp_vclamp(chg_data, chg_desc->ircmp_vclamp);
	if (ret < 0)
		dev_err(chg_data->dev,
			"%s: set IR compensation vclamp failed\n", __func__);

	/* Disable USB charger type detection first, no matter use it or not */
	ret = rt5081a_enable_chgdet_flow(chg_data, false);
	if (ret < 0)
		dev_err(chg_data->dev,
			"%s: disable usb chrdet failed\n", __func__);

	return ret;
}


static const mtk_charger_intf rt5081a_mchr_intf[CHARGING_CMD_NUMBER] = {
	[CHARGING_CMD_INIT] = rt5081a_hw_init,
	[CHARGING_CMD_DUMP_REGISTER] = rt5081a_dump_register,
	[CHARGING_CMD_ENABLE] = rt5081a_enable_charging,
	[CHARGING_CMD_SET_HIZ_SWCHR] = rt5081a_enable_hz,
	[CHARGING_CMD_ENABLE_SAFETY_TIMER] = rt5081a_enable_safety_timer,
	[CHARGING_CMD_ENABLE_OTG] = rt5081a_enable_otg,
	[CHARGING_CMD_ENABLE_POWER_PATH] = rt5081a_enable_power_path,
	[CHARGING_CMD_ENABLE_DIRECT_CHARGE] = rt5081a_enable_direct_charge,
	[CHARGING_CMD_SET_CURRENT] = rt5081a_set_ichg,
	[CHARGING_CMD_SET_INPUT_CURRENT] = rt5081a_set_aicr,
	[CHARGING_CMD_SET_VINDPM] = rt5081a_set_mivr,
	[CHARGING_CMD_SET_CV_VOLTAGE] = rt5081a_set_cv,
	[CHARGING_CMD_SET_BOOST_CURRENT_LIMIT] = rt5081a_set_otg_current_limit,
	[CHARGING_CMD_SET_TA_CURRENT_PATTERN] = rt5081a_set_pep_current_pattern,
	[CHARGING_CMD_SET_TA20_RESET] = rt5081a_set_pep20_reset,
	[CHARGING_CMD_SET_TA20_CURRENT_PATTERN] = rt5081a_set_pep20_current_pattern,
	[CHARGING_CMD_SET_ERROR_STATE] = rt5081a_set_error_state,
	[CHARGING_CMD_GET_CURRENT] = rt5081a_get_ichg,
	[CHARGING_CMD_GET_INPUT_CURRENT] = rt5081a_get_aicr,
	[CHARGING_CMD_GET_CHARGER_TEMPERATURE] = rt5081a_get_tchg,
	[CHARGING_CMD_GET_CHARGING_STATUS] = rt5081a_is_charging_done,
	[CHARGING_CMD_GET_IS_POWER_PATH_ENABLE] = rt5081a_is_power_path_enable,
	[CHARGING_CMD_GET_IS_SAFETY_TIMER_ENABLE] = rt5081a_is_safety_timer_enable,
	[CHARGING_CMD_RESET_WATCH_DOG_TIMER] = rt5081a_kick_wdt,
	[CHARGING_CMD_GET_IBUS] = rt5081a_get_ibus,
	[CHARGING_CMD_GET_VBUS] = rt5081a_get_vbus,
	[CHARGING_CMD_RUN_AICL] = rt5081a_run_aicl,
	[CHARGING_CMD_RESET_DC_WATCH_DOG_TIMER] = rt5081a_kick_dc_wdt,
	[CHARGING_CMD_ENABLE_DC_VBUSOV] = rt5081a_enable_dc_vbusov,
	[CHARGING_CMD_SET_DC_VBUSOV] = rt5081a_set_dc_vbusov,
	[CHARGING_CMD_ENABLE_DC_VBUSOC] = rt5081a_enable_dc_ibusoc,
	[CHARGING_CMD_SET_DC_VBUSOC] = rt5081a_set_dc_ibusoc,
	[CHARGING_CMD_ENABLE_DC_VBATOV] = rt5081a_enable_dc_vbatov,
	[CHARGING_CMD_SET_DC_VBATOV] = rt5081a_set_dc_vbatov,
	[CHARGING_CMD_GET_IS_DC_ENABLE] = rt5081a_is_dc_enable,
	[CHARGING_CMD_SET_IRCMP_RESISTOR] = rt5081a_set_ircmp_resistor,
	[CHARGING_CMD_SET_IRCMP_VOLT_CLAMP] = rt5081a_set_ircmp_vclamp,
	[CHARGING_CMD_SET_PEP20_EFFICIENCY_TABLE] = rt5081a_set_pep20_efficiency_table,
#if defined(CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT)
	[CHARGING_CMD_GET_CHARGER_TYPE] = rt5081a_get_charger_type,
	[CHARGING_CMD_ENABLE_CHR_TYPE_DET] = rt5081a_enable_chg_type_det,
#else
	[CHARGING_CMD_GET_CHARGER_TYPE] = mtk_charger_get_charger_type,
#endif /* CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT */

	/*
	 * The following interfaces are not related to charger
	 * Define in mtk_charger_intf.c
	 */
	[CHARGING_CMD_SW_INIT] = mtk_charger_sw_init,
	[CHARGING_CMD_SET_HV_THRESHOLD] = mtk_charger_set_hv_threshold,
	[CHARGING_CMD_GET_HV_STATUS] = mtk_charger_get_hv_status,
	[CHARGING_CMD_GET_BATTERY_STATUS] = mtk_charger_get_battery_status,
	[CHARGING_CMD_GET_CHARGER_DET_STATUS] = mtk_charger_get_charger_det_status,
	[CHARGING_CMD_GET_IS_PCM_TIMER_TRIGGER] = mtk_charger_get_is_pcm_timer_trigger,
	[CHARGING_CMD_SET_PLATFORM_RESET] = mtk_charger_set_platform_reset,
	[CHARGING_CMD_GET_PLATFORM_BOOT_MODE] = mtk_charger_get_platform_boot_mode,
	[CHARGING_CMD_SET_POWER_OFF] = mtk_charger_set_power_off,
	[CHARGING_CMD_GET_POWER_SOURCE] = mtk_charger_get_power_source,
	[CHARGING_CMD_GET_CSDAC_FALL_FLAG] = mtk_charger_get_csdac_full_flag,
	[CHARGING_CMD_DISO_INIT] = mtk_charger_diso_init,
	[CHARGING_CMD_GET_DISO_STATE] = mtk_charger_get_diso_state,
	[CHARGING_CMD_SET_VBUS_OVP_EN] = mtk_charger_set_vbus_ovp_en,
	[CHARGING_CMD_GET_BIF_VBAT] = mtk_charger_get_bif_vbat,
	[CHARGING_CMD_SET_CHRIND_CK_PDN] = mtk_charger_set_chrind_ck_pdn,
	[CHARGING_CMD_GET_BIF_TBAT] = mtk_charger_get_bif_tbat,
	[CHARGING_CMD_SET_DP] = mtk_charger_set_dp,
	[CHARGING_CMD_GET_BIF_IS_EXIST] = mtk_charger_get_bif_is_exist,
};


static int rt5081a_pmu_charger_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct rt5081a_pmu_charger_data *chg_data;
	bool use_dt = pdev->dev.of_node;

	pr_info("%s: (%s)\n", __func__, RT5081A_PMU_CHARGER_DRV_VERSION);

	chg_data = devm_kzalloc(&pdev->dev, sizeof(*chg_data), GFP_KERNEL);
	if (!chg_data)
		return -ENOMEM;

	mutex_init(&chg_data->adc_access_lock);
	mutex_init(&chg_data->irq_access_lock);
	mutex_init(&chg_data->aicr_access_lock);
	mutex_init(&chg_data->ichg_access_lock);
	mutex_init(&chg_data->pe_access_lock);
	mutex_init(&chg_data->bc12_access_lock);
	mutex_init(&chg_data->hidden_mode_lock);
	mutex_init(&chg_data->chgdet_lock);
	chg_data->chip = dev_get_drvdata(pdev->dev.parent);
	chg_data->dev = &pdev->dev;
	chg_data->chg_type = CHARGER_UNKNOWN;
	chg_data->aicr_limit = -1;
	chg_data->adc_hang = false;
	chg_data->bc12_en = true;
	chg_data->hidden_mode_cnt = 0;
	chg_data->ieoc_wkard = false;
	chg_data->ieoc = 250000; /* register default value 250mA */
	chg_data->ichg = 2000000;
	atomic_set(&chg_data->bc12_cnt, 0);
	atomic_set(&chg_data->bc12_wkard, 0);
#ifdef CONFIG_TCPC_CLASS
	atomic_set(&chg_data->tcpc_usb_connected, 0);
#endif

	if (use_dt) {
		ret = rt_parse_dt(&pdev->dev, chg_data);
		if (ret < 0)
			dev_err(chg_data->dev, "%s: parse dts failed\n",
				__func__);
	}
	platform_set_drvdata(pdev, chg_data);

	/* Init wait queue head */
	init_waitqueue_head(&chg_data->wait_queue);

#if defined(CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT) && !defined(CONFIG_TCPC_CLASS)
	INIT_WORK(&chg_data->chgdet_work, rt5081a_chgdet_work_handler);
#endif /* CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT && !CONFIG_TCPC_CLASS */
	/* Do initial setting */
	ret = rt5081a_chg_init_setting(chg_data);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: sw init failed\n", __func__);
		goto err_chg_init_setting;
	}

	/* SW workaround */
	ret = rt5081a_chg_sw_workaround(chg_data);
	if (ret < 0) {
		dev_err(chg_data->dev, "%s: software workaround failed\n",
			__func__);
		goto err_chg_sw_workaround;
	}

	rt5081a_pmu_charger_irq_register(pdev);

	chg_data->mchr_info.mchr_intf = rt5081a_mchr_intf;
	mtk_charger_set_info(&chg_data->mchr_info);

	rt5081a_dump_register(&chg_data->mchr_info, NULL);

	/* Schedule work for microB's BC1.2 */
#if defined(CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT) && !defined(CONFIG_TCPC_CLASS)
	schedule_work(&chg_data->chgdet_work);
#endif /* CONFIG_RT5081A_PMU_CHARGER_TYPE_DETECT && !CONFIG_TCPC_CLASS */

	dev_info(&pdev->dev, "%s successfully\n", __func__);

	return ret;

err_chg_sw_workaround:
err_chg_init_setting:
	mutex_destroy(&chg_data->ichg_access_lock);
	mutex_destroy(&chg_data->adc_access_lock);
	mutex_destroy(&chg_data->irq_access_lock);
	mutex_destroy(&chg_data->aicr_access_lock);
	mutex_destroy(&chg_data->pe_access_lock);
	mutex_destroy(&chg_data->bc12_access_lock);
	mutex_destroy(&chg_data->hidden_mode_lock);
	mutex_destroy(&chg_data->chgdet_lock);
	return ret;
}

static int rt5081a_pmu_charger_remove(struct platform_device *pdev)
{
	struct rt5081a_pmu_charger_data *chg_data = platform_get_drvdata(pdev);

	if (chg_data) {
		mutex_destroy(&chg_data->ichg_access_lock);
		mutex_destroy(&chg_data->adc_access_lock);
		mutex_destroy(&chg_data->irq_access_lock);
		mutex_destroy(&chg_data->aicr_access_lock);
		mutex_destroy(&chg_data->pe_access_lock);
		mutex_destroy(&chg_data->bc12_access_lock);
		mutex_destroy(&chg_data->hidden_mode_lock);
		mutex_destroy(&chg_data->chgdet_lock);
		dev_info(chg_data->dev, "%s successfully\n", __func__);
	}

	return 0;
}

static const struct of_device_id rt_ofid_table[] = {
	{ .compatible = "richtek,rt5081a_pmu_charger", },
	{ },
};
MODULE_DEVICE_TABLE(of, rt_ofid_table);

static const struct platform_device_id rt_id_table[] = {
	{ "rt5081a_pmu_charger", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, rt_id_table);

static struct platform_driver rt5081a_pmu_charger = {
	.driver = {
		.name = "rt5081a_pmu_charger",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(rt_ofid_table),
	},
	.probe = rt5081a_pmu_charger_probe,
	.remove = rt5081a_pmu_charger_remove,
	.id_table = rt_id_table,
};
module_platform_driver(rt5081a_pmu_charger);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("cy_huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Richtek RT5081A PMU Charger");
MODULE_VERSION(RT5081A_PMU_CHARGER_DRV_VERSION);

