
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/printk.h>
#include <stdarg.h>
#include <linux/slab.h>
#include "mt-plat/met_drv.h"

#include "ddp_mmp.h"
#include "debug.h"

#include "disp_drv_log.h"

#include "disp_lcm.h"
#include "disp_utils.h"
#include "mtkfb_info.h"
#include "mtkfb.h"

#include "ddp_hal.h"
#include "ddp_dump.h"
#include "ddp_path.h"
#include "ddp_drv.h"

#include "m4u.h"
#include "m4u_port.h"

#include "primary_display.h"
#include "cmdq_def.h"
#include "cmdq_record.h"
#include "cmdq_reg.h"
#include "cmdq_core.h"

#include "ddp_manager.h"
#include "disp_drv_platform.h"
#include "display_recorder.h"
#include "disp_session.h"
#include "ddp_mmp.h"
#include <linux/ftrace_event.h>

#if defined(CONFIG_MT_ENG_BUILD) || !defined(CONFIG_MTK_GMO_RAM_OPTIMIZE)
unsigned int gCapturePriLayerEnable = 0;
unsigned int gCaptureWdmaLayerEnable = 0;
unsigned int gCaptureRdmaLayerEnable = 0;
unsigned int gCapturePriLayerDownX = 20;
unsigned int gCapturePriLayerDownY = 20;
unsigned int gCapturePriLayerNum = 4;

static DEFINE_SPINLOCK(gdprec_logger_spinlock);

static dprec_debug_control _control = { 0 };

static reg_base_map reg_map[] = {
	{"MMSYS", (0xf4000000)},
	{"OVL0", (0xF400c000)},
	{"OVL1", (0xF400d000)},
	{"RDMA0", (0xF400e000)},
	{"RDMA1", (0xF400f000)},
	{"RDMA2", (0xF4010000)},
	{"WDMA0", (0xF4011000)},
	{"WDMA1", (0xF4012000)},
	{"COLOR0", (0xF4013000)},
	{"COLOR1", (0xF4014000)},
	{"AAL", (0xF4015000)},
	{"GAMMA", (0xF4016000)},
	{"MERGE", (0xF4017000)},
	{"SPLIT0", (0xF4018000)},
	{"SPLIT1", (0xF4019000)},
	{"UFOE", (0xF401a000)},
	{"DSI0", (0xF401b000)},
	{"DSI1", (0xF401c000)},
	{"DPI", (0xF401d000)},
	{"PWM0", (0xF401e000)},
	{"PWM1", (0xF401f000)},
	{"MUTEX", (0xF4020000)},
	{"SMI_LARB0", (0xF4021000)},
	{"SMI_COMMON", (0xF4022000)},
	{"OD", (0xF4023000)},
	{"MIPITX0", (0xF0215000)},
	{"MIPITX1", (0xF0216000)},
};

static event_string_map event_map[] = {
	{"Set Config Dirty", DPREC_EVENT_CMDQ_SET_DIRTY},
	{"Wait Stream EOF", DPREC_EVENT_CMDQ_WAIT_STREAM_EOF},
	{"Signal CMDQ Event-Stream EOF", DPREC_EVENT_CMDQ_SET_EVENT_ALLOW},
	{"Flush CMDQ", DPREC_EVENT_CMDQ_FLUSH},
	{"Reset CMDQ", DPREC_EVENT_CMDQ_RESET},
	{"Frame Done", DPREC_EVENT_FRAME_DONE},
	{"Frame Start", DPREC_EVENT_FRAME_START},
};



static MMP_Event dprec_mmp_event_spy(DPREC_LOGGER_ENUM l)
{
	switch (l & 0xffffff) {
	case DPREC_LOGGER_PRIMARY_MUTEX:
		return ddp_mmp_get_events()->primary_sw_mutex;
	case DPREC_LOGGER_PRIMARY_TRIGGER:
		return ddp_mmp_get_events()->primary_trigger;
	case DPREC_LOGGER_PRIMARY_CONFIG:
		return ddp_mmp_get_events()->primary_config;
	case DPREC_LOGGER_PRIMARY_CMDQ_SET_DIRTY:
		return ddp_mmp_get_events()->primary_set_dirty;
	case DPREC_LOGGER_PRIMARY_CMDQ_FLUSH:
		return ddp_mmp_get_events()->primary_cmdq_flush;
	case DPREC_LOGGER_DISPMGR_PREPARE:
		return ddp_mmp_get_events()->session_prepare;
	case DPREC_LOGGER_DISPMGR_SET_INPUT:
		return ddp_mmp_get_events()->session_set_input;
	case DPREC_LOGGER_DISPMGR_TRIGGER:
		return ddp_mmp_get_events()->session_trigger;
	case DPREC_LOGGER_DISPMGR_RELEASE:
		return ddp_mmp_get_events()->session_release;
	case DPREC_LOGGER_DISPMGR_WAIT_VSYNC:
		return ddp_mmp_get_events()->session_wait_vsync;
	case DPREC_LOGGER_DISPMGR_CACHE_SYNC:
		return ddp_mmp_get_events()->primary_cache_sync;
	case DPREC_LOGGER_DSI_EXT_TE:
		return ddp_mmp_get_events()->dsi_te;
	}
	return 0xffff;
}

static void dprec_to_mmp(unsigned int type_logsrc, MMP_LogType mmp_log, unsigned int data1,
			 unsigned data2)
{
	int MMP_Event = dprec_mmp_event_spy(type_logsrc);
	if (MMP_Event < 0xffff)
		MMProfileLogEx(MMP_Event, mmp_log, data1, data2);

}

static const char *_find_module_by_reg_addr(unsigned int reg)
{
	int i = 0;
	unsigned int module_offset = 0x1000;
	unsigned int base = (reg & (~(module_offset - 1)));
	for (i = 0; i < sizeof(reg_map) / sizeof(reg_base_map); i++) {
		if (base == reg_map[i].module_reg_base)
			return reg_map[i].module_name;
	}

	return "unknown";
}

static const char *_get_event_string(DPREC_EVENT event)
{
	int i = 0;
	for (i = 0; i < sizeof(event_map) / sizeof(event_string_map); i++) {
		if (event == event_map[i].event)
			return event_map[i].event_string;
	}

	return "unknown";
}

static unsigned long long get_current_time_us(void)
{
	unsigned long long time = sched_clock();
	struct timeval t;

	/* return do_div(time,1000); */
	return time;

	do_gettimeofday(&t);
	return (t.tv_sec & 0xFFF) * 1000000 + t.tv_usec;
}


#define dprec_string_max_length 512
static unsigned char dprec_string_buffer[dprec_string_max_length] = { 0 };
dprec_logger logger[DPREC_LOGGER_NUM] = { { 0 } };
dprec_logger old_logger[DPREC_LOGGER_NUM] = { { 0 } };

