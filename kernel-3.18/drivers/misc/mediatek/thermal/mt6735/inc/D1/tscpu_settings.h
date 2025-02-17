
#include "mt_gpufreq.h"
#define MIN(_a_, _b_) ((_a_) > (_b_) ? (_b_) : (_a_))
#define MAX(_a_, _b_) ((_a_) > (_b_) ? (_a_) : (_b_))
#define _BIT_(_bit_)		(unsigned)(1 << (_bit_))
#define _BITMASK_(_bits_)	(((unsigned) -1 >> (31 - ((1) ? _bits_))) & ~((1U << ((0) ? _bits_)) - 1))

#define ENALBE_SW_FILTER		(1)
#define THERMAL_GET_AHB_BUS_CLOCK		    (0)
#define THERMAL_PERFORMANCE_PROFILE         (0)

/* 1: turn on GPIO toggle monitor; 0: turn off */
#define THERMAL_GPIO_OUT_TOGGLE             (0)

/* 1: turn on adaptive AP cooler; 0: turn off */
#define CPT_ADAPTIVE_AP_COOLER              (1)

/* 1: turn on supports to MET logging; 0: turn off */
#define CONFIG_SUPPORT_MET_MTKTSCPU         (0)

#define THERMAL_CONTROLLER_HW_FILTER        (1)	/* 1, 2, 4, 8, 16 */

/* 1: turn on thermal controller HW thermal protection; 0: turn off */
#define THERMAL_CONTROLLER_HW_TP            (1)

/* 1: turn on fast polling in this sw module; 0: turn off */
#define MTKTSCPU_FAST_POLLING               (1)

#if CPT_ADAPTIVE_AP_COOLER
#define MAX_CPT_ADAPTIVE_COOLERS            (3)

#define THERMAL_HEADROOM                    (0)
#define CONTINUOUS_TM                       (1)
#define DYNAMIC_GET_GPU_POWER			    (1)

/* 1: turn on precise power budgeting; 0: turn off */
#define PRECISE_HYBRID_POWER_BUDGET         (0)
#endif

/* 1: thermal driver fast polling, use hrtimer; 0: turn off */
/* #define THERMAL_DRV_FAST_POLL_HRTIMER          (1) */

/* 1: thermal driver update temp to MET directly, use hrtimer; 0: turn off */
#define THERMAL_DRV_UPDATE_TEMP_DIRECT_TO_MET  (1)
#define THERMAL_INIT_VALUE (0xDA1)
/* double check */
#define TS_CONFIGURE     TS_CON1_TM	/* depend on CPU design */
#define TS_CONFIGURE_P   TS_CON1_P	/* depend on CPU design */
#define TS_TURN_ON       0xFFFFFFCF	/* turn on TS_CON1[5:4] 2'b 00  11001111 -> 0xCF  ~(0x30) */
#define TS_TURN_OFF      0x00000030	/* turn off thermal */
/* chip dependent */
#define ADDRESS_INDEX_0  43	/* 0x102061A0 */
#define ADDRESS_INDEX_1	 42	/* 0x1020619C */
#define ADDRESS_INDEX_2	 44	/* 0x102061A4 */

#define CLEAR_TEMP 26111

/* TSCON1 bit table */
#define TSCON0_bit_6_7_00 0x00	/* TSCON0[7:6]=2'b00 */
#define TSCON0_bit_6_7_01 0x40	/* TSCON0[7:6]=2'b01 */
#define TSCON0_bit_6_7_10 0x80	/* TSCON0[7:6]=2'b10 */
#define TSCON0_bit_6_7_11 0xc0	/* TSCON0[7:6]=2'b11 */
#define TSCON0_bit_6_7_MASK 0xc0

#define TSCON1_bit_4_5_00 0x00	/* TSCON1[5:4]=2'b00 */
#define TSCON1_bit_4_5_01 0x10	/* TSCON1[5:4]=2'b01 */
#define TSCON1_bit_4_5_10 0x20	/* TSCON1[5:4]=2'b10 */
#define TSCON1_bit_4_5_11 0x30	/* TSCON1[5:4]=2'b11 */
#define TSCON1_bit_4_5_MASK 0x30

#define TSCON1_bit_0_2_000 0x00	/* TSCON1[2:0]=3'b000 */
#define TSCON1_bit_0_2_001 0x01	/* TSCON1[2:0]=3'b001 */
#define TSCON1_bit_0_2_010 0x02	/* TSCON1[2:0]=3'b010 */
#define TSCON1_bit_0_2_011 0x03	/* TSCON1[2:0]=3'b011 */
#define TSCON1_bit_0_2_100 0x04	/* TSCON1[2:0]=3'b100 */
#define TSCON1_bit_0_2_101 0x05	/* TSCON1[2:0]=3'b101 */
#define TSCON1_bit_0_2_110 0x06	/* TSCON1[2:0]=3'b110 */
#define TSCON1_bit_0_2_111 0x07	/* TSCON1[2:0]=3'b111 */
#define TSCON1_bit_0_2_MASK 0x07


