
#ifndef _MT_SPM_SLEEP_
#define _MT_SPM_SLEEP_

#include <linux/kernel.h>
#include "mt_spm.h"
extern int spm_set_sleep_wakesrc(u32 wakesrc, bool enable, bool replace);
extern u32 spm_get_sleep_wakesrc(void);
extern wake_reason_t spm_go_to_sleep(u32 spm_flags, u32 spm_data);

extern bool spm_is_md_sleep(void);
extern bool spm_is_md1_sleep(void);
extern bool spm_is_md2_sleep(void);
extern bool spm_is_conn_sleep(void);
extern void spm_set_wakeup_src_check(void);
extern bool spm_check_wakeup_src(void);
extern void spm_poweron_config_set(void);
extern void spm_md32_sram_con(u32 value);
extern void spm_ap_mdsrc_req(u8 set);
extern bool spm_set_suspned_pcm_init_flag(u32 *suspend_flags);

extern void spm_output_sleep_option(void);

#endif