#define dprec_dump_max_length (1024*8*4)
static unsigned char dprec_string_buffer_analysize[dprec_dump_max_length] = { 0 };

static unsigned int analysize_length;

char dprec_error_log_buffer[DPREC_ERROR_LOG_BUFFER_LENGTH];
static dprec_logger_event dprec_vsync_irq_event;
static met_log_map dprec_met_info[DISP_SESSION_MEMORY + 2] = {
	{"UNKWON", 0, 0},
	{"OVL0-DSI", 0, 0},
	{"OVL1-MHL", 0, 0},
	{"OVL1-SMS", 0, 0},
};

int dprec_init(void)
{
	memset((void *)&_control, 0, sizeof(_control));
	memset((void *)&logger, 0, sizeof(logger));
	memset((void *)dprec_error_log_buffer, 0, DPREC_ERROR_LOG_BUFFER_LENGTH);
	ddp_mmp_init();

	dprec_logger_event_init(&dprec_vsync_irq_event, "VSYNC_IRQ", DPREC_LOGGER_LEVEL_SYSTRACE,
				NULL);

	return 0;
}

void dprec_event_op(DPREC_EVENT event)
{
	int len = 0;

	if (_control.overall_switch == 0)
		return;

	len += scnprintf(dprec_string_buffer + len, dprec_string_max_length - len, "[DPREC]");
	len += scnprintf(dprec_string_buffer + len, dprec_string_max_length - len, "[EVENT]");
	len += scnprintf(dprec_string_buffer + len, dprec_string_max_length - len, "[%s]", _get_event_string(event));
	len += scnprintf(dprec_string_buffer + len, dprec_string_max_length - len, "\n");

	pr_debug("%s\n", dprec_string_buffer);
	return;
}


static long long nsec_high(unsigned long long nsec)
{
	if ((long long)nsec < 0) {
		nsec = -nsec;
		do_div(nsec, 1000000);
		return -nsec;
	}
	do_div(nsec, 1000000);

	return nsec;
}

static unsigned long nsec_low(unsigned long long nsec)
{
	unsigned long ret = 0;
	if ((long long)nsec < 0)
		nsec = -nsec;

	ret = do_div(nsec, 1000000);
	return ret / 10000;
}

static long long msec_high(unsigned long long nsec)
{
	if ((long long)nsec < 0) {
		nsec = -nsec;
		do_div(nsec, 1000000000);
		return -nsec;
	}
	do_div(nsec, 1000000000);

	return nsec;
}

static unsigned long msec_low(unsigned long long nsec)
{
	unsigned long ret = 0;
	if ((long long)nsec < 0)
		nsec = -nsec;

	ret = do_div(nsec, 1000000000);
	return ret / 10000000;
}


static const char *dprec_logger_spy(DPREC_LOGGER_ENUM l)
{
	switch (l) {
	case DPREC_LOGGER_PRIMARY_TRIGGER:
		return "Primary Path Trigger";
	case DPREC_LOGGER_PRIMARY_MUTEX:
		return "Primary Path Mutex";
	case DPREC_LOGGER_DISPMGR_WAIT_VSYNC:
		return "Wait VYSNC Signal";
	case DPREC_LOGGER_RDMA0_TRANSFER:
		return "RDMA0 Transfer";
	case DPREC_LOGGER_DSI_EXT_TE:
		return "DSI_EXT_TE";
	case DPREC_LOGGER_ESD_RECOVERY:
		return "ESD Recovery";
	case DPREC_LOGGER_ESD_CHECK:
		return "ESD Check";
	case DPREC_LOGGER_ESD_CMDQ:
		return "ESD CMDQ Keep";
	case DPREC_LOGGER_PRIMARY_CONFIG:
		return "Primary Path Config";
	case DPREC_LOGGER_DISPMGR_PREPARE:
		return "DISPMGR Prepare";
	case DPREC_LOGGER_DISPMGR_CACHE_SYNC:
		return "DISPMGR Cache Sync";
	case DPREC_LOGGER_DISPMGR_SET_INPUT:
		return "DISPMGR Set Input";
	case DPREC_LOGGER_DISPMGR_TRIGGER:
		return "DISPMGR Trigger";
	case DPREC_LOGGER_DISPMGR_RELEASE:
		return "DISPMGR Release";
	case DPREC_LOGGER_PRIMARY_CMDQ_SET_DIRTY:
		return "Primary CMDQ Dirty";
	case DPREC_LOGGER_PRIMARY_CMDQ_FLUSH:
		return "Primary CMDQ Flush";
	case DPREC_LOGGER_PRIMARY_BUFFER_KEEP:
		return "Fence Buffer Keep";
	case DPREC_LOGGER_WDMA_DUMP:
		return "Screen Capture(wdma)";

	default:
		return "unknown";
	}
}

void dprec_logger_trigger(unsigned int type_logsrc, unsigned int val1, unsigned int val2)
{
	unsigned long flags = 0;
	DPREC_LOGGER_ENUM source;
	dprec_logger *l;
	unsigned long long time;

	dprec_to_mmp(type_logsrc, MMProfileFlagPulse, val1, val2);

	source = type_logsrc & 0xffffff;
	spin_lock_irqsave(&gdprec_logger_spinlock, flags);
	l = &logger[source];
	time = get_current_time_us();
	if (l->count == 0) {
		l->ts_start = time;
		l->ts_trigger = time;
		l->period_min_frame = 0xffffffffffffffff;
	} else {
		l->period_frame = time - l->ts_trigger;

		if (l->period_frame > l->period_max_frame)
			l->period_max_frame = l->period_frame;

		if (l->period_frame < l->period_min_frame)
			l->period_min_frame = l->period_frame;

		l->ts_trigger = time;

		if (l->count == 0)
			l->ts_start = l->ts_trigger;

	}
	l->count++;

	if (source == DPREC_LOGGER_DSI_EXT_TE) {
		/* DISPMSG("count=%d, time=%llu, period=%llu, max=%llu,min=%llu,start=%llu,trigger=%llu\n", l->count,
		 * time, l->period_frame, l->period_max_frame, l->period_min_frame, l->ts_start, l->ts_trigger); */
		/* DISPMSG("count=%d,period=%lld.%02lums,max=%lld.%02lums\n", l->count,
		 * SPLIT_NS(l->period_frame), SPLIT_NS(l->period_max_frame)); */
	}

	if (source == DPREC_LOGGER_OVL_FRAME_COMPLETE_1SECOND ||
	    source == DPREC_LOGGER_PQ_TRIGGER_1SECOND) {
		if (l->ts_trigger - l->ts_start > 1000 * 1000 * 1000) {
			old_logger[source] = *l;
			memset(l, 0, sizeof(logger[0]));
		}
	}

	spin_unlock_irqrestore(&gdprec_logger_spinlock, flags);
}

