

#ifndef __MT_HOTPLUG_STRATEGY_INTERNAL_H__
#define __MT_HOTPLUG_STRATEGY_INTERNAL_H__

/*=============================================================*/
/* Include files */
/*=============================================================*/
/* system includes */
#include <linux/kernel.h>
#include <linux/io.h>		/* __raw_readl */
#include <linux/platform_device.h>	/* struct platform_driver */
#include <linux/kthread.h>	/* struct task_struct */
#include <linux/timer.h>	/* struct timer_list */
#include <linux/sched/rt.h>	/* MAX_RT_PRIO */

/* project includes */

/* local includes */
#include <mach/mt_ppm_api.h>
/* forward references */

/*=============================================================*/
/* Macro definition */
/*=============================================================*/
#define HPS_TASK_RT_PRIORITY                   (MAX_RT_PRIO - 3)
#define HPS_TASK_NORMAL_PRIORITY                   (MIN_NICE)
#define HPS_TIMER_INTERVAL_MS               (40)

#define HPS_PERIODICAL_BY_WAIT_QUEUE        (1)
#define HPS_PERIODICAL_BY_TIMER             (2)
#define HPS_PERIODICAL_BY_HR_TIMER          (3)

#define MAX_CPU_UP_TIMES                    (10)
#define MAX_CPU_DOWN_TIMES                  (100)
#define MAX_TLP_TIMES                       (10)
/* cpu capability of big / little = 1.7, aka 170, 170 - 100 = 70 */
#define CPU_DMIPS_BIG_LITTLE_DIFF           (70)

#define DEF_CPU_UP_THRESHOLD                (95)
#define DEF_CPU_UP_TIMES                    (3)
#define DEF_CPU_DOWN_THRESHOLD              (85)
#define DEF_CPU_DOWN_TIMES                  (1)
#define DEF_TLP_TIMES                       (1)

#define EN_CPU_INPUT_BOOST                  (1)
#define DEF_CPU_INPUT_BOOST_CPU_NUM         (2)

#define EN_CPU_RUSH_BOOST                   (1)
#define DEF_CPU_RUSH_BOOST_THRESHOLD        (98)
#define DEF_CPU_RUSH_BOOST_TIMES            (1)

#define EN_HPS_LOG                          (1)
#define EN_ISR_LOG                          (0)
#define HPS_HRT_BT_EN						(1)
#define HPS_HRT_DBG_MS			(5000)
#define HPS_BIG_CLUSTER_ID					(2)
#define hps_emerg(fmt, args...)             pr_emerg("[HPS] " fmt, ##args)
#define hps_alert(fmt, args...)             pr_alert("[HPS] " fmt, ##args)
#define hps_crit(fmt, args...)              pr_crit("[HPS] " fmt, ##args)
#define hps_error(fmt, args...)             pr_err("[HPS] " fmt, ##args)
#define hps_warn(fmt, args...)              pr_warn("[HPS] " fmt, ##args)
/* #define hps_warn(fmt, args...)              printk(KERN_DEBUG "[HPS] " fmt, ##args) */
/* #define hps_warn(fmt, args...)              pr_emerg("[HPS] " fmt, ##args) */
#define hps_notice(fmt, args...)            pr_notice("[HPS] " fmt, ##args)
#define hps_info(fmt, args...)              pr_info("[HPS] " fmt, ##args)
/* #define hps_debug(fmt, args...)             pr_debug("[HPS] " fmt, ##args) */
#define hps_debug(fmt, args...)             pr_debug("[HPS] " fmt, ##args)
/* #define hps_debug(fmt, args...)             pr_emerg("[HPS] " fmt, ##args) */

#if EN_ISR_LOG
#define hps_isr_info(fmt, args...)          hps_notice(fmt, ##args)
#else
#define hps_isr_info(fmt, args...)          hps_debug(fmt, ##args)
#endif

#define hps_read(addr)                      __raw_readl(IOMEM(addr))
#define hps_write(addr, val)                mt_reg_sync_writel(val, addr)

/* #define ARCH_TYPE_NOT_READY                 -1 */
/* #define ARCH_TYPE_NO_CLUSTER                0 */
/* #define ARCH_TYPE_big_LITTLE                1 */
/* #define ARCH_TYPE_LITTLE_LITTLE             2 */


#define CPUHP_INTERVAL          (5LL * NSEC_PER_SEC)

extern struct hrtimer cpuhp_timer;

/*=============================================================*/
/* Type definition */
/*=============================================================*/
typedef enum {
	INIT_STATE_NOT_READY = 0,
	INIT_STATE_DONE
} hps_init_state_e;

typedef enum {
	STATE_LATE_RESUME = 0,
	STATE_EARLY_SUSPEND,
	STATE_SUSPEND,
	STATE_COUNT
} hps_ctxt_state_e;


/* TODO: verify do you need action? no use now */

