

#ifndef _MT6589_THERMAL_H
#define _MT6589_THERMAL_H

#include <linux/module.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include "mt-plat/sync_write.h"
#include "mtk_thermal_typedefs.h"
#include "mt-plat/mtk_mdm_monitor.h"
#include "mt_gpufreq.h"
/* #include "mach/mt6575_auxadc_hw.h" */

#if !defined(CONFIG_MTK_LEGACY)
#include <linux/clk.h>
#endif				/* !defined(CONFIG_MTK_LEGACY) */

#if 1
extern void __iomem *thermal_base;
extern void __iomem *auxadc_base;
extern void __iomem *infracfg_ao_base;
extern void __iomem *apmixed_base;
extern void __iomem *INFRACFG_AO_base;

#define THERM_CTRL_BASE_2    thermal_base
#define AUXADC_BASE_2        auxadc_ts_base
#define INFRACFG_AO_BASE_2   infracfg_ao_base
#define APMIXED_BASE_2       apmixed_base
#else
#include <mach/mt_reg_base.h>
#define AUXADC_BASE_2     AUXADC_BASE
#define THERM_CTRL_BASE_2 THERM_CTRL_BASE
#define PERICFG_BASE_2    PERICFG_BASE
#define APMIXED_BASE_2    APMIXED_BASE
#endif

#define MT6752_EVB_BUILD_PASS	/* Jerry fix build error FIX_ME */

/* K2 AUXADC_BASE: 0xF1001000 from Vincent Liang 2014.5.8 */

#define AUXADC_CON0_V       (AUXADC_BASE_2 + 0x000)	/* yes, 0x11003000 */
#define AUXADC_CON1_V       (AUXADC_BASE_2 + 0x004)
#define AUXADC_CON1_SET_V   (AUXADC_BASE_2 + 0x008)
#define AUXADC_CON1_CLR_V   (AUXADC_BASE_2 + 0x00C)
#define AUXADC_CON2_V       (AUXADC_BASE_2 + 0x010)
/* #define AUXADC_CON3_V       (AUXADC_BASE_2 + 0x014) */
#define AUXADC_DAT0_V       (AUXADC_BASE_2 + 0x014)
#define AUXADC_DAT1_V       (AUXADC_BASE_2 + 0x018)
#define AUXADC_DAT2_V       (AUXADC_BASE_2 + 0x01C)
#define AUXADC_DAT3_V       (AUXADC_BASE_2 + 0x020)
#define AUXADC_DAT4_V       (AUXADC_BASE_2 + 0x024)
#define AUXADC_DAT5_V       (AUXADC_BASE_2 + 0x028)
#define AUXADC_DAT6_V       (AUXADC_BASE_2 + 0x02C)
#define AUXADC_DAT7_V       (AUXADC_BASE_2 + 0x030)
#define AUXADC_DAT8_V       (AUXADC_BASE_2 + 0x034)
#define AUXADC_DAT9_V       (AUXADC_BASE_2 + 0x038)
#define AUXADC_DAT10_V       (AUXADC_BASE_2 + 0x03C)
#define AUXADC_DAT11_V       (AUXADC_BASE_2 + 0x040)
#define AUXADC_MISC_V       (AUXADC_BASE_2 + 0x094)

