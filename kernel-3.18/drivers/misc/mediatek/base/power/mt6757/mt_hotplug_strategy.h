

#ifndef __MT_HOTPLUG_STRATEGY_H__
#define __MT_HOTPLUG_STRATEGY_H__

/*=============================================================*/
/* Include files */
/*=============================================================*/

/* system includes */

/* project includes */

/* local includes */

/* forward references */

/*=============================================================*/
/* Macro definition */
/*=============================================================*/
typedef enum {
	BASE_PERF_SERV = 0,
	BASE_PPM_SERV,
	BASE_COUNT
} hps_base_type_e;

typedef enum {
	LIMIT_THERMAL = 0,
	LIMIT_PPM_SERV,
	LIMIT_LOW_BATTERY,
	LIMIT_ULTRA_POWER_SAVING,
	LIMIT_POWER_SERV,
	LIMIT_COUNT
} hps_limit_type_e;

/*=============================================================*/
/* Type definition */
/*=============================================================*/

/*=============================================================*/
/* Global variable declaration */
/*=============================================================*/

/*=============================================================*/
/* Global function declaration */
/*=============================================================*/
extern int hps_get_enabled(unsigned int *enabled_ptr);
extern int hps_set_enabled(unsigned int enabled);
extern int hps_get_cpu_num_base(hps_base_type_e type, unsigned int *little_cpu_ptr,
				unsigned int *big_cpu_ptr);
extern int hps_set_cpu_num_base(hps_base_type_e type, unsigned int little_cpu,
				unsigned int big_cpu);
extern int hps_get_cpu_num_limit(hps_limit_type_e type, unsigned int *little_cpu_ptr,
				 unsigned int *big_cpu_ptr);
extern int hps_set_cpu_num_limit(hps_limit_type_e type, unsigned int little_cpu,
				 unsigned int big_cpu);
extern int hps_get_tlp(unsigned int *tlp_ptr);
extern int hps_get_num_possible_cpus(unsigned int *little_cpu_ptr, unsigned int *big_cpu_ptr);
extern int hps_get_num_online_cpus(unsigned int *little_cpu_ptr, unsigned int *big_cpu_ptr);
extern int hps_set_PPM_request(unsigned int little_min, unsigned int little_max,
			       unsigned int big_min, unsigned int big_max);
extern unsigned int hps_get_hvytsk(unsigned int cluster_id);
/*=============================================================*/
/* End */
/*=============================================================*/

#endif				/* __MT_HOTPLUG_STRATEGY_H__ */
