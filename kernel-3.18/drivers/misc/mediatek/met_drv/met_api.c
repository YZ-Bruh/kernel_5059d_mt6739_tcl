
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tracepoint.h>
#include <trace/events/sched.h>
#include <trace/events/power.h>

#include <linux/kallsyms.h>
#include <linux/printk.h>
#include <linux/ftrace_event.h>

#define MET_DEFINE_PROBE(probe_name, proto) \
		static void probe_##probe_name(void *data, PARAMS(proto))
#define MET_REGISTER_TRACE(probe_name) \
		register_trace_##probe_name(probe_##probe_name, NULL)
#define MET_UNREGISTER_TRACE(probe_name) \
		unregister_trace_##probe_name(probe_##probe_name, NULL)

struct met_api_tbl {
	int (*met_tag_start)(unsigned int class_id, const char *name);
	int (*met_tag_end)(unsigned int class_id, const char *name);
	int (*met_tag_async_start)(unsigned int class_id, const char *name, unsigned int cookie);
	int (*met_tag_async_end)(unsigned int class_id, const char *name, unsigned int cookie);
	int (*met_tag_oneshot)(unsigned int class_id, const char *name, unsigned int value);
	int (*met_tag_userdata)(char *pData);
	int (*met_tag_dump)(unsigned int class_id, const char *name, void *data, unsigned int length);
	int (*met_tag_disable)(unsigned int class_id);
	int (*met_tag_enable)(unsigned int class_id);
	int (*met_set_dump_buffer)(int size);
	int (*met_save_dump_buffer)(const char *pathname);
	int (*met_save_log)(const char *pathname);
	int (*met_show_bw_limiter)(void);
	int (*met_reg_bw_limiter)(void *fp);
	int (*met_show_clk_tree)(const char *name, unsigned int addr, unsigned int status);
	int (*met_reg_clk_tree)(void *fp);
	void (*met_sched_switch)(struct task_struct *prev, struct task_struct *next);
};

struct met_api_tbl met_ext_api;
EXPORT_SYMBOL(met_ext_api);

int met_tag_init(void)
{
	return 0;
}
EXPORT_SYMBOL(met_tag_init);

int met_tag_uninit(void)
{
	return 0;
}
EXPORT_SYMBOL(met_tag_uninit);

int met_tag_start(unsigned int class_id, const char *name)
{
	if (met_ext_api.met_tag_start)
		return met_ext_api.met_tag_start(class_id, name);
	return 0;
}
EXPORT_SYMBOL(met_tag_start);

int met_tag_end(unsigned int class_id, const char *name)
{
	if (met_ext_api.met_tag_end)
		return met_ext_api.met_tag_end(class_id, name);
	return 0;
}
EXPORT_SYMBOL(met_tag_end);

int met_tag_async_start(unsigned int class_id, const char *name, unsigned int cookie)
{
	if (met_ext_api.met_tag_async_start)
		return met_ext_api.met_tag_async_start(class_id, name, cookie);
	return 0;
}
EXPORT_SYMBOL(met_tag_async_start);

int met_tag_async_end(unsigned int class_id, const char *name, unsigned int cookie)
{
	if (met_ext_api.met_tag_async_end)
		return met_ext_api.met_tag_async_end(class_id, name, cookie);
	return 0;
}
EXPORT_SYMBOL(met_tag_async_end);

int met_tag_oneshot(unsigned int class_id, const char *name, unsigned int value)
{
	if (met_ext_api.met_tag_oneshot)
		return met_ext_api.met_tag_oneshot(class_id, name, value);
	return 0;
}
EXPORT_SYMBOL(met_tag_oneshot);

int met_tag_userdata(char *pData)
{
	if (met_ext_api.met_tag_userdata)
		return met_ext_api.met_tag_userdata(pData);
	return 0;
}
EXPORT_SYMBOL(met_tag_userdata);

int met_tag_dump(unsigned int class_id, const char *name, void *data, unsigned int length)
{
	if (met_ext_api.met_tag_dump)
		return met_ext_api.met_tag_dump(class_id, name, data, length);
	return 0;
}
EXPORT_SYMBOL(met_tag_dump);

int met_tag_disable(unsigned int class_id)
{
	if (met_ext_api.met_tag_disable)
		return met_ext_api.met_tag_disable(class_id);
	return 0;
}
EXPORT_SYMBOL(met_tag_disable);

int met_tag_enable(unsigned int class_id)
{
	if (met_ext_api.met_tag_enable)
		return met_ext_api.met_tag_enable(class_id);
	return 0;
}
EXPORT_SYMBOL(met_tag_enable);