unsigned long long dprec_logger_get_current_hold_period(unsigned int type_logsrc)
{
	unsigned long long period = 0;
	unsigned long flags = 0;

	unsigned long long time = get_current_time_us();
	DPREC_LOGGER_ENUM source = type_logsrc & 0xffffff;
	dprec_logger *l;

	spin_lock_irqsave(&gdprec_logger_spinlock, flags);
	l = &logger[source];
	if (l->ts_trigger)
		period = (time - l->ts_trigger);
	else
		period = 0;

	spin_unlock_irqrestore(&gdprec_logger_spinlock, flags);

	return period;
}

void dprec_logger_start(unsigned int type_logsrc, unsigned int val1, unsigned int val2)
{
	unsigned long flags = 0;
	DPREC_LOGGER_ENUM source;
	dprec_logger *l;
	unsigned long long time;

	dprec_to_mmp(type_logsrc, MMProfileFlagStart, val1, val2);

	source = type_logsrc & 0xffffff;
	spin_lock_irqsave(&gdprec_logger_spinlock, flags);
	l = &logger[source];
	time = get_current_time_us();

	if (l->count == 0) {
		l->ts_start = time;
		l->period_min_frame = 0xffffffffffffffff;
	}

	l->ts_trigger = time;

	if (source == DPREC_LOGGER_RDMA0_TRANSFER_1SECOND) {
		unsigned long long rec_period = l->ts_trigger - l->ts_start;

		if (rec_period > 1000 * 1000 * 1000) {
			old_logger[DPREC_LOGGER_RDMA0_TRANSFER_1SECOND] = *l;
			memset(l, 0, sizeof(logger[0]));
			l->ts_start = time;
			l->period_min_frame = 0xffffffffffffffff;
		}
	}

	spin_unlock_irqrestore(&gdprec_logger_spinlock, flags);
}

void dprec_logger_done(unsigned int type_logsrc, unsigned int val1, unsigned int val2)
{
	unsigned long flags = 0;
	DPREC_LOGGER_ENUM source;
	dprec_logger *l;
	unsigned long long time;

	dprec_to_mmp(type_logsrc, MMProfileFlagEnd, val1, val2);

	source = type_logsrc & 0xffffff;
	spin_lock_irqsave(&gdprec_logger_spinlock, flags);
	l = &logger[source];
	time = get_current_time_us();

	if (l->ts_start == 0)
		goto done;


	l->period_frame = time - l->ts_trigger;

	if (l->period_frame > l->period_max_frame)
		l->period_max_frame = l->period_frame;

	if (l->period_frame < l->period_min_frame)
		l->period_min_frame = l->period_frame;

	l->period_total += l->period_frame;
	l->count++;

done:
	spin_unlock_irqrestore(&gdprec_logger_spinlock, flags);
}

void dprec_logger_event_init(dprec_logger_event *p, char *name, uint32_t level,
			     MMP_Event *mmp_root)
{
	if (p) {
		scnprintf(p->name, sizeof(p->name) / sizeof(p->name[0]), name);
		if (mmp_root)
			p->mmp = MMProfileRegisterEvent(*mmp_root, name);
		else
			p->mmp = MMProfileRegisterEvent(ddp_mmp_get_events()->DDP, name);

		MMProfileEnableEventRecursive(p->mmp, 1);

		p->level = level;

		memset((void *)&p->logger, 0, sizeof(p->logger));
		DISPDBG("dprec logger event init, name=%s, level=0x%08x\n", name, level);
	}
}

#ifdef CONFIG_TRACING

static unsigned long __read_mostly tracing_mark_write_addr;
static inline void __mt_update_tracing_mark_write_addr(void)
{
	if (unlikely(0 == tracing_mark_write_addr))
		tracing_mark_write_addr = kallsyms_lookup_name("tracing_mark_write");
}

static inline void mmp_kernel_trace_begin(char *name)
{
	__mt_update_tracing_mark_write_addr();
	preempt_disable();
	event_trace_printk(tracing_mark_write_addr, "B|%d|%s\n", current->tgid, name);
	preempt_enable();
}

static inline void mmp_kernel_trace_counter(char *name, int count)
{
	__mt_update_tracing_mark_write_addr();
	preempt_disable();
	event_trace_printk(tracing_mark_write_addr, "C|%d|%s|%d\n",
			   in_interrupt() ? -1 : current->tgid, name, count);
	preempt_enable();
}

static inline void mmp_kernel_trace_end(void)
{
	__mt_update_tracing_mark_write_addr();
	preempt_disable();
	event_trace_printk(tracing_mark_write_addr, "E\n");
	preempt_enable();
}

void dprec_logger_frame_seq_begin(unsigned int session_id, unsigned frm_sequence)
{
	unsigned device_type = DISP_SESSION_TYPE(session_id);
	if (frm_sequence <= 0 || session_id <= 0)
		return;
	if (device_type > DISP_SESSION_MEMORY) {
		pr_err("seq_begin session_id(0x%x) error, seq(%d)\n", session_id, frm_sequence);
		return;
	}

	if (dprec_met_info[device_type].begin_frm_seq != frm_sequence) {
		__mt_update_tracing_mark_write_addr();
		preempt_disable();
		event_trace_printk(tracing_mark_write_addr, "S|%d|%s|%d\n", current->tgid,
				   dprec_met_info[device_type].log_name, frm_sequence);
		preempt_enable();
		dprec_met_info[device_type].begin_frm_seq = frm_sequence;
	}
}

void dprec_logger_frame_seq_end(unsigned int session_id, unsigned frm_sequence)
{
	unsigned device_type = DISP_SESSION_TYPE(session_id);
	if (frm_sequence <= 0 || session_id <= 0)
		return;
	if (device_type > DISP_SESSION_MEMORY) {
		pr_err("seq_end session_id(0x%x) , seq(%d)\n", session_id, frm_sequence);
		return;
	}

	if (dprec_met_info[device_type].end_frm_seq != frm_sequence) {
		__mt_update_tracing_mark_write_addr();
		preempt_disable();
		event_trace_printk(tracing_mark_write_addr, "F|%d|%s|%d\n", current->tgid,
				   dprec_met_info[device_type].log_name, frm_sequence);
		preempt_enable();
		dprec_met_info[device_type].end_frm_seq = frm_sequence;
	}
}

#else
void dprec_logger_frame_seq_begin(unsigned int session_id, unsigned frm_sequence)
{

}

void dprec_logger_frame_seq_end(unsigned int session_id, unsigned frm_sequence)
{

}
#endif