/* ADC value to mcu */
/* chip dependent */
#define TEMPADC_MCU1    ((0xC0&TSCON0_bit_6_7_00)|(0x07&TSCON1_bit_0_2_000))
#define TEMPADC_MCU2    ((0xC0&TSCON0_bit_6_7_00)|(0x07&TSCON1_bit_0_2_001))
#define TEMPADC_MCU3    ((0xC0&TSCON0_bit_6_7_00)|(0x07&TSCON1_bit_0_2_010))
#define TEMPADC_MCU4    ((0xC0&TSCON0_bit_6_7_00)|(0x07&TSCON1_bit_0_2_011))
#define TEMPADC_ABB     ((0xC0&TSCON0_bit_6_7_01)|(0x07&TSCON1_bit_0_2_000))

#define TS_FILL(n) {#n, n}
#define TS_LEN_ARRAY(name) (sizeof(name)/sizeof(name[0]))
#define MAX_TS_NAME 20

#define CPU_COOLER_NUM 34
#define MTK_TS_CPU_RT                       (0)

#ifdef CONFIG_MTK_RAM_CONSOLE
#define CONFIG_THERMAL_AEE_RR_REC (1)
#else
#define CONFIG_THERMAL_AEE_RR_REC (0)
#endif

#define thermal_setl(addr, val)     mt_reg_sync_writel(readl(addr) | (val), ((void *)addr))
#define thermal_clrl(addr, val)     mt_reg_sync_writel(readl(addr) & ~(val), ((void *)addr))

#define MTKTSCPU_TEMP_CRIT 120000	/* 120.000 degree Celsius */

#define y_curr_repeat_times 1
#define THERMAL_NAME    "mtk-thermal"

#define TS_MS_TO_NS(x) (x * 1000 * 1000)

#if THERMAL_GET_AHB_BUS_CLOCK
#define THERMAL_MODULE_SW_CG_SET	(therm_clk_infracfg_ao_base + 0x88)
#define THERMAL_MODULE_SW_CG_CLR	(therm_clk_infracfg_ao_base + 0x8C)
#define THERMAL_MODULE_SW_CG_STA	(therm_clk_infracfg_ao_base + 0x94)

#define THERMAL_CG	(therm_clk_infracfg_ao_base + 0x80)
#define THERMAL_DCM	(therm_clk_infracfg_ao_base + 0x70)
#endif

#define TSCPU_LOG_TAG		"[CPU_Thermal]"