int met_set_dump_buffer(int size)
{
	if (met_ext_api.met_set_dump_buffer)
		return met_ext_api.met_set_dump_buffer(size);
	return 0;
}
EXPORT_SYMBOL(met_set_dump_buffer);

int met_save_dump_buffer(const char *pathname)
{
	if (met_ext_api.met_save_dump_buffer)
		return met_ext_api.met_save_dump_buffer(pathname);
	return 0;
}
EXPORT_SYMBOL(met_save_dump_buffer);

int met_save_log(const char *pathname)
{
	if (met_ext_api.met_save_log)
		return met_ext_api.met_save_log(pathname);
	return 0;
}
EXPORT_SYMBOL(met_save_log);

int met_show_bw_limiter(void)
{
	if (met_ext_api.met_show_bw_limiter)
		return met_ext_api.met_show_bw_limiter();
	return 0;
}
EXPORT_SYMBOL(met_show_bw_limiter);

int met_reg_bw_limiter(void *fp)
{
	if (met_ext_api.met_reg_bw_limiter)
		return met_ext_api.met_reg_bw_limiter(fp);
	return 0;
}
EXPORT_SYMBOL(met_reg_bw_limiter);

int met_show_clk_tree(const char *name,
				unsigned int addr,
				unsigned int status)
{
	if (met_ext_api.met_show_clk_tree)
		return met_ext_api.met_show_clk_tree(name, addr, status);
	return 0;
}
EXPORT_SYMBOL(met_show_clk_tree);

int met_reg_clk_tree(void *fp)
{
	if (met_ext_api.met_reg_clk_tree)
		return met_ext_api.met_reg_clk_tree(fp);
	return 0;
}
EXPORT_SYMBOL(met_reg_clk_tree);

MET_DEFINE_PROBE(sched_switch, TP_PROTO(struct task_struct *prev, struct task_struct *next))
{
	if (met_ext_api.met_sched_switch)
		met_ext_api.met_sched_switch(prev, next);
}

int met_reg_switch(void)
{
	if (MET_REGISTER_TRACE(sched_switch)) {
		pr_debug("can not register callback of sched_switch\n");
		return -ENODEV;
	} else
		return 0;
}
EXPORT_SYMBOL(met_reg_switch);

void met_unreg_switch(void)
{
	MET_UNREGISTER_TRACE(sched_switch);
}
EXPORT_SYMBOL(met_unreg_switch);

void met_cpu_frequency(unsigned int frequency, unsigned int cpu_id)
{
	trace_cpu_frequency(frequency, cpu_id);
}
EXPORT_SYMBOL(met_cpu_frequency);

void met_tracing_record_cmdline(struct task_struct *tsk)
{
	tracing_record_cmdline(tsk);
}
EXPORT_SYMBOL(met_tracing_record_cmdline);

void met_set_kptr_restrict(int value)
{
	kptr_restrict = value;
}
EXPORT_SYMBOL(met_set_kptr_restrict);

int met_get_kptr_restrict(void)
{
	return kptr_restrict;
}
EXPORT_SYMBOL(met_get_kptr_restrict);

int enable_met_backlight_tag(void)
{
	return 0;
}
EXPORT_SYMBOL(enable_met_backlight_tag);

int output_met_backlight_tag(int level)
{
	return 0;
}
EXPORT_SYMBOL(output_met_backlight_tag);

/* the following handle weak function in met_drv.h */
void met_mmsys_event_gce_thread_begin(ulong thread_no, ulong task_handle,
				ulong engineFlag, void *pCmd, ulong size)
{
}
EXPORT_SYMBOL(met_mmsys_event_gce_thread_begin);

void met_mmsys_event_gce_thread_end(ulong thread_no, ulong task_handle, ulong engineFlag)
{
}
EXPORT_SYMBOL(met_mmsys_event_gce_thread_end);

void met_mmsys_event_disp_sof(int mutex_id)
{
}
EXPORT_SYMBOL(met_mmsys_event_disp_sof);

void met_mmsys_event_disp_mutex_eof(int mutex_id)
{
}
EXPORT_SYMBOL(met_mmsys_event_disp_mutex_eof);

void met_mmsys_event_disp_ovl_eof(int ovl_id)
{
}
EXPORT_SYMBOL(met_mmsys_event_disp_ovl_eof);

void met_mmsys_config_isp_base_addr(unsigned long *isp_reg_list)
{
}
EXPORT_SYMBOL(met_mmsys_config_isp_base_addr);

void met_mmsys_event_isp_pass1_begin(int sensor_id)
{
}
EXPORT_SYMBOL(met_mmsys_event_isp_pass1_begin);

void met_mmsys_event_isp_pass1_end(int sensor_id)
{
}
EXPORT_SYMBOL(met_mmsys_event_isp_pass1_end);