typedef enum {
	ACTION_NONE = 0,
	ACTION_BASE_LITTLE,	/* bit  1, 0x0002 */
	ACTION_BASE_BIG,	/* bit  2, 0x0004 */
	ACTION_LIMIT_LITTLE,	/* bit  3, 0x0008 */
	ACTION_LIMIT_BIG,	/* bit  4, 0x0010 */
	ACTION_RUSH_BOOST_LITTLE,	/* bit  5, 0x0020 */
	ACTION_RUSH_BOOST_BIG,	/* bit  6, 0x0040 */
	ACTION_UP_LITTLE,	/* bit  7, 0x0080 */
	ACTION_UP_BIG,		/* bit  8, 0x0100 */
	ACTION_DOWN_LITTLE,	/* bit  9, 0x0200 */
	ACTION_DOWN_BIG,	/* bit 10, 0x0400 */
	ACTION_BIG_TO_LITTLE,	/* bit 11, 0x0800 */
	ACTION_INPUT,		/* bit 12 */
	ACTION_ROOT_2_LITTLE,	/*bit 13, 0x2000 */
	ACTION_ROOT_2_BIG,	/*bit 14, 0x4000 */
	ACTION_COUNT
} hps_ctxt_action_e;

typedef enum {
	HPS_FUNC_CTRL_HPS,	/* bit  0, 0x0001 */
	HPS_FUNC_CTRL_RUSH,	/* bit  1, 0x0002 */
	HPS_FUNC_CTRL_HVY_TSK,	/* bit  2, 0x0004 */
	HPS_FUNC_CTRL_PPM_INIT,	/* big  3, 0x0008 */
	HPS_FUNC_CTRL_EFUSE,	/* big  4, 0x0010 */
	HPS_FUNC_CTRL_COUNT
} hps_ctxt_func_ctrl_e;

#define HPS_SYS_CHANGE_ROOT	(0x001)
struct hps_sys_ops {
	char *func_name;
	unsigned int func_id;
	unsigned int enabled;
	int (*hps_sys_func_ptr)(void);
};

struct hps_cluster_info {
	unsigned int cluster_id;
	unsigned int core_num;
	unsigned int cpu_id_min;
	unsigned int cpu_id_max;
	unsigned int limit_value;
	unsigned int base_value;
	unsigned int ref_limit_value;
	unsigned int ref_base_value;
	unsigned int is_root;
	unsigned int hvyTsk_value;
	unsigned int online_core_num;
	unsigned int target_core_num;
};

typedef struct hps_sys_struct {
	unsigned int cluster_num;
	struct hps_cluster_info *cluster_info;
	unsigned int func_num;
	struct hps_sys_ops *hps_sys_ops;
	unsigned int is_set_root_cluster;
	unsigned int root_cluster_id;
	unsigned int total_online_cores;
	unsigned int tlp_avg;
	unsigned int rush_cnt;
	unsigned int up_load_avg;
	unsigned int down_load_avg;
	unsigned int action_id;
} hps_sys_t;

typedef struct hps_ctxt_struct {
	/* state */
	unsigned int init_state;
	unsigned int state;
	unsigned int is_interrupt;
	ktime_t hps_regular_ktime;
	ktime_t hps_hrt_ktime;
	/* enabled */
	unsigned int enabled;
	unsigned int early_suspend_enabled;
	/* default 1, disable all big cores if is_hmp after early suspend stage (aka screen off) */
	unsigned int suspend_enabled;	/* default 1, disable hotplug strategy in suspend flow */
	unsigned int cur_dump_enabled;
	unsigned int stats_dump_enabled;
	unsigned int power_mode;
	unsigned int ppm_power_mode;
	unsigned int heavy_task_enabled;
	unsigned int is_ppm_init;
	unsigned int hps_func_control;
	/* core */
	struct mutex lock;	/* Synchronizes accesses */
	struct mutex break_lock;	/* Synchronizes accesses */
	struct mutex para_lock;
	struct task_struct *tsk_struct_ptr;
	wait_queue_head_t wait_queue;
	struct timer_list tmr_list;
	unsigned int periodical_by;
	struct hrtimer hr_timer;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend es_handler;
#endif				/* #ifdef CONFIG_HAS_EARLYSUSPEND */
	struct platform_driver pdrv;

	/* backup */
	unsigned int enabled_backup;
	unsigned int rush_boost_enabled_backup;

	/* cpu arch */
	unsigned int is_hmp;
	unsigned int is_amp;
	struct cpumask little_cpumask;
	struct cpumask big_cpumask;
	unsigned int little_cpu_id_min;
	unsigned int little_cpu_id_max;
	unsigned int big_cpu_id_min;
	unsigned int big_cpu_id_max;

	/* algo config */
	unsigned int up_threshold;
	unsigned int up_times;
	unsigned int down_threshold;
	unsigned int down_times;
	unsigned int input_boost_enabled;
	unsigned int input_boost_cpu_num;
	unsigned int rush_boost_enabled;
	unsigned int rush_boost_threshold;
	unsigned int rush_boost_times;
	unsigned int tlp_times;

	/* algo bound */
	unsigned int little_num_base_perf_serv;
	unsigned int little_num_limit_thermal;
	unsigned int little_num_limit_low_battery;
	unsigned int little_num_limit_ultra_power_saving;
	unsigned int little_num_limit_power_serv;
	unsigned int big_num_base_perf_serv;
	unsigned int big_num_limit_thermal;
	unsigned int big_num_limit_low_battery;
	unsigned int big_num_limit_ultra_power_saving;
	unsigned int big_num_limit_power_serv;

	/* algo statistics */
	unsigned int cur_loads;
	unsigned int cur_tlp;
	unsigned int cur_iowait;
	unsigned int cur_nr_heavy_task;
	unsigned int up_loads_sum;
	unsigned int up_loads_count;
	unsigned int up_loads_history[MAX_CPU_UP_TIMES];
	unsigned int up_loads_history_index;
	unsigned int down_loads_sum;
	unsigned int down_loads_count;
	unsigned int down_loads_history[MAX_CPU_DOWN_TIMES];
	unsigned int down_loads_history_index;
	unsigned int rush_count;
	unsigned int tlp_sum;
	unsigned int tlp_count;
	unsigned int tlp_history[MAX_TLP_TIMES];
	unsigned int tlp_history_index;
	unsigned int tlp_avg;

	/* For fast hotplug integration */
	unsigned int wake_up_by_fasthotplug;
	unsigned int root_cpu;

	/* algo action */
	unsigned long action;
	atomic_t is_ondemand;
	atomic_t is_break;

	/* misc */
	unsigned int test0;
	unsigned int test1;
} hps_ctxt_t;