#define AUXADC_CON0_P       (auxadc_ts_phy_base + 0x000)
#define AUXADC_CON1_P       (auxadc_ts_phy_base + 0x004)
#define AUXADC_CON1_SET_P   (auxadc_ts_phy_base + 0x008)
#define AUXADC_CON1_CLR_P   (auxadc_ts_phy_base + 0x00C)
#define AUXADC_CON2_P       (auxadc_ts_phy_base + 0x010)
/* #define AUXADC_CON3_P       (auxadc_ts_phy_base + 0x014) */
#define AUXADC_DAT0_P       (auxadc_ts_phy_base + 0x014)
#define AUXADC_DAT1_P       (auxadc_ts_phy_base + 0x018)
#define AUXADC_DAT2_P       (auxadc_ts_phy_base + 0x01C)
#define AUXADC_DAT3_P       (auxadc_ts_phy_base + 0x020)
#define AUXADC_DAT4_P       (auxadc_ts_phy_base + 0x024)
#define AUXADC_DAT5_P       (auxadc_ts_phy_base + 0x028)
#define AUXADC_DAT6_P       (auxadc_ts_phy_base + 0x02C)
#define AUXADC_DAT7_P       (auxadc_ts_phy_base + 0x030)
#define AUXADC_DAT8_P       (auxadc_ts_phy_base + 0x034)
#define AUXADC_DAT9_P       (auxadc_ts_phy_base + 0x038)
#define AUXADC_DAT10_P       (auxadc_ts_phy_base + 0x03C)
#define AUXADC_DAT11_P       (auxadc_ts_phy_base + 0x040)

#define AUXADC_MISC_P       (auxadc_ts_phy_base + 0x094)


/* APB Module infracfg_ao */
/* #define INFRACFG_AO_BASE (0xF0001000) */
#define INFRA_GLOBALCON_RST_0_SET (INFRACFG_AO_BASE_2 + 0x120)	/* yes, 0x10001000 */
#define INFRA_GLOBALCON_RST_0_CLR (INFRACFG_AO_BASE_2 + 0x124)	/* yes, 0x10001000 */
#define INFRA_GLOBALCON_RST_0_STA (INFRACFG_AO_BASE_2 + 0x128)	/* yes, 0x10001000 */
/* K2 APMIXED_BASE: 0x1000C000 from KJ 2014.5.8 // TODO: FIXME */
#define TS_CON0_TM             (APMIXED_BASE_2 + 0x600)	/* yes 0x10209000 */
#define TS_CON1_TM             (APMIXED_BASE_2 + 0x604)
#define TS_CON0_P           (apmixed_phy_base + 0x600)
#define TS_CON1_P           (apmixed_phy_base + 0x604)
/* #define TS_CON0             (APMIXED_BASE_2 + 0x800) */
/* #define TS_CON1             (APMIXED_BASE_2 + 0x804) */
/* #define TS_CON2             (APMIXED_BASE_2 + 0x808) */
/* #define TS_CON3             (APMIXED_BASE_2 + 0x80C) */

#define TEMPMONCTL0         (THERM_CTRL_BASE_2 + 0x000)	/* yes 0xF100B000 */
#define TEMPMONCTL1         (THERM_CTRL_BASE_2 + 0x004)
#define TEMPMONCTL2         (THERM_CTRL_BASE_2 + 0x008)
#define TEMPMONINT          (THERM_CTRL_BASE_2 + 0x00C)
#define TEMPMONINTSTS       (THERM_CTRL_BASE_2 + 0x010)
#define TEMPMONIDET0        (THERM_CTRL_BASE_2 + 0x014)
#define TEMPMONIDET1        (THERM_CTRL_BASE_2 + 0x018)
#define TEMPMONIDET2        (THERM_CTRL_BASE_2 + 0x01C)
#define TEMPH2NTHRE         (THERM_CTRL_BASE_2 + 0x024)
#define TEMPHTHRE           (THERM_CTRL_BASE_2 + 0x028)
#define TEMPCTHRE           (THERM_CTRL_BASE_2 + 0x02C)
#define TEMPOFFSETH         (THERM_CTRL_BASE_2 + 0x030)
#define TEMPOFFSETL         (THERM_CTRL_BASE_2 + 0x034)
#define TEMPMSRCTL0         (THERM_CTRL_BASE_2 + 0x038)
#define TEMPMSRCTL1         (THERM_CTRL_BASE_2 + 0x03C)
#define TEMPAHBPOLL         (THERM_CTRL_BASE_2 + 0x040)
#define TEMPAHBTO           (THERM_CTRL_BASE_2 + 0x044)
#define TEMPADCPNP0         (THERM_CTRL_BASE_2 + 0x048)
#define TEMPADCPNP1         (THERM_CTRL_BASE_2 + 0x04C)
#define TEMPADCPNP2         (THERM_CTRL_BASE_2 + 0x050)
#define TEMPADCPNP3         (THERM_CTRL_BASE_2 + 0x0B4)