void dprec_start(dprec_logger_event *event, unsigned int val1, unsigned int val2)
{
	if (event) {
		if (event->level & DPREC_LOGGER_LEVEL_MMP)
			MMProfileLogEx(event->mmp, MMProfileFlagStart, val1, val2);

		if (event->level & DPREC_LOGGER_LEVEL_LOGGER) {
			unsigned long flags = 0;
			dprec_logger *l;
			unsigned long long time;

			spin_lock_irqsave(&gdprec_logger_spinlock, flags);
			l = &(event->logger);
			time = get_current_time_us();
			if (l->count == 0) {
				l->ts_start = time;
				l->period_min_frame = 0xffffffffffffffff;
			}

			l->ts_trigger = time;

			spin_unlock_irqrestore(&gdprec_logger_spinlock, flags);
		}
		if (event->level & DPREC_LOGGER_LEVEL_MOBILE_LOG)
			pr_debug("DISP/%s start,0x%08x,0x%08x\n", event->name, val1, val2);

		if (event->level & DPREC_LOGGER_LEVEL_UART_LOG)
			pr_debug("DISP/%s start,0x%08x,0x%08x\n", event->name, val1, val2);

#ifdef CONFIG_TRACING
		if (event->level & DPREC_LOGGER_LEVEL_SYSTRACE && _control.systrace) {
			char name[256];

			scnprintf(name, sizeof(name) / sizeof(name[0]), "K_%s_0x%x_0x%x",
				  event->name, val1, val2);

			mmp_kernel_trace_begin(name);
			/* trace_printk("B|%d|%s\n", current->pid, event->name); */
		}
#endif
	}
}

void dprec_done(dprec_logger_event *event, unsigned int val1, unsigned int val2)
{
	if (event) {
		if (event->level & DPREC_LOGGER_LEVEL_MMP)
			MMProfileLogEx(event->mmp, MMProfileFlagEnd, val1, val2);

		if (event->level & DPREC_LOGGER_LEVEL_LOGGER) {
			unsigned long flags = 0;
			dprec_logger *l;
			unsigned long long time;

			spin_lock_irqsave(&gdprec_logger_spinlock, flags);
			l = &(event->logger);
			time = get_current_time_us();

			if (l->ts_start != 0) {
				l->period_frame = time - l->ts_trigger;

				if (l->period_frame > l->period_max_frame)
					l->period_max_frame = l->period_frame;

				if (l->period_frame < l->period_min_frame)
					l->period_min_frame = l->period_frame;

				l->ts_trigger = 0;
				l->period_total += l->period_frame;
				l->count++;
			}

			spin_unlock_irqrestore(&gdprec_logger_spinlock, flags);
		}
		if (event->level & DPREC_LOGGER_LEVEL_MOBILE_LOG)
			pr_debug("DISP/%s done,0x%08x,0x%08x\n", event->name, val1, val2);

		if (event->level & DPREC_LOGGER_LEVEL_UART_LOG)
			pr_debug("DISP/%s done,0x%08x,0x%08x\n", event->name, val1, val2);

#ifdef CONFIG_TRACING
		if (event->level & DPREC_LOGGER_LEVEL_SYSTRACE && _control.systrace) {
			mmp_kernel_trace_end();
			/* trace_printk("E|%s\n", event->name); */
		}
#endif
	}
}

void dprec_trigger(dprec_logger_event *event, unsigned int val1, unsigned int val2)
{
	if (event) {
		if (event->level & DPREC_LOGGER_LEVEL_MMP)
			MMProfileLogEx(event->mmp, MMProfileFlagPulse, val1, val2);

		if (event->level & DPREC_LOGGER_LEVEL_LOGGER) {
			unsigned long flags = 0;
			dprec_logger *l;
			unsigned long long time;

			spin_lock_irqsave(&gdprec_logger_spinlock, flags);
			l = &(event->logger);
			time = get_current_time_us();
			if (l->count == 0) {
				l->ts_start = time;
				l->ts_trigger = time;
				l->period_min_frame = 0xffffffffffffffff;
			} else {
				l->period_frame = time - l->ts_trigger;

				if (l->period_frame > l->period_max_frame)
					l->period_max_frame = l->period_frame;

				if (l->period_frame < l->period_min_frame)
					l->period_min_frame = l->period_frame;

				l->ts_trigger = time;

				if (l->count == 0)
					l->ts_start = l->ts_trigger;

			}

			l->count++;
			spin_unlock_irqrestore(&gdprec_logger_spinlock, flags);
		}
		if (event->level & DPREC_LOGGER_LEVEL_MOBILE_LOG)
			pr_debug("DISP/%s trigger,0x%08x,0x%08x\n", event->name, val1, val2);

		if (event->level & DPREC_LOGGER_LEVEL_UART_LOG)
			pr_debug("DISP/%s trigger,0x%08x,0x%08x\n", event->name, val1, val2);

#ifdef CONFIG_TRACING
		if (event->level & DPREC_LOGGER_LEVEL_SYSTRACE && _control.systrace) {
			char name[256];
			scnprintf(name, sizeof(name) / sizeof(name[0]), "K_%s_0x%x_0x%x",
				  event->name, val1, val2);
			mmp_kernel_trace_begin(name);
			mmp_kernel_trace_end();
			/* trace_printk("B|%d|%s\n", current->pid, event->name); */
		}
#endif
	}
}


void dprec_submit(dprec_logger_event *event, unsigned int val1, unsigned int val2)
{
	if (event) {
		if (event->level & DPREC_LOGGER_LEVEL_MMP)
			MMProfileLogEx(event->mmp, MMProfileFlagPulse, val1, val2);

		if (event->level & DPREC_LOGGER_LEVEL_LOGGER)
			;
		if (event->level & DPREC_LOGGER_LEVEL_MOBILE_LOG)
			pr_debug("DISP/%s trigger,0x%08x,0x%08x\n", event->name, val1, val2);

		if (event->level & DPREC_LOGGER_LEVEL_UART_LOG)
			pr_debug("DISP/%s trigger,0x%08x,0x%08x\n", event->name, val1, val2);

	}
}

void dprec_logger_submit(unsigned int type_logsrc, unsigned long long period,
			 unsigned int fence_idx)
{
	unsigned long flags = 0;
	DPREC_LOGGER_ENUM source = type_logsrc & 0xffffff;
	dprec_logger *l;

	spin_lock_irqsave(&gdprec_logger_spinlock, flags);
	l = &logger[source];

	l->period_frame = period;

	if (l->period_frame > l->period_max_frame)
		l->period_max_frame = l->period_frame;

	if (l->period_frame < l->period_min_frame)
		l->period_min_frame = l->period_frame;

	l->period_total += l->period_frame;
	l->count++;

	spin_unlock_irqrestore(&gdprec_logger_spinlock, flags);

	dprec_to_mmp(type_logsrc, MMProfileFlagPulse, (unsigned int)l->period_max_frame, fence_idx);
}

static unsigned long long ts_dprec_reset;
void dprec_logger_reset_all(void)
{
	int i = 0;

	for (i = 0; i < sizeof(logger) / sizeof(logger[0]); i++)
		dprec_logger_reset(i);
	ts_dprec_reset = get_current_time_us();
}

