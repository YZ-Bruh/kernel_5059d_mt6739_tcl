
#ifndef _MTK_THERMAL_PLATFORM_H
#define _MTK_THERMAL_PLATFORM_H

#include <linux/thermal.h>
#include "mtk_thermal_typedefs.h"

extern
int mtk_thermal_get_cpu_info(int *nocores, int **cpufreq, int **cpuloading);

extern
int mtk_thermal_get_gpu_info(int *nocores, int **gpufreq, int **gpuloading);

extern
int mtk_thermal_get_batt_info(int *batt_voltage, int *batt_current, int *batt_temp);

extern
int mtk_thermal_get_extra_info(int *no_extra_attr,
			       char ***attr_names, int **attr_values, char ***attr_unit);

extern
int mtk_thermal_force_get_batt_temp(void);


enum {
	MTK_THERMAL_SCEN_CALL = 0x1
};

extern
unsigned int mtk_thermal_set_user_scenarios(unsigned int mask);

extern
unsigned int mtk_thermal_clear_user_scenarios(unsigned int mask);

extern int force_get_tbat(void);
#endif				/* _MTK_THERMAL_PLATFORM_H */


/* --- SPA parameters --- */
struct spa_Tpolicy_info {
	int min_cpu_power[3];
	int min_gpu_power[3];
	int steady_target_tj;
	int steady_exit_tj;
};

struct spa_system_info {
	int cpu_Tj;
	int Tpcb;
	int OPP_power;
	unsigned int fg_app_pid;
	unsigned int avg_fps;
	int WIFI_UL_Tput;
	int MD_UL_Tput;
	int chg_current_limit;
	int input_current_limit;
	int camera_on;
	int game_mode;
};

struct SPA_T {
	struct spa_Tpolicy_info t_spa_Tpolicy_info;
	struct spa_system_info t_spa_system_info;
};

extern struct SPA_T thermal_spa_t;
/* --- SPA parameters --- */
typedef enum {
	TA_DAEMON_CMD_GET_INIT_FLAG = 0,
	TA_DAEMON_CMD_SET_DAEMON_PID,
	TA_DAEMON_CMD_NOTIFY_DAEMON,
	TA_DAEMON_CMD_NOTIFY_DAEMON_CATMINIT,
	TA_DAEMON_CMD_SET_TTJ,
	TA_DAEMON_CMD_GET_TPCB,
	TA_DAEMON_CMD_GET_TI,

	TA_DAEMON_CMD_TO_KERNEL_NUMBER
} TA_DAEMON_CTRL_CMD_TO_KERNEL; /*must sync userspace/kernel: TA_DAEMON_CTRL_CMD_FROM_USER*/

#define TAD_NL_MSG_T_HDR_LEN 12
#define TAD_NL_MSG_MAX_LEN 512

struct tad_nl_msg_t {
	unsigned int tad_cmd;
	unsigned int tad_data_len;
	unsigned int tad_ret_data_len;
	char tad_data[TAD_NL_MSG_MAX_LEN];
};

enum {
	TA_CATMPLUS = 1,
	TA_CONTINUOUS = 2,
	TA_CATMPLUS_TTJ = 3
};


struct cATM_params_t {
	int CATM_ON;
	int K_TT;
	int K_SUM_TT_LOW;
	int K_SUM_TT_HIGH;
	int MIN_SUM_TT;
	int MAX_SUM_TT;
	int MIN_TTJ;
	int CATMP_STEADY_TTJ_DELTA;
};
struct continuetm_params_t {
	int STEADY_TARGET_TJ;
	int MAX_TARGET_TJ;
	int TRIP_TPCB;
	int STEADY_TARGET_TPCB;
};


struct CATM_T {
	struct cATM_params_t t_catm_par;
	struct continuetm_params_t t_continuetm_par;
};
extern struct CATM_T thermal_atm_t;
int wakeup_ta_algo(int flow_state);
int ta_get_ttj(void);

extern int mtk_thermal_get_tpcb_target(void);
extern int tsatm_thermal_get_catm_type(void);