#define TEMPADCMUX          (THERM_CTRL_BASE_2 + 0x054)
#define TEMPADCEXT          (THERM_CTRL_BASE_2 + 0x058)
#define TEMPADCEXT1         (THERM_CTRL_BASE_2 + 0x05C)
#define TEMPADCEN           (THERM_CTRL_BASE_2 + 0x060)
#define TEMPPNPMUXADDR      (THERM_CTRL_BASE_2 + 0x064)
#define TEMPADCMUXADDR      (THERM_CTRL_BASE_2 + 0x068)
#define TEMPADCEXTADDR      (THERM_CTRL_BASE_2 + 0x06C)
#define TEMPADCEXT1ADDR     (THERM_CTRL_BASE_2 + 0x070)
#define TEMPADCENADDR       (THERM_CTRL_BASE_2 + 0x074)
#define TEMPADCVALIDADDR    (THERM_CTRL_BASE_2 + 0x078)
#define TEMPADCVOLTADDR     (THERM_CTRL_BASE_2 + 0x07C)
#define TEMPRDCTRL          (THERM_CTRL_BASE_2 + 0x080)
#define TEMPADCVALIDMASK    (THERM_CTRL_BASE_2 + 0x084)
#define TEMPADCVOLTAGESHIFT (THERM_CTRL_BASE_2 + 0x088)
#define TEMPADCWRITECTRL    (THERM_CTRL_BASE_2 + 0x08C)
#define TEMPMSR0            (THERM_CTRL_BASE_2 + 0x090)
#define TEMPMSR1            (THERM_CTRL_BASE_2 + 0x094)
#define TEMPMSR2            (THERM_CTRL_BASE_2 + 0x098)
#define TEMPMSR3            (THERM_CTRL_BASE_2 + 0x0B8)

#define TEMPIMMD0           (THERM_CTRL_BASE_2 + 0x0A0)
#define TEMPIMMD1           (THERM_CTRL_BASE_2 + 0x0A4)
#define TEMPIMMD2           (THERM_CTRL_BASE_2 + 0x0A8)
#define TEMPIMMD3           (THERM_CTRL_BASE_2 + 0x0BC)


#define TEMPPROTCTL         (THERM_CTRL_BASE_2 + 0x0C0)
#define TEMPPROTTA          (THERM_CTRL_BASE_2 + 0x0C4)
#define TEMPPROTTB          (THERM_CTRL_BASE_2 + 0x0C8)
#define TEMPPROTTC          (THERM_CTRL_BASE_2 + 0x0CC)

#define TEMPSPARE0          (THERM_CTRL_BASE_2 + 0x0F0)
#define TEMPSPARE1          (THERM_CTRL_BASE_2 + 0x0F4)
#define TEMPSPARE2          (THERM_CTRL_BASE_2 + 0x0F8)
#define TEMPSPARE3          (THERM_CTRL_BASE_2 + 0x0FC)

#define PTPCORESEL          (THERM_CTRL_BASE_2 + 0x400)
#define THERMINTST          (THERM_CTRL_BASE_2 + 0x404)
#define PTPODINTST          (THERM_CTRL_BASE_2 + 0x408)
#define THSTAGE0ST          (THERM_CTRL_BASE_2 + 0x40C)
#define THSTAGE1ST          (THERM_CTRL_BASE_2 + 0x410)
#define THSTAGE2ST          (THERM_CTRL_BASE_2 + 0x414)
#define THAHBST0            (THERM_CTRL_BASE_2 + 0x418)
#define THAHBST1            (THERM_CTRL_BASE_2 + 0x41C)	/* Only for DE debug */
#define PTPSPARE0           (THERM_CTRL_BASE_2 + 0x420)
#define PTPSPARE1           (THERM_CTRL_BASE_2 + 0x424)
#define PTPSPARE2           (THERM_CTRL_BASE_2 + 0x428)
#define PTPSPARE3           (THERM_CTRL_BASE_2 + 0x42C)
#define THSLPEVEB           (THERM_CTRL_BASE_2 + 0x430)


