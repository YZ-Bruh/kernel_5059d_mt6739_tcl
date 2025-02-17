
#ifndef _MT_VCOREFS_MANAGER_H
#define _MT_VCOREFS_MANAGER_H

#include "mt_vcorefs_governor.h"

/* Manager extern API */
extern int is_vcorefs_can_work(void);

/* Main function API (Called by kicker request for DVFS) */
extern int vcorefs_request_dvfs_opp(enum dvfs_kicker, enum dvfs_opp);

/* Called by governor for init flow */
extern void vcorefs_drv_init(bool, bool, int);
extern int init_vcorefs_sysfs(void);
extern u32 log_mask(void);

/* Called by MET */
typedef void (*vcorefs_req_handler_t) (enum dvfs_kicker kicker, enum dvfs_opp opp);
extern void vcorefs_register_req_notify(vcorefs_req_handler_t handler);

/* AEE debug */
extern void aee_rr_rec_vcore_dvfs_opp(u32 val);
extern u32 aee_rr_curr_vcore_dvfs_opp(void);
extern void aee_rr_rec_vcore_dvfs_status(u32 val);
extern u32 aee_rr_curr_vcore_dvfs_status(void);

#endif	/* _MT_VCOREFS_MANAGER_H */