void dprec_logger_reset(DPREC_LOGGER_ENUM source)
{
	dprec_logger *l = &logger[source];

	memset((void *)l, 0, sizeof(dprec_logger));
	l->period_min_frame = 10000000;
}

int dprec_logger_get_result_string(DPREC_LOGGER_ENUM source, char *stringbuf, int strlen)
{
	unsigned long flags = 0;
	int len = 0;
	dprec_logger *l = &logger[source];
	unsigned long long total = 0;
	unsigned long long avg;
	unsigned long long count;
	unsigned long long fps_high = 0;
	unsigned long fps_low = 0;

	spin_lock_irqsave(&gdprec_logger_spinlock, flags);
	/* calculate average period need use available total period */
	if (l->period_total)
		total = l->period_total;
	else
		total = l->ts_trigger - l->ts_start;

	avg = total ? total : 1;
	count = l->count ? l->count : 1;
	do_div(avg, count);

	/* calculate fps need use whole time period */
	/* total = l->ts_trigger - l->ts_start; */
	fps_high = l->count;
	do_div(total, 1000 * 1000 * 1000);
	if (total == 0)
		total = 1;
	fps_high *= 100;
	do_div(fps_high, total);
	fps_low = do_div(fps_high, 100);
	len += scnprintf(stringbuf + len, strlen - len,
		      "|%-24s|%8llu |%8lld.%02ld |%8llu.%02ld |%8llu.%02ld |%8llu.%02ld |\n",
		      dprec_logger_spy(source), l->count, fps_high, fps_low,
		      nsec_high(avg), nsec_low(avg),
		      nsec_high(l->period_max_frame), nsec_low(l->period_max_frame),
		      nsec_high(l->period_min_frame), nsec_low(l->period_min_frame)
			);
	spin_unlock_irqrestore(&gdprec_logger_spinlock, flags);
	return len;
}

int dprec_logger_get_result_string_all(char *stringbuf, int strlen)
{
	int n = 0;
	int i = 0;

	n += scnprintf(stringbuf + n, strlen - n,
		       "|**** Display Driver Statistic Information Dump ****\n");
	n += scnprintf(stringbuf + n, strlen - n, "|Timestamp Begin=%llu.%03lds, End=%llu.%03lds\n",
		       msec_high(ts_dprec_reset), msec_low(ts_dprec_reset),
		       msec_high(get_current_time_us()), msec_low(get_current_time_us())
		       );
	n += scnprintf(stringbuf + n, strlen - n,
		       "|------------------------+---------+------------+------------+------------+------------|\n");
	n += scnprintf(stringbuf + n, strlen - n,
		       "|Event                   | count   | fps        |average(ms) | max(ms)    | min(ms)    |\n");
	n += scnprintf(stringbuf + n, strlen - n,
		       "|------------------------+---------+------------+------------+------------+------------|\n");
	for (i = 0; i < sizeof(logger) / sizeof(logger[0]); i++)
		n += dprec_logger_get_result_string(i, stringbuf + n, strlen - n);

	n += scnprintf(stringbuf + n, strlen - n,
		       "|------------------------+---------+------------+------------+------------+------------|\n");
	n += scnprintf(stringbuf + n, strlen - n, "dprec error log buffer\n");

	return n;
}


int dprec_logger_get_result_value(DPREC_LOGGER_ENUM source, fpsEx *fps)
{
	unsigned long flags = 0;
	int len = 0;
	dprec_logger *l = &logger[source];
	unsigned long long fps_high = 0;
	unsigned long fps_low = 0;
	unsigned long long avg;
	unsigned long long count;
	unsigned long long total = 0;

	spin_lock_irqsave(&gdprec_logger_spinlock, flags);
	if (source == DPREC_LOGGER_RDMA0_TRANSFER_1SECOND ||
	    source == DPREC_LOGGER_OVL_FRAME_COMPLETE_1SECOND ||
	    source == DPREC_LOGGER_PQ_TRIGGER_1SECOND)
		l = &old_logger[source];

	/* calculate average period need use available total period */
	if (l->period_total)
		total = l->period_total;
	else
		total = l->ts_trigger - l->ts_start;

	avg = total ? total : 1;
	count = l->count ? l->count : 1;
	do_div(avg, count);

	/* calculate fps need use whole time period */
	total = l->ts_trigger - l->ts_start;
	if (source == DPREC_LOGGER_RDMA0_TRANSFER_1SECOND ||
	    source == DPREC_LOGGER_OVL_FRAME_COMPLETE_1SECOND ||
	    source == DPREC_LOGGER_PQ_TRIGGER_1SECOND) {
		fps_high = l->count * 1000;
		do_div(total, 1000 * 1000);
	} else {
		fps_high = l->count;
		do_div(total, 1000 * 1000 * 1000);
	}
	if (total == 0)
		total = 1;
	fps_low = do_div(fps_high, total);

	if (source == DPREC_LOGGER_RDMA0_TRANSFER_1SECOND ||
	    source == DPREC_LOGGER_OVL_FRAME_COMPLETE_1SECOND ||
	    source == DPREC_LOGGER_PQ_TRIGGER_1SECOND) {
		fps_low *= 1000;
		do_div(fps_low, total);
	}
	if (fps != NULL) {
		fps->fps = fps_high;
		fps->fps_low = fps_low;
		fps->count = count;
		fps->avg = avg;
		fps->max_period = l->period_max_frame;
		fps->min_period = l->period_min_frame;
	}

	spin_unlock_irqrestore(&gdprec_logger_spinlock, flags);
	return len;
}

typedef enum {
	DPREC_REG_OP = 1,
	DPREC_CMDQ_EVENT,
	DPREC_ERROR,
	DPREC_FENCE
} dprec_record_type;

typedef struct {
	unsigned int use_cmdq;
	unsigned int reg;
	unsigned int val;
	unsigned int mask;
} dprec_reg_op_record;

typedef struct {
	unsigned int fence_fd;
	unsigned int buffer_idx;
	unsigned int ion_fd;
} dprec_fence_record;

typedef struct {
	dprec_record_type type;
	unsigned int ts;
	union {
		dprec_fence_record fence;
		dprec_reg_op_record reg_op;
	} rec;
} dprec_record;

#if 0 /* defined but not used */
static int rdma0_done_cnt;
#endif