#define PTPSPARE0_P           (thermal_phy_base + 0x420)
#define PTPSPARE1_P           (thermal_phy_base + 0x424)
#define PTPSPARE2_P           (thermal_phy_base + 0x428)
#define PTPSPARE3_P           (thermal_phy_base + 0x42C)

#define THERMAL_ENABLE_SEN0     0x1
#define THERMAL_ENABLE_SEN1     0x2
#define THERMAL_ENABLE_SEN2     0x4
#define THERMAL_MONCTL0_MASK    0x00000007

#define THERMAL_PUNT_MASK       0x00000FFF
#define THERMAL_FSINTVL_MASK    0x03FF0000
#define THERMAL_SPINTVL_MASK    0x000003FF
#define THERMAL_MON_INT_MASK    0x0007FFFF

#define THERMAL_MON_CINTSTS0    0x000001
#define THERMAL_MON_HINTSTS0    0x000002
#define THERMAL_MON_LOINTSTS0   0x000004
#define THERMAL_MON_HOINTSTS0   0x000008
#define THERMAL_MON_NHINTSTS0   0x000010
#define THERMAL_MON_CINTSTS1    0x000020
#define THERMAL_MON_HINTSTS1    0x000040
#define THERMAL_MON_LOINTSTS1   0x000080
#define THERMAL_MON_HOINTSTS1   0x000100
#define THERMAL_MON_NHINTSTS1   0x000200
#define THERMAL_MON_CINTSTS2    0x000400
#define THERMAL_MON_HINTSTS2    0x000800
#define THERMAL_MON_LOINTSTS2   0x001000
#define THERMAL_MON_HOINTSTS2   0x002000
#define THERMAL_MON_NHINTSTS2   0x004000
#define THERMAL_MON_TOINTSTS    0x008000
#define THERMAL_MON_IMMDINTSTS0 0x010000
#define THERMAL_MON_IMMDINTSTS1 0x020000
#define THERMAL_MON_IMMDINTSTS2 0x040000
#define THERMAL_MON_FILTINTSTS0 0x080000
#define THERMAL_MON_FILTINTSTS1 0x100000
#define THERMAL_MON_FILTINTSTS2 0x200000


#define THERMAL_tri_SPM_State0	0x20000000
#define THERMAL_tri_SPM_State1	0x40000000
#define THERMAL_tri_SPM_State2	0x80000000


#define THERMAL_MSRCTL0_MASK    0x00000007
#define THERMAL_MSRCTL1_MASK    0x00000038
#define THERMAL_MSRCTL2_MASK    0x000001C0






typedef enum {
	THERMAL_SENSOR1 = 0,	/*TS_MCU1 */
	THERMAL_SENSOR2 = 1,	/*TS_MCU2 */
	THERMAL_SENSOR3 = 2,	/*TS_MCU3 */
	THERMAL_SENSOR4 = 3,	/*TS_MCU4 */
	THERMAL_SENSOR_ABB = 4,	/*TS_ABB */
	THERMAL_SENSOR_NUM
} thermal_sensor_name;

typedef enum {
	THERMAL_BANK0 = 0,	/* CPU           (TSMCU2) */
	THERMAL_BANK1 = 1,	/* SOC + GPU (TSMCU1) */
	THERMAL_BANK2 = 2,	/* LTE       (TSMCU3) */
	THERMAL_BANK_NUM
} thermal_bank_name;


struct TS_PTPOD {
	unsigned int ts_MTS;
	unsigned int ts_BTS;
};

extern int mtktscpu_limited_dmips;

extern int tscpu_is_temp_valid(void); /* Valid if it returns 1, invalid if it returns 0*/
extern void get_thermal_slope_intercept(struct TS_PTPOD *ts_info, thermal_bank_name ts_bank);
extern void set_taklking_flag(bool flag);
extern int tscpu_get_cpu_temp(void);
extern int tscpu_get_temp_by_bank(thermal_bank_name ts_bank);

