
#ifndef _DISP_LOWPOWER_H_
#define _DISP_LOWPOWER_H_

unsigned int dsi_phy_get_clk(DISP_MODULE_ENUM module);
void primary_display_idlemgr_enter_idle_nolock(void);


golden_setting_context *get_golden_setting_pgc(void);
int primary_display_lowpower_init(void);
void primary_display_sodi_rule_init(void);
void kick_logger_dump(char *string);
void kick_logger_dump_reset(void);
char *get_kick_dump(void);
unsigned int get_kick_dump_size(void);
int primary_display_is_idle(void);
void primary_display_idlemgr_kick(const char *source, int need_lock);
void exit_pd_by_cmdq(cmdqRecHandle handler);
void enter_pd_by_cmdq(cmdqRecHandle handler);
void enter_share_sram(CMDQ_EVENT_ENUM resourceEvent);
void leave_share_sram(CMDQ_EVENT_ENUM resourceEvent);
void set_hrtnum(unsigned int new_hrtnum);
void set_enterulps(unsigned flag);
void set_is_dc(unsigned int is_dc);
unsigned int set_one_layer(unsigned int is_onelayer);
void set_rdma_width_height(unsigned int width, unsigned height);
void enable_idlemgr(unsigned int flag);
unsigned int get_idlemgr_flag(void);
unsigned int set_idlemgr(unsigned int flag, int need_lock);
int _blocking_flush(void);
unsigned int get_idlemgr_flag(void);
unsigned int set_idlemgr(unsigned int flag, int need_lock);
void primary_display_sodi_enable(int flag);
/**************************************** for met******************************************* */
unsigned int is_mipi_enterulps(void);

unsigned int get_mipi_clk(void);

#endif
