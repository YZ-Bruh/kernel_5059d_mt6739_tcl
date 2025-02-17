
#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_config.h>
#include "mali_kbase_config_platform.h"

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>

/* mtk */
#include <platform/mtk_platform_common.h>
#include <mtk_mali_config.h>
#include <mt_gpufreq.h>

#include "mtk_common.h"
#include "mtk_mfg_reg.h"

#ifdef ENABLE_COMMON_DVFS
#include <ged_dvfs.h>
#endif

struct mtk_config *g_config;
volatile void *g_MFG_base;

static int _mtk_pm_callback_power_on(void)
{
	struct mtk_config *config = g_config;

	if (!config) {
		pr_alert("MALI: mtk_config is NULL\n");
		return -1;
	}

	/* Step1: turn gpu pmic power */
	mt_gpufreq_voltage_enable_set(1);

	/* Step2: turn on clocks by sequence
	 * MFG_ASYNC -> MFG -> CORE 0 -> CORE 1
	*/
	MTKCLK_prepare_enable(clk_mfg_async);
	MTKCLK_prepare_enable(clk_mfg);
	MTKCLK_prepare_enable(clk_mfg_core0);
	MTKCLK_prepare_enable(clk_mfg_core1);

	/* Step3: turn on CG */
	MTKCLK_prepare_enable(clk_mfg_main);

	mtk_set_vgpu_power_on_flag(MTK_VGPU_POWER_ON);

#ifdef ENABLE_COMMON_DVFS
	ged_dvfs_gpu_clock_switch_notify(1);
#endif

	return 1;
}

static void _mtk_pm_callback_power_off(void)
{
	int polling_retry = 100000;
    struct mtk_config *config = g_config;
#ifdef MTK_GPU_EARLY_PORTING
    int dvfs_cnt = 0;
#endif

#ifdef ENABLE_COMMON_DVFS
	ged_dvfs_gpu_clock_switch_notify(0);
#endif
	mtk_set_vgpu_power_on_flag(MTK_VGPU_POWER_OFF);

	/* polling mfg idle */
	MFG_write32(MFG_DEBUG_SEL, 0x3);
	while ((MFG_read32(MFG_DEBUG_A) & MFG_DEBUG_IDEL) != MFG_DEBUG_IDEL && --polling_retry) {
		udelay(1);
	}

	if (polling_retry <= 0) {
		pr_MTK_err("[dump] polling fail: idle rem:%d - MFG_DBUG_A=%x\n", polling_retry, MFG_read32(MFG_DEBUG_A));
	}

	/* Turn off clock by sequence */
	MTKCLK_disable_unprepare(clk_mfg_main);
	MTKCLK_disable_unprepare(clk_mfg_core1);
	MTKCLK_disable_unprepare(clk_mfg_core0);
	MTKCLK_disable_unprepare(clk_mfg);
	MTKCLK_disable_unprepare(clk_mfg_async);

#ifdef MTK_GPU_EARLY_PORTING
	dvfs_cnt = mt_gpufreq_get_dvfs_table_num();
	pr_MTK_info("[MALI][power off] set idx(%d) to driver instead of poweroff\n", (dvfs_cnt-1));
	mt_gpufreq_target(dvfs_cnt - 1);

#else
	mt_gpufreq_voltage_enable_set(0);
#endif

}

static void *_mtk_of_ioremap(const char *node_name)
{
	struct device_node *node;
	node = of_find_compatible_node(NULL, NULL, node_name);

	if (node)
		return of_iomap(node, 0);

	pr_MTK_err("cannot find [%s] of_node, please fix me\n", node_name);
	return NULL;
}

static int pm_callback_power_on(struct kbase_device *kbdev)
{
	_mtk_pm_callback_power_on();
	return 0;
}

static void pm_callback_power_off(struct kbase_device *kbdev)
{
	_mtk_pm_callback_power_off();
}

struct kbase_pm_callback_conf pm_callbacks = {
	.power_on_callback = pm_callback_power_on,
	.power_off_callback = pm_callback_power_off,
	.power_suspend_callback  = NULL,
	.power_resume_callback = NULL
};

static struct kbase_platform_config versatile_platform_config = {
#ifndef CONFIG_OF
	.io_resources = &io_resources
#endif
};

struct kbase_platform_config *kbase_get_platform_config(void)
{
	return &versatile_platform_config;
}


int kbase_platform_early_init(void)
{
	/* Nothing needed at this stage */
	return 0;
}

int mtk_platform_init(struct platform_device *pdev, struct kbase_device *kbdev)
{
	struct mtk_config *config;

	if (!pdev || !kbdev) {
		pr_alert("input parameter is NULL \n");
		return -1;
	}

	config = (struct mtk_config *)kbdev->mtk_config;
	if (!config) {
		pr_alert("[MALI] Alloc mtk_config\n");
		config = kmalloc(sizeof(struct mtk_config), GFP_KERNEL);
		if (NULL == config) {
			pr_alert("[MALI] Fail to alloc mtk_config\n");
			return -1;
		}
		g_config = kbdev->mtk_config = config;
	}

	config->mfg_register = g_MFG_base = _mtk_of_ioremap("mediatek,g3d_configmediatek,g3d_config");
	if (g_MFG_base == NULL) {
		pr_alert("[MALI] Fail to remap MGF register\n");
		return -1;
	}

	config->clk_mfg = devm_clk_get(&pdev->dev, "mtcmos-mfg");
	if (IS_ERR(config->clk_mfg)) {
		pr_alert("cannot get mtcmos mfg\n");
		return PTR_ERR(config->clk_mfg);
	}
	config->clk_mfg_async = devm_clk_get(&pdev->dev, "mtcmos-mfg-async");
	if (IS_ERR(config->clk_mfg_async)) {
		pr_alert("cannot get mtcmos mfg-async\n");
		return PTR_ERR(config->clk_mfg_async);
	}
	config->clk_mfg_core0 = devm_clk_get(&pdev->dev, "mtcmos-mfg-core0");
	if (IS_ERR(config->clk_mfg_core0)) {
		pr_alert("cannot get mtcmos-mfg-core0\n");
		return PTR_ERR(config->clk_mfg_core0);
	}
	config->clk_mfg_core1 = devm_clk_get(&pdev->dev, "mtcmos-mfg-core1");
	if (IS_ERR(config->clk_mfg_core1)) {
		pr_alert("cannot get mtcmos-mfg-core1\n");
		return PTR_ERR(config->clk_mfg_core1);
	}
	config->clk_mfg_main = devm_clk_get(&pdev->dev, "mfg-main");
	if (IS_ERR(config->clk_mfg_main)) {
		pr_alert("cannot get cg mfg-main\n");
		return PTR_ERR(config->clk_mfg_main);
	}

	dev_MTK_err(kbdev->dev, "xxxx clk_mfg:%p\n", config->clk_mfg);
	dev_MTK_err(kbdev->dev, "xxxx clk_mfg_async:%p\n", config->clk_mfg_async);
	dev_MTK_err(kbdev->dev, "xxxx clk_mfg_core0:%p\n", config->clk_mfg_core0);
	dev_MTK_err(kbdev->dev, "xxxx clk_mfg_core1:%p\n", config->clk_mfg_core1);
	dev_MTK_err(kbdev->dev, "xxxx clk_mfg_main:%p\n", config->clk_mfg_main);

	return 0;
}