#define THERMAL_WRAP_WR32(val, addr)        mt_reg_sync_writel((val), ((void *)addr))


extern int get_immediate_cpu_wrap(void);
extern int get_immediate_gpu_wrap(void);
extern int get_immediate_soc_wrap(void);

/*add for DLPT*/
extern int tscpu_get_min_cpu_pwr(void);
extern int tscpu_get_min_gpu_pwr(void);

/*4 thermal sensors*/
typedef enum {
	MTK_THERMAL_SENSOR_TS1 = 0,
	MTK_THERMAL_SENSOR_TS2,
	MTK_THERMAL_SENSOR_TS3,
	MTK_THERMAL_SENSOR_TS4,
	MTK_THERMAL_SENSOR_TSABB,

	ATM_CPU_LIMIT,
	ATM_GPU_LIMIT,

	MTK_THERMAL_SENSOR_CPU_COUNT
} MTK_THERMAL_SENSOR_CPU_ID_MET;



extern int tscpu_get_cpu_temp_met(MTK_THERMAL_SENSOR_CPU_ID_MET id);


typedef void (*met_thermalsampler_funcMET) (void);
extern void mt_thermalsampler_registerCB(met_thermalsampler_funcMET pCB);

extern void tscpu_start_thermal(void);
extern void tscpu_stop_thermal(void);
extern void tscpu_cancel_thermal_timer(void);
extern void tscpu_start_thermal_timer(void);
extern void mtkts_btsmdpa_cancel_thermal_timer(void);
extern void mtkts_btsmdpa_start_thermal_timer(void);
extern void mtkts_bts_cancel_thermal_timer(void);
extern void mtkts_bts_start_thermal_timer(void);
extern void mtkts_pmic_cancel_thermal_timer(void);
extern void mtkts_pmic_start_thermal_timer(void);
extern void mtkts_battery_cancel_thermal_timer(void);
extern void mtkts_battery_start_thermal_timer(void);
extern void mtkts_pa_cancel_thermal_timer(void);
extern void mtkts_pa_start_thermal_timer(void);
extern void mtkts_wmt_cancel_thermal_timer(void);
extern void mtkts_wmt_start_thermal_timer(void);

extern void mtkts_allts_cancel_ts1_timer(void);
extern void mtkts_allts_start_ts1_timer(void);
extern void mtkts_allts_cancel_ts2_timer(void);
extern void mtkts_allts_start_ts2_timer(void);
extern void mtkts_allts_cancel_ts3_timer(void);
extern void mtkts_allts_start_ts3_timer(void);
extern void mtkts_allts_cancel_ts4_timer(void);
extern void mtkts_allts_start_ts4_timer(void);
extern void mtkts_allts_cancel_ts5_timer(void);
extern void mtkts_allts_start_ts5_timer(void);

extern int mtkts_bts_get_hw_temp(void);

extern int get_immediate_ts1_wrap(void);
extern int get_immediate_ts2_wrap(void);
extern int get_immediate_ts3_wrap(void);
extern int get_immediate_ts4_wrap(void);
extern int get_immediate_tsabb_wrap(void);

extern int mtkts_get_ts1_temp(void);
extern int mtkts_get_ts2_temp(void);
extern int mtkts_get_ts3_temp(void);
extern int mtkts_get_ts4_temp(void);
extern int mtkts_get_tsabb_temp(void);

extern int is_cpu_power_unlimit(void);	/* in mtk_ts_cpu.c */
extern int is_cpu_power_min(void);	/* in mtk_ts_cpu.c */
extern int get_cpu_target_tj(void);
extern int get_cpu_target_offset(void);

extern int mtk_mdm_get_md_info(struct md_info **p_inf, int *size);
extern int mtk_gpufreq_register(struct mt_gpufreq_power_table_info *freqs, int num);

extern int get_target_tj(void);

#endif