void dprec_stub_irq(unsigned int irq_bit)
{
	/* DISP_REG_SET(NULL,DISP_REG_CONFIG_MUTEX_INTEN,0xffffffff); */
	if (irq_bit == DDP_IRQ_DSI0_EXT_TE) {
		dprec_logger_trigger(DPREC_LOGGER_DSI_EXT_TE, irq_bit, 0);
	} else if (irq_bit == DDP_IRQ_RDMA0_START) {
		dprec_logger_start(DPREC_LOGGER_RDMA0_TRANSFER, irq_bit, 0);
		dprec_logger_start(DPREC_LOGGER_RDMA0_TRANSFER_1SECOND, irq_bit, 0);
	} else if (irq_bit == DDP_IRQ_RDMA0_DONE) {
		dprec_logger_done(DPREC_LOGGER_RDMA0_TRANSFER, irq_bit, 0);
		dprec_logger_done(DPREC_LOGGER_RDMA0_TRANSFER_1SECOND, irq_bit, 0);
	} else if (irq_bit == DDP_IRQ_OVL0_FRAME_COMPLETE) {
		dprec_logger_trigger(DPREC_LOGGER_OVL_FRAME_COMPLETE_1SECOND, irq_bit, 0);
	}
	/* DISPMSG("irq:0x%08x\n", irq_bit); */
}

static unsigned int vsync_cnt;

void dprec_stub_event(DISP_PATH_EVENT event)
{
	/* DISP_REG_SET(NULL,DISP_REG_CONFIG_MUTEX_INTEN,0xffffffff); */
	if (event == DISP_PATH_EVENT_IF_VSYNC) {
		vsync_cnt++;
		/* MMProfileLogEx(ddp_mmp_get_events()->vsync_count, MMProfileFlagPulse, vsync_cnt, 0); */
		dprec_start(&dprec_vsync_irq_event, 0, 0);
		dprec_done(&dprec_vsync_irq_event, 0, 0);
	}
}

unsigned int dprec_get_vsync_count(void)
{
	return vsync_cnt;
}

void dprec_reg_op(void *cmdq, unsigned int reg, unsigned int val, unsigned int mask)
{
	int len = 0;

	if (!cmdq)
		MMProfileLogEx(ddp_mmp_get_events()->dprec_cpu_write_reg, MMProfileFlagPulse, reg, val);

	if (cmdq) {
		if (mask) {
			DISPPR_HWOP("%s/0x%08x/0x%08x=0x%08x&0x%08x\n",
				    _find_module_by_reg_addr(reg), (unsigned int)cmdq, reg, val,
				    mask);
		} else {
			DISPPR_HWOP("%s/0x%08x/0x%08x=0x%08x\n", _find_module_by_reg_addr(reg),
				    (unsigned int)cmdq, reg, val);
		}

	} else {
		if (mask)
			DISPPR_HWOP("%s/%08x=%08x&%08x\n", _find_module_by_reg_addr(reg), reg, val, mask);
		else
			DISPPR_HWOP("%s/%08x=%08x\n", _find_module_by_reg_addr(reg), reg, val);

	}

	if (_control.overall_switch == 0)
		return;


	len += scnprintf(dprec_string_buffer + len, dprec_string_max_length - len, "[DPREC]");
	len += scnprintf(dprec_string_buffer + len, dprec_string_max_length - len, "[%s]",
			 _find_module_by_reg_addr(reg));
	len += scnprintf(dprec_string_buffer + len, dprec_string_max_length - len, "[%s]",
			 cmdq ? "CMDQ" : "CPU");

	if (cmdq)
		len += scnprintf(dprec_string_buffer + len, dprec_string_max_length - len, "[0x%p]", cmdq);

	len += scnprintf(dprec_string_buffer + len, dprec_string_max_length - len, "0x%08x=0x%08x",
			 reg, val);

	if (mask)
		len += scnprintf(dprec_string_buffer + len, dprec_string_max_length - len, "&0x%08x", mask);

	len += scnprintf(dprec_string_buffer + len, dprec_string_max_length - len, "\n");

	pr_debug("%s\n", dprec_string_buffer);

	if (_control.cmm_dump) {
		/*pr_debug("[CMM]D.S SD:0x%08x %\LE %\LONG 0x%08x; write %s\n",
		 * (_control.cmm_dump_use_va)?reg:(reg&0x1fffffff),
		 * mask?(val|mask):val,_find_module_by_reg_addr(reg)); */
	}

	return;
}

void dprec_logger_vdump(const char *fmt, ...)
{
	va_list vargs;
	int tmp;

	va_start(vargs, fmt);

	if (analysize_length >= dprec_dump_max_length - 10)
		return;

	tmp = vscnprintf(dprec_string_buffer_analysize + analysize_length,
		      dprec_dump_max_length - analysize_length, fmt, vargs);

	analysize_length += tmp;
	if (analysize_length > dprec_dump_max_length)
		analysize_length = dprec_dump_max_length;

	va_end(vargs);
}

void dprec_logger_dump(char *string)
{
	dprec_logger_vdump(string);
}

void dprec_logger_dump_reset(void)
{
	analysize_length = 0;

	memset(dprec_string_buffer_analysize, 0, sizeof(dprec_string_buffer_analysize));
}

char *dprec_logger_get_dump_addr()
{
	return dprec_string_buffer_analysize;
}

unsigned int dprec_logger_get_dump_len(void)
{
	pr_debug("dump_len %d\n", analysize_length);

	return analysize_length;
}

typedef enum {
	DPREC_DEBUG_BIT_OVERALL_SWITCH = 0,
	DPREC_DEBUG_BIT_CMM_DUMP_SWITCH,
	DPREC_DEBUG_BIT_CMM_DUMP_VA,
	DPREC_DEBUG_BIT_SYSTRACE,
} DPREC_DEBUG_BIT_ENUM;

int dprec_handle_option(unsigned int option)
{
	_control.overall_switch = (option & (1 << DPREC_DEBUG_BIT_OVERALL_SWITCH));
	_control.cmm_dump = (option & (1 << DPREC_DEBUG_BIT_CMM_DUMP_SWITCH));
	_control.cmm_dump_use_va = (option & (1 << DPREC_DEBUG_BIT_CMM_DUMP_VA));
	_control.systrace = (option & (1 << DPREC_DEBUG_BIT_SYSTRACE));
	DISPMSG("dprec control=%p\n", &_control);

	return 0;
}

/* return true if overall_switch is set. this will dump all register setting by default. */
int dprec_option_enabled(void)
{
	return _control.overall_switch;
}

#if 0 /* defined but not used */
static int dprec_state_machine_op(DPREC_STM_EVENT op)
{

}
#endif

int dprec_mmp_dump_ovl_layer(OVL_CONFIG_STRUCT *ovl_layer, unsigned int l,
			     unsigned int session /*1:primary, 2:external, 3:memory */)
{
	if (gCapturePriLayerEnable) {
		if (gCapturePriLayerNum >= primary_display_get_max_layer())
			ddp_mmp_ovl_layer(ovl_layer, gCapturePriLayerDownX, gCapturePriLayerDownY, session);
		else if (gCapturePriLayerNum == l)
			ddp_mmp_ovl_layer(ovl_layer, gCapturePriLayerDownX, gCapturePriLayerDownY, session);

		return 0;
	}
	return -1;
}