typedef struct hps_cpu_ctxt_struct {
	unsigned int load;
} hps_cpu_ctxt_t;

/*=============================================================*/
/* Global variable declaration */
/*=============================================================*/
extern hps_ctxt_t hps_ctxt;
extern hps_sys_t hps_sys;
DECLARE_PER_CPU(hps_cpu_ctxt_t, hps_percpu_ctxt);
/* forward references */
extern struct cpumask cpu_domain_big_mask;	/* definition in kernel-3.10/arch/arm/kernel/topology.c */
extern struct cpumask cpu_domain_little_mask;	/* definition in kernel-3.10/arch/arm/kernel/topology.c */

extern void sched_get_nr_running_avg(int *avg, int *iowait_avg);
	/* definition in mediatek/kernel/kernel/sched/rq_stats.c */

/*=============================================================*/
/* Global function declaration */
/*=============================================================*/
extern int hps_ops_init(void);

extern void hps_ctxt_reset_stas_nolock(void);
extern void hps_ctxt_reset_stas(void);
extern void hps_ctxt_print_basic(int toUart);
extern void hps_ctxt_print_algo_config(int toUart);
extern void hps_ctxt_print_algo_bound(int toUart);
extern void hps_ctxt_print_algo_stats_cur(int toUart);
extern void hps_ctxt_print_algo_stats_up(int toUart);
extern void hps_ctxt_print_algo_stats_down(int toUart);
extern void hps_ctxt_print_algo_stats_tlp(int toUart);
extern int hps_restart_timer(void);
extern int hps_del_timer(void);
extern int hps_core_deinit(void);
extern int hps_core_init(void);
extern int hps_core_deinit(void);
extern int hps_task_start(void);
extern void hps_task_stop(void);
extern void hps_task_wakeup_nolock(void);
extern void hps_task_wakeup(void);


extern void hps_algo_main(void);
extern int hps_cal_core_num(struct hps_sys_struct *hps_sys, int core_val, int base_val);
extern unsigned int hps_get_cluster_cpus(unsigned int cluster_id);

extern void hps_set_break_en(int hps_break_en);
extern int hps_get_break_en(void);
extern int hps_procfs_init(void);

#define num_possible_little_cpus()          cpumask_weight(&hps_ctxt.little_cpumask)
#define num_possible_big_cpus()             cpumask_weight(&hps_ctxt.big_cpumask)

extern int hps_cpu_init(void);
extern int hps_cpu_deinit(void);
/* extern int hps_cpu_get_arch_type(void); */
extern unsigned int num_online_little_cpus(void);
extern unsigned int num_online_big_cpus(void);
extern int hps_cpu_is_cpu_big(int cpu);
extern int hps_cpu_is_cpu_little(int cpu);
extern unsigned int hps_cpu_get_percpu_load(int cpu);
extern unsigned int hps_cpu_get_nr_heavy_task(void);
extern void hps_cpu_get_tlp(unsigned int *avg, unsigned int *iowait_avg);
extern unsigned int sched_get_nr_heavy_task2(int cluster_id);
/*=============================================================*/
/* End */
/*=============================================================*/
extern struct cpumask cpu_domain_big_mask;
extern struct cpumask cpu_domain_little_mask;
extern void sched_get_nr_running_avg(int *avg, int *iowait_avg);

extern unsigned int sched_get_percpu_load(int cpu, bool reset, bool use_maxfreq);
extern unsigned int sched_get_nr_heavy_task(void);

#endif				/* __MT_HOTPLUG_STRATEGY_INTERNAL_H__ */