#define tscpu_dprintk(fmt, args...)   \
	do {                                    \
		if (tscpu_debug_log) {                \
			pr_debug(TSCPU_LOG_TAG fmt, ##args); \
		}                                   \
	} while (0)

#define tscpu_printk(fmt, args...) pr_debug(TSCPU_LOG_TAG fmt, ##args)
#define tscpu_warn(fmt, args...)  pr_warn(TSCPU_LOG_TAG fmt, ##args)

/* Align with thermal_sensor_name   struct @ Mt_thermal.h */
typedef enum thermal_sensor_enum {
	MCU1 = 0,
	MCU2,
	MCU3,
	MCU4,
	ABB,
	ENUM_MAX,
} ts_e;

typedef struct {
	char ts_name[MAX_TS_NAME];
	ts_e type;
} thermal_sensor_t;

typedef struct {
	thermal_sensor_t ts[ENUM_MAX];
	int ts_number;
} bank_t;

#if (CONFIG_THERMAL_AEE_RR_REC == 1)
enum thermal_state {
	TSCPU_SUSPEND = 0,
	TSCPU_RESUME = 1,
	TSCPU_NORMAL = 2,
	TSCPU_INIT = 3
};
enum atm_state {
	ATM_WAKEUP = 0,
	ATM_CPULIMIT  = 1,
	ATM_GPULIMIT  = 2,
	ATM_DONE    = 3,
};
#endif

struct mtk_cpu_power_info {
	unsigned int cpufreq_khz;
	unsigned int cpufreq_ncpu;
	unsigned int cpufreq_power;
};

/*In src/mtk_tc.c*/
extern int tscpu_debug_log;
extern const struct of_device_id mt_thermal_of_match[2];
extern int tscpu_bank_ts[THERMAL_BANK_NUM][ENUM_MAX];
extern int tscpu_bank_ts_r[THERMAL_BANK_NUM][ENUM_MAX]; /* raw data */
extern bank_t tscpu_g_bank[THERMAL_BANK_NUM];
extern int tscpu_polling_trip_temp1;
extern int tscpu_polling_trip_temp2;
extern int tscpu_polling_factor1;
extern int tscpu_polling_factor2;
#if MTKTSCPU_FAST_POLLING
extern int fast_polling_trip_temp;
extern int fast_polling_trip_temp_high;
extern int fast_polling_factor;
extern int tscpu_cur_fp_factor;
extern int tscpu_next_fp_factor;
#endif

/*In common/thermal_zones/mtk_ts_cpu.c*/
extern int Num_of_GPU_OPP;
extern struct mt_gpufreq_power_table_info *mtk_gpu_power;
extern int tscpu_read_curr_temp;
#if MTKTSCPU_FAST_POLLING
extern int tscpu_cur_fp_factor;
#endif

#if !defined(CONFIG_MTK_CLKMGR)
extern struct clk *therm_main;	/* main clock for Thermal */
#endif

#if CPT_ADAPTIVE_AP_COOLER
extern int tscpu_g_curr_temp;
extern int tscpu_g_prev_temp;
#if (THERMAL_HEADROOM == 1) || (CONTINUOUS_TM == 1)
extern int bts_cur_temp;	/* in mtk_ts_bts.c */
#endif
#endif

#ifdef CONFIG_OF
extern u32 thermal_irq_number;
extern void __iomem *thermal_base;
extern void __iomem *auxadc_ts_base;
extern void __iomem *infracfg_ao_base;
extern void __iomem *apmixed_base;
extern void __iomem *INFRACFG_AO_base;

extern int thermal_phy_base;
extern int auxadc_ts_phy_base;
extern int apmixed_phy_base;
extern int pericfg_phy_base;
#endif
extern char *adaptive_cooler_name;

/*common/coolers/mtk_cooler_atm.c*/
extern unsigned int adaptive_cpu_power_limit;
extern unsigned int adaptive_gpu_power_limit;
extern int TARGET_TJS[MAX_CPT_ADAPTIVE_COOLERS];

/*common/coolers/mtk_cooler_dtm.c*/
extern unsigned int static_cpu_power_limit;
extern unsigned int static_gpu_power_limit;
extern int tscpu_cpu_dmips[CPU_COOLER_NUM];
/*In common/thermal_zones/mtk_ts_cpu.c*/
extern void tscpu_print_all_temperature(int isDprint);
extern void tscpu_update_tempinfo(void);
#if THERMAL_GPIO_OUT_TOGGLE
void tscpu_set_GPIO_toggle_for_monitor(void);
#endif
extern void tscpu_thermal_tempADCPNP(int adc, int order);
extern int tscpu_thermal_ADCValueOfMcu(ts_e type);
extern void tscpu_thermal_enable_all_periodoc_sensing_point(thermal_bank_name bank_num);
extern void tscpu_update_tempinfo(void);
extern int tscpu_max_temperature(void);

/*In common/thermal_zones/mtk_ts.c*/
extern int get_io_reg_base(void);
extern void tscpu_config_all_tc_hw_protect(int temperature, int temperature2);
extern void tscpu_reset_thermal(void);
extern void tscpu_thermal_initial_all_bank(void);
extern int tscpu_switch_bank(thermal_bank_name bank);
extern void tscpu_thermal_read_bank_temp(thermal_bank_name bank, ts_e type, int order);
extern void tscpu_thermal_cal_prepare(void);
extern void tscpu_thermal_cal_prepare_2(__u32 ret);
extern irqreturn_t tscpu_thermal_all_bank_interrupt_handler(int irq, void *dev_id);
extern int tscpu_thermal_clock_on(void);
extern int tscpu_thermal_clock_off(void);
extern int tscpu_read_temperature_info(struct seq_file *m, void *v);
extern int tscpu_thermal_fast_init(void);
extern int tscpu_get_curr_temp(void);
extern bool mtk_get_gpu_loading(unsigned int *pLoading);
extern int IMM_IsAdcInitReady(void);
#if (CONFIG_THERMAL_AEE_RR_REC == 1)
extern void aee_rr_rec_thermal_temp1(u8 val);
extern void aee_rr_rec_thermal_temp2(u8 val);
extern void aee_rr_rec_thermal_temp3(u8 val);
extern void aee_rr_rec_thermal_temp4(u8 val);
extern void aee_rr_rec_thermal_temp5(u8 val);
extern void aee_rr_rec_thermal_status(u8 val);
extern void aee_rr_rec_thermal_ATM_status(u8 val);
extern void aee_rr_rec_thermal_ktime(u64 val);

extern u8 aee_rr_curr_thermal_temp1(void);
extern u8 aee_rr_curr_thermal_temp2(void);
extern u8 aee_rr_curr_thermal_temp3(void);
extern u8 aee_rr_curr_thermal_temp4(void);
extern u8 aee_rr_curr_thermal_temp5(void);
extern u8 aee_rr_curr_thermal_status(void);
extern u8 aee_rr_curr_thermal_ATM_status(void);
extern u64 aee_rr_curr_thermal_ktime(void);
#endif
/*aee related*/