int dprec_mmp_dump_wdma_layer(void *wdma_layer, unsigned int wdma_num)
{
	if (gCaptureWdmaLayerEnable) {
		ddp_mmp_wdma_layer((WDMA_CONFIG_STRUCT *) wdma_layer, wdma_num,
				   gCapturePriLayerDownX, gCapturePriLayerDownY);
	}
	return -1;
}

int dprec_mmp_dump_rdma_layer(void *rdma_layer, unsigned int rdma_num)
{
	if (gCaptureRdmaLayerEnable) {
		ddp_mmp_rdma_layer((RDMA_CONFIG_STRUCT *) rdma_layer, rdma_num,
				   gCapturePriLayerDownX, gCapturePriLayerDownY);
	}
	return -1;
}

struct logger_buffer {
	char **buffer_ptr;
	unsigned int len;
	unsigned int id;
	const unsigned int count;
	const unsigned int size;
};

static char **err_buffer;
static char **fence_buffer;
static char **dbg_buffer;
static char **dump_buffer;
static char **status_buffer;
static struct logger_buffer dprec_logger_buffer[DPREC_LOGGER_PR_NUM] = {
	{0, 0, 0, ERROR_BUFFER_COUNT, LOGGER_BUFFER_SIZE},
	{0, 0, 0, FENCE_BUFFER_COUNT, LOGGER_BUFFER_SIZE},
	{0, 0, 0, DEBUG_BUFFER_COUNT, LOGGER_BUFFER_SIZE},
	{0, 0, 0, DUMP_BUFFER_COUNT, LOGGER_BUFFER_SIZE},
	{0, 0, 0, STATUS_BUFFER_COUNT, LOGGER_BUFFER_SIZE},
};
bool is_buffer_init;
char *debug_buffer = NULL;

void init_log_buffer(void)
{
	int i, buf_size, buf_idx;
	char *temp_buf;

	/*
	1. Allocate debug buffer. This buffer used to store the output data.
	*/
	debug_buffer = kzalloc(sizeof(char) * DEBUG_BUFFER_SIZE, GFP_KERNEL);
	if (!debug_buffer)
		goto err;

	/*
	2. Allocate Error, Fence, Debug and Dump log buffer slot
	*/
	err_buffer = kzalloc(sizeof(char *) * ERROR_BUFFER_COUNT, GFP_KERNEL);
	if (!err_buffer)
		goto err;
	fence_buffer = kzalloc(sizeof(char *) * FENCE_BUFFER_COUNT, GFP_KERNEL);
	if (!fence_buffer)
		goto err;
	dbg_buffer = kzalloc(sizeof(char *) * DEBUG_BUFFER_COUNT, GFP_KERNEL);
	if (!dbg_buffer)
		goto err;
	dump_buffer = kzalloc(sizeof(char *) * DUMP_BUFFER_COUNT, GFP_KERNEL);
	if (!dump_buffer)
		goto err;
	status_buffer = kzalloc(sizeof(char *) * DUMP_BUFFER_COUNT, GFP_KERNEL);
	if (!status_buffer)
		goto err;

	/*
	3. Allocate log ring buffer.
	*/
	buf_size = sizeof(char) * (DEBUG_BUFFER_SIZE - 4096);
	temp_buf = kzalloc(buf_size, GFP_KERNEL);
	if (!temp_buf)
		goto err;

	/*
	4. Dispatch log ring buffer to each buffer slot
	*/
	buf_idx = 0;
	for (i = 0 ; i < ERROR_BUFFER_COUNT ; i++) {
		err_buffer[i] = (temp_buf + buf_idx * LOGGER_BUFFER_SIZE);
		buf_idx++;
	}
	dprec_logger_buffer[0].buffer_ptr = err_buffer;

	for (i = 0 ; i < FENCE_BUFFER_COUNT ; i++) {
		fence_buffer[i] = (temp_buf + buf_idx * LOGGER_BUFFER_SIZE);
		buf_idx++;
	}
	dprec_logger_buffer[1].buffer_ptr = fence_buffer;

	for (i = 0 ; i < DEBUG_BUFFER_COUNT ; i++) {
		dbg_buffer[i] = (temp_buf + buf_idx * LOGGER_BUFFER_SIZE);
		buf_idx++;
	}
	dprec_logger_buffer[2].buffer_ptr = dbg_buffer;

	for (i = 0 ; i < DUMP_BUFFER_COUNT ; i++) {
		dump_buffer[i] = (temp_buf + buf_idx * LOGGER_BUFFER_SIZE);
		buf_idx++;
	}
	dprec_logger_buffer[3].buffer_ptr = dump_buffer;

	for (i = 0 ; i < STATUS_BUFFER_COUNT ; i++) {
		status_buffer[i] = (temp_buf + buf_idx * LOGGER_BUFFER_SIZE);
		buf_idx++;
	}
	dprec_logger_buffer[4].buffer_ptr = status_buffer;

	is_buffer_init = true;
	pr_warn("[DISP]%s success\n", __func__);
	return;
err:
	err_buffer = 0;
	fence_buffer = 0;
	dbg_buffer = 0;
	dump_buffer = 0;
	debug_buffer = 0;
	status_buffer = 0;
	pr_err("[DISP]%s: log buffer allocation fail\n", __func__);
}

void get_disp_err_buffer(unsigned long *addr, unsigned long *size, unsigned long *start)
{
	*addr = 0;
	*size = 0;
	*start = 0;
}

void get_disp_fence_buffer(unsigned long *addr, unsigned long *size, unsigned long *start)
{
	*addr = 0;
	*size = 0;
	*start = 0;
}

void get_disp_dbg_buffer(unsigned long *addr, unsigned long *size, unsigned long *start)
{
	if (is_buffer_init) {
		*addr = (unsigned long)err_buffer[0];
		*size = (DEBUG_BUFFER_SIZE - 4096);
		*start = 0;
	} else {
		*addr = 0;
		*size = 0;
		*start = 0;
	}
}

void get_disp_dump_buffer(unsigned long *addr, unsigned long *size, unsigned long *start)
{
	*addr = 0;
	*size = 0;
	*start = 0;
}

static DEFINE_SPINLOCK(dprec_logger_spinlock);

