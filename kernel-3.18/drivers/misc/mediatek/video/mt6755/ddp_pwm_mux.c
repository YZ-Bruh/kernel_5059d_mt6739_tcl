#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include "ddp_clkmgr.h"
#include <ddp_pwm_mux.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include "ddp_reg.h"

#define PWM_MSG(fmt, arg...) pr_debug("[PWM] " fmt "\n", ##arg)
#define PWM_ERR(fmt, arg...) pr_err("[PWM] " fmt "\n", ##arg)
/* #define HARD_CODE_CONFIG */
static void __iomem *disp_pmw_mux_base;

#ifndef MUX_DISPPWM_ADDR /* disp pwm source clock select register address */
#define MUX_DISPPWM_ADDR (disp_pmw_mux_base + 0xB0)
#endif
#ifdef HARD_CODE_CONFIG
#ifndef MUX_UPDATE_ADDR /* disp pwm source clock update register address */
#define MUX_UPDATE_ADDR (disp_pmw_mux_base + 0x4)
#endif
#endif

/* clock hard code access API */
#define DRV_Reg32(addr) INREG32(addr)
#define clk_readl(addr) DRV_Reg32(addr)
#define clk_writel(addr, val) mt_reg_sync_writel(val, addr)

static eDDP_CLK_ID disp_pwm_get_clkid(unsigned int clk_req)
{
	eDDP_CLK_ID clkid = -1;

	switch (clk_req) {
	case 0:
		clkid = ULPOSC_D8; /* ULPOSC 26M */
		break;
	case 1:
		clkid = ULPOSC_D2; /* ULPOSC 104M */
		break;
	case 2:
		clkid = UNIVPLL2_D4; /* PLL 104M */
		break;
	default:
		clkid = -1;
		break;
	}

	return clkid;
}

#define DTSI_TOPCKGEN "mediatek,topckgen"
static int disp_pwm_get_muxbase(void)
{
	int ret = 0;
	struct device_node *node;

	if (disp_pmw_mux_base != NULL)
		return 0;

	node = of_find_compatible_node(NULL, NULL, DTSI_TOPCKGEN);
	if (!node) {
		PWM_ERR("DISP find TOPCKGEN node failed\n");
		return -1;
	}
	disp_pmw_mux_base = of_iomap(node, 0);
	if (!disp_pmw_mux_base) {
		PWM_ERR("DISP TOPCKGEN base failed\n");
		return -1;
	}

	return ret;
}

static unsigned int disp_pwm_get_pwmmux(void)
{
	unsigned int regsrc = 0;

	if (MUX_DISPPWM_ADDR != NULL)
		regsrc = clk_readl(MUX_DISPPWM_ADDR);
	else
		PWM_ERR("mux addr illegal");

	return regsrc;
}

int disp_pwm_set_pwmmux(unsigned int clk_req)
{
	unsigned int regsrc;
	int ret = 0;
	eDDP_CLK_ID clkid = -1;

	clkid = disp_pwm_get_clkid(clk_req);
	ret = disp_pwm_get_muxbase();
	regsrc = disp_pwm_get_pwmmux();

	if (clkid != -1) {
		ddp_clk_enable(MUX_PWM);
		ddp_clk_set_parent(MUX_PWM, clkid);
		ddp_clk_disable(MUX_PWM);
	}

	PWM_MSG("clk_req=%d clkid=%d, PWM_MUX %x->%x",
		clk_req, clkid, regsrc, disp_pwm_get_pwmmux());

	return 0;
}

static void __iomem *disp_pmw_osc_base;

#ifndef OSC_ULPOSC_ADDR /* rosc control register address */
#define OSC_ULPOSC_ADDR (disp_pmw_osc_base + 0x458)
#endif

static int get_ulposc_base(void)
{
	int ret = 0;
	struct device_node *node;

	if (disp_pmw_osc_base != NULL)
		return 0;

	node = of_find_compatible_node(NULL, NULL, "mediatek,sleep");
	if (!node) {
		PWM_ERR("DISP find SLEEP node failed\n");
		return -1;
	}
	disp_pmw_osc_base = of_iomap(node, 0);
	if (!disp_pmw_osc_base) {
		PWM_ERR("DISP find SLEEP base failed\n");
		return -1;
	}

	return ret;
}

static int get_ulposc_status(void)
{
	unsigned int regosc;
	int ret = -1;

	if (get_ulposc_base() == -1) {
		PWM_ERR("get ULPOSC status fail");
		return ret;
	}

	regosc = clk_readl(OSC_ULPOSC_ADDR);
	if ((regosc & 0x5) != 0x5) {
		PWM_MSG("ULPOSC is off (%x)", regosc);
		ret = 0;
	} else {
		PWM_MSG("ULPOSC is on (%x)", regosc);
		ret = 1;
	}

	return ret;
}

static int ulposc_enable(eDDP_CLK_ID clkid)
{
	int ret = 0;

	ret = ddp_clk_prepare_enable(clkid);
	get_ulposc_status();

	return ret;
}

static int ulposc_disable(eDDP_CLK_ID clkid)
{
	int ret = 0;

	ret = ddp_clk_disable_unprepare(clkid);
	get_ulposc_status();

	return ret;
}

int disp_pwm_clksource_enable(int clk_req)
{
	int ret = 0;
	eDDP_CLK_ID clkid = -1;

	clkid = disp_pwm_get_clkid(clk_req);

	switch (clkid) {
	case ULPOSC_D2:
	case ULPOSC_D8:
		ulposc_enable(clkid);
		break;
	default:
		break;
	}

	return ret;
}

int disp_pwm_clksource_disable(int clk_req)
{
	int ret = 0;
	eDDP_CLK_ID clkid = -1;

	clkid = disp_pwm_get_clkid(clk_req);

	switch (clkid) {
	case ULPOSC_D2:
	case ULPOSC_D8:
		ulposc_disable(clkid);
		break;
	default:
		break;
	}

	return ret;
}