int dprec_logger_pr(unsigned int type, char *fmt, ...)
{
	int n = 0;
	unsigned long flags = 0;
	uint64_t time = get_current_time_us();
	unsigned long rem_nsec;
	char **buf_arr;
	char *buf = NULL;
	int len = 0;

	if (type >= DPREC_LOGGER_PR_NUM)
		return -1;

	if (!is_buffer_init)
		return -1;

	spin_lock_irqsave(&dprec_logger_spinlock, flags);
	if (dprec_logger_buffer[type].len < 128) {
		dprec_logger_buffer[type].id++;
		dprec_logger_buffer[type].id = dprec_logger_buffer[type].id % dprec_logger_buffer[type].count;
		dprec_logger_buffer[type].len = dprec_logger_buffer[type].size;
	}
	buf_arr = dprec_logger_buffer[type].buffer_ptr;
	buf = buf_arr[dprec_logger_buffer[type].id] +
		dprec_logger_buffer[type].size - dprec_logger_buffer[type].len;
	len = dprec_logger_buffer[type].len;

	if (buf) {
		va_list args;

		rem_nsec = do_div(time, 1000000000);
		n += snprintf(buf + n, len - n, "[%5lu.%06lu]", (unsigned long)time,
			      rem_nsec / 1000);

		va_start(args, fmt);
		n += vscnprintf(buf + n, len - n, fmt, args);
		va_end(args);
	}

	dprec_logger_buffer[type].len -= n;
	spin_unlock_irqrestore(&dprec_logger_spinlock, flags);

	return n;
}

char *get_dprec_status_ptr(int buffer_idx)
{
	if (buffer_idx < dprec_logger_buffer[DPREC_LOGGER_STATUS].count)
		return dprec_logger_buffer[DPREC_LOGGER_STATUS].buffer_ptr[buffer_idx];
	else
		return NULL;
}

static char *_logger_pr_type_spy(DPREC_LOGGER_PR_TYPE type)
{
	switch (type) {
	case DPREC_LOGGER_ERROR:
		return "error";
	case DPREC_LOGGER_FENCE:
		return "fence";
	case DPREC_LOGGER_DEBUG:
		return "dbg";
	case DPREC_LOGGER_DUMP:
		return "dump";
	case DPREC_LOGGER_STATUS:
		return "status";
	default:
		return "unknown";
	}
}

int dprec_logger_get_buf(DPREC_LOGGER_PR_TYPE type, char *stringbuf, int len)
{
	int n = 0;
	int i;
	int c = dprec_logger_buffer[type].id;
	char **buf_arr;

	if (type >= DPREC_LOGGER_PR_NUM || len < 0)
		return 0;

	if (!is_buffer_init)
		return 0;

	buf_arr = dprec_logger_buffer[type].buffer_ptr;

	for (i = 0; i < dprec_logger_buffer[type].count; i++) {
		c++;
		c %= dprec_logger_buffer[type].count;
		n += scnprintf(stringbuf + n, len - n, "dprec log buffer[%s][%d]\n",
					   _logger_pr_type_spy(type), c);
		n += scnprintf(stringbuf + n, len - n, "%s\n", buf_arr[c]);

	}

	return n;
}

#else

unsigned int gCapturePriLayerEnable = 0;
unsigned int gCaptureWdmaLayerEnable = 0;
unsigned int gCapturePriLayerDownX = 20;
unsigned int gCapturePriLayerDownY = 20;
unsigned int gCapturePriLayerNum = 4;


dprec_logger logger[DPREC_LOGGER_NUM] = { { 0 } };

unsigned int dprec_error_log_len = 0;
unsigned int dprec_error_log_buflen = DPREC_ERROR_LOG_BUFFER_LENGTH;
unsigned int dprec_error_log_id = 0;


int dprec_init(void)
{
	return 0;
}

void dprec_event_op(DPREC_EVENT event)
{
}

void dprec_logger_trigger(unsigned int type_logsrc, unsigned int val1, unsigned int val2)
{
}

unsigned long long dprec_logger_get_current_hold_period(unsigned int type_logsrc)
{
	return 0;
}

void dprec_logger_start(unsigned int type_logsrc, unsigned int val1, unsigned int val2)
{
}

void dprec_logger_done(unsigned int type_logsrc, unsigned int val1, unsigned int val2)
{
}

void dprec_logger_event_init(dprec_logger_event *p, char *name, uint32_t level,
			     MMP_Event *mmp_root)
{
}

void dprec_logger_frame_seq_begin(unsigned int session_id, unsigned frm_sequence)
{
}

void dprec_logger_frame_seq_end(unsigned int session_id, unsigned frm_sequence)
{
}

void dprec_start(dprec_logger_event *event, unsigned int val1, unsigned int val2)
{
}

void dprec_done(dprec_logger_event *event, unsigned int val1, unsigned int val2)
{
}

void dprec_trigger(dprec_logger_event *event, unsigned int val1, unsigned int val2)
{
}

void dprec_submit(dprec_logger_event *event, unsigned int val1, unsigned int val2)
{
}

void dprec_logger_submit(unsigned int type_logsrc, unsigned long long period,
			 unsigned int fence_idx)
{
}

void dprec_logger_reset_all(void)
{
}

void dprec_logger_reset(DPREC_LOGGER_ENUM source)
{
}

int dprec_logger_get_result_string(DPREC_LOGGER_ENUM source, char *stringbuf, int strlen)
{
	return 0;
}

int dprec_logger_get_result_string_all(char *stringbuf, int strlen)
{
	return 0;
}

void dprec_stub_irq(unsigned int irq_bit)
{
}

void dprec_stub_event(DISP_PATH_EVENT event)
{
}

unsigned int dprec_get_vsync_count(void)
{
	return 0;
}

void dprec_reg_op(void *cmdq, unsigned int reg, unsigned int val, unsigned int mask)
{
}

void dprec_logger_vdump(const char *fmt, ...)
{
}

void dprec_logger_dump(char *string)
{
}

void dprec_logger_dump_reset(void)
{
}

char *dprec_logger_get_dump_addr()
{
	return NULL;
}

unsigned int dprec_logger_get_dump_len(void)
{
	return 0;
}

int dprec_handle_option(unsigned int option)
{
	return 0;
}

int dprec_option_enabled(void)
{
	return 0;
}

int dprec_mmp_dump_ovl_layer(OVL_CONFIG_STRUCT *ovl_layer, unsigned int l,
			     unsigned int session /*1:primary, 2:external, 3:memory */)
{
	return 0;
}

int dprec_mmp_dump_wdma_layer(void *wdma_layer, unsigned int wdma_num)
{
	return 0;
}

int dprec_mmp_dump_rdma_layer(void *rdma_layer, unsigned int rdma_num)
{
	return 0;
}

int dprec_logger_pr(unsigned int type, char *fmt, ...)
{
	return 0;
}

int dprec_logger_get_buf(DPREC_LOGGER_PR_TYPE type, char *stringbuf, int len)
{
	return 0;
}
/*fix build error for add visual debug info*/
int dprec_logger_get_result_value(DPREC_LOGGER_ENUM source, fpsEx *fps)
{
	return 0;
}

char *get_dprec_status_ptr(int buffer_idx)
{
	return NULL;
}
char *debug_buffer = NULL;
bool is_buffer_init = false;
void init_log_buffer(void)
{

}
#endif
