
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/ktime.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/switch.h>
#include "mt_idle.h"
#include "mt_spm.h"	/* for sodi reg addr define */
#include "mt_spm_idle.h"
/* #include "mach/eint.h" */
/* #include <cust_eint.h> */
#include "mt-plat/mt_smi.h"
#include "mtk_ion.h"
#include "ion_drv.h"
#include "m4u.h"
#include "m4u_port.h"
#include "m4u_priv.h"

#include "disp_drv_platform.h"
#include "debug.h"
#include "disp_drv_log.h"
#include "disp_lcm.h"
#include "disp_utils.h"
#include "mtkfb.h"
#include "ddp_hal.h"
#include "ddp_dump.h"
#include "ddp_path.h"
#include "ddp_drv.h"
#include "ddp_reg.h"
#include "disp_session.h"
#include "primary_display.h"
#include "cmdq_def.h"
#include "cmdq_record.h"
#include "cmdq_reg.h"
#include "cmdq_core.h"
#include "ddp_rdma.h"
#include "ddp_manager.h"
#include "mtkfb_fence.h"
#include "display_recorder.h"
#include "fbconfig_kdebug_x.h"
#include "ddp_mmp.h"
#include "mtk_sync.h"
#include "ddp_irq.h"
#include "disp_session.h"
#include "disp_helper.h"
#include "ddp_reg.h"
#include "mtk_disp_mgr.h"
#include "ddp_dsi.h"
#include "m4u.h"
#include "mtkfb_console.h"
#if defined(CONFIG_MTK_LEGACY)
#include <mach/mt_gpio.h>
#include <cust_gpio_usage.h>
#else
#include "disp_dts_gpio.h"
#endif
#include "mt-plat/aee.h"

#include "ddp_clkmgr.h"
#include "mmdvfs_mgr.h" /* FIXME: this header file copied to dispsys tmp */
#include "mt_vcorefs_governor.h" /* FIXME: this header file copied to dispsys tmp */
#include "mt_vcorefs_manager.h" /* FIXME: this header file copied to dispsys tmp */
#include "disp_lowpower.h"
#include "disp_recovery.h"
#include "mt_spm_sodi_cmdq.h"
#include "ddp_od.h"
#include "mtk_hrt.h"
#include "disp_rect.h"
#include "disp_partial.h"
#include "ddp_aal.h"

#define MMSYS_CLK_LOW (0)
#define MMSYS_CLK_HIGH (1)

#define _DEBUG_DITHER_HANG_

#define FRM_UPDATE_SEQ_CACHE_NUM (DISP_INTERNAL_BUFFER_COUNT+1)

static disp_internal_buffer_info *decouple_buffer_info[DISP_INTERNAL_BUFFER_COUNT];
static RDMA_CONFIG_STRUCT decouple_rdma_config;
static WDMA_CONFIG_STRUCT decouple_wdma_config;
static disp_mem_output_config mem_config;
atomic_t hwc_configing = ATOMIC_INIT(0);
static unsigned int primary_session_id = MAKE_DISP_SESSION(DISP_SESSION_PRIMARY, 0);
static disp_frm_seq_info frm_update_sequence[FRM_UPDATE_SEQ_CACHE_NUM];
static unsigned int frm_update_cnt;
static unsigned int gPresentFenceIndex;
unsigned int gTriggerDispMode = 0; /* 0: normal, 1: lcd only, 2: none of lcd and lcm */
static unsigned int g_keep;
static unsigned int g_skip;
#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
static struct switch_dev disp_switch_data;
#endif

#if 0
/* global variable for idle manager */
static unsigned long long idlemgr_last_kick_time = ~(0ULL);
static int session_mode_before_enter_idle;
static int is_primary_idle;
static struct task_struct *primary_display_idlemgr_task;
static DECLARE_WAIT_QUEUE_HEAD(idlemgr_wait_queue);
#endif

static struct hrtimer cmd_mode_update_timer;
#if 0 /* defined but not used */
static ktime_t cmd_mode_update_timer_period;
#endif
static int is_fake_timer_inited;

static struct task_struct *primary_display_switch_dst_mode_task;
static struct task_struct *present_fence_release_worker_task;
static struct task_struct *primary_path_aal_task;
static struct task_struct *primary_delay_trigger_task;
static struct task_struct *primary_od_trigger_task;
static struct task_struct *decouple_update_rdma_config_thread;
static struct task_struct *decouple_trigger_thread;
static struct task_struct *init_decouple_buffer_thread;
static struct sg_table table;

static int decouple_mirror_update_rdma_config_thread(void *data);
static int decouple_trigger_worker_thread(void *data);

struct task_struct *primary_display_frame_update_task = NULL;
wait_queue_head_t primary_display_frame_update_wq;
atomic_t primary_display_frame_update_event = ATOMIC_INIT(0);
DISP_PRIMARY_PATH_MODE primary_display_mode = DIRECT_LINK_MODE;
int primary_display_def_dst_mode = 0;
int primary_display_cur_dst_mode = 0;
unsigned long long last_primary_trigger_time;
bool is_switched_dst_mode = false;
int primary_trigger_cnt = 0;
atomic_t decouple_update_rdma_event = ATOMIC_INIT(0);
DECLARE_WAIT_QUEUE_HEAD(decouple_update_rdma_wq);
atomic_t decouple_trigger_event = ATOMIC_INIT(0);
DECLARE_WAIT_QUEUE_HEAD(decouple_trigger_wq);
wait_queue_head_t primary_display_present_fence_wq;
atomic_t primary_display_present_fence_update_event = ATOMIC_INIT(0);
static unsigned int _need_lfr_check(void);

#ifdef CONFIG_MTK_DISPLAY_120HZ_SUPPORT
static int od_need_start;
#endif

/* dvfs */
static atomic_t dvfs_ovl_req_status = ATOMIC_INIT(HRT_LEVEL_LOW);
static int dvfs_last_ovl_req = HRT_LEVEL_LOW;

/* delayed trigger */
static atomic_t delayed_trigger_kick = ATOMIC_INIT(0);
static atomic_t od_trigger_kick = ATOMIC_INIT(0);

typedef struct {
	DISP_POWER_STATE state;
	unsigned int lcm_fps;
	int lcm_refresh_rate;
	int max_layer;
	int need_trigger_overlay;
	int need_trigger_ovl1to2;
	int need_trigger_dcMirror_out;
	DISP_PRIMARY_PATH_MODE mode;
	unsigned int session_id;
	int session_mode;
	int ovl1to2_mode;
	unsigned int last_vsync_tick;
	unsigned long framebuffer_mva;
	unsigned long framebuffer_va;
	struct mutex lock;
	struct mutex capture_lock;
	struct mutex switch_dst_lock;
	disp_lcm_handle *plcm;
	cmdqRecHandle cmdq_handle_config_esd;
	cmdqRecHandle cmdq_handle_config;
	disp_path_handle dpmgr_handle;
	disp_path_handle ovl2mem_path_handle;
	cmdqRecHandle cmdq_handle_ovl1to2_config;
	cmdqRecHandle cmdq_handle_trigger;
	char *mutex_locker;
	int vsync_drop;
	unsigned int dc_buf_id;
	unsigned int dc_buf[DISP_INTERNAL_BUFFER_COUNT];
	unsigned int force_fps_keep_count;
	unsigned int force_fps_skip_count;
	cmdqBackupSlotHandle cur_config_fence;
	cmdqBackupSlotHandle subtractor_when_free;
	cmdqBackupSlotHandle rdma_buff_info;
	cmdqBackupSlotHandle ovl_status_info;
	cmdqBackupSlotHandle ovl_config_time;
	cmdqBackupSlotHandle dither_status_info;

	int is_primary_sec;
	int primary_display_scenario;
#ifdef CONFIG_MTK_DISPLAY_120HZ_SUPPORT
	int request_fps;
#endif
} display_primary_path_context;

#define pgc	_get_context()

static int smart_ovl_try_switch_mode_nolock(void);

static display_primary_path_context *_get_context(void)
{
	static int is_context_inited;
	static display_primary_path_context g_context;

	if (!is_context_inited) {
		memset((void *)&g_context, 0, sizeof(display_primary_path_context));
		is_context_inited = 1;
	}

	return &g_context;
}

static void _primary_path_lock(const char *caller)
{
	dprec_logger_start(DPREC_LOGGER_PRIMARY_MUTEX, 0, 0);
	disp_sw_mutex_lock(&(pgc->lock));
	pgc->mutex_locker = (char *)caller;
}

static void _primary_path_unlock(const char *caller)
{
	pgc->mutex_locker = NULL;
	disp_sw_mutex_unlock(&(pgc->lock));
	dprec_logger_done(DPREC_LOGGER_PRIMARY_MUTEX, 0, 0);
}

static const char *session_mode_spy(unsigned int mode)
{
	switch (mode) {
	case DISP_SESSION_DIRECT_LINK_MODE:
		return "DIRECT_LINK";
	case DISP_SESSION_DIRECT_LINK_MIRROR_MODE:
		return "DIRECT_LINK_MIRROR";
	case DISP_SESSION_DECOUPLE_MODE:
		return "DECOUPLE";
	case DISP_SESSION_DECOUPLE_MIRROR_MODE:
		return "DECOUPLE_MIRROR";
	case DISP_SESSION_RDMA_MODE:
		return "RDMA_MODE";
	default:
		return "UNKNOWN";
	}
}

int primary_display_is_directlink_mode(void)
{
	DISP_MODE mode = pgc->session_mode;

	if (mode == DISP_SESSION_DIRECT_LINK_MIRROR_MODE || mode == DISP_SESSION_DIRECT_LINK_MODE)
		return 1;
	else
		return 0;
}

DISP_MODE primary_get_sess_mode(void)
{
	return pgc->session_mode;
}

unsigned int primary_get_sess_id(void)
{
	return pgc->session_id;
}

disp_lcm_handle *primary_get_lcm(void)
{
	return pgc->plcm;
}

void *primary_get_dpmgr_handle(void)
{
	return pgc->dpmgr_handle;
}

void *primary_get_ovl2mem_handle(void)
{
	return pgc->ovl2mem_path_handle;
}

int primary_display_is_decouple_mode(void)
{
	DISP_MODE mode = pgc->session_mode;

	if (mode == DISP_SESSION_DECOUPLE_MODE || mode == DISP_SESSION_DECOUPLE_MIRROR_MODE)
		return 1;
	else
		return 0;
}

int _is_mirror_mode(DISP_MODE mode)
{
	if (mode == DISP_SESSION_DIRECT_LINK_MIRROR_MODE || mode == DISP_SESSION_DECOUPLE_MIRROR_MODE)
		return 1;
	else
		return 0;
}
int primary_display_is_mirror_mode(void)
{
	return _is_mirror_mode(pgc->session_mode);
}

int primary_is_sec(void)
{
	return pgc->is_primary_sec;
}

void _primary_path_switch_dst_lock(void)
{
	mutex_lock(&(pgc->switch_dst_lock));
}

void _primary_path_switch_dst_unlock(void)
{
	mutex_unlock(&(pgc->switch_dst_lock));
}

int primary_display_partial_support(void)
{
	return disp_partial_is_support();
}

int primary_display_config_full_roi(disp_ddp_path_config *pconfig, disp_path_handle disp_handle,
		cmdqRecHandle cmdq_handle)
{
	struct disp_rect total_dirty_roi = { 0, 0, 0, 0};

	if (disp_partial_is_support()) {
		assign_full_lcm_roi(&total_dirty_roi);
		if (!rect_equal(&total_dirty_roi, &pconfig->ovl_partial_roi)) {
			pconfig->ovl_partial_roi = total_dirty_roi;
			dpmgr_path_update_partial_roi(disp_handle,
					total_dirty_roi, cmdq_handle);
			if (disp_helper_get_option(DISP_OPT_DYNAMIC_RDMA_GOLDEN_SETTING)) {
				/* update rdma goden settin*/
				set_rdma_width_height(total_dirty_roi.width, total_dirty_roi.height);
				dpmgr_path_ioctl(disp_handle, cmdq_handle, DDP_RDMA_GOLDEN_SETTING, pconfig);
			}
			/* update ovl to full layer */
			pconfig->ovl_dirty = 1;
			pconfig->ovl_partial_dirty = 0;
			dpmgr_path_config(disp_handle, pconfig, cmdq_handle);
			pconfig->ovl_layer_scanned = 0;
			pconfig->ovl_partial_dirty = 0;
			pconfig->ovl_dirty = 0;
		}
		return 0;
	}
	return -1;
}

static int _disp_primary_path_switch_dst_mode_thread(void *data)
{
	while (1) {
		msleep(1000);

		if (((sched_clock() - last_primary_trigger_time) / 1000) > 500000) {	/* 500ms not trigger disp */
			primary_display_switch_dst_mode(0);	/* switch to cmd mode */
			is_switched_dst_mode = true;
		}
		if (kthread_should_stop())
			break;
	}
	return 0;
}

static DECLARE_WAIT_QUEUE_HEAD(display_state_wait_queue);

DISP_POWER_STATE primary_get_state(void)
{
	return pgc->state;
}
static DISP_POWER_STATE primary_set_state(DISP_POWER_STATE new_state)
{
	DISP_POWER_STATE old_state = pgc->state;

	pgc->state = new_state;
	DISPMSG("%s %d to %d\n", __func__, old_state, new_state);
	wake_up(&display_state_wait_queue);
	return old_state;
}

#define __primary_display_wait_state(condition, timeout) \
	wait_event_timeout(display_state_wait_queue, condition, timeout)

long primary_display_wait_state(DISP_POWER_STATE state, long timeout)
{
	long ret;

	ret = __primary_display_wait_state(primary_get_state() == state, timeout);
	return ret;
}

long primary_display_wait_not_state(DISP_POWER_STATE state, long timeout)
{
	long ret;

	ret = __primary_display_wait_state(primary_get_state() != state, timeout);
	return ret;
}

int dynamic_debug_msg_print(unsigned int mva, int w, int h, int pitch, int bytes_per_pix)
{
	int ret = 0;
	unsigned int layer_size = pitch * h;
	unsigned int real_mva = 0;
	unsigned long kva = 0;
	unsigned int real_size = 0, mapped_size = 0;

	static MFC_HANDLE mfc_handle;

	if (disp_helper_get_option(DISP_OPT_SHOW_VISUAL_DEBUG_INFO)) {
		m4u_query_mva_info(mva, layer_size, &real_mva, &real_size);
		if (ret < 0) {
			pr_debug("m4u_query_mva_info error\n");
			return -1;
		}
		ret = m4u_mva_map_kernel(real_mva, real_size, &kva, &mapped_size);
		if (ret < 0) {
			pr_debug("m4u_mva_map_kernel fail.\n");
			return -1;
		}
		if (layer_size > mapped_size) {
			pr_debug("warning: layer size > mapped size\n");
			goto err1;
		}

		ret = MFC_Open(&mfc_handle,
			       (void *)kva,
			       pitch,
			       h,
			       bytes_per_pix,
			       DAL_COLOR_WHITE,
			       DAL_COLOR_RED);
		if (ret != MFC_STATUS_OK)
			goto err1;
		screen_logger_print(mfc_handle);
		MFC_Close(mfc_handle);
err1:
		m4u_mva_unmap_kernel(real_mva, real_size, kva);
	}
	return 0;
}

static int primary_show_basic_debug_info(struct disp_frame_cfg_t *cfg)
{
	int i;
	fpsEx fps;
	char disp_tmp[20];
	int dst_layer_id = 0;

	dprec_logger_get_result_value(DPREC_LOGGER_RDMA0_TRANSFER_1SECOND, &fps);
	snprintf(disp_tmp, sizeof(disp_tmp), ",rdma_fps:%lld.%02lld,", fps.fps, fps.fps_low);
	screen_logger_add_message("rdma_fps", MESSAGE_REPLACE, disp_tmp);

	dprec_logger_get_result_value(DPREC_LOGGER_OVL_FRAME_COMPLETE_1SECOND, &fps);
	snprintf(disp_tmp, sizeof(disp_tmp), "ovl_fps:%lld.%02lld,", fps.fps, fps.fps_low);
	screen_logger_add_message("ovl_fps", MESSAGE_REPLACE, disp_tmp);

	dprec_logger_get_result_value(DPREC_LOGGER_PQ_TRIGGER_1SECOND, &fps);
	snprintf(disp_tmp, sizeof(disp_tmp), "PQ_trigger:%lld.%02lld,", fps.fps, fps.fps_low);
	screen_logger_add_message("PQ trigger", MESSAGE_REPLACE, disp_tmp);

	snprintf(disp_tmp, sizeof(disp_tmp), primary_display_is_video_mode() ? "vdo," : "cmd,");
	screen_logger_add_message("mode", MESSAGE_REPLACE, disp_tmp);

	for (i = 0; i < cfg->input_layer_num; i++) {
		if (cfg->input_cfg[i].tgt_offset_y == 0 &&
		    cfg->input_cfg[i].layer_enable) {
			dst_layer_id = dst_layer_id > cfg->input_cfg[i].layer_id ?
				dst_layer_id : cfg->input_cfg[i].layer_id;
		}
	}

	dynamic_debug_msg_print((unsigned long)cfg->input_cfg[dst_layer_id].src_phy_addr,
				cfg->input_cfg[dst_layer_id].tgt_width,
				cfg->input_cfg[dst_layer_id].tgt_height,
				cfg->input_cfg[dst_layer_id].src_pitch,
				4);
	return 0;
}

#if 0 /* defined but not used */
static int primary_dynamic_debug(unsigned int mva, unsigned int pitch, unsigned int w, unsigned int h,
				unsigned int x_pos, unsigned int block_sz)
{
	unsigned int real_mva, real_size, map_size;
	unsigned long map_va;
	int ret;
	unsigned char *buf_va;
	int x, y;

	if (!disp_helper_get_option(DISP_OPT_DYNAMIC_DEBUG))
		return 0;

	ret = m4u_query_mva_info(mva, 0, &real_mva, &real_size);
	if (ret) {
		pr_err("%s error to query mva = 0x%x\n", __func__, mva);
		return -1;
	}
	ret = m4u_mva_map_kernel(real_mva, real_size, &map_va, &map_size);
	if (ret) {
		pr_err("%s error to map mva = 0x%x\n", __func__, real_mva);
		return -1;
	}

	buf_va = (unsigned char *)(map_va + (mva - real_mva));
	if (x_pos + block_sz > w)
		block_sz = w/2;
	if (block_sz > h)
		block_sz = h;

	for (y = 0; y < block_sz; y++) {
		for (x = 0; x < block_sz * 4; x++)
			buf_va[x_pos * 4 + x + y * pitch] = 255;
	}

	m4u_mva_unmap_kernel(real_mva, real_size, map_va);

	return 0;

}
#endif

/*************************** fps calculate ************************/
#define FPS_ARRAY_SZ	30
struct fps_ctx_t {
	unsigned long long last_trig;
	unsigned int array[FPS_ARRAY_SZ];
	unsigned int total;
	unsigned int wnd_sz;
	unsigned int cur_wnd_sz;
	struct mutex lock;
	int is_inited;
} primary_fps_ctx;

#if 0 /* defined but not used */
static struct task_struct *fps_monitor;
static int fps_monitor_thread(void *data);
#endif

static int _fps_ctx_reset(struct fps_ctx_t *fps_ctx, int reserve_num)
{
	int i;

	if (reserve_num >= FPS_ARRAY_SZ) {
		pr_err("%s error to reset, reserve=%d\n", __func__, reserve_num);
		BUG();
	}
	for (i = reserve_num; i < FPS_ARRAY_SZ; i++)
		fps_ctx->array[i] = 0;

	if (reserve_num < fps_ctx->cur_wnd_sz)
		fps_ctx->cur_wnd_sz = reserve_num;

	/* re-calc total */
	fps_ctx->total = 0;
	for (i = 0; i < fps_ctx->cur_wnd_sz; i++)
		fps_ctx->total += fps_ctx->array[i];

	return 0;
}

static int _fps_ctx_update(struct fps_ctx_t *fps_ctx, unsigned int fps, unsigned long long time_ns)
{
	int i;

	fps_ctx->total -= fps_ctx->array[fps_ctx->wnd_sz-1];

	for (i = fps_ctx->wnd_sz - 1; i > 0; i--)
		fps_ctx->array[i] = fps_ctx->array[i-1];

	fps_ctx->array[0] = fps;

	fps_ctx->total += fps_ctx->array[0];
	if (fps_ctx->cur_wnd_sz < fps_ctx->wnd_sz)
		fps_ctx->cur_wnd_sz++;

	fps_ctx->last_trig = time_ns;

	return 0;
}

static int fps_ctx_init(struct fps_ctx_t *fps_ctx, int wnd_sz)
{
	if (fps_ctx->is_inited)
		return 0;

	memset(fps_ctx, 0, sizeof(*fps_ctx));
	mutex_init(&fps_ctx->lock);

	if (wnd_sz > FPS_ARRAY_SZ) {
		pr_err("%s error: wnd_sz = %d\n", __func__, wnd_sz);
		wnd_sz = FPS_ARRAY_SZ;
	}
	fps_ctx->wnd_sz = wnd_sz;

	/* fps_monitor = kthread_create(fps_monitor_thread, NULL, "fps_monitor");*/
	/* wake_up_process(fps_monitor); */
	fps_ctx->is_inited = 1;

	return 0;
}

static unsigned int _fps_ctx_calc_cur_fps(struct fps_ctx_t *fps_ctx, unsigned long long cur_ns)
{
	unsigned long long delta;
	unsigned long long fps = 1000000000;

	delta = cur_ns - fps_ctx->last_trig;
	do_div(fps, delta);

	if (fps > 120ULL)
		fps = 120ULL;

	return (unsigned int)fps;
}

static unsigned int _fps_ctx_get_avg_fps(struct fps_ctx_t *fps_ctx)
{
	unsigned int avg_fps;

	if (fps_ctx->cur_wnd_sz == 0)
		return 0;
	avg_fps = fps_ctx->total / fps_ctx->cur_wnd_sz;
	return avg_fps;
}

static unsigned int _fps_ctx_get_avg_fps_ext(struct fps_ctx_t *fps_ctx, unsigned int abs_fps)
{
	unsigned int avg_fps;

	avg_fps = (fps_ctx->total + abs_fps) / (fps_ctx->cur_wnd_sz + 1);
	return avg_fps;
}

#if 0 /* defined but not used */
static int _fps_ctx_check_drastic_change(struct fps_ctx_t *fps_ctx, unsigned int abs_fps)
{
	unsigned int avg_fps;

	avg_fps = _fps_ctx_get_avg_fps(fps_ctx);

	/* if long time no trigger, ex:500ms
	 * abs_fps=1, avg_fps may be 58
	 * we should let avg_fps decline rapidly */
	if (abs_fps < avg_fps / 4 || abs_fps > avg_fps * 4) {
		_fps_ctx_reset(fps_ctx, fps_ctx->cur_wnd_sz / 4);
	} else if (abs_fps < avg_fps / 2 || abs_fps > avg_fps * 2) {
		if (fps_ctx->cur_wnd_sz >= 2)
			_fps_ctx_reset(fps_ctx, fps_ctx->cur_wnd_sz / 2);
	}

	return 0;
}
#endif

static int fps_ctx_update(struct fps_ctx_t *fps_ctx)
{
	unsigned int abs_fps, avg_fps;
	unsigned long long ns = sched_clock();

	mutex_lock(&fps_ctx->lock);
	abs_fps = _fps_ctx_calc_cur_fps(fps_ctx, ns);

	/* _fps_ctx_check_drastic_change(fps_ctx, abs_fps); */

	avg_fps = _fps_ctx_get_avg_fps(fps_ctx);

	/* if long time no trigger, ex:500ms
	 * abs_fps=1, avg_fps may be 58
	 * we should let avg_fps decline rapidly */

	if (abs_fps < avg_fps / 2 && avg_fps > 10)
		_fps_ctx_reset(fps_ctx, 0);


	_fps_ctx_update(fps_ctx, abs_fps, ns);

	MMProfileLogEx(ddp_mmp_get_events()->fps_set, MMProfileFlagPulse, abs_fps, fps_ctx->cur_wnd_sz);
	mutex_unlock(&fps_ctx->lock);
	return 0;
}

static int fps_ctx_get_fps(struct fps_ctx_t *fps_ctx, unsigned int *fps, int *stable)
{
	unsigned long long ns = sched_clock();
	unsigned int abs_fps = 0, avg_fps = 0;

	*stable = 1;
	mutex_lock(&fps_ctx->lock);

	abs_fps = _fps_ctx_calc_cur_fps(fps_ctx, ns);
	avg_fps = _fps_ctx_get_avg_fps(fps_ctx);

	/* if long time no trigger, we should pull fps down */
	if (abs_fps < avg_fps/2 && avg_fps > 10) {
		_fps_ctx_reset(fps_ctx, 0);
		*fps = abs_fps;
		*stable = 0;
		goto done;
	}

	if (fps_ctx->cur_wnd_sz < fps_ctx->wnd_sz/2)
		*stable = 0;

	if (abs_fps < avg_fps)
		*fps = _fps_ctx_get_avg_fps_ext(fps_ctx, abs_fps);
	else
		*fps = avg_fps;

done:
	MMProfileLogEx(ddp_mmp_get_events()->fps_get, MMProfileFlagPulse, *fps, *stable);
	mutex_unlock(&fps_ctx->lock);
	return 0;
}

static int fps_ctx_set_wnd_sz(struct fps_ctx_t *fps_ctx, unsigned int wnd_sz)
{
	int i;

	if (!fps_ctx->is_inited)
		return fps_ctx_init(fps_ctx, wnd_sz);

	if (wnd_sz > FPS_ARRAY_SZ) {
		pr_err("error: %s wnd_sz=%d\n", __func__, wnd_sz);
		return -1;
	}
	mutex_lock(&fps_ctx->lock);

	fps_ctx->total = 0;
	fps_ctx->wnd_sz = wnd_sz;

	for (i = 0; i < wnd_sz; i++)
		fps_ctx->total += fps_ctx->array[i];

	mutex_unlock(&fps_ctx->lock);
	return 0;
}

#if 0 /* defined but not used */
static int fps_ctx_get_wnd_sz(struct fps_ctx_t *fps_ctx, unsigned int *cur_wnd, unsigned int *max_wnd)
{
	mutex_lock(&fps_ctx->lock);
	if (cur_wnd)
		*cur_wnd = fps_ctx->cur_wnd_sz;
	if (max_wnd)
		*max_wnd = fps_ctx->wnd_sz;
	mutex_unlock(&fps_ctx->lock);
	return 0;
}
#endif

int primary_fps_ctx_set_wnd_sz(unsigned int wnd_sz)
{
	return fps_ctx_set_wnd_sz(&primary_fps_ctx, wnd_sz);
}

#if 0 /* defined but not used */
static int fps_monitor_thread(void *data)
{
	int ret = 0;

	msleep(16000);
	while (1) {
		int fps, stable;

		msleep(20);
		_primary_path_lock(__func__);
		fps_ctx_get_fps(&primary_fps_ctx, &fps, &stable);
		_primary_path_unlock(__func__);
	}

	return 0;
}
#endif

/*********************** fps calculate finish *********************************/

/*********************** idle manager *****************************************/
int primary_display_get_debug_state(char *stringbuf, int buf_len)
{
	int len = 0;
	LCM_PARAMS *lcm_param = disp_lcm_get_params(pgc->plcm);
	LCM_DRIVER *lcm_drv = pgc->plcm->drv;

	len += scnprintf(stringbuf + len, buf_len - len,
		      "|--------------------------------------------------------------------------------------|\n");
	len += scnprintf(stringbuf + len, buf_len - len,
		      "|********Primary Display Path General Information********\n");
	if (pgc->state == DISP_ALIVE)
		len += scnprintf(stringbuf + len, buf_len - len, "|Primary Display is %s\n",
				 dpmgr_path_is_idle(pgc->dpmgr_handle) ? "idle" : "busy");

	if (mutex_trylock(&(pgc->lock))) {
		mutex_unlock(&(pgc->lock));
		len += scnprintf(stringbuf + len, buf_len - len,
			      "|primary path global mutex is free\n");
	} else {
		len += scnprintf(stringbuf + len, buf_len - len,
			      "|primary path global mutex is hold by [%s]\n", pgc->mutex_locker);
	}

	if (lcm_param && lcm_drv)
		len += scnprintf(stringbuf + len, buf_len - len,
			      "|LCM Driver=[%s]\tResolution=%dx%d,Interface:%s, LCM Connected:%s\n", lcm_drv->name,
			      lcm_param->width, lcm_param->height,
			      (lcm_param->type == LCM_TYPE_DSI) ? "DSI" : "Other", islcmconnected ? "Y" : "N");
	len += scnprintf(stringbuf + len, buf_len - len,
		      "|State=%s\tlcm_fps=%d\tmax_layer=%d\tmode:%d\tvsync_drop=%d\n",
		      pgc->state == DISP_ALIVE ? "Alive" : "Sleep", pgc->lcm_fps, pgc->max_layer,
		      pgc->mode, pgc->vsync_drop);
	len += scnprintf(stringbuf + len, buf_len - len,
		      "|cmdq_handle_config=%p\tcmdq_handle_trigger=%p\tdpmgr_handle=%p\tovl2mem_path_handle=%p\n",
		      pgc->cmdq_handle_config, pgc->cmdq_handle_trigger, pgc->dpmgr_handle,
		      pgc->ovl2mem_path_handle);
	len += scnprintf(stringbuf + len, buf_len - len, "|Current display driver status=%s + %s\n",
		      primary_display_is_video_mode() ? "video mode" : "cmd mode",
		      primary_display_cmdq_enabled() ? "CMDQ Enabled" : "CMDQ Disabled");

	return len;
}

static DISP_MODULE_ENUM _get_dst_module_by_lcm(disp_lcm_handle *plcm)
{
	if (plcm == NULL) {
		DISPERR("plcm is null\n");
		return DISP_MODULE_UNKNOWN;
	}

	if (plcm->params->type == LCM_TYPE_DSI) {
		if (plcm->lcm_if_id == LCM_INTERFACE_DSI0)
			return DISP_MODULE_DSI0;
		else if (plcm->lcm_if_id == LCM_INTERFACE_DSI1)
			return DISP_MODULE_DSI1;
		else if (plcm->lcm_if_id == LCM_INTERFACE_DSI_DUAL)
			return DISP_MODULE_DSIDUAL;
		else
			return DISP_MODULE_DSI0;

	} else if (plcm->params->type == LCM_TYPE_DPI) {
		return DISP_MODULE_DPI;
	}
	DISPERR("can't find primary path dst module\n");
	return DISP_MODULE_UNKNOWN;
}



int _should_wait_path_idle(void)
{
	/* trigger operation:  VDO+CMDQ  CMD+CMDQ VDO+CPU  CMD+CPU
	 * 1.wait idle:        N         N        Y        Y
	 */
	if (primary_display_cmdq_enabled()) {
		if (primary_display_is_video_mode())
			return 0;
		else
			return 0;

	} else {
		if (primary_display_is_video_mode())
			return dpmgr_path_is_busy(pgc->dpmgr_handle);
		else
			return dpmgr_path_is_busy(pgc->dpmgr_handle);

	}
}

int _should_update_lcm(void)
{
	/* trigger operation:  VDO+CMDQ  CMD+CMDQ VDO+CPU  CMD+CPU
	 * 2.lcm update:          N         Y       N        Y
	 */
	if (primary_display_cmdq_enabled()) {
		if (primary_display_is_video_mode())
			return 0;

		/* lcm_update can't use cmdq now */
		return 0;
	}
	if (primary_display_is_video_mode())
		return 0;

	return 1;
}

int _should_start_path(void)
{
	/* trigger operation:  VDO+CMDQ  CMD+CMDQ VDO+CPU  CMD+CPU
	 * 3.path start:	idle->Y      Y    idle->Y     Y
	 */
	if (primary_display_cmdq_enabled()) {
		if (primary_display_is_video_mode()) {
			return 0;
			/* return dpmgr_path_is_idle(pgc->dpmgr_handle); */
		} else {
			return 0;
		}
	} else {
		if (primary_display_is_video_mode())
			return dpmgr_path_is_idle(pgc->dpmgr_handle);
		else
			return 1;

	}
}

int _should_trigger_path(void)
{
	/* trigger operation:  VDO+CMDQ  CMD+CMDQ VDO+CPU  CMD+CPU
	 * 4.path trigger:     idle->Y      Y     idle->Y     Y
	 * 5.mutex enable:        N         N     idle->Y     Y
	 */
	/* this is not a perfect design, we can't decide path trigger(ovl/rdma/dsi..) separately with mutex enable */
	/* but it's lucky because path trigger and mutex enable is the same w/o cmdq, and it's correct w/ CMDQ(Y+N). */
	if (primary_display_cmdq_enabled()) {
		if (primary_display_is_video_mode()) {
			return 0;
			/* return dpmgr_path_is_idle(pgc->dpmgr_handle); */
		} else {
			return 0;
		}
	} else {
		if (primary_display_is_video_mode())
			return dpmgr_path_is_idle(pgc->dpmgr_handle);
		else
			return 1;

	}
}

int _should_set_cmdq_dirty(void)
{
	/* trigger operation:  VDO+CMDQ  CMD+CMDQ VDO+CPU  CMD+CPU
	 * 6.set cmdq dirty:	    N         Y       N        N
	 */
	if (primary_display_cmdq_enabled()) {
		if (primary_display_is_video_mode())
			return 0;
		else
			return 1;

	} else {
		if (primary_display_is_video_mode())
			return 0;
		else
			return 0;

	}
}

int _should_flush_cmdq_config_handle(void)
{
	/* trigger operation:  VDO+CMDQ  CMD+CMDQ VDO+CPU  CMD+CPU
	 * 7.flush cmdq:          Y         Y       N        N
	 */
	if (primary_display_cmdq_enabled()) {
		if (primary_display_is_video_mode())
			return 1;
		else
			return 1;

	} else {
		if (primary_display_is_video_mode())
			return 0;
		else
			return 0;

	}
}

int _should_reset_cmdq_config_handle(void)
{
	if (primary_display_cmdq_enabled()) {
		if (primary_display_is_video_mode())
			return 1;
		else
			return 1;

	} else {
		if (primary_display_is_video_mode())
			return 0;
		else
			return 0;

	}
}

int _should_insert_wait_frame_done_token(void)
{
	/* trigger operation:  VDO+CMDQ  CMD+CMDQ VDO+CPU  CMD+CPU
	 * 7.flush cmdq:          Y         Y       N        N
	 */
	if (primary_display_cmdq_enabled()) {
		if (primary_display_is_video_mode())
			return 1;
		else
			return 1;

	} else {
		if (primary_display_is_video_mode())
			return 0;
		else
			return 0;

	}
}

int _should_trigger_interface(void)
{
	if (pgc->mode == DECOUPLE_MODE)
		return 0;
	else
		return 1;

}

int _should_config_ovl_input(void)
{
	/* should extend this when display path dynamic switch is ready */
	if (pgc->mode == SINGLE_LAYER_MODE || pgc->mode == DEBUG_RDMA1_DSI0_MODE)
		return 0;
	else
		return 1;

}

static long int get_current_time_us(void)
{
	struct timeval t;

	do_gettimeofday(&t);
	return (t.tv_sec & 0xFFF) * 1000000 + t.tv_usec;
}


static enum hrtimer_restart _DISP_CmdModeTimer_handler(struct hrtimer *timer)
{
	DISPMSG("fake timer, wake up\n");
	dpmgr_signal_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);
#if 0
	if ((get_current_time_us() - pgc->last_vsync_tick) > 16666) {
		dpmgr_signal_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);
		pgc->last_vsync_tick = get_current_time_us();
	}
#endif
	hrtimer_forward_now(timer, ns_to_ktime(16666666));
	return HRTIMER_RESTART;
}

int _init_vsync_fake_monitor(int fps)
{
	if (is_fake_timer_inited)
		return 0;

	is_fake_timer_inited = 1;

	if (fps == 0)
		fps = 6000;


	hrtimer_init(&cmd_mode_update_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cmd_mode_update_timer.function = _DISP_CmdModeTimer_handler;
	hrtimer_start(&cmd_mode_update_timer, ns_to_ktime(16666666), HRTIMER_MODE_REL);

	return 0;
}

static int _build_path_decouple(void)
{
	return 0;
}

static int _build_path_single_layer(void)
{
	return 0;
}

static int _build_path_debug_rdma1_dsi0(void)
{
	int ret = 0;

	DISP_MODULE_ENUM dst_module = 0;

	pgc->mode = DEBUG_RDMA1_DSI0_MODE;

	pgc->dpmgr_handle = dpmgr_create_path(DDP_SCENARIO_SUB_RDMA1_DISP, pgc->cmdq_handle_config);
	if (pgc->dpmgr_handle) {
		DISPDBG("dpmgr create path SUCCESS(%p)\n", pgc->dpmgr_handle);
	} else {
		DISPERR("dpmgr create path FAIL\n");
		return -1;
	}

	dst_module = _get_dst_module_by_lcm(pgc->plcm);
	dpmgr_path_set_dst_module(pgc->dpmgr_handle, dst_module);
	DISPCHECK("dpmgr set dst module FINISHED(%s)\n", ddp_get_module_name(dst_module));

	if (disp_helper_get_option(DISP_OPT_USE_M4U)) {
		M4U_PORT_STRUCT sPort;

		sPort.ePortID = M4U_PORT_DISP_RDMA1;
		sPort.Virtuality = disp_helper_get_option(DISP_OPT_USE_M4U);
		sPort.Security = 0;
		sPort.Distance = 1;
		sPort.Direction = 0;
		ret = m4u_config_port(&sPort);
		if (ret == 0) {
			DISPDBG("config M4U Port %s to %s SUCCESS\n",
				  ddp_get_module_name(DISP_MODULE_RDMA1),
				  disp_helper_get_option(DISP_OPT_USE_M4U) ? "virtual" :
				  "physical");
		} else {
			DISPERR("config M4U Port %s to %s FAIL(ret=%d)\n",
				  ddp_get_module_name(DISP_MODULE_RDMA1),
				  disp_helper_get_option(DISP_OPT_USE_M4U) ? "virtual" :
				  "physical", ret);
			return -1;
		}
	}

	dpmgr_set_lcm_utils(pgc->dpmgr_handle, pgc->plcm->drv);

	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE);

	return ret;
}

static void _cmdq_build_trigger_loop(void)
{
	int ret = 0;

	if (pgc->cmdq_handle_trigger == NULL) {
		cmdqRecCreate(CMDQ_SCENARIO_TRIGGER_LOOP, &(pgc->cmdq_handle_trigger));
		DISPMSG("primary path trigger thread cmd handle=%p\n", pgc->cmdq_handle_trigger);
	}
	cmdqRecReset(pgc->cmdq_handle_trigger);

	if (primary_display_is_video_mode()) {
		/* if (_need_lfr_check()) */
		/* ret = cmdqRecWait(pgc->cmdq_handle_trigger, CMDQ_EVENT_DISP_DSI0_EOF); */

		ddp_mutex_set_sof_wait(dpmgr_path_get_mutex(pgc->dpmgr_handle), pgc->cmdq_handle_trigger, 0);

		cmdqRecWaitNoClear(pgc->cmdq_handle_trigger, CMDQ_EVENT_DISP_RDMA0_EOF);
		cmdqRecWaitNoClear(pgc->cmdq_handle_trigger, CMDQ_EVENT_MUTEX0_STREAM_EOF);
		cmdqRecClearEventToken(pgc->cmdq_handle_trigger, CMDQ_EVENT_DISP_RDMA0_EOF);
		cmdqRecClearEventToken(pgc->cmdq_handle_trigger, CMDQ_EVENT_MUTEX0_STREAM_EOF);

		/* wait and clear rdma0_sof for vfp change */
		cmdqRecClearEventToken(pgc->cmdq_handle_trigger, CMDQ_EVENT_DISP_RDMA0_SOF);

		/* for some module(like COLOR) to read hw register to GPR after frame done */
		dpmgr_path_build_cmdq(pgc->dpmgr_handle, pgc->cmdq_handle_trigger,
				      CMDQ_AFTER_STREAM_EOF, 0);
	} else {
		/* DSI command mode doesn't have mutex_stream_eof, need use CMDQ token instead */
		ret = cmdqRecWait(pgc->cmdq_handle_trigger, CMDQ_SYNC_TOKEN_CONFIG_DIRTY);

		if (need_wait_esd_eof()) {
			/* Wait esd config thread done. */
			ret = cmdqRecWaitNoClear(pgc->cmdq_handle_trigger, CMDQ_SYNC_TOKEN_ESD_EOF);
		}
		/* ret = cmdqRecWait(pgc->cmdq_handle_trigger, CMDQ_EVENT_MDP_DSI0_TE_SOF); */
		/* for operations before frame transfer, such as waiting for DSI TE */
#ifndef CONFIG_FPGA_EARLY_PORTING		/* fpga has no TE signal */
		if (islcmconnected)
			dpmgr_path_build_cmdq(pgc->dpmgr_handle, pgc->cmdq_handle_trigger, CMDQ_WAIT_LCM_TE, 0);
#endif
		ret = cmdqRecWaitNoClear(pgc->cmdq_handle_trigger, CMDQ_SYNC_TOKEN_CABC_EOF);
		/* cleat frame done token, now the config thread will not allowed to config registers. */
		/* remember that config thread's priority is higher than trigger thread,
		 * so all the config queued before will be applied then STREAM_EOF token be cleared */
		/* this is what CMDQ did as "Merge" */
		ret = cmdqRecClearEventToken(pgc->cmdq_handle_trigger, CMDQ_SYNC_TOKEN_STREAM_EOF);

		ret = cmdqRecClearEventToken(pgc->cmdq_handle_trigger, CMDQ_SYNC_TOKEN_CONFIG_DIRTY);

		/* clear rdma EOF token before wait */
		ret = cmdqRecClearEventToken(pgc->cmdq_handle_trigger, CMDQ_EVENT_DISP_RDMA0_EOF);

		dpmgr_path_build_cmdq(pgc->dpmgr_handle, pgc->cmdq_handle_trigger, CMDQ_BEFORE_STREAM_SOF, 0);

		if (disp_helper_get_option(DISP_OPT_SODI_SUPPORT))
			exit_pd_by_cmdq(pgc->cmdq_handle_trigger);

		/* enable mutex, only cmd mode need this */
		/* this is what CMDQ did as "Trigger" */
		dpmgr_path_trigger(pgc->dpmgr_handle, pgc->cmdq_handle_trigger, CMDQ_ENABLE);
		if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER)) {
			/* for force_commit/bypass_shadow mode, need to get/release_mutex after enable_mutex */
			if (disp_helper_get_option(DISP_OPT_SHADOW_MODE) != 0) {
				dpmgr_path_mutex_get(pgc->dpmgr_handle, pgc->cmdq_handle_trigger);
				dpmgr_path_mutex_release(pgc->dpmgr_handle, pgc->cmdq_handle_trigger);
			}
		}
		dpmgr_path_build_cmdq(pgc->dpmgr_handle, pgc->cmdq_handle_trigger,
				      CMDQ_AFTER_STREAM_SOF, 1);

		/* waiting for frame done, because we can't use mutex stream eof here,
		 * so need to let dpmanager help to decide which event to wait */
		/* most time we wait rdmax frame done event. */
		ret = cmdqRecWait(pgc->cmdq_handle_trigger, CMDQ_EVENT_DISP_RDMA0_EOF);
		dpmgr_path_build_cmdq(pgc->dpmgr_handle, pgc->cmdq_handle_trigger,
				      CMDQ_WAIT_STREAM_EOF_EVENT, 0);

		/* dsi is not idle rightly after rdma frame done,
		 * so we need to polling about 1us for dsi returns to idle */
		/* do not polling dsi idle directly which will decrease CMDQ performance */
		dpmgr_path_build_cmdq(pgc->dpmgr_handle, pgc->cmdq_handle_trigger,
				      CMDQ_CHECK_IDLE_AFTER_STREAM_EOF, 0);

		/* for some module(like COLOR) to read hw register to GPR after frame done */
		dpmgr_path_build_cmdq(pgc->dpmgr_handle, pgc->cmdq_handle_trigger,
				      CMDQ_AFTER_STREAM_EOF, 0);

		if (disp_helper_get_option(DISP_OPT_SODI_SUPPORT))
			enter_pd_by_cmdq(pgc->cmdq_handle_trigger);

		/* polling DSI idle */
		/* ret = cmdqRecPoll(pgc->cmdq_handle_trigger, 0x1401b00c, 0, 0x80000000); */
		/* polling wdma frame done */
		/* ret = cmdqRecPoll(pgc->cmdq_handle_trigger, 0x140060A0, 1, 0x1); */

		/* now frame done, config thread is allowed to config register now */
		ret = cmdqRecSetEventToken(pgc->cmdq_handle_trigger, CMDQ_SYNC_TOKEN_STREAM_EOF);
		ret = cmdqRecSetEventToken(pgc->cmdq_handle_trigger, CMDQ_SYNC_TOKEN_CABC_EOF);
		/* RUN forever!!!! */
		BUG_ON(ret < 0);
	}

	/* dump trigger loop instructions to check whether dpmgr_path_build_cmdq works correctly */
	DISPCHECK("primary display BUILD cmdq trigger loop finished\n");

	return;

}

void disp_spm_enter_cg_mode(void)
{
	MMProfileLogEx(ddp_mmp_get_events()->cg_mode, MMProfileFlagPulse, 0, 0);
}

void disp_spm_enter_power_down_mode(void)
{
	MMProfileLogEx(ddp_mmp_get_events()->power_down_mode, MMProfileFlagPulse, 0, 0);
}

#if 0 /* defined but not used */
static void _cmdq_build_monitor_loop(void)
{
	int ret = 0;
	cmdqRecHandle g_cmdq_handle_monitor;

	cmdqRecCreate(CMDQ_SCENARIO_DISP_SCREEN_CAPTURE, &(g_cmdq_handle_monitor));
	DISPMSG("primary path monitor thread cmd handle=%p\n", g_cmdq_handle_monitor);

	cmdqRecReset(g_cmdq_handle_monitor);

	/* wait and clear stream_done, HW will assert mutex enable automatically in frame done reset. */
	/* todo: should let dpmanager to decide wait which mutex's eof. */
	ret = cmdqRecWait(g_cmdq_handle_monitor, CMDQ_EVENT_DISP_RDMA0_UNDERRUN);

	cmdqRecReadToDataRegister(g_cmdq_handle_monitor, 0x10006b0c,
				  CMDQ_DATA_REG_2D_SHARPNESS_1_DST);
	cmdqRecWriteFromDataRegister(g_cmdq_handle_monitor, CMDQ_DATA_REG_2D_SHARPNESS_1_DST,
				     0x1401b280);

	cmdqRecReadToDataRegister(g_cmdq_handle_monitor, 0x10006b08,
				  CMDQ_DATA_REG_2D_SHARPNESS_1_DST);
	cmdqRecWriteFromDataRegister(g_cmdq_handle_monitor, CMDQ_DATA_REG_2D_SHARPNESS_1_DST,
				     0x1401b284);

	cmdqRecReadToDataRegister(g_cmdq_handle_monitor, 0x10006b04,
				  CMDQ_DATA_REG_2D_SHARPNESS_1_DST);
	cmdqRecWriteFromDataRegister(g_cmdq_handle_monitor, CMDQ_DATA_REG_2D_SHARPNESS_1_DST,
				     0x1401b288);

	cmdqRecReadToDataRegister(g_cmdq_handle_monitor, 0x1401b16c,
				  CMDQ_DATA_REG_2D_SHARPNESS_1_DST);
	cmdqRecWriteFromDataRegister(g_cmdq_handle_monitor, CMDQ_DATA_REG_2D_SHARPNESS_1_DST,
				     0x1401b28C);

	ret = cmdqRecStartLoop(g_cmdq_handle_monitor);
}
#endif

void _cmdq_start_trigger_loop(void)
{
	int ret = 0;

	/* cmdqRecDumpCommand(pgc->cmdq_handle_trigger);*/
	/* this should be called only once because trigger loop will nevet stop */
	ret = cmdqRecStartLoop(pgc->cmdq_handle_trigger);
	if (!primary_display_is_video_mode()) {
		if (need_wait_esd_eof()) {
			/* Need set esd check eof synctoken to let trigger loop go. */
			cmdqCoreSetEvent(CMDQ_SYNC_TOKEN_ESD_EOF);
		}
		/* need to set STREAM_EOF for the first time, otherwise we will stuck in dead loop */
		cmdqCoreSetEvent(CMDQ_SYNC_TOKEN_STREAM_EOF);
		cmdqCoreSetEvent(CMDQ_SYNC_TOKEN_CABC_EOF);
		cmdqCoreSetEvent(CMDQ_EVENT_DISP_WDMA0_EOF);
		dprec_event_op(DPREC_EVENT_CMDQ_SET_EVENT_ALLOW);
	}

	DISPCHECK("primary display START cmdq trigger loop finished\n");

}

void _cmdq_stop_trigger_loop(void)
{
	int ret = 0;

	/* this should be called only once because trigger loop will nevet stop */
	ret = cmdqRecStopLoop(pgc->cmdq_handle_trigger);
	DISPCHECK("primary display STOP cmdq trigger loop finished\n");
}

static void _cmdq_set_config_handle_dirty(void)
{
	if (!primary_display_is_video_mode()) {
		dprec_logger_start(DPREC_LOGGER_PRIMARY_CMDQ_SET_DIRTY, 0, 0);
		/* only command mode need to set dirty */
		cmdqRecSetEventToken(pgc->cmdq_handle_config, CMDQ_SYNC_TOKEN_CONFIG_DIRTY);
		dprec_event_op(DPREC_EVENT_CMDQ_SET_DIRTY);
		dprec_logger_done(DPREC_LOGGER_PRIMARY_CMDQ_SET_DIRTY, 0, 0);
	}
}

static void _cmdq_handle_clear_dirty(cmdqRecHandle cmdq_handle)
{
	if (!primary_display_is_video_mode()) {
		dprec_logger_trigger(DPREC_LOGGER_PRIMARY_CMDQ_SET_DIRTY, 0, 1);
		cmdqRecClearEventToken(cmdq_handle, CMDQ_SYNC_TOKEN_CONFIG_DIRTY);
	}
}

static void _cmdq_set_config_handle_dirty_mira(void *handle)
{
	if (!primary_display_is_video_mode()) {
		dprec_logger_start(DPREC_LOGGER_PRIMARY_CMDQ_SET_DIRTY, 0, 0);
		/* only command mode need to set dirty */
		cmdqRecSetEventToken(handle, CMDQ_SYNC_TOKEN_CONFIG_DIRTY);
		dprec_event_op(DPREC_EVENT_CMDQ_SET_DIRTY);
		dprec_logger_done(DPREC_LOGGER_PRIMARY_CMDQ_SET_DIRTY, 0, 0);
	}
}

static void _cmdq_reset_config_handle(void)
{
	cmdqRecReset(pgc->cmdq_handle_config);
	dprec_event_op(DPREC_EVENT_CMDQ_RESET);
}

static void _cmdq_flush_config_handle(int blocking, CmdqAsyncFlushCB callback, unsigned int userdata)
{
	dprec_logger_start(DPREC_LOGGER_PRIMARY_CMDQ_FLUSH, blocking, (unsigned long)callback);
	if (blocking) {
		cmdqRecFlush(pgc->cmdq_handle_config);
	} else {
		if (callback)
			cmdqRecFlushAsyncCallback(pgc->cmdq_handle_config, callback, userdata);
		else
			cmdqRecFlushAsync(pgc->cmdq_handle_config);
	}

	dprec_event_op(DPREC_EVENT_CMDQ_FLUSH);
	dprec_logger_done(DPREC_LOGGER_PRIMARY_CMDQ_FLUSH, userdata, 0);

	if (dprec_option_enabled())
		cmdqRecDumpCommand(pgc->cmdq_handle_config);

}

static void _cmdq_flush_config_handle_mira(void *handle, int blocking)
{
	dprec_logger_start(DPREC_LOGGER_PRIMARY_CMDQ_FLUSH, 0, 0);
	if (blocking)
		cmdqRecFlush(handle);
	else
		cmdqRecFlushAsync(handle);

	dprec_event_op(DPREC_EVENT_CMDQ_FLUSH);
	dprec_logger_done(DPREC_LOGGER_PRIMARY_CMDQ_FLUSH, 0, 0);
}

void _cmdq_insert_wait_primary_path_frame_done(void *handle)
{
	if (primary_display_is_video_mode())
		cmdqRecWaitNoClear(handle, CMDQ_EVENT_MUTEX0_STREAM_EOF);
	else
		cmdqRecWaitNoClear(handle, CMDQ_SYNC_TOKEN_STREAM_EOF);
}

void _cmdq_insert_wait_frame_done_token_mira(void *handle)
{
	if (primary_display_is_video_mode()) {
		cmdqRecWaitNoClear(handle, CMDQ_EVENT_DISP_RDMA0_EOF);
		cmdqRecWaitNoClear(handle, CMDQ_EVENT_MUTEX0_STREAM_EOF);
		ddp_mutex_set_sof_wait(dpmgr_path_get_mutex(pgc->dpmgr_handle), handle, 0);
	} else {
		cmdqRecWaitNoClear(handle, CMDQ_SYNC_TOKEN_STREAM_EOF);
	}

	dprec_event_op(DPREC_EVENT_CMDQ_WAIT_STREAM_EOF);
}

static void update_frm_seq_info(unsigned int addr, unsigned int addr_offset, unsigned int seq,
				DISP_FRM_SEQ_STATE state)
{
	int i = 0;

	if (FRM_CONFIG == state) {
		frm_update_sequence[frm_update_cnt].state = state;
		frm_update_sequence[frm_update_cnt].mva = addr;
		frm_update_sequence[frm_update_cnt].max_offset = addr_offset;
		if (seq > 0)
			frm_update_sequence[frm_update_cnt].seq = seq;
		MMProfileLogEx(ddp_mmp_get_events()->primary_seq_config, MMProfileFlagPulse, addr,
			       seq);

	} else if (FRM_TRIGGER == state) {
		frm_update_sequence[frm_update_cnt].state = FRM_TRIGGER;
		MMProfileLogEx(ddp_mmp_get_events()->primary_seq_trigger, MMProfileFlagPulse,
			       frm_update_cnt, frm_update_sequence[frm_update_cnt].seq);

		dprec_logger_frame_seq_begin(pgc->session_id,
					     frm_update_sequence[frm_update_cnt].seq);

		frm_update_cnt++;
		frm_update_cnt %= FRM_UPDATE_SEQ_CACHE_NUM;


	} else if (FRM_START == state) {
		for (i = 0; i < FRM_UPDATE_SEQ_CACHE_NUM; i++) {
			if ((abs(addr - frm_update_sequence[i].mva) <= frm_update_sequence[i].max_offset)
			    && (frm_update_sequence[i].state == FRM_TRIGGER)) {
				MMProfileLogEx(ddp_mmp_get_events()->primary_seq_rdma_irq,
					       MMProfileFlagPulse, frm_update_sequence[i].mva,
					       frm_update_sequence[i].seq);
				frm_update_sequence[i].state = FRM_START;
				dprec_logger_frame_seq_end(pgc->session_id,
							   frm_update_sequence[i].seq);
				dprec_logger_frame_seq_begin(0, frm_update_sequence[i].seq);
				/* /break; */
			}
		}
	} else if (FRM_END == state) {
		for (i = 0; i < FRM_UPDATE_SEQ_CACHE_NUM; i++) {
			if (FRM_START == frm_update_sequence[i].state) {
				frm_update_sequence[i].state = FRM_END;
				dprec_logger_frame_seq_end(0, frm_update_sequence[i].seq);
				MMProfileLogEx(ddp_mmp_get_events()->primary_seq_release,
					       MMProfileFlagPulse, frm_update_sequence[i].mva,
					       frm_update_sequence[i].seq);

			}
		}
	}

}

static int _config_wdma_output(WDMA_CONFIG_STRUCT *wdma_config,
			       disp_path_handle disp_handle, cmdqRecHandle cmdq_handle)
{
	disp_ddp_path_config *pconfig = dpmgr_path_get_last_config(disp_handle);

	pconfig->wdma_config = *wdma_config;
	pconfig->wdma_dirty = 1;
	dpmgr_path_config(disp_handle, pconfig, cmdq_handle);
	return 0;
}

static int _config_rdma_input_data(RDMA_CONFIG_STRUCT *rdma_config,
				   disp_path_handle disp_handle, cmdqRecHandle cmdq_handle)
{
	disp_ddp_path_config *pconfig = dpmgr_path_get_last_config(disp_handle);

	pconfig->rdma_config = *rdma_config;
	pconfig->rdma_dirty = 1;
	dpmgr_path_config(disp_handle, pconfig, cmdq_handle);
	return 0;
}

static void directlink_path_add_memory(WDMA_CONFIG_STRUCT *p_wdma, DISP_MODULE_ENUM after_engine)
{
	int ret = 0;
	cmdqRecHandle cmdq_handle = NULL;
	cmdqRecHandle cmdq_wait_handle = NULL;
	disp_ddp_path_config *pconfig = NULL;

	/* create config thread */
	ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &cmdq_handle);
	if (ret != 0) {
		DISPERR("dl_to_dc capture:Fail to create cmdq handle\n");
		ret = -1;
		goto out;
	}
	cmdqRecReset(cmdq_handle);

	/* create wait thread */
	ret = cmdqRecCreate(CMDQ_SCENARIO_DISP_SCREEN_CAPTURE, &cmdq_wait_handle);
	if (ret != 0) {
		DISPERR("dl_to_dc capture:Fail to create cmdq wait handle\n");
		ret = -1;
		goto out;
	}
	cmdqRecReset(cmdq_wait_handle);

	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		dpmgr_path_mutex_get(pgc->dpmgr_handle, cmdq_handle);
	} else {
		/* configure config thread */
		_cmdq_insert_wait_frame_done_token_mira(cmdq_handle);
	}


	dpmgr_path_add_memout(pgc->dpmgr_handle, after_engine, cmdq_handle);

	pconfig = dpmgr_path_get_last_config(pgc->dpmgr_handle);
	primary_display_config_full_roi(pconfig, pgc->dpmgr_handle, cmdq_handle);
	pconfig->wdma_config = *p_wdma;

	if (disp_helper_get_option(DISP_OPT_DECOUPLE_MODE_USE_RGB565)) {
		pconfig->wdma_config.outputFormat = UFMT_RGB565;
		pconfig->wdma_config.dstPitch = pconfig->wdma_config.srcWidth * 2;
	}
	pconfig->wdma_dirty = 1;
	ret = dpmgr_path_config(pgc->dpmgr_handle, pconfig, cmdq_handle);

	_cmdq_set_config_handle_dirty_mira(cmdq_handle);
	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER)) {
		if (disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0)
			dpmgr_path_mutex_release(pgc->dpmgr_handle, cmdq_handle);
	}
	_cmdq_flush_config_handle_mira(cmdq_handle, 0);
	DISPDBG("dl_to_dc capture:Flush add memout mva(0x%lx)\n", p_wdma->dstAddress);

	/* wait wdma0 sof */
	cmdqRecWait(cmdq_wait_handle, CMDQ_EVENT_DISP_WDMA0_SOF);
	cmdqRecFlush(cmdq_wait_handle);
	DISPMSG("dl_to_dc capture:Flush wait wdma sof\n");
out:
	cmdqRecDestroy(cmdq_handle);
	cmdqRecDestroy(cmdq_wait_handle);
}


void disp_enable_emi_force_on(unsigned int enable, void *cmdq_handle)
{
}

static int _DL_switch_to_DC_fast(void)
{
	int ret = 0;
	DDP_SCENARIO_ENUM old_scenario, new_scenario;
	RDMA_CONFIG_STRUCT rdma_config = decouple_rdma_config;
	WDMA_CONFIG_STRUCT wdma_config = decouple_wdma_config;

	disp_ddp_path_config *data_config_dl = NULL;
	disp_ddp_path_config *data_config_dc = NULL;
	unsigned int mva = pgc->dc_buf[pgc->dc_buf_id];	/* mva for 1. ovl->wdma and 2.Rdma->dsi , */
	struct ddp_io_golden_setting_arg gset_arg;

	if (mva == 0) {
		DISPERR("%s, dc buffer does not exist\n", __func__);
		return -1;
	}
	wdma_config.dstAddress = mva;
	wdma_config.security = DISP_NORMAL_BUFFER;

	/* 1.save a temp frame to intermediate buffer */
	directlink_path_add_memory(&wdma_config, DISP_MODULE_OVL0);

	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_mode, MMProfileFlagPulse, 1, 0);

	/* 2.reset primary handle */
	_cmdq_reset_config_handle();
	_cmdq_handle_clear_dirty(pgc->cmdq_handle_config);

	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		/* release_mutex first for "pgc->cmdq_handle_config", because:
		 * If someone has get_mutex, then release it first
		 * If no one has get_mutex, then nothing happens
		 * TODO: create a new cmdq handle make it more simple
		 */
		dpmgr_path_mutex_release(pgc->dpmgr_handle, pgc->cmdq_handle_config);
		dpmgr_path_mutex_get(pgc->dpmgr_handle, pgc->cmdq_handle_config);
	} else {
		_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);
	}
	/* 3.modify interface path handle to new scenario(rdma->dsi) */
	old_scenario = dpmgr_get_scenario(pgc->dpmgr_handle);
	new_scenario = DDP_SCENARIO_PRIMARY_RDMA0_COLOR0_DISP;

	dpmgr_modify_path_power_on_new_modules(pgc->dpmgr_handle, new_scenario, 0);

	dpmgr_modify_path(pgc->dpmgr_handle, new_scenario, pgc->cmdq_handle_config,
			  primary_display_is_video_mode() ? DDP_VIDEO_MODE : DDP_CMD_MODE, 0);

	/* 4.config rdma from directlink mode to memory mode */
	rdma_config.address = mva;
	rdma_config.security = DISP_NORMAL_BUFFER;

	data_config_dl = dpmgr_path_get_last_config(pgc->dpmgr_handle);
	data_config_dl->rdma_config = rdma_config;
	data_config_dl->rdma_config.dst_x = 0;
	data_config_dl->rdma_config.dst_y = 0;
	data_config_dl->rdma_config.dst_h = data_config_dl->dst_h;
	data_config_dl->rdma_config.dst_w = data_config_dl->dst_w;
	data_config_dl->rdma_dirty = 1;

	/* no need ioctl because of rdma_dirty */
	set_is_dc(1);

	ret = dpmgr_path_config(pgc->dpmgr_handle, data_config_dl, pgc->cmdq_handle_config);

	screen_logger_add_message("sess_mode", MESSAGE_REPLACE, (char *)session_mode_spy(DISP_SESSION_DECOUPLE_MODE));
	dynamic_debug_msg_print(mva, rdma_config.width, rdma_config.height, rdma_config.pitch,
			UFMT_GET_Bpp(rdma_config.inputFormat));

	memset(&gset_arg, 0, sizeof(gset_arg));
	gset_arg.dst_mod_type = dpmgr_path_get_dst_module_type(pgc->dpmgr_handle);
	gset_arg.is_decouple_mode = 1;
	dpmgr_path_ioctl(pgc->dpmgr_handle, pgc->cmdq_handle_config, DDP_OVL_GOLDEN_SETTING, &gset_arg);

	/* screen_logger_add_message("sess_mode", MESSAGE_REPLACE, session_mode_spy(DISP_SESSION_DECOUPLE_MODE)); */
	/* dynamic_debug_msg_print(mva, rdma_config.width, rdma_config.height, rdma_config.pitch, */
	/* ovl_get_Bpp_from_dpColorFmt(rdma_config.inputFormat)); */

	/* 5. backup rdma address to slots */
	cmdqRecBackupUpdateSlot(pgc->cmdq_handle_config, pgc->rdma_buff_info, 0, rdma_config.address);
	cmdqRecBackupUpdateSlot(pgc->cmdq_handle_config, pgc->rdma_buff_info, 1, rdma_config.pitch);
	cmdqRecBackupUpdateSlot(pgc->cmdq_handle_config, pgc->rdma_buff_info, 2, rdma_config.inputFormat);
	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		dpmgr_path_mutex_release(pgc->dpmgr_handle, pgc->cmdq_handle_config);
	}
	/* 6 .flush to cmdq */
	_cmdq_set_config_handle_dirty();
	_cmdq_flush_config_handle(1, NULL, 0);

	dpmgr_modify_path_power_off_old_modules(old_scenario, new_scenario, 0);

	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_mode, MMProfileFlagPulse, 2, 0);

	/* ddp_mmp_rdma_layer(&rdma_config, 0,  20, 20); */

	/* 7.reset  cmdq */
	_cmdq_reset_config_handle();
	_cmdq_handle_clear_dirty(pgc->cmdq_handle_config);

	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		dpmgr_path_mutex_get(pgc->dpmgr_handle, pgc->cmdq_handle_config);
	} else {
		_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);
	}

	/* 9. create ovl2mem path handle */
	cmdqRecReset(pgc->cmdq_handle_ovl1to2_config);

	pgc->ovl2mem_path_handle =
		dpmgr_create_path(DDP_SCENARIO_PRIMARY_OVL_MEMOUT, pgc->cmdq_handle_ovl1to2_config);

	if (pgc->ovl2mem_path_handle) {
		DISPDBG("dpmgr create ovl memout path SUCCESS(%p)\n", pgc->ovl2mem_path_handle);
	} else {
		DISPERR("dpmgr create path FAIL\n");
		return -1;
	}
	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		/* If full shadow mode, get/release_mutex to surround all configs */
		dpmgr_path_mutex_get(pgc->ovl2mem_path_handle, pgc->cmdq_handle_ovl1to2_config);
	}

	dpmgr_path_set_video_mode(pgc->ovl2mem_path_handle, 0);
	dpmgr_path_init(pgc->ovl2mem_path_handle, CMDQ_ENABLE);

	data_config_dc = dpmgr_path_get_last_config(pgc->ovl2mem_path_handle);
	data_config_dc->dst_w = rdma_config.width;
	data_config_dc->dst_h = rdma_config.height;
	data_config_dc->dst_dirty = 1;

	/* move ovl config info from dl to dc */
	memcpy(data_config_dc->ovl_config, data_config_dl->ovl_config,
	       sizeof(data_config_dl->ovl_config));

	ret = dpmgr_path_config(pgc->ovl2mem_path_handle, data_config_dc,
				pgc->cmdq_handle_ovl1to2_config);

	memset(&gset_arg, 0, sizeof(gset_arg));
	gset_arg.dst_mod_type = dpmgr_path_get_dst_module_type(pgc->ovl2mem_path_handle);
	gset_arg.is_decouple_mode = 1;
	dpmgr_path_ioctl(pgc->ovl2mem_path_handle, pgc->cmdq_handle_ovl1to2_config, DDP_OVL_GOLDEN_SETTING, &gset_arg);

	ret = dpmgr_path_start(pgc->ovl2mem_path_handle, CMDQ_ENABLE);

	/* cmdqRecDumpCommand(pgc->cmdq_handle_ovl1to2_config); */
	/* cmdqRecClearEventToken(pgc->cmdq_handle_ovl1to2_config, CMDQ_EVENT_DISP_WDMA0_EOF);*/
	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		dpmgr_path_mutex_release(pgc->ovl2mem_path_handle, pgc->cmdq_handle_ovl1to2_config);
	}
	_cmdq_flush_config_handle_mira(pgc->cmdq_handle_ovl1to2_config, 0);
	cmdqRecReset(pgc->cmdq_handle_ovl1to2_config);
	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		/* If full shadow mode, get/release_mutex to surround all configs
		 * For the first frame of decouple mode */
		dpmgr_path_mutex_get(pgc->ovl2mem_path_handle, pgc->cmdq_handle_ovl1to2_config);
	} else {
		cmdqRecWait(pgc->cmdq_handle_ovl1to2_config, CMDQ_EVENT_DISP_WDMA0_EOF);
	}

	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_mode, MMProfileFlagPulse, 3, 0);

	/* 11..enable event for new path */
	/* dpmgr_enable_event(pgc->ovl2mem_path_handle, DISP_PATH_EVENT_FRAME_COMPLETE); */
	/* dpmgr_map_event_to_irq(pgc->ovl2mem_path_handle, DISP_PATH_EVENT_FRAME_START,
	   DDP_IRQ_WDMA0_FRAME_COMPLETE); */
	/* dpmgr_enable_event(pgc->ovl2mem_path_handle, DISP_PATH_EVENT_FRAME_START); */

	if (primary_display_is_video_mode()) {
		if (_need_lfr_check()) {
			dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
					       DDP_IRQ_DSI0_FRAME_DONE);
		} else {
			dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
					       DDP_IRQ_RDMA0_DONE);
		}
	}
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);
	/* dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE); */
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_START);

	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_mode, MMProfileFlagPulse, 4, 0);

	return ret;
}

static int DL_switch_to_DC_fast(int sw_only)
{
	int ret = 0;

	if (!sw_only)
		ret = _DL_switch_to_DC_fast();
	else
		ret = -1; /* not support yet */

	return ret;
}

static int modify_path_power_off_callback(unsigned long userdata)
{
	DDP_SCENARIO_ENUM old_scenario, new_scenario;
	int layer;

	old_scenario = userdata >> 16;
	new_scenario = userdata & ((1 << 16) - 1);
	dpmgr_modify_path_power_off_old_modules(old_scenario, new_scenario, 0);

	/* release output buffer */
	layer = disp_sync_get_output_interface_timeline_id();
	mtkfb_release_layer_fence(primary_session_id, layer);
	return 0;
}

static int _DC_switch_to_DL_fast(void)
{
	int ret = 0;
	int layer = 0;
	disp_ddp_path_config *data_config_dl = NULL;
	disp_ddp_path_config *data_config_dc = NULL;
	DDP_SCENARIO_ENUM old_scenario, new_scenario;
	struct ddp_io_golden_setting_arg gset_arg;

	/* 3.destroy ovl->mem path. */
	data_config_dc = dpmgr_path_get_last_config(pgc->ovl2mem_path_handle);
	data_config_dl = dpmgr_path_get_last_config(pgc->dpmgr_handle);

	/* copy ovl config from DC handle to DL handle */;
	memcpy(data_config_dl->ovl_config, data_config_dc->ovl_config, sizeof(data_config_dl->ovl_config));

	/* wait and get_mutex in the last frame configs */

	dpmgr_path_deinit(pgc->ovl2mem_path_handle, (unsigned long)(pgc->cmdq_handle_ovl1to2_config));
	dpmgr_destroy_path(pgc->ovl2mem_path_handle, pgc->cmdq_handle_ovl1to2_config);
	/* clear sof token for next dl to dc */
	cmdqRecClearEventToken(pgc->cmdq_handle_ovl1to2_config, CMDQ_EVENT_DISP_WDMA0_SOF);

	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		dpmgr_path_mutex_release(pgc->ovl2mem_path_handle, pgc->cmdq_handle_ovl1to2_config);
	}

	_cmdq_flush_config_handle_mira(pgc->cmdq_handle_ovl1to2_config, 1);
	cmdqRecReset(pgc->cmdq_handle_ovl1to2_config);
	pgc->ovl2mem_path_handle = NULL;

	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_mode, MMProfileFlagPulse, 1, 1);

	/* release output buffer */
	layer = disp_sync_get_output_timeline_id();
	mtkfb_release_layer_fence(primary_session_id, layer);

	/* 4.modify interface path handle to new scenario(rdma->dsi) */
	_cmdq_reset_config_handle();
	_cmdq_handle_clear_dirty(pgc->cmdq_handle_config);

	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		/* release_mutex first for "pgc->cmdq_handle_config", because:
		 * If someone has get_mutex, then release it first
		 * If no one has get_mutex, then nothing happens
		 * TODO: create a new cmdq handle make it more simple
		 */
		dpmgr_path_mutex_release(pgc->dpmgr_handle, pgc->cmdq_handle_config);
		dpmgr_path_mutex_get(pgc->dpmgr_handle, pgc->cmdq_handle_config);
	} else {
		_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);
	}

	old_scenario = dpmgr_get_scenario(pgc->dpmgr_handle);
	new_scenario = DDP_SCENARIO_PRIMARY_DISP;

	dpmgr_modify_path_power_on_new_modules(pgc->dpmgr_handle, new_scenario, 0);

	dpmgr_modify_path(pgc->dpmgr_handle, new_scenario, pgc->cmdq_handle_config,
			  primary_display_is_video_mode() ? DDP_VIDEO_MODE : DDP_CMD_MODE, 0);

	/* 5.config rdma from memory mode to directlink mode */
	data_config_dl->rdma_config = decouple_rdma_config;
	data_config_dl->rdma_config.address = 0;
	data_config_dl->rdma_config.pitch = 0;
	data_config_dl->rdma_config.security = DISP_NORMAL_BUFFER;
	data_config_dl->rdma_dirty = 1;
	data_config_dl->dst_dirty = 1;

	/* no need ioctl because of rdma_dirty */
	set_is_dc(0);

	ret = dpmgr_path_config(pgc->dpmgr_handle, data_config_dl, pgc->cmdq_handle_config);

	memset(&gset_arg, 0, sizeof(gset_arg));
	gset_arg.dst_mod_type = dpmgr_path_get_dst_module_type(pgc->dpmgr_handle);
	gset_arg.is_decouple_mode = 0;
	dpmgr_path_ioctl(pgc->dpmgr_handle, pgc->cmdq_handle_config, DDP_OVL_GOLDEN_SETTING, &gset_arg);


	cmdqRecBackupUpdateSlot(pgc->cmdq_handle_config, pgc->rdma_buff_info, 0, 0);

	/* cmdqRecDumpCommand(pgc->cmdq_handle_config); */
	_cmdq_set_config_handle_dirty();

	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		dpmgr_path_mutex_release(pgc->dpmgr_handle, pgc->cmdq_handle_config);
	}
	/* if blocking flush won't cause UX issue, we should simplify this code: remove callback
	 * else we should move disable_sodi to callback, and change to nonblocking flush */
	_cmdq_flush_config_handle(0, modify_path_power_off_callback, (old_scenario << 16) | new_scenario);
	/* modify_path_power_off_callback((old_scenario << 16) | new_scenario); */

	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_mode, MMProfileFlagPulse, 2, 1);

	_cmdq_reset_config_handle();
	_cmdq_handle_clear_dirty(pgc->cmdq_handle_config);

	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		dpmgr_path_mutex_release(pgc->dpmgr_handle, pgc->cmdq_handle_config);
	} else {
		_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);
	}

	/* 9.enable event for new path */
	if (primary_display_is_video_mode()) {
		dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
				       DDP_IRQ_RDMA0_DONE);
	}
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE);
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_START);

	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_mode, MMProfileFlagPulse, 3, 1);

/* out: */
	return ret;
}

static int _DC_switch_to_DL_sw_only(void)
{
	int ret = 0;
	int layer = 0;
	disp_ddp_path_config *data_config_dc = NULL;
	disp_ddp_path_config *data_config_dl = NULL;
	DDP_SCENARIO_ENUM old_scenario, new_scenario;

	/* 3.destroy ovl->mem path. */
	data_config_dc = dpmgr_path_get_last_config(pgc->ovl2mem_path_handle);
	data_config_dl = dpmgr_path_get_last_config(pgc->dpmgr_handle);

	dpmgr_destroy_path_handle(pgc->ovl2mem_path_handle);

	cmdqRecReset(pgc->cmdq_handle_ovl1to2_config);
	pgc->ovl2mem_path_handle = NULL;

	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_mode, MMProfileFlagPulse, 1, 1);

	/* release output buffer */
	layer = disp_sync_get_output_timeline_id();
	mtkfb_release_layer_fence(primary_session_id, layer);

	/* 4.modify interface path handle to new scenario(rdma->dsi) */
	_cmdq_reset_config_handle();
	_cmdq_handle_clear_dirty(pgc->cmdq_handle_config);
	_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);

	old_scenario = dpmgr_get_scenario(pgc->dpmgr_handle);
	new_scenario = DDP_SCENARIO_PRIMARY_DISP;
	dpmgr_modify_path_power_on_new_modules(pgc->dpmgr_handle, new_scenario, 1);
	dpmgr_modify_path(pgc->dpmgr_handle, new_scenario, pgc->cmdq_handle_config,
			  primary_display_is_video_mode() ? DDP_VIDEO_MODE : DDP_CMD_MODE, 1);
	dpmgr_modify_path_power_off_old_modules(old_scenario, new_scenario, 1);

	/* 5.config rdma from memory mode to directlink mode */
	data_config_dl->rdma_config = decouple_rdma_config;
	data_config_dl->rdma_config.address = 0;
	data_config_dl->rdma_config.pitch = 0;
	data_config_dl->rdma_config.security = DISP_NORMAL_BUFFER;
	/* no need ioctl because of rdma_dirty */
	set_is_dc(0);

	/* release output buffer */
	layer = disp_sync_get_output_interface_timeline_id();
	mtkfb_release_layer_fence(primary_session_id, layer);

	_cmdq_reset_config_handle();
	_cmdq_handle_clear_dirty(pgc->cmdq_handle_config);
	_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);

	/* 9.enable event for new path */
	if (primary_display_is_video_mode()) {
		if (_need_lfr_check()) {
			dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
					       DDP_IRQ_DSI0_FRAME_DONE);
		} else {
			dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
					       DDP_IRQ_RDMA0_DONE);
		}
	}
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE);
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_START);

	if (!primary_display_is_video_mode())
		_cmdq_build_trigger_loop();

	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_mode, MMProfileFlagPulse, 3, 1);

	return ret;
}

static int DC_switch_to_DL_fast(int sw_only)
{
	int ret = 0;

	if (!sw_only)
		ret = _DC_switch_to_DL_fast();
	else
		ret = _DC_switch_to_DL_sw_only();

	return ret;
}

static int DL_switch_to_rdma_mode(cmdqRecHandle handle, int block)
{
	int ret;
	DDP_SCENARIO_ENUM old_scenario, new_scenario;
	int need_flush = 0;
	struct ddp_io_golden_setting_arg gset_arg;

	if (!handle) {
		ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &handle);
		if (ret) {
			DISPERR("%s:%d, create cmdq handle fail!ret=%d\n", __func__, __LINE__, ret);
			return -1;
		}
		_cmdq_insert_wait_frame_done_token_mira(handle);
		need_flush = 1;
	}

	old_scenario = dpmgr_get_scenario(pgc->dpmgr_handle);
	new_scenario = DDP_SCENARIO_PRIMARY_RDMA0_COLOR0_DISP;
	dpmgr_modify_path_power_on_new_modules(pgc->dpmgr_handle, new_scenario, 0);
	dpmgr_modify_path(pgc->dpmgr_handle, new_scenario, handle,
			primary_display_is_video_mode() ? DDP_VIDEO_MODE : DDP_CMD_MODE, 0);
	dpmgr_modify_path_power_off_old_modules(old_scenario, new_scenario, 0);

	memset(&gset_arg, 0, sizeof(gset_arg));
	gset_arg.dst_mod_type = dpmgr_path_get_dst_module_type(pgc->dpmgr_handle);
	gset_arg.is_decouple_mode = 1;
	dpmgr_path_ioctl(pgc->dpmgr_handle, handle, DDP_OVL_GOLDEN_SETTING, &gset_arg);

	if (need_flush) {
		if (block)
			cmdqRecFlush(handle);
		else
			cmdqRecFlushAsync(handle);
		cmdqRecDestroy(handle);
	}

	return 0;
}

static int rdma_mode_switch_to_DL(cmdqRecHandle handle, int block)
{
	int ret;
	DDP_SCENARIO_ENUM old_scenario, new_scenario;
	int need_flush = 0;
	disp_ddp_path_config *pconfig;
	struct ddp_io_golden_setting_arg gset_arg;

	if (!handle) {
		ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &handle);
		if (ret) {
			DISPERR("%s:%d, create cmdq handle fail!ret=%d\n", __func__, __LINE__, ret);
			return -1;
		}
		_cmdq_insert_wait_frame_done_token_mira(handle);
		need_flush = 1;
	}

	old_scenario = dpmgr_get_scenario(pgc->dpmgr_handle);
	new_scenario = DDP_SCENARIO_PRIMARY_DISP;
	dpmgr_modify_path_power_on_new_modules(pgc->dpmgr_handle, new_scenario, 0);
	dpmgr_modify_path(pgc->dpmgr_handle, new_scenario, handle,
			primary_display_is_video_mode() ? DDP_VIDEO_MODE : DDP_CMD_MODE, 0);
	dpmgr_modify_path_power_off_old_modules(old_scenario, new_scenario, 0);

	/* set rdma to DL mode */
	pconfig = dpmgr_path_get_last_config(pgc->dpmgr_handle);
	pconfig->rdma_config.address = 0;
	pconfig->rdma_config.pitch = 0;
	pconfig->rdma_config.width = pconfig->dst_w;
	pconfig->rdma_config.height = pconfig->dst_h;
	pconfig->rdma_config.security = DISP_NORMAL_BUFFER;
	pconfig->rdma_dirty = 1;
	pconfig->dst_dirty = 1;
	if (need_flush) {
		/* re-config ovl because ovl is new-coming */
		pconfig->ovl_dirty = 1;
	}

	/* no need ioctl because of rdma_dirty */
	set_is_dc(0);

	ret = dpmgr_path_config(pgc->dpmgr_handle, pconfig, handle);
	/* clear dirty set by this func */
	pconfig = dpmgr_path_get_last_config(pgc->dpmgr_handle);

	memset(&gset_arg, 0, sizeof(gset_arg));
	gset_arg.dst_mod_type = dpmgr_path_get_dst_module_type(pgc->dpmgr_handle);
	gset_arg.is_decouple_mode = 0;
	dpmgr_path_ioctl(pgc->dpmgr_handle, handle, DDP_OVL_GOLDEN_SETTING, &gset_arg);

	if (need_flush) {
		if (block)
			cmdqRecFlush(handle);
		else
			cmdqRecFlushAsync(handle);
		cmdqRecDestroy(handle);
	}

	return 0;
}

static int config_display_m4u_port(void)
{
	int ret = 0;
	M4U_PORT_STRUCT sPort;
	char *m4u_usage = disp_helper_get_option(DISP_OPT_USE_M4U) ? "virtual" : "physical";

	sPort.ePortID = M4U_PORT_DISP_OVL0;
	sPort.Virtuality = disp_helper_get_option(DISP_OPT_USE_M4U);
	sPort.Security = 0;
	sPort.Distance = 1;
	sPort.Direction = 0;
	ret = m4u_config_port(&sPort);
	if (ret) {
		DISPERR("config M4U Port %s to %s FAIL(ret=%d)\n",
			  ddp_get_module_name(DISP_MODULE_OVL0), m4u_usage, ret);
		return -1;
	}

	sPort.ePortID = M4U_PORT_DISP_2L_OVL0_LARB4;
	ret = m4u_config_port(&sPort);
	if (ret) {
		DISPERR("config M4U Port %s to %s FAIL(ret=%d)\n",
			  ddp_get_module_name(DISP_MODULE_OVL0_2L), m4u_usage, ret);
		return -1;
	}

	sPort.ePortID = M4U_PORT_DISP_2L_OVL0_LARB0;
	ret = m4u_config_port(&sPort);
	if (ret) {
		DISPERR("config M4U Port %s to %s FAIL(ret=%d)\n",
			  ddp_get_module_name(DISP_MODULE_OVL0_2L), m4u_usage, ret);
		return -1;
	}

	sPort.ePortID = M4U_PORT_DISP_2L_OVL1;
	ret = m4u_config_port(&sPort);
	if (ret) {
		DISPERR("config M4U Port %s to %s FAIL(ret=%d)\n",
			  ddp_get_module_name(DISP_MODULE_OVL0_2L), m4u_usage, ret);
		return -1;
	}

	sPort.ePortID = M4U_PORT_DISP_RDMA0;
	ret = m4u_config_port(&sPort);
	if (ret) {
		DISPERR("config M4U Port %s to %s FAIL(ret=%d)\n",
			  ddp_get_module_name(DISP_MODULE_RDMA0), m4u_usage, ret);
		return -1;
	}

	sPort.ePortID = M4U_PORT_DISP_WDMA0;
	ret = m4u_config_port(&sPort);
	if (ret) {
		DISPERR("config M4U Port %s to %s FAIL(ret=%d)\n",
			  ddp_get_module_name(DISP_MODULE_WDMA0), m4u_usage, ret);
		return -1;
	}
	return ret;
}

static disp_internal_buffer_info *allocat_decouple_buffer(int size)
{
	void *buffer_va = NULL;
	unsigned int buffer_mva = 0;
	unsigned int mva_size = 0;

	struct ion_mm_data mm_data;

	struct ion_client *client = NULL;
	struct ion_handle *handle = NULL;
	disp_internal_buffer_info *buf_info = NULL;

	memset((void *)&mm_data, 0, sizeof(struct ion_mm_data));
	client = ion_client_create(g_ion_device, "disp_decouple");

	buf_info = kzalloc(sizeof(disp_internal_buffer_info), GFP_KERNEL);
	if (buf_info) {
		handle = ion_alloc(client, size, 0, ION_HEAP_MULTIMEDIA_MASK, 0);
		if (IS_ERR(handle)) {
			DISPERR("Fatal Error, ion_alloc for size %d failed\n", size);
			ion_free(client, handle);
			ion_client_destroy(client);
			kfree(buf_info);
			return NULL;
		}

		buffer_va = ion_map_kernel(client, handle);
		if (buffer_va == NULL) {
			DISPERR("ion_map_kernrl failed\n");
			ion_free(client, handle);
			ion_client_destroy(client);
			kfree(buf_info);
			return NULL;
		}
		mm_data.config_buffer_param.kernel_handle = handle;
		mm_data.mm_cmd = ION_MM_CONFIG_BUFFER;
		if (ion_kernel_ioctl(client, ION_CMD_MULTIMEDIA, (unsigned long)&mm_data) < 0) {
			DISPERR("ion_test_drv: Config buffer failed.\n");
			ion_free(client, handle);
			ion_client_destroy(client);
			kfree(buf_info);
			return NULL;
		}

		ion_phys(client, handle, (unsigned long int *)&buffer_mva, (size_t *)&mva_size);
		if (buffer_mva == 0) {
			DISPERR("Fatal Error, get mva failed\n");
			ion_free(client, handle);
			ion_client_destroy(client);
			kfree(buf_info);
			return NULL;
		}
		buf_info->handle = handle;
		buf_info->mva = buffer_mva;
		buf_info->size = mva_size;
		buf_info->va = buffer_va;
	} else {
		DISPERR("Fatal error, kzalloc internal buffer info failed!!\n");
		kfree(buf_info);
		return NULL;
	}
	return buf_info;
}

static int init_decouple_buffers(void)
{
	int i = 0;
	enum UNIFIED_COLOR_FMT fmt = UFMT_RGB888;
	int height = disp_helper_get_option(DISP_OPT_FAKE_LCM_HEIGHT);
	int width = disp_helper_get_option(DISP_OPT_FAKE_LCM_WIDTH);
	int Bpp = UFMT_GET_Bpp(fmt);

	int buffer_size = width * height * Bpp;

	for (i = 0; i < DISP_INTERNAL_BUFFER_COUNT; i++) {	/* INTERNAL Buf 3 frames */
		decouple_buffer_info[i] = allocat_decouple_buffer(buffer_size);
		if (decouple_buffer_info[i] != NULL)
			pgc->dc_buf[i] = decouple_buffer_info[i]->mva;

	}

	/* initialize rdma config */
	decouple_rdma_config.height = height;
	decouple_rdma_config.width = width;
	decouple_rdma_config.idx = 0;
	decouple_rdma_config.inputFormat = fmt;
	decouple_rdma_config.pitch = width * Bpp;
	decouple_rdma_config.security = DISP_NORMAL_BUFFER;
	decouple_rdma_config.dst_x = 0;
	decouple_rdma_config.dst_y = 0;
	decouple_rdma_config.dst_w = disp_helper_get_option(DISP_OPT_FAKE_LCM_WIDTH);
	decouple_rdma_config.dst_h = disp_helper_get_option(DISP_OPT_FAKE_LCM_HEIGHT);

	/* initialize wdma config */
	decouple_wdma_config.srcHeight = height;
	decouple_wdma_config.srcWidth = width;
	decouple_wdma_config.clipX = 0;
	decouple_wdma_config.clipY = 0;
	decouple_wdma_config.clipHeight = height;
	decouple_wdma_config.clipWidth = width;
	decouple_wdma_config.outputFormat = fmt;
	decouple_wdma_config.useSpecifiedAlpha = 1;
	decouple_wdma_config.alpha = 0xFF;
	decouple_wdma_config.dstPitch = width * Bpp;
	decouple_wdma_config.security = DISP_NORMAL_BUFFER;

	pr_warn("%s done\n", __func__);
	return 0;

}

static int _init_decouple_buffers_thread(void *data)
{
	init_decouple_buffers();
	return 0;
}

static int _build_path_direct_link(void)
{
	int ret = 0;

	DISPFUNC();
	pgc->mode = DIRECT_LINK_MODE;

	pgc->dpmgr_handle = dpmgr_create_path(DDP_SCENARIO_PRIMARY_DISP, pgc->cmdq_handle_config);
	if (pgc->dpmgr_handle) {
		DISPDBG("dpmgr create path SUCCESS(%p)\n", pgc->dpmgr_handle);
	} else {
		DISPERR("dpmgr create path FAIL\n");
		return -1;
	}
	dpmgr_set_lcm_utils(pgc->dpmgr_handle, pgc->plcm->drv);

	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE);
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_START);

	return ret;
}

static int _convert_disp_input_to_ovl(OVL_CONFIG_STRUCT *dst, disp_input_config *src)
{
	int ret = 0;
	int force_disable_alpha = 0;
	enum UNIFIED_COLOR_FMT tmp_fmt;
	unsigned int Bpp = 0;

	if (!src || !dst) {
		disp_aee_print("%s src(0x%p) or dst(0x%p) is null\n", __func__, src, dst);
		return -1;
	}

	dst->layer = src->layer_id;
	dst->isDirty = 1;
	dst->buff_idx = src->next_buff_idx;
	dst->layer_en = src->layer_enable;

	/* if layer is disable, we just needs config above params. */
	if (!src->layer_enable)
		return 0;

	tmp_fmt = disp_fmt_to_unified_fmt(src->src_fmt);
	/* display don't support X channel, like XRGB8888
	 * we need to enable const_bld*/
	ufmt_disable_X_channel(tmp_fmt, &dst->fmt, &dst->const_bld);
#if 0
	if (tmp_fmt != dst->fmt)
		force_disable_alpha = 1;
#endif
	Bpp = UFMT_GET_Bpp(dst->fmt);

	dst->addr = (unsigned long)(src->src_phy_addr);
	dst->vaddr = (unsigned long)(src->src_base_addr);
	dst->src_x = src->src_offset_x;
	dst->src_y = src->src_offset_y;
	dst->src_w = src->src_width;
	dst->src_h = src->src_height;
	dst->src_pitch = src->src_pitch * Bpp;
	dst->dst_x = src->tgt_offset_x;
	dst->dst_y = src->tgt_offset_y;

	/* dst W/H should <= src W/H */
	dst->dst_w = min(src->src_width, src->tgt_width);
	dst->dst_h = min(src->src_height, src->tgt_height);

	dst->keyEn = src->src_use_color_key;
	dst->key = src->src_color_key;

	dst->aen = force_disable_alpha ? 0 : src->alpha_enable;
	dst->sur_aen = force_disable_alpha ? 0 : src->sur_aen;

	dst->alpha = src->alpha;
	dst->src_alpha = src->src_alpha;
	dst->dst_alpha = src->dst_alpha;

	dst->identity = src->identity;
	dst->connected_type = src->connected_type;
	dst->security = src->security;
	dst->yuv_range = src->yuv_range;

	if (src->buffer_source == DISP_BUFFER_ALPHA) {
		dst->source = OVL_LAYER_SOURCE_RESERVED;	/* dim layer, constant alpha */
	} else if (src->buffer_source == DISP_BUFFER_ION || src->buffer_source == DISP_BUFFER_MVA) {
		dst->source = OVL_LAYER_SOURCE_MEM;	/* from memory */
	} else {
		DISPERR("unknown source = %d", src->buffer_source);
		dst->source = OVL_LAYER_SOURCE_MEM;
	}
	dst->ext_sel_layer = src->ext_sel_layer;
	return ret;
}

#if 0 /* defined but not used */
static int _convert_disp_input_to_rdma(RDMA_CONFIG_STRUCT *dst,
				       disp_session_input_config *session_input)
{
	pr_err("%s not implement yet!!!!!!!!!!!!!!!\n", __func__);
	return -1;
}
#endif

int _trigger_display_interface(int blocking, void *callback, unsigned int userdata)
{
	int ret;

	DISPFUNC();
	if (_should_wait_path_idle()) {
		ret = dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ * 1);
		if (ret <= 0)
			primary_display_diagnose();
	}

	if (_should_update_lcm()) {
		int x = disp_helper_get_option(DISP_OPT_FAKE_LCM_X);
		int y = disp_helper_get_option(DISP_OPT_FAKE_LCM_Y);
		int width = disp_helper_get_option(DISP_OPT_FAKE_LCM_WIDTH);
		int height = disp_helper_get_option(DISP_OPT_FAKE_LCM_HEIGHT);

		disp_lcm_update(pgc->plcm, x, y, width, height, 0);
	}

	if (_should_start_path())
		dpmgr_path_start(pgc->dpmgr_handle, primary_display_cmdq_enabled());

	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		dpmgr_path_mutex_release(pgc->dpmgr_handle, pgc->cmdq_handle_config);
	}

	if (_should_trigger_path()) {
#ifndef CONFIG_FPGA_EARLY_PORTING	/* fpga has no vsync */
		if (islcmconnected)
			dpmgr_wait_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);
#endif
		dpmgr_path_trigger(pgc->dpmgr_handle, NULL, primary_display_cmdq_enabled());
	}

	if (_should_set_cmdq_dirty())
		_cmdq_set_config_handle_dirty();

	if (_should_flush_cmdq_config_handle())
		_cmdq_flush_config_handle(blocking, callback, userdata);

	if (_should_reset_cmdq_config_handle())
		_cmdq_reset_config_handle();

	/* clear cmdq dirty in case trigger loop starts here */
	if (_should_set_cmdq_dirty())
		_cmdq_handle_clear_dirty(pgc->cmdq_handle_config);

	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		/* next frame started here */
		dpmgr_path_mutex_get(pgc->dpmgr_handle, pgc->cmdq_handle_config);
	} else {
		if (_should_insert_wait_frame_done_token())
			_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);
	}

	if (_need_lfr_check())
		dpmgr_path_build_cmdq(pgc->dpmgr_handle, pgc->cmdq_handle_config, CMDQ_DSI_LFR_MODE, 0);

	return 0;
}

int _trigger_ovl_to_memory(disp_path_handle disp_handle,
				  cmdqRecHandle cmdq_handle,
				  CmdqAsyncFlushCB callback, unsigned int data)
{
	int layer = 0;
	unsigned int rdma_pitch_sec;

	MMProfileLogEx(ddp_mmp_get_events()->ovl_trigger, MMProfileFlagStart, 0, data);

	/* If full shadow mode: enable mutex in dpmgr_path_trigger() will signal sof
	 * If force_commit/bypass mode: enable_mutex before get/release_mutex
	 * to copy configs from shadow into working
	 */
	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		dpmgr_path_mutex_release(disp_handle, cmdq_handle);
	}
	dpmgr_path_trigger(disp_handle, cmdq_handle, CMDQ_ENABLE);
	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) != 0) {
		/* If force_commit/bypass mode, get/release_mutex after enable_mutex */
		dpmgr_path_mutex_get(disp_handle, cmdq_handle);
		dpmgr_path_mutex_release(disp_handle, cmdq_handle);
	}

	cmdqRecWaitNoClear(cmdq_handle, CMDQ_EVENT_DISP_WDMA0_EOF);

	layer = disp_sync_get_output_timeline_id();
	cmdqRecBackupUpdateSlot(cmdq_handle, pgc->cur_config_fence, layer, mem_config.buff_idx);

	layer = disp_sync_get_output_interface_timeline_id();
	cmdqRecBackupUpdateSlot(cmdq_handle, pgc->cur_config_fence, layer,
				mem_config.interface_idx);

	cmdqRecBackupUpdateSlot(cmdq_handle, pgc->rdma_buff_info, 0, (unsigned int)mem_config.addr);

	/* rdma pitch only use bit[15..0], we use bit[31:30] to store secure information */
	rdma_pitch_sec = mem_config.pitch | (mem_config.security << 30);
	cmdqRecBackupUpdateSlot(cmdq_handle, pgc->rdma_buff_info, 1, rdma_pitch_sec);
	cmdqRecBackupUpdateSlot(cmdq_handle, pgc->rdma_buff_info, 2, (unsigned int)mem_config.fmt);

	cmdqRecFlushAsyncCallback(cmdq_handle, callback, data);
	cmdqRecReset(cmdq_handle);
	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		/* If full shadow mode, get/release_mutex to surround all configs */
		dpmgr_path_mutex_get(disp_handle, cmdq_handle);
	} else {
		cmdqRecWait(cmdq_handle, CMDQ_EVENT_DISP_WDMA0_EOF);
	}

	MMProfileLogEx(ddp_mmp_get_events()->ovl_trigger, MMProfileFlagEnd, 0, data);

	return 0;
}

int _trigger_overlay_engine(void)
{
	/* maybe we need a simple merge mechanism for CPU config. */
	dpmgr_path_trigger(pgc->ovl2mem_path_handle, NULL,
			   disp_helper_get_option(DISP_OPT_USE_CMDQ));

	return 0;
}



static unsigned int _need_lfr_check(void)
{
	unsigned int ret = 0;

#ifdef CONFIG_OF
	if ((pgc->plcm->params->dsi.lfr_enable == 1) && (islcmconnected == 1))
		ret = 1;

#else
	if (pgc->plcm->params->dsi.lfr_enable == 1)
		ret = 1;

#endif
	return ret;
}

static int __primary_check_trigger(void)
{
	int ret = 0;

	MMProfileLogEx(ddp_mmp_get_events()->primary_display_aalod_trigger,
		       MMProfileFlagStart, 0, 0);

	_primary_path_lock(__func__);
	if (pgc->state != DISP_ALIVE)
		goto out;
	if (primary_display_is_video_mode())
		goto out;

	dprec_logger_trigger(DPREC_LOGGER_PQ_TRIGGER_1SECOND, 0, 0);

	if (disp_helper_get_option(DISP_OPT_USE_CMDQ)) {
		static cmdqRecHandle handle;

		if (!handle)
			ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &handle);
		cmdqRecReset(handle);

		primary_display_idlemgr_kick((char *)__func__, 0);
		_cmdq_insert_wait_frame_done_token_mira(handle);

#ifdef CONFIG_MTK_DISPLAY_120HZ_SUPPORT
				if (od_need_start) {
					od_need_start = 0;
					disp_od_start_read(handle);
				}
				disp_od_update_status(handle);
#endif
		_cmdq_set_config_handle_dirty_mira(handle);
		_cmdq_flush_config_handle_mira(handle, 0);
	} else {
		dpmgr_wait_event(pgc->dpmgr_handle, DISP_PATH_EVENT_TRIGGER);
		DISPMSG("Force Trigger Display Path\n");
		primary_display_trigger(1, NULL, 0);
	}

	atomic_set(&delayed_trigger_kick, 1);

out:
			MMProfileLogEx(ddp_mmp_get_events()->primary_display_aalod_trigger,
				       MMProfileFlagEnd, 0, 0);
			_primary_path_unlock(__func__);
			return 0;
}

static int _disp_primary_path_check_trigger(void *data)
{
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_TRIGGER);
	while (1) {
		dpmgr_wait_event(pgc->dpmgr_handle, DISP_PATH_EVENT_TRIGGER);
		__primary_check_trigger();
		if (kthread_should_stop())
			break;
	}

	return 0;
}

static int _disp_primary_path_check_trigger_delay_33ms(void *data)
{
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_DELAYED_TRIGGER_33ms);
	while (1) {
		dpmgr_wait_event(pgc->dpmgr_handle, DISP_PATH_EVENT_DELAYED_TRIGGER_33ms);
		atomic_set(&delayed_trigger_kick, 0);

		if (disp_helper_get_option(DISP_OPT_DELAYED_TRIGGER))
			usleep_range(32000, 33000);

		if (!atomic_read(&delayed_trigger_kick))
			__primary_check_trigger();
		if (kthread_should_stop())
			break;
	}

	return 0;
}

static int _disp_primary_path_check_trigger_od(void *data)
{
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_OD_TRIGGER);
	while (1) {
		dpmgr_wait_event(pgc->dpmgr_handle, DISP_PATH_EVENT_OD_TRIGGER);
		atomic_set(&od_trigger_kick, 1);
		if (kthread_should_stop())
			break;
	}

	return 0;
}

unsigned int cmdqDdpClockOn(uint64_t engineFlag)
{
	return 0;
}

unsigned int cmdqDdpClockOff(uint64_t engineFlag)
{
	return 0;
}

unsigned int cmdqDdpDumpInfo(uint64_t engineFlag, char *pOutBuf, unsigned int bufSize)
{
	DISPERR("cmdq timeout:%llu\n", engineFlag);
	primary_display_diagnose();

	if (primary_display_is_decouple_mode())
		ddp_dump_analysis(DISP_MODULE_OVL0);

	ddp_dump_analysis(DISP_MODULE_WDMA0);

	/* try to set event by CPU to avoid blocking auto test such as Monkey/MTBF */
	/* cmdqCoreSetEvent(CMDQ_SYNC_TOKEN_STREAM_EOF); */
	/* cmdqCoreSetEvent(CMDQ_EVENT_DISP_RDMA0_EOF); */

	return 0;
}

unsigned int cmdqDdpResetEng(uint64_t engineFlag)
{
	return 0;
}

#if 0 /* defined but not used */
static void _RDMA0_INTERNAL_IRQ_Handler(DISP_MODULE_ENUM module, unsigned int param)
{
	if (param & 0x2) {
		/* RDMA Start */
		spm_sodi_mempll_pwr_mode(1);
		MMProfileLogEx(ddp_mmp_get_events()->sodi_disable, MMProfileFlagPulse, 0, 0);
	} else if (param & 0x4) {
		/* RDMA Done */
		spm_sodi_mempll_pwr_mode(0);
		MMProfileLogEx(ddp_mmp_get_events()->sodi_enable, MMProfileFlagPulse, 0, 0);
	}
}
#endif

#if 0
void primary_display_sodi_rule_init(void)
{
	/* enable sodi when display driver is ready */
	if (disp_helper_get_option(DISP_HELPER_OPTION_SODI_SUPPORT)) {
		if (primary_display_is_video_mode())
			spm_enable_sodi(1);
		else
			spm_enable_sodi(1);

		return;
	}
}
#endif

int primary_display_change_lcm_resolution(unsigned int width, unsigned int height)
{
	if (pgc->plcm) {
		DISPMSG("LCM Resolution will be changed, original: %dx%d, now: %dx%d\n",
			pgc->plcm->params->width, pgc->plcm->params->height, width, height);
		/* align with 4 is the minimal check, to ensure we can boot up into kernel,
		 * and could modify dfo setting again using meta tool */
		/* otherwise we will have a panic in lk(root cause unknown). */
		if (width > pgc->plcm->params->width || height > pgc->plcm->params->height
		    || width == 0 || height == 0 || width % 4 || height % 4) {
			DISPERR("Invalid resolution: %dx%d\n", width, height);
			return -1;
		}

		if (primary_display_is_video_mode()) {
			DISPERR("Warning!!!Video Mode can't support multiple resolution!\n");
			return -1;
		}

		pgc->plcm->params->width = width;
		pgc->plcm->params->height = height;

		return 0;
	} else {
		return -1;
	}
}

static int _wdma_fence_release_callback(unsigned long userdata)
{
	int fence_idx, layer;

	layer = disp_sync_get_output_timeline_id();

	cmdqBackupReadSlot(pgc->cur_config_fence, layer, &fence_idx);
	mtkfb_release_fence(primary_session_id, layer, fence_idx);
	MMProfileLogEx(ddp_mmp_get_events()->primary_wdma_fence_release, MMProfileFlagPulse, layer,
		       fence_idx);

	return 0;
}

static int _Interface_fence_release_callback(unsigned long userdata)
{
	int layer = disp_sync_get_output_interface_timeline_id();
	int ret = 0;

#ifdef _DEBUG_DITHER_HANG_
	if (primary_display_is_video_mode()) {
		unsigned int status;

		cmdqBackupReadSlot(pgc->dither_status_info, 0, &status);
		if ((status) != 0x10001) {
			/* dither is not idle !! */
			DISPERR("disp dither status error!! stat=0x%x\n", status);
			/* disp_aee_print("dither_stat 0x%x\n", status); */
			MMProfileLogEx(ddp_mmp_get_events()->primary_error, MMProfileFlagPulse, status, 1);
			primary_display_diagnose();
			ret = -1;
		}
	}
#endif

	if (userdata > 0) {
		mtkfb_release_fence(primary_session_id, layer, userdata);
		MMProfileLogEx(ddp_mmp_get_events()->primary_wdma_fence_release, MMProfileFlagPulse,
			       layer, userdata);
	}

	return ret;
}

static int _decouple_update_rdma_config_nolock(void)
{
	int interface_fence = 0;
	int layer = 0;
	int ret = 0;

	if (primary_display_is_decouple_mode()) {
		static cmdqRecHandle cmdq_handle;
		unsigned int rdma_pitch_sec;

		layer = disp_sync_get_output_timeline_id();
		cmdqBackupReadSlot(pgc->cur_config_fence, layer, &interface_fence);

		if (primary_get_state() != DISP_ALIVE) {
			/* don't trigger rdma */
			/* release interface fence */
			_Interface_fence_release_callback(interface_fence > 1 ? interface_fence - 1 : 0);

			return -1;
		}

		if (cmdq_handle == NULL)
			ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &cmdq_handle);
		if (ret == 0) {
			RDMA_CONFIG_STRUCT tmpConfig = decouple_rdma_config;

			cmdqRecReset(cmdq_handle);
			_cmdq_insert_wait_frame_done_token_mira(cmdq_handle);
			cmdqBackupReadSlot(pgc->rdma_buff_info, 0, (uint32_t *)(&(tmpConfig.address)));

			/* rdma pitch only use bit[15..0], we use bit[31:30] to store secure information */
			cmdqBackupReadSlot(pgc->rdma_buff_info, 1, &(rdma_pitch_sec));
			tmpConfig.pitch = rdma_pitch_sec & ~(3<<30);
			tmpConfig.security = rdma_pitch_sec >> 30;

			cmdqBackupReadSlot(pgc->rdma_buff_info, 2, &(tmpConfig.inputFormat));

			tmpConfig.height = primary_display_get_height();
			tmpConfig.width = primary_display_get_width();
			tmpConfig.yuv_range = 1; /* BT601 */
#ifdef _DEBUG_DITHER_HANG_
			if (primary_display_is_video_mode()) {
				cmdqRecBackupRegisterToSlot(cmdq_handle, pgc->dither_status_info,
							    0, disp_addr_convert(DISP_REG_DITHER_OUT_CNT));
			}
#endif
			_config_rdma_input_data(&tmpConfig, pgc->dpmgr_handle, cmdq_handle);
			_cmdq_set_config_handle_dirty_mira(cmdq_handle);

			cmdqRecFlushAsyncCallback(cmdq_handle, (CmdqAsyncFlushCB)_Interface_fence_release_callback,
					interface_fence > 1 ? interface_fence - 1 : 0);

			dprec_mmp_dump_rdma_layer(&tmpConfig, 0);
			MMProfileLogEx(ddp_mmp_get_events()->primary_rdma_config, MMProfileFlagPulse,
					interface_fence, tmpConfig.address);
		} else {
			DISPERR("fail to create cmdq\n");
		}
	}
	return 0;
}


static int decouple_update_rdma_config(void)
{
	int ret;

	_primary_path_lock(__func__);
	ret = _decouple_update_rdma_config_nolock();
	_primary_path_unlock(__func__);
	return ret;
}

static int _request_dvfs_perf(int req)
{
	if (atomic_read(&dvfs_ovl_req_status) != req) {
		switch (req) {
		case HRT_LEVEL_HIGH:
			mmdvfs_set_step(MMDVFS_SCEN_DISP, MMDVFS_VOLTAGE_HIGH);
			break;
		case HRT_LEVEL_LOW:
			mmdvfs_set_step(MMDVFS_SCEN_DISP, MMDVFS_VOLTAGE_LOW);
			break;
		case HRT_LEVEL_EXTREME_LOW:
			mmdvfs_set_step(MMDVFS_SCEN_DISP, MMDVFS_VOLTAGE_LOW_LOW);
			break;
		}
		atomic_set(&dvfs_ovl_req_status, req);
	}

	return 0;
}

static int _ovl_fence_release_callback(unsigned long userdata)
{
	int i = 0;
	unsigned int addr = 0;
	int ret = 0;
	int real_hrt_level = 0;

	MMProfileLogEx(ddp_mmp_get_events()->session_release, MMProfileFlagStart, 1, userdata);

	/* check overlap layer */
	cmdqBackupReadSlot(pgc->subtractor_when_free, i, &real_hrt_level);
	real_hrt_level >>= 16;

	_primary_path_lock(__func__);

	if (real_hrt_level > HRT_LEVEL_LOW &&
		primary_display_is_directlink_mode()) {

		_request_dvfs_perf(HRT_LEVEL_HIGH);
	} else if (real_hrt_level > HRT_LEVEL_EXTREME_LOW) {
		/* be carefull for race condition !! because callback may delay */
		/* so we need to check last request when ovl_config */
		if (dvfs_last_ovl_req == HRT_LEVEL_LOW)
			_request_dvfs_perf(HRT_LEVEL_LOW);
	} else {
		if (dvfs_last_ovl_req == HRT_LEVEL_EXTREME_LOW)
			_request_dvfs_perf(HRT_LEVEL_EXTREME_LOW);
	}
	_primary_path_unlock(__func__);

	/* check last ovl status: should be idle when config */
	if (primary_display_is_video_mode() && !primary_display_is_decouple_mode()) {
		unsigned int status;

		cmdqBackupReadSlot(pgc->ovl_status_info, 0, &status);
#ifdef DEBUG_OVL_CONFIG_TIME
		unsigned int time_event = 0;
		unsigned int time_event1 = 0;
		unsigned int time_event2 = 0;

		cmdqBackupReadSlot(pgc->ovl_config_time, 0, &time_event);
		cmdqBackupReadSlot(pgc->ovl_config_time, 1, &time_event1);
		cmdqBackupReadSlot(pgc->ovl_config_time, 2, &time_event2);
		DISPMSG("ovl config time_event %d time_event1 %d time_event2 %d time1_diff  %d  time2_diff %d\n",
			time_event, time_event1, time_event2, time_event1 - time_event, time_event2 - time_event1);
#endif
		if ((status & 0x1) != 0) {
			/* ovl is not idle !! */
			DISPERR("disp ovl status error!! stat=0x%x\n", status);
			/* disp_aee_print("ovl_stat 0x%x\n", status); */
			MMProfileLogEx(ddp_mmp_get_events()->primary_error, MMProfileFlagPulse, status, 0);
			primary_display_diagnose();
			ret = -1;
		}
	}

	for (i = 0; i < PRIMARY_SESSION_INPUT_LAYER_COUNT; i++) {
		int fence_idx = 0;
		int subtractor = 0;

		if (i == primary_display_get_option("ASSERT_LAYER") && is_DAL_Enabled()) {
			mtkfb_release_layer_fence(primary_session_id, i);
		} else {
			cmdqBackupReadSlot(pgc->cur_config_fence, i, &fence_idx);
			cmdqBackupReadSlot(pgc->subtractor_when_free, i, &subtractor);
			subtractor &= 0xFFFF;
			mtkfb_release_fence(primary_session_id, i, fence_idx - subtractor);
		}
		MMProfileLogEx(ddp_mmp_get_events()->primary_ovl_fence_release, MMProfileFlagPulse,
			       i, fence_idx - subtractor);
	}

	addr = ddp_ovl_get_cur_addr(!_should_config_ovl_input(), 0);
	if ((primary_display_is_decouple_mode() == 0))
		update_frm_seq_info(addr, 0, 2, FRM_START);

	MMProfileLogEx(ddp_mmp_get_events()->session_release, MMProfileFlagEnd, 1, userdata);
	return ret;
}

/* #define UPDATE_RDMA_CONFIG_USING_CMDQ_CALLBACK */
static int _ovl_wdma_fence_release_callback(unsigned long userdata)
{
	int ret = 0;

	ret = _ovl_fence_release_callback(userdata);
	ret |= _wdma_fence_release_callback(userdata);

#ifdef UPDATE_RDMA_CONFIG_USING_CMDQ_CALLBACK
	ret |= decouple_update_rdma_config();
#endif

	return ret;
}


#ifdef UPDATE_RDMA_CONFIG_USING_CMDQ_CALLBACK
static int decouple_mirror_update_rdma_config_thread(void *data)
{
	return 0;
}
#else
static void decouple_mirror_irq_callback(DISP_MODULE_ENUM module, unsigned int reg_value)
{
#if defined(CONFIG_TRUSTONIC_TEE_SUPPORT) && defined(CONFIG_MTK_SEC_VIDEO_PATH_SUPPORT)
	/* In TEE, we have to protect WDMA registers, so we can't enable WDMA interrupt */
	/* here we use ovl frame done interrupt instead */
	if ((module == DISP_MODULE_OVL0) && (primary_display_is_decouple_mode())) {
		if (reg_value & 0x2) { /* OVL0 frame done */
			atomic_set(&decouple_update_rdma_event, 1);
			wake_up_interruptible(&decouple_update_rdma_wq);
	    }
	}

#else
	if ((module == DISP_MODULE_WDMA0) && (primary_display_is_decouple_mode())) {
		if (reg_value & 0x1) { /* wdma0 frame done */
			atomic_set(&decouple_update_rdma_event, 1);
			wake_up_interruptible(&decouple_update_rdma_wq);
		}
	}
#endif
}

static int decouple_mirror_update_rdma_config_thread(void *data)
{
	struct sched_param param = {.sched_priority = 94 };

	sched_setscheduler(current, SCHED_RR, &param);

	disp_register_module_irq_callback(DISP_MODULE_WDMA0, decouple_mirror_irq_callback);
	disp_register_module_irq_callback(DISP_MODULE_OVL0, decouple_mirror_irq_callback);
	while (1) {
		wait_event_interruptible(decouple_update_rdma_wq,
					 atomic_read(&decouple_update_rdma_event));
		atomic_set(&decouple_update_rdma_event, 0);
		decouple_update_rdma_config();
		if (kthread_should_stop())
			break;
	}

	return 0;
}
#endif

static int primary_display_remove_output(void *callback, unsigned int userdata)
{
	int ret = 0;
	static cmdqRecHandle cmdq_handle;
	static cmdqRecHandle cmdq_wait_handle;

	/* create config thread */
	if (cmdq_handle == NULL)
		ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &cmdq_handle);

	if (ret == 0) {
		/* capture thread wait wdma sof */
		if (cmdq_wait_handle == NULL)
			ret = cmdqRecCreate(CMDQ_SCENARIO_DISP_SCREEN_CAPTURE, &cmdq_wait_handle);

		if (ret == 0) {
			cmdqRecReset(cmdq_wait_handle);
			cmdqRecWait(cmdq_wait_handle, CMDQ_EVENT_DISP_WDMA0_SOF);
			cmdqRecFlush(cmdq_wait_handle);
			/* cmdqRecDestroy(cmdq_wait_handle); */
		} else {
			DISPERR("fail to create  wait handle\n");
		}
		cmdqRecReset(cmdq_handle);

		if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
				disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
			dpmgr_path_mutex_get(pgc->dpmgr_handle, cmdq_handle);
		} else {
			_cmdq_insert_wait_frame_done_token_mira(cmdq_handle);
		}
		/* update output fence */
		cmdqRecBackupUpdateSlot(cmdq_handle, pgc->cur_config_fence,
					disp_sync_get_output_timeline_id(), mem_config.buff_idx);

		dpmgr_path_remove_memout(pgc->dpmgr_handle, cmdq_handle);

		cmdqRecClearEventToken(cmdq_handle, CMDQ_EVENT_DISP_WDMA0_SOF);
		if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
				disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
			dpmgr_path_mutex_release(pgc->dpmgr_handle, cmdq_handle);
		}
		_cmdq_set_config_handle_dirty_mira(cmdq_handle);
		cmdqRecFlushAsyncCallback(cmdq_handle, callback, 0);
		pgc->need_trigger_ovl1to2 = 0;
		/* cmdqRecDestroy(cmdq_handle); */
	} else {
		ret = -1;
		DISPERR("fail to remove memout out\n");
	}
	return ret;
}

static void primary_display_frame_update_irq_callback(DISP_MODULE_ENUM module, unsigned int param)
{
	/* /if (pgc->session_mode == DISP_SESSION_DIRECT_LINK_MIRROR_MODE) */
	/* /    return; */

	if (module == DISP_MODULE_RDMA0) {
		if (param & 0x2) {	/* rdma0 frame start  0x20 */
			if (pgc->session_id > 0 && primary_display_is_decouple_mode())
				update_frm_seq_info(ddp_ovl_get_cur_addr(1, 0), 0, 1, FRM_START);
		}

		if (param & 0x4) {	/* rdma0 frame done */
			atomic_set(&primary_display_frame_update_event, 1);
			wake_up_interruptible(&primary_display_frame_update_wq);
		}
	}

	if ((module == DISP_MODULE_OVL0) && (primary_display_is_decouple_mode() == 0)) {
		if (param & 0x2) {	/* ov0 frame done */
			atomic_set(&primary_display_frame_update_event, 1);
			wake_up_interruptible(&primary_display_frame_update_wq);
		}
	}

}

static int primary_display_frame_update_kthread(void *data)
{
	struct sched_param param = {.sched_priority = 94 };

	sched_setscheduler(current, SCHED_RR, &param);
	for (;;) {
		wait_event_interruptible(primary_display_frame_update_wq,
					 atomic_read(&primary_display_frame_update_event));
		atomic_set(&primary_display_frame_update_event, 0);

		if (pgc->session_id > 0)
			update_frm_seq_info(0, 0, 0, FRM_END);

		if (kthread_should_stop())
			break;

	}

	return 0;
}

static int _present_fence_release_worker_thread(void *data)
{
	struct sched_param param = {.sched_priority = 87 };

	sched_setscheduler(current, SCHED_RR, &param);

	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);

	while (1) {
		int fence_increment = 0;
		int timeline_id;
		disp_sync_info *layer_info;

		wait_event_interruptible(primary_display_present_fence_wq,
					 atomic_read(&primary_display_present_fence_update_event));
		atomic_set(&primary_display_present_fence_update_event, 0);
		if (!islcmconnected && !primary_display_is_video_mode()) {
			DISPCHECK("LCM Not Connected && CMD Mode\n");
			msleep(20);
		} else {
			dpmgr_wait_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);
			/* dpmgr_wait_event(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE); */
		}

		timeline_id = disp_sync_get_present_timeline_id();

		layer_info = _get_sync_info(primary_session_id, timeline_id);
		if (layer_info == NULL) {
			MMProfileLogEx(ddp_mmp_get_events()->present_fence_release,
				       MMProfileFlagPulse, -1, 0x5a5a5a5a);
			continue;
		}

		_primary_path_lock(__func__);
		fence_increment = gPresentFenceIndex - layer_info->timeline->value;
		if (fence_increment > 0) {
			timeline_inc(layer_info->timeline, fence_increment);
			DISPPR_FENCE("R+/%s%d/L%d/id%d\n",
				     disp_session_mode_spy(primary_session_id),
				     DISP_SESSION_DEV(primary_session_id), timeline_id,
				     gPresentFenceIndex);
		}
		MMProfileLogEx(ddp_mmp_get_events()->present_fence_release, MMProfileFlagPulse,
			       gPresentFenceIndex, fence_increment);
		_primary_path_unlock(__func__);

		if (atomic_read(&od_trigger_kick)) {
			atomic_set(&od_trigger_kick, 0);
			__primary_check_trigger();
		}
	}

	return 0;
}

int primary_display_set_frame_buffer_address(unsigned long va, unsigned long mva)
{

	DISPDBG("framebuffer va 0x%lx, mva 0x%lx\n", va, mva);
	pgc->framebuffer_va = va;
	pgc->framebuffer_mva = mva;

	return 0;
}

unsigned long primary_display_get_frame_buffer_mva_address(void)
{
	return pgc->framebuffer_mva;
}

unsigned long primary_display_get_frame_buffer_va_address(void)
{
	return pgc->framebuffer_va;
}

int is_dim_layer(unsigned long mva)
{
	if (mva == get_dim_layer_mva_addr())
		return 1;
	return 0;
}

unsigned long get_dim_layer_mva_addr(void)
{
	static unsigned long dim_layer_mva;

	if (dim_layer_mva == 0) {
		int frame_buffer_size = ALIGN_TO(DISP_GetScreenWidth(), MTK_FB_ALIGNMENT) *
			ALIGN_TO(DISP_GetScreenHeight(), MTK_FB_ALIGNMENT) * 4;

		dim_layer_mva = pgc->framebuffer_mva + (DISP_GetPages() - 1) * frame_buffer_size;
		DISPMSG("init dim layer mva %lu, size %d", dim_layer_mva, frame_buffer_size);
	}
	return dim_layer_mva;
}

static int init_cmdq_slots(cmdqBackupSlotHandle *pSlot, int count, int init_val)
{
	int i;

	cmdqBackupAllocateSlot(pSlot, count);

	for (i = 0; i < count; i++)
		cmdqBackupWriteSlot(*pSlot, i, init_val);

	return 0;
}

static int update_primary_intferface_module(void)
{
	/* update interface module, it may be: dsi0/dsi1/dsi_dual */
	DISP_MODULE_ENUM interface_module;

	interface_module = _get_dst_module_by_lcm(pgc->plcm);
	ddp_set_dst_module(DDP_SCENARIO_PRIMARY_DISP, interface_module);
	ddp_set_dst_module(DDP_SCENARIO_PRIMARY_RDMA0_COLOR0_DISP, interface_module);
	ddp_set_dst_module(DDP_SCENARIO_PRIMARY_RDMA0_DISP, interface_module);
	ddp_set_dst_module(DDP_SCENARIO_PRIMARY_BYPASS_RDMA, interface_module);

	ddp_set_dst_module(DDP_SCENARIO_PRIMARY_ALL, interface_module);
	ddp_set_dst_module(DDP_SCENARIO_DITHER_1TO2, interface_module);
	ddp_set_dst_module(DDP_SCENARIO_UFOE_1TO2, interface_module);

	return 0;
}

int primary_display_init(char *lcm_name, unsigned int lcm_fps, int is_lcm_inited)
{
	DISP_STATUS ret = DISP_STATUS_OK;
	DISP_MODULE_ENUM dst_module = 0;
	LCM_PARAMS *lcm_param = NULL;
	int use_cmdq = disp_helper_get_option(DISP_OPT_USE_CMDQ);
	disp_ddp_path_config *data_config;
	struct ddp_io_golden_setting_arg gset_arg;

	DISPCHECK("primary_display_init begin lcm=%s, inited=%d\n", lcm_name, is_lcm_inited);

	dprec_init();
	dpmgr_init();

	init_cmdq_slots(&(pgc->ovl_config_time), 3, 0);
	init_cmdq_slots(&(pgc->cur_config_fence), DISP_SESSION_TIMELINE_COUNT, 0);
	init_cmdq_slots(&(pgc->subtractor_when_free), DISP_SESSION_TIMELINE_COUNT, 0);
	init_cmdq_slots(&(pgc->rdma_buff_info), 3, 0);
	init_cmdq_slots(&(pgc->ovl_status_info), 4, 0);
	init_cmdq_slots(&(pgc->dither_status_info), 1, 0x10001);

	mutex_init(&(pgc->capture_lock));
	mutex_init(&(pgc->lock));
	mutex_init(&(pgc->switch_dst_lock));

	fps_ctx_init(&primary_fps_ctx, disp_helper_get_option(DISP_OPT_FPS_CALC_WND));

	_primary_path_lock(__func__);

	pgc->plcm = disp_lcm_probe(lcm_name, LCM_INTERFACE_NOTDEFINED, is_lcm_inited);

	if (pgc->plcm == NULL) {
		DISPDBG("disp_lcm_probe returns null\n");
		ret = DISP_STATUS_ERROR;
		goto done;
	} else {
		DISPCHECK("disp_lcm_probe SUCCESS\n");
	}

	lcm_param = disp_lcm_get_params(pgc->plcm);

	if (lcm_param == NULL) {
		DISPERR("get lcm params FAILED\n");
		ret = DISP_STATUS_ERROR;
		goto done;
	}

	update_primary_intferface_module();

	if (use_cmdq) {
		ret = cmdqCoreRegisterCB(CMDQ_GROUP_DISP, (CmdqClockOnCB)cmdqDdpClockOn,
					 (CmdqDumpInfoCB)cmdqDdpDumpInfo, (CmdqResetEngCB)cmdqDdpResetEng,
					 (CmdqClockOffCB)cmdqDdpClockOff);
		if (ret) {
			DISPERR("cmdqCoreRegisterCB failed, ret=%d\n", ret);
			ret = DISP_STATUS_ERROR;
			goto done;
		}

		ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &(pgc->cmdq_handle_config));
		if (ret) {
			DISPDBG("cmdqRecCreate FAIL, ret=%d\n", ret);
			ret = DISP_STATUS_ERROR;
			goto done;
		} else {
			DISPDBG("cmdqRecCreate SUCCESS, g_cmdq_handle=%p\n",
				  pgc->cmdq_handle_config);
		}
		/* create ovl2mem path cmdq handle */
		ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_MEMOUT, &(pgc->cmdq_handle_ovl1to2_config));
		if (ret != 0) {
			DISPERR("cmdqRecCreate FAIL, ret=%d\n", ret);
			ret = DISP_STATUS_ERROR;
			goto done;
		}
	} else {
		pgc->cmdq_handle_config = NULL;
		pgc->cmdq_handle_ovl1to2_config = NULL;
	}

	if (primary_display_mode == DIRECT_LINK_MODE) {
		_build_path_direct_link();
		pgc->session_mode = DISP_SESSION_DIRECT_LINK_MODE;
		DISPCHECK("primary display is DIRECT LINK MODE\n");
	} else if (primary_display_mode == DECOUPLE_MODE) {
		_build_path_decouple();
		pgc->session_mode = DISP_SESSION_DECOUPLE_MODE;

		DISPCHECK("primary display is DECOUPLE MODE\n");
	} else if (primary_display_mode == SINGLE_LAYER_MODE) {
		_build_path_single_layer();

		DISPCHECK("primary display is SINGLE LAYER MODE\n");
	} else if (primary_display_mode == DEBUG_RDMA1_DSI0_MODE) {
		_build_path_debug_rdma1_dsi0();

		DISPCHECK("primary display is DEBUG RDMA1 DSI0 MODE\n");
	} else {
		DISPCHECK("primary display mode is WRONG\n");
	}

	if (use_cmdq && is_lcm_inited) {
		/* if lcm is not inited (no LK),
		 * the first config should not wait frame done
		 * because there's no frame done for vdo mode */
		_cmdq_reset_config_handle();
		_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);
	}
	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		/* the first frame after reboot */
		dpmgr_path_mutex_get(pgc->dpmgr_handle, pgc->cmdq_handle_config);
	}
	config_display_m4u_port();
	primary_display_set_max_layer(PRIMARY_SESSION_INPUT_LAYER_COUNT);
	if (init_decouple_buffer_thread == NULL) {
		init_decouple_buffer_thread = kthread_create(_init_decouple_buffers_thread,
								    NULL, "init_decouple_buffer");
		wake_up_process(init_decouple_buffer_thread);
	}
	/* init_decouple_buffers(); */

	dpmgr_path_set_video_mode(pgc->dpmgr_handle, primary_display_is_video_mode());
	DISPDBG("primary_display_init->dpmgr_path_init\n");
	dpmgr_path_init(pgc->dpmgr_handle, use_cmdq);

	/* use fake timer to generate vsync signal for cmd mode w/o LCM(originally using LCM TE Signal as VSYNC) */
	/* so we don't need to modify display driver's behavior. */
	if (disp_helper_get_option(DISP_OPT_NO_LCM_FOR_LOW_POWER_MEASUREMENT)) {
		/* only for low power measurement */
		DISPWARN("WARNING!!!!!! FORCE NO LCM MODE!!!\n");
		islcmconnected = 0;

		/* no need to change video mode vsync behavior */
		if (!primary_display_is_video_mode()) {
			_init_vsync_fake_monitor(lcm_fps);

			dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
					       DDP_IRQ_UNKNOWN);
		}
	}

	if (use_cmdq) {
		_cmdq_build_trigger_loop();
		_cmdq_start_trigger_loop();
	}

	DISPCHECK("primary_display_init->dpmgr_path_config\n");

	data_config = dpmgr_path_get_last_config(pgc->dpmgr_handle);
	memcpy(&(data_config->dispif_config), lcm_param, sizeof(LCM_PARAMS));
	data_config->dst_w = disp_helper_get_option(DISP_OPT_FAKE_LCM_WIDTH);
	data_config->dst_h = disp_helper_get_option(DISP_OPT_FAKE_LCM_HEIGHT);
	data_config->p_golden_setting_context = get_golden_setting_pgc();

	if (lcm_param->type == LCM_TYPE_DSI) {
		if (lcm_param->dsi.data_format.format == LCM_DSI_FORMAT_RGB888)
			data_config->lcm_bpp = 24;
		else if (lcm_param->dsi.data_format.format == LCM_DSI_FORMAT_RGB565)
			data_config->lcm_bpp = 16;
		else if (lcm_param->dsi.data_format.format == LCM_DSI_FORMAT_RGB666)
			data_config->lcm_bpp = 18;
	} else if (lcm_param->type == LCM_TYPE_DPI) {
		if (lcm_param->dpi.format == LCM_DPI_FORMAT_RGB888)
			data_config->lcm_bpp = 24;
		else if (lcm_param->dpi.format == LCM_DPI_FORMAT_RGB565)
			data_config->lcm_bpp = 16;
		if (lcm_param->dpi.format == LCM_DPI_FORMAT_RGB666)
			data_config->lcm_bpp = 18;
	}

	data_config->fps = lcm_fps;
	data_config->dst_dirty = 1;

	ret = dpmgr_path_config(pgc->dpmgr_handle, data_config, pgc->cmdq_handle_config);

	memset(&gset_arg, 0, sizeof(gset_arg));
	gset_arg.dst_mod_type = dpmgr_path_get_dst_module_type(pgc->dpmgr_handle);
	gset_arg.is_decouple_mode = 0;
	dpmgr_path_ioctl(pgc->dpmgr_handle, pgc->cmdq_handle_config, DDP_OVL_GOLDEN_SETTING, &gset_arg);
	if (use_cmdq) {
		_cmdq_flush_config_handle(1, NULL, 0);
		_cmdq_reset_config_handle();
		if (is_lcm_inited)
			_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);
	}

	/* no need lcm power on if lcm is inited in lk */
	ret = disp_lcm_init(pgc->plcm, !is_lcm_inited);

	/* path start must after lcm init for video mode, because dsi_start will set mode */
	dpmgr_path_start(pgc->dpmgr_handle, use_cmdq);

	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		dpmgr_path_mutex_release(pgc->dpmgr_handle, pgc->cmdq_handle_config);
	}

	if (use_cmdq) {
		_cmdq_flush_config_handle(1, NULL, 0);
		_cmdq_reset_config_handle();
	}

	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		dpmgr_path_mutex_get(pgc->dpmgr_handle, pgc->cmdq_handle_config);
	} else {
		if (use_cmdq)
			_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);
	}


	if (!is_lcm_inited && primary_display_is_video_mode())
		dpmgr_path_trigger(pgc->dpmgr_handle, NULL, 0);

	if (disp_helper_get_option(DISP_OPT_MET_LOG))
		set_enterulps(0);

	primary_display_check_recovery_init();

	if (disp_helper_get_option(DISP_OPT_SWITCH_DST_MODE)) {
		primary_display_switch_dst_mode_task = kthread_create(_disp_primary_path_switch_dst_mode_thread,
								      NULL, "display_switch_dst_mode");
		wake_up_process(primary_display_switch_dst_mode_task);
	}

	if (decouple_update_rdma_config_thread == NULL) {
		decouple_update_rdma_config_thread = kthread_create(decouple_mirror_update_rdma_config_thread,
								    NULL, "decouple_update_rdma_cfg");
		wake_up_process(decouple_update_rdma_config_thread);
	}

	if (decouple_trigger_thread == NULL) {
		decouple_trigger_thread = kthread_create(decouple_trigger_worker_thread,
							 NULL, "decouple_trigger");
		wake_up_process(decouple_trigger_thread);
	}

	if (disp_helper_get_stage() == DISP_HELPER_STAGE_NORMAL) {
		primary_path_aal_task = kthread_create(_disp_primary_path_check_trigger,
						       NULL, "display_check_aal");
		wake_up_process(primary_path_aal_task);
	}

	if (disp_helper_get_stage() == DISP_HELPER_STAGE_NORMAL) {
		primary_delay_trigger_task =
		    kthread_create(_disp_primary_path_check_trigger_delay_33ms, NULL, "disp_delay_trigger");
		wake_up_process(primary_delay_trigger_task);
	}

	if (disp_helper_get_stage() == DISP_HELPER_STAGE_NORMAL) {
		primary_od_trigger_task =
		    kthread_create(_disp_primary_path_check_trigger_od, NULL, "disp_od_trigger");
		wake_up_process(primary_od_trigger_task);
	}

	if (disp_helper_get_option(DISP_OPT_PRESENT_FENCE)) {
		init_waitqueue_head(&primary_display_present_fence_wq);
		present_fence_release_worker_task = kthread_create(_present_fence_release_worker_thread,
								   NULL, "present_fence_worker");
		wake_up_process(present_fence_release_worker_task);
	}

	if (disp_helper_get_option(DISP_OPT_PERFORMANCE_DEBUG)) {
		if (primary_display_frame_update_task == NULL) {
			init_waitqueue_head(&primary_display_frame_update_wq);
			disp_register_module_irq_callback(DISP_MODULE_RDMA0,
							  primary_display_frame_update_irq_callback);
			disp_register_module_irq_callback(DISP_MODULE_OVL0,
							  primary_display_frame_update_irq_callback);
			primary_display_frame_update_task = kthread_create(primary_display_frame_update_kthread,
									   NULL, "frame_update_worker");
			wake_up_process(primary_display_frame_update_task);
		}
	}

	if (primary_display_is_video_mode()) {
		/*
		if (disp_helper_get_option(DISP_HELPER_OPTION_IDLE_MGR))
			primary_display_idlemgr_init();
		*/

		if (disp_helper_get_option(DISP_OPT_SWITCH_DST_MODE)) {
			primary_display_cur_dst_mode = 1;	/* video mode */
			primary_display_def_dst_mode = 1;	/* default mode is video mode */
		}
		if (_need_lfr_check()) {
			dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
					       DDP_IRQ_DSI0_FRAME_DONE);
		} else {
			dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
					       DDP_IRQ_RDMA0_DONE);
		}
	}

	pgc->lcm_fps = lcm_fps;
	pgc->lcm_refresh_rate = 60;
	/* keep lowpower init after setting lcm_fps */
	primary_display_lowpower_init();

	pgc->state = DISP_ALIVE;
#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
	disp_switch_data.name = "disp";
	disp_switch_data.index = 0;
	disp_switch_data.state = DISP_ALIVE;
	ret = switch_dev_register(&disp_switch_data);
#endif

	DISPCHECK("primary_display_init done\n");

done:
	if (disp_helper_get_stage() != DISP_HELPER_STAGE_NORMAL)
		primary_display_diagnose();

	dst_module = _get_dst_module_by_lcm(pgc->plcm);
	_primary_path_unlock(__func__);
	return ret;
}

static void _primary_protect_mode_switch(void)
{
	int try_cnt = 50;

	while ((--try_cnt) && atomic_read(&hwc_configing)) {
		udelay(1000);
		/* DISPCHECK("detecting protect mode switch\n"); */
	}
	if (try_cnt <= 0)
		DISPCHECK("display warning:switch mode when hwc config\n");
}

static int request_lcm_refresh_rate_change(int fps);
int primary_display_set_lcm_refresh_rate(int fps)
{
	int ret = 0;

	DISPCHECK("set lcm fps(%d)\n", fps);
	_primary_protect_mode_switch();

#ifdef CONFIG_MTK_DISPLAY_120HZ_SUPPORT
	/* Switch back to cmd mode */
	if (fps == 60 && primary_display_is_video_mode())
		primary_display_switch_dst_mode(0);
#endif

	_primary_path_lock(__func__);
	if (pgc->state == DISP_SLEPT) {
		_primary_path_unlock(__func__);
		DISPCHECK("Sleep State set lcm rate\n");
		return -1;
	}

	/* TODO: Should skip while MHL connected */

	/*
	Do not change to 120HZ here due to the last 60HZ frame update request, which ovl
	layers has been dispatched by HRT cacualtion with 60HZ, has not been executed yet.
	If the 60HZ frame request executed in 120HZ mode, the HRT may out of bound.
	Switch to 120HZ mode when the first 120 frame update request coming.
	*/
	if (fps == 120)
		ret = request_lcm_refresh_rate_change(fps);
	else
		ret = _display_set_lcm_refresh_rate(fps);
	_primary_path_unlock(__func__);
	return ret;
}

int primary_display_get_lcm_refresh_rate(void)
{
	return pgc->lcm_refresh_rate;
}

#ifdef CONFIG_MTK_DISPLAY_120HZ_SUPPORT
static int od_by_pass;

void primary_display_od_bypass(int bypass)
{
	DISPCHECK("odbyass %d\n", bypass);
	od_by_pass = bypass;
}

static void _display_set_refresh_rate_post_proc(int fps)
{
	if (fps == 60) {
		/* TODO: switch path after adjusting fps */
		od_need_start = 0;
		spm_enable_sodi(1);
	} else if (fps == 120) {
		if (!od_by_pass)
			od_need_start = 1;
		spm_enable_sodi(0);
	}
}
#endif

static int request_lcm_refresh_rate_change(int fps)
{
#ifdef CONFIG_MTK_DISPLAY_120HZ_SUPPORT
	static cmdqRecHandle cmdq_handle, cmdq_pre_handle;
	int ret;

	if (pgc->state == DISP_SLEPT) {
		DISPCHECK("Sleep State set lcm rate\n");
		return -EPERM;
	}

	if (primary_display_get_lcm_max_refresh_rate() <= 60) {
		DISPCHECK("not support set lcm rate!!\n");
		return -EPERM;
	}

	if (fps == pgc->lcm_refresh_rate) {
		pgc->request_fps = 0;
		return 0;
	}

	if (cmdq_handle == NULL) {
		ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &cmdq_handle);
		if (ret != 0) {
			DISPCHECK("fail to create primary cmdq handle for adjust fps\n");
			return -EINVAL;
		}
	}
	if (cmdq_pre_handle == NULL) {
		ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_MEMOUT, &cmdq_pre_handle);
		if (ret != 0) {
			DISPCHECK("fail to create memout cmdq handle for adjust fps\n");
			cmdqRecDestroy(cmdq_handle);
			cmdq_handle = NULL;
			return -EINVAL;
		}
	}
	primary_display_idlemgr_kick(__func__, 0);

	pgc->request_fps = fps;
	pgc->lcm_refresh_rate = fps;
#endif
	return 0;
}

int _display_set_lcm_refresh_rate(int fps)
{
#ifdef CONFIG_MTK_DISPLAY_120HZ_SUPPORT
	static cmdqRecHandle cmdq_handle, cmdq_pre_handle;
	disp_path_handle disp_handle;
	disp_ddp_path_config *pconfig = NULL;
	int ret = 0;

	if (pgc->state == DISP_SLEPT) {
		DISPCHECK("Sleep State set lcm rate\n");
		return -EPERM;
	}

	if (primary_display_get_lcm_max_refresh_rate() <= 60) {
		DISPCHECK("not support set lcm rate!!\n");
		return -EPERM;
	}

	if (fps == pgc->lcm_refresh_rate && pgc->request_fps == 0)
		return 0;

	if (fps == 60 && pgc->request_fps == 120) {
		pgc->lcm_refresh_rate = fps;
		pgc->request_fps = 0;
		DISPCHECK("LCM refresh rate is 60fps already\n");
		return 0;
	}

	if (cmdq_handle == NULL) {
		ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &cmdq_handle);
		if (ret != 0) {
			DISPCHECK("fail to create primary cmdq handle for adjust fps\n");
			return -EINVAL;
		}
	}
	if (cmdq_pre_handle == NULL) {
		ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_MEMOUT, &cmdq_pre_handle);
		if (ret != 0) {
			DISPCHECK("fail to create memout cmdq handle for adjust fps\n");
			cmdqRecDestroy(cmdq_handle);
			cmdq_handle = NULL;
			return -EINVAL;
		}
	}
	primary_display_idlemgr_kick(__func__, 0);

	/* don't move ,switch need this part*/
	pgc->lcm_refresh_rate = fps;
	pgc->request_fps = 0;
	DISPCHECK("[refresh_rate]:fps(%d)\n", fps);

	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_fps, MMProfileFlagStart, fps, 0);

	/* TODO: switch path before adjusting fps
	if (fps == 120) {
		Switch path.
	}
	*/

	cmdqRecReset(cmdq_handle);
	_cmdq_insert_wait_frame_done_token_mira(cmdq_handle);
	ret = cmdqRecClearEventToken(cmdq_handle, CMDQ_EVENT_DSI_TE);
	ret = cmdqRecWait(cmdq_handle, CMDQ_EVENT_DSI_TE);
	/* 1.Change PLL CLOCK parameter and build fps lcm command */
	disp_lcm_adjust_fps(cmdq_handle, pgc->plcm, fps);

	/* 2.Change RDMA golden setting */
	disp_handle = pgc->dpmgr_handle;
	pconfig = dpmgr_path_get_last_config(disp_handle);
	pconfig->p_golden_setting_context->fps = fps;
	pconfig->dispif_config.dsi.PLL_CLOCK = pgc->plcm->params->dsi.PLL_CLOCK;
	dpmgr_path_ioctl(primary_get_dpmgr_handle(), cmdq_handle, DDP_RDMA_GOLDEN_SETTING, pconfig);
	/* 3.Change DSI clock */
	dpmgr_path_ioctl(pgc->dpmgr_handle, cmdq_handle, DDP_PHY_CLK_CHANGE, &pgc->plcm->params->dsi.PLL_CLOCK);
	/* OD Enable */
	if (!od_by_pass) {
		if (fps == 120)
			disp_od_set_enabled(cmdq_handle, 1);
		else
			disp_od_set_enabled(cmdq_handle, 0);
	}

	if (pgc->session_mode == DISP_SESSION_DECOUPLE_MODE)
		/* need sync, make sure od is config done, even if od in decouple path*/
		_cmdq_flush_config_handle_mira(cmdq_handle, 1);
	else
		_cmdq_flush_config_handle_mira(cmdq_handle, 0);

	_display_set_refresh_rate_post_proc(fps);

	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_fps, MMProfileFlagEnd, fps, 0);
#endif
	return 0;
}

int primary_display_get_lcm_max_refresh_rate(void)
{
	if (disp_lcm_is_support_adjust_fps(pgc->plcm) != 0)
		return 120;
	return 60;
}

int primary_display_deinit(void)
{
	_primary_path_lock(__func__);

	_cmdq_stop_trigger_loop();
	dpmgr_path_deinit(pgc->dpmgr_handle, CMDQ_DISABLE);
	_primary_path_unlock(__func__);
	return 0;
}

/* register rdma done event */
int primary_display_wait_for_idle(void)
{
	DISP_STATUS ret = DISP_STATUS_OK;

	DISPFUNC();

	_primary_path_lock(__func__);

	_primary_path_unlock(__func__);
	return ret;
}

int primary_display_wait_for_dump(void)
{
	return 0;
}

int primary_display_release_fence_fake(void)
{
	unsigned int layer_en = 0;
	unsigned int addr = 0;
	unsigned int fence_idx = -1;
	unsigned int session_id = MAKE_DISP_SESSION(DISP_SESSION_PRIMARY, 0);
	int i = 0;

	DISPFUNC();

	for (i = 0; i < PRIMARY_SESSION_INPUT_LAYER_COUNT; i++) {
		if (i == primary_display_get_option("ASSERT_LAYER") && is_DAL_Enabled()) {
			mtkfb_release_layer_fence(session_id, 3);
		} else {
			disp_sync_get_cached_layer_info(session_id, i, &layer_en, (unsigned long *)&addr, &fence_idx);
			if (fence_idx == -1) {
				DISPPR_ERROR("find fence for layer %d,addr 0x%08x fail, unregistered\n", i, addr);
			} else if (fence_idx < 0) {
				DISPPR_ERROR("find fence idx for layer %d,addr 0x%08x fail,unknown\n", i, addr);
			} else {
				if (layer_en)
					mtkfb_release_fence(session_id, i, fence_idx - 1);
				else
					mtkfb_release_fence(session_id, i, fence_idx);
			}
		}
	}

	return 0;
}

int primary_display_wait_for_vsync(void *config)
{
	disp_session_vsync_config *c = (disp_session_vsync_config *)config;
	int ret = 0, has_vsync = 1;
	unsigned long long ts = 0ULL;

	/* kick idle manager here to ensure sodi is disabled when screen update begin(not 100% ensure) */
	primary_display_idlemgr_kick(__func__, 1);

#ifdef CONFIG_FPGA_EARLY_PORTING
	if (!primary_display_is_video_mode())
		has_vsync = 0;	/* fpga has no TE signal */
#endif

	if (!islcmconnected || !has_vsync) {
		DISPCHECK("use fake vsync: lcm_connect=%d, has_vsync=%d\n",
			  islcmconnected, has_vsync);
		msleep(20);
		return 0;
	}

	if (pgc->force_fps_keep_count && pgc->force_fps_skip_count) {
		g_keep++;
		DISPMSG("vsync|keep %d\n", g_keep);
		if (g_keep == pgc->force_fps_keep_count) {
			g_keep = 0;

			while (g_skip != pgc->force_fps_skip_count) {
				g_skip++;
				DISPMSG("vsync|skip %d\n", g_skip);
				ret =
				    dpmgr_wait_event_timeout(pgc->dpmgr_handle,
							     DISP_PATH_EVENT_IF_VSYNC, HZ / 10);
				if (ret == -2) {
					DISPPR_ERROR("vsync for primary display path not enabled yet\n");
					return -1;
				} else if (ret == 0) {
					primary_display_release_fence_fake();
				}
			}
			g_skip = 0;
		}
	} else {
		g_keep = 0;
		g_skip = 0;
	}

	ret = dpmgr_wait_event_ts(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC, &ts);

	if (ret == -2) {
		DISPPR_ERROR("vsync for primary display path not enabled yet\n");
		goto out;
	} else if (ret == 0) {
		/* primary_display_release_fence_fake(); */
	}

	if (pgc->vsync_drop) {
		/* ret = dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC, HZ/10); */
		ret = dpmgr_wait_event_ts(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC, &ts);
	}

out:
	c->vsync_ts = ts;
	c->vsync_cnt++;
	c->lcm_fps = pgc->lcm_refresh_rate;

	return ret;
}

unsigned int primary_display_get_ticket(void)
{
	return dprec_get_vsync_count();
}

int primary_suspend_release_fence(void)
{
	unsigned int session = (unsigned int)((DISP_SESSION_PRIMARY) << 16 | (0));
	unsigned int i = 0;

	for (i = 0; i < DISP_SESSION_TIMELINE_COUNT; i++) {
		DISPDBG("mtkfb_release_layer_fence  session=0x%x,layerid=%d\n", session, i);
		mtkfb_release_layer_fence(session, i);
	}
	return 0;
}

/* Need rull roi when suspend*/
int suspend_to_full_roi(void)
{
	int ret = 0;
	cmdqRecHandle handle = NULL;
	disp_ddp_path_config *data_config = NULL;

	if (!disp_partial_is_support())
		return -1;

	if (!primary_display_is_directlink_mode())
		return -1;

	ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &handle);
	if (ret) {
		DISPERR("%s:%d, create cmdq handle fail!ret=%d\n", __func__, __LINE__, ret);
		return -1;
	}
	cmdqRecReset(handle);
	_cmdq_insert_wait_frame_done_token_mira(handle);

	data_config = dpmgr_path_get_last_config(pgc->dpmgr_handle);

	primary_display_config_full_roi(data_config, pgc->dpmgr_handle, handle);

	cmdqRecFlush(handle);
	cmdqRecDestroy(handle);
	return ret;
}

int primary_display_suspend(void)
{
	DISP_STATUS ret = DISP_STATUS_OK;

	DISPCHECK("primary_display_suspend begin\n");
	MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagStart, 0, 0);
	primary_display_idlemgr_kick(__func__, 1);

	if (disp_helper_get_option(DISP_OPT_SWITCH_DST_MODE))
		primary_display_switch_dst_mode(primary_display_def_dst_mode);

#ifdef CONFIG_MTK_DISPLAY_120HZ_SUPPORT
	/* Switch back to cmd mode */
	if (primary_display_is_video_mode())
		primary_display_switch_dst_mode(0);
#endif
	_primary_path_switch_dst_lock();
	disp_sw_mutex_lock(&(pgc->capture_lock));
	_primary_path_lock(__func__);

	while (primary_get_state() == DISP_BLANK) {
		_primary_path_unlock(__func__);
		DISPCHECK("primary_display_suspend wait tui finish!!\n");
#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
		switch_set_state(&disp_switch_data, DISP_SLEPT);
#endif
		primary_display_wait_state(DISP_ALIVE, MAX_SCHEDULE_TIMEOUT);
		_primary_path_lock(__func__);
		DISPCHECK("primary_display_suspend wait tui done stat=%d\n", primary_get_state());
	}

	if (pgc->state == DISP_SLEPT) {
		DISPWARN("primary display path is already sleep, skip\n");
		goto done;
	}
	primary_display_idlemgr_kick(__func__, 0);

	if (pgc->session_mode == DISP_SESSION_RDMA_MODE) {
		/* switch back to DL mode before suspend */
		do_primary_display_switch_mode(DISP_SESSION_DIRECT_LINK_MODE,
					pgc->session_id, 0, NULL, 1);
	}

	/* restore to 60 fps */
	_display_set_lcm_refresh_rate(60);

	/* restore to full roi */
	suspend_to_full_roi();

	/* need leave share sram for suspend */
	if (disp_helper_get_option(DISP_OPT_SHARE_SRAM))
		leave_share_sram(CMDQ_SYNC_RESOURCE_WROT1);

	/* switch to vencpll before disable mmsys clk */
	if (disp_helper_get_option(DISP_OPT_DYNAMIC_SWITCH_MMSYSCLK))
		;/* mmdvfs_notify_mmclk_switch_request(MMDVFS_EVENT_OVL_SINGLE_LAYER_EXIT); */

	/* blocking flush before stop trigger loop */
	_blocking_flush();
	MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagPulse, 0, 1);
	if (dpmgr_path_is_busy(pgc->dpmgr_handle)) {
		int event_ret;

		MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagPulse, 1, 2);
		event_ret = dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ * 1);

		MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagPulse, 2, 2);
		DISPCHECK("[POWER]primary display path is busy now, wait frame done, event_ret=%d\n", event_ret);
		if (event_ret <= 0) {
			DISPERR("wait frame done in suspend timeout\n");
			MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagPulse, 3,
				       2);
			primary_display_diagnose();
			ret = -1;
		}
	}

	MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagPulse, 0, 2);

	if (disp_helper_get_option(DISP_OPT_USE_CMDQ)) {
		DISPCHECK("[POWER]display cmdq trigger loop stop\n");
		_cmdq_stop_trigger_loop();
	}
	MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagPulse, 0, 3);

	DISPDBG("[POWER]primary display path stop[begin]\n");
	dpmgr_path_stop(pgc->dpmgr_handle, CMDQ_DISABLE);
	DISPCHECK("[POWER]primary display path stop[end]\n");
	MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagPulse, 0, 4);

	if (dpmgr_path_is_busy(pgc->dpmgr_handle)) {
		MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagPulse, 1, 4);
		DISPERR("[POWER]stop display path failed, still busy\n");
		dpmgr_path_reset(pgc->dpmgr_handle, CMDQ_DISABLE);
		ret = -1;
		/* even path is busy(stop fail), we still need to continue power off other module/devices */
		/* goto done; */
	}
	MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagPulse, 0, 5);
	/**
	 * must get/release_mutex to copy configs from shadow into working
	 */
	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER)) {
		if (disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
			dpmgr_path_mutex_get(pgc->dpmgr_handle, NULL);
			dpmgr_path_mutex_release(pgc->dpmgr_handle, NULL);
			dpmgr_path_mutex_enable(pgc->dpmgr_handle, NULL);
		} else {
			dpmgr_path_mutex_enable(pgc->dpmgr_handle, NULL);
			dpmgr_path_mutex_get(pgc->dpmgr_handle, NULL);
			dpmgr_path_mutex_release(pgc->dpmgr_handle, NULL);
		}
	}
	DISPCHECK("[POWER]lcm suspend[begin]\n");
	disp_lcm_suspend(pgc->plcm);
	DISPCHECK("[POWER]lcm suspend[end]\n");
	MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagPulse, 0, 6);
	DISPDBG("[POWER]primary display path Release Fence[begin]\n");
	primary_suspend_release_fence();
	DISPCHECK("[POWER]primary display path Release Fence[end]\n");
	MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagPulse, 0, 7);

	DISPDBG("[POWER]dpmanager path power off[begin]\n");
	dpmgr_path_power_off(pgc->dpmgr_handle, CMDQ_DISABLE);
	if (disp_helper_get_option(DISP_OPT_MET_LOG))
		set_enterulps(1);

	DISPCHECK("[POWER]dpmanager path power off[end]\n");
	MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagPulse, 0, 8);

	pgc->lcm_refresh_rate = 60;
	/* pgc->state = DISP_SLEPT; */
done:
	primary_set_state(DISP_SLEPT);
	_primary_path_unlock(__func__);
	disp_sw_mutex_unlock(&(pgc->capture_lock));
	_primary_path_switch_dst_unlock();

	aee_kernel_wdt_kick_Powkey_api("mtkfb_early_suspend", WDT_SETBY_Display);
	primary_trigger_cnt = 0;
	MMProfileLogEx(ddp_mmp_get_events()->primary_suspend, MMProfileFlagEnd, 0, 0);
	DISPCHECK("primary_display_suspend end\n");

	/* set MMDVFS to low gear, prevent keep low_low gear after suspend */
	_request_dvfs_perf(HRT_LEVEL_LOW);

	return ret;
}

int primary_display_get_lcm_index(void)
{
	int index = 0;

	DISPFUNC();

	if (pgc->plcm == NULL) {
		DISPERR("lcm handle is null\n");
		return 0;
	}

	index = pgc->plcm->index;
	DISPMSG("lcm index = %d\n", index);
	return index;
}

int primary_display_resume(void)
{
	DISP_STATUS ret = DISP_STATUS_OK;
	struct ddp_io_golden_setting_arg gset_arg;
	int i;

	DISPCHECK("primary_display_resume begin\n");
	MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagStart, 0, 0);

	_primary_path_lock(__func__);
	if (pgc->state == DISP_ALIVE) {
		DISPCHECK("primary display path is already resume, skip\n");
		goto done;
	}
	MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 0, 1);

	if (is_ipoh_bootup) {
		DISPCHECK("[primary display path] leave primary_display_resume -- IPOH\n");
		DISPCHECK("ESD check start[begin]\n");
		primary_display_esd_check_enable(1);
		DISPCHECK("ESD check start[end]\n");
		is_ipoh_bootup = false;
		DISPDBG("[POWER]start cmdq[begin]--IPOH\n");
		if (disp_helper_get_option(DISP_OPT_USE_CMDQ))
			_cmdq_start_trigger_loop();
		enable_idlemgr(1);
		DISPDBG("[POWER]start cmdq[end]--IPOH\n");
		/* pgc->state = DISP_ALIVE; */
		goto done;
	}

	DISPDBG("dpmanager path power on[begin]\n");
	dpmgr_path_power_on(pgc->dpmgr_handle, CMDQ_DISABLE);
	if (disp_helper_get_option(DISP_OPT_MET_LOG))
		set_enterulps(0);

	DISPCHECK("dpmanager path power on[end]\n");

	if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
			disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
		dpmgr_path_mutex_get(pgc->dpmgr_handle, NULL);
	}

	DISPCHECK("dpmanager path reset[begin]\n");
	dpmgr_path_reset(pgc->dpmgr_handle, CMDQ_DISABLE);
	DISPCHECK("dpmanager path reset[end]\n");

	MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 0, 2);

	DISPDBG("[POWER]dpmanager re-init[begin]\n");

	{
		LCM_PARAMS *lcm_param;
		disp_ddp_path_config *data_config;

		/* disconnect primary path first *
		 * because MMsys config register may not power off during early suspend
		 * BUT session mode may change in primary_display_switch_mode() */
		ddp_disconnect_path(DDP_SCENARIO_PRIMARY_ALL, NULL);
		ddp_disconnect_path(DDP_SCENARIO_PRIMARY_RDMA0_COLOR0_DISP, NULL);
		DISPCHECK("cmd/video mode=%d\n", primary_display_is_video_mode());
		dpmgr_path_set_video_mode(pgc->dpmgr_handle, primary_display_is_video_mode());

		dpmgr_path_connect(pgc->dpmgr_handle, CMDQ_DISABLE);
		if (primary_display_is_decouple_mode()) {
			if (pgc->ovl2mem_path_handle)
				dpmgr_path_connect(pgc->ovl2mem_path_handle, CMDQ_DISABLE);
			else
				DISPERR("in decouple_mode but no ovl2mem_path_handle\n");
		}

		MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 1, 2);
		lcm_param = disp_lcm_get_params(pgc->plcm);

		data_config = dpmgr_path_get_last_config(pgc->dpmgr_handle);
		memcpy(&(data_config->dispif_config), lcm_param, sizeof(LCM_PARAMS));

		data_config->dst_w = disp_helper_get_option(DISP_OPT_FAKE_LCM_WIDTH);
		data_config->dst_h = disp_helper_get_option(DISP_OPT_FAKE_LCM_HEIGHT);
		if (lcm_param->type == LCM_TYPE_DSI) {
			if (lcm_param->dsi.data_format.format == LCM_DSI_FORMAT_RGB888)
				data_config->lcm_bpp = 24;
			else if (lcm_param->dsi.data_format.format == LCM_DSI_FORMAT_RGB565)
				data_config->lcm_bpp = 16;
			else if (lcm_param->dsi.data_format.format == LCM_DSI_FORMAT_RGB666)
				data_config->lcm_bpp = 18;
		} else if (lcm_param->type == LCM_TYPE_DPI) {
			if (lcm_param->dpi.format == LCM_DPI_FORMAT_RGB888)
				data_config->lcm_bpp = 24;
			else if (lcm_param->dpi.format == LCM_DPI_FORMAT_RGB565)
				data_config->lcm_bpp = 16;
			if (lcm_param->dpi.format == LCM_DPI_FORMAT_RGB666)
				data_config->lcm_bpp = 18;
		}

		data_config->fps = pgc->lcm_fps;
		if (disp_partial_is_support()) {
			data_config->ovl_partial_roi.x = 0;
			data_config->ovl_partial_roi.y = 0;
			data_config->ovl_partial_roi.width = primary_display_get_width();
			data_config->ovl_partial_roi.height = primary_display_get_height();
			if (disp_helper_get_option(DISP_OPT_DYNAMIC_RDMA_GOLDEN_SETTING)) {
				/* update rdma goden settin */
				set_rdma_width_height(data_config->ovl_partial_roi.width,
						data_config->ovl_partial_roi.height);
			}
		}
		data_config->dst_dirty = 1;

		/* disable all ovl layers to show black screen */
		/* note that if WFD is connected, we may miss the black setting before the last suspend */
		for (i = 0; i < ARRAY_SIZE(data_config->ovl_config); i++) {
			if (is_DAL_Enabled() &&
				data_config->ovl_config[i].layer == primary_display_get_option("ASSERT_LAYER"))
				continue;
			data_config->ovl_config[i].layer_en = 0;
		}
		data_config->ovl_dirty = 1;

		ret = dpmgr_path_config(pgc->dpmgr_handle, data_config, NULL);
		MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 2, 2);
		data_config->dst_dirty = 0;

		memset(&gset_arg, 0, sizeof(gset_arg));
		gset_arg.dst_mod_type = dpmgr_path_get_dst_module_type(pgc->dpmgr_handle);
		gset_arg.is_decouple_mode = primary_display_is_decouple_mode();
		dpmgr_path_ioctl(pgc->dpmgr_handle, NULL, DDP_OVL_GOLDEN_SETTING, &gset_arg);

	}
	DISPCHECK("[POWER]dpmanager re-init[end]\n");
	MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 0, 3);

	DISPDBG("[POWER]lcm resume[begin]\n");
	disp_lcm_resume(pgc->plcm);
	DISPCHECK("[POWER]lcm resume[end]\n");

	MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 0, 4);
	if (dpmgr_path_is_busy(pgc->dpmgr_handle)) {
		MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 1, 4);
		DISPERR("[POWER]Fatal error, we didn't start display path but it's already busy\n");
		ret = -1;
		/* goto done; */
	}

	MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 0, 5);
	DISPCHECK("[POWER]dpmgr path start[begin]\n");
	dpmgr_path_start(pgc->dpmgr_handle, CMDQ_DISABLE);

	if (primary_display_is_decouple_mode())
		dpmgr_path_start(pgc->ovl2mem_path_handle, CMDQ_DISABLE);

	DISPCHECK("[POWER]dpmgr path start[end]\n");

	MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 0, 6);
	if (dpmgr_path_is_busy(pgc->dpmgr_handle)) {
		MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 1, 6);
		DISPERR
		    ("[POWER]Fatal error, we didn't trigger display path but it's already busy\n");
		ret = -1;
		/* goto done; */
	}
	MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 0, 7);
	if (disp_helper_get_option(DISP_OPT_USE_CMDQ)) {
		DISPCHECK("[POWER]build cmdq trigger loop[begin]\n");
			  _cmdq_build_trigger_loop();
		DISPCHECK("[POWER]build cmdq trigger loop[end]\n");
	}
	if (primary_display_is_video_mode()) {
		MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 1, 7);
		/* for video mode, we need to force trigger here */
		/* for cmd mode, just set DPREC_EVENT_CMDQ_SET_EVENT_ALLOW when trigger loop start */
		if (_should_reset_cmdq_config_handle())
			_cmdq_reset_config_handle();
		/* for next frame */
		if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
				disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
			dpmgr_path_mutex_get(pgc->dpmgr_handle, pgc->cmdq_handle_config);
		} else {
			if (_should_insert_wait_frame_done_token())
				_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);
		}

		dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC, DDP_IRQ_RDMA0_DONE);
		dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);

		/* for current frame */
		if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
				disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
			dpmgr_path_mutex_release(pgc->dpmgr_handle, NULL);
		}
		dpmgr_path_trigger(pgc->dpmgr_handle, NULL, CMDQ_DISABLE);
		if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
				disp_helper_get_option(DISP_OPT_SHADOW_MODE) != 0) {
			dpmgr_path_mutex_get(pgc->dpmgr_handle, NULL);
			dpmgr_path_mutex_release(pgc->dpmgr_handle, NULL);
		}

	}
	MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 0, 8);

	if (disp_helper_get_option(DISP_OPT_USE_CMDQ)) {
		DISPDBG("[POWER]start cmdq[begin]\n");
		_cmdq_start_trigger_loop();
		DISPDBG("[POWER]start cmdq[end]\n");
	}
	MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 0, 9);

	/* primary_display_diagnose(); */
	MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 0, 10);

	if (!primary_display_is_video_mode()) {
		DISPCHECK("[POWER]triggger cmdq[begin]\n");
		if (_should_reset_cmdq_config_handle())
			_cmdq_reset_config_handle();

		/* for next frame */
		if (disp_helper_get_option(DISP_OPT_SHADOW_REGISTER) &&
				disp_helper_get_option(DISP_OPT_SHADOW_MODE) == 0) {
			dpmgr_path_mutex_get(pgc->dpmgr_handle, pgc->cmdq_handle_config);
		} else {
			if (_should_insert_wait_frame_done_token())
				_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);
		}

		dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC, DDP_IRQ_DSI0_EXT_TE);
		dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);

		/* refresh black picture of ovl bg */
		_trigger_display_interface(1, NULL, 0);
		DISPCHECK("[POWER]triggger cmdq[end]\n");
		mdelay(16);	/* wait for one frame for pms workarround!!!! */
	}
	MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagPulse, 0, 11);

	/* (in suspend) when we stop trigger loop
	 * if no other thread is running, cmdq may disable its clock
	 * all cmdq event will be cleared after suspend */
	cmdqCoreSetEvent(CMDQ_EVENT_DISP_WDMA0_EOF);

	/* reinit fake timer for debug, we can enable option then press powerkey to enable thsi feature. */
	/* use fake timer to generate vsync signal for cmd mode w/o LCM(originally using LCM TE Signal as VSYNC) */
	/* so we don't need to modify display driver's behavior. */
	if (disp_helper_get_option(DISP_OPT_NO_LCM_FOR_LOW_POWER_MEASUREMENT)) {
		/* only for low power measurement */
		DISPWARN("WARNING!!!!!! FORCE NO LCM MODE!!!\n");
		islcmconnected = 0;

		/* no need to change video mode vsync behavior */
		if (!primary_display_is_video_mode()) {
			_init_vsync_fake_monitor(pgc->lcm_fps);

			dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
					       DDP_IRQ_UNKNOWN);
		}
	}

	/* need enter share sram for resume */
	if (disp_helper_get_option(DISP_OPT_SHARE_SRAM))
		enter_share_sram(CMDQ_SYNC_RESOURCE_WROT1);

done:
	primary_set_state(DISP_ALIVE);
#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
	switch_set_state(&disp_switch_data, DISP_ALIVE);
#endif
	_primary_path_unlock(__func__);

	aee_kernel_wdt_kick_Powkey_api("mtkfb_late_resume", WDT_SETBY_Display);
	MMProfileLogEx(ddp_mmp_get_events()->primary_resume, MMProfileFlagEnd, 0, 0);
	return 0;
}

int primary_display_ipoh_restore(void)
{
	DISPMSG("primary_display_ipoh_restore In\n");
	DISPDBG("ESD check stop[begin]\n");
	enable_idlemgr(0);
	primary_display_esd_check_enable(0);
	DISPCHECK("ESD check stop[end]\n");
	if (NULL != pgc->cmdq_handle_trigger) {
		struct TaskStruct *pTask = pgc->cmdq_handle_trigger->pRunningTask;

		if (NULL != pTask) {
			DISPCHECK("[Primary_display]display cmdq trigger loop stop[begin]\n");
			_cmdq_stop_trigger_loop();
			DISPCHECK("[Primary_display]display cmdq trigger loop stop[end]\n");
			ddp_mutex_set_sof_wait(dpmgr_path_get_mutex(pgc->dpmgr_handle), NULL, 0);
		}
	}
	DISPMSG("primary_display_ipoh_restore Out\n");
	return 0;
}

int primary_display_start(void)
{
	DISP_STATUS ret = DISP_STATUS_OK;

	DISPFUNC();

	_primary_path_lock(__func__);
	dpmgr_path_start(pgc->dpmgr_handle, CMDQ_DISABLE);

	if (dpmgr_path_is_busy(pgc->dpmgr_handle)) {
		DISPERR("Fatal error, we didn't trigger display path but it's already busy\n");
		ret = -1;
		goto done;
	}

done:
	_primary_path_unlock(__func__);
	return ret;
}

int primary_display_stop(void)
{
	DISP_STATUS ret = DISP_STATUS_OK;

	DISPFUNC();
	_primary_path_lock(__func__);

	if (dpmgr_path_is_busy(pgc->dpmgr_handle))
		dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ * 1);

	dpmgr_path_stop(pgc->dpmgr_handle, CMDQ_DISABLE);

	if (dpmgr_path_is_busy(pgc->dpmgr_handle)) {
		DISPERR("stop display path failed, still busy\n");
		ret = -1;
		goto done;
	}

done:
	_primary_path_unlock(__func__);
	return ret;
}

void primary_display_update_present_fence(unsigned int fence_idx)
{
	gPresentFenceIndex = fence_idx;
	atomic_set(&primary_display_present_fence_update_event, 1);
	wake_up_interruptible(&primary_display_present_fence_wq);
}

/* the function will trigger ovl->wdma */
static int trigger_decouple_mirror(void)
{
	if (pgc->need_trigger_dcMirror_out == 0) {
		/* DISPPR_ERROR("There is no output config when decouple mirror!!\n"); */
	} else {
		pgc->need_trigger_dcMirror_out = 0;

		/* check if still in decouple mirror mode: *
		 * DC->DL switch may happen before vsync, it may free ovl2mem_handle ! */
		if (pgc->session_mode == DISP_SESSION_DECOUPLE_MIRROR_MODE) {
			_trigger_ovl_to_memory(pgc->ovl2mem_path_handle,
						      pgc->cmdq_handle_ovl1to2_config,
						      (CmdqAsyncFlushCB)_ovl_wdma_fence_release_callback,
						      DISP_SESSION_DECOUPLE_MIRROR_MODE);
			dprec_logger_trigger(DPREC_LOGGER_PRIMARY_TRIGGER, 0xffffffff, 0);
		} else {
			dprec_logger_trigger(DPREC_LOGGER_PRIMARY_TRIGGER, 0xffffffff, 0xaaaaaaaa);
		}
	}
	return 0;
}

static int primary_display_trigger_nolock(int blocking, void *callback, int need_merge)
{
	int ret = 0;
	/* DISPFUNC(); */

	last_primary_trigger_time = sched_clock();
	if (is_switched_dst_mode) {
		primary_display_switch_dst_mode(1);	/* swith to vdo mode if trigger disp */
		is_switched_dst_mode = false;
	}

	if (gTriggerDispMode > 0) {
		primary_display_release_fence_fake();
		return ret;
	}

	primary_trigger_cnt++;

	if (pgc->state == DISP_SLEPT) {
		DISPERR("%s, skip because primary dipslay is sleep\n", __func__);
		goto done;
	}
	primary_display_idlemgr_kick(__func__, 0);

	dprec_logger_start(DPREC_LOGGER_PRIMARY_TRIGGER, 0, 0);

	if (pgc->session_mode == DISP_SESSION_DIRECT_LINK_MODE ||
		pgc->session_mode == DISP_SESSION_RDMA_MODE) {
		_trigger_display_interface(blocking, _ovl_fence_release_callback,
					   DISP_SESSION_DIRECT_LINK_MODE);
	} else if (pgc->session_mode == DISP_SESSION_DIRECT_LINK_MIRROR_MODE) {
		_trigger_display_interface(0, _ovl_fence_release_callback,
					   DISP_SESSION_DIRECT_LINK_MIRROR_MODE);

		if (pgc->need_trigger_ovl1to2 == 0) {
			DISPPR_ERROR("There is no output config when directlink mirror!!\n");
		} else {
			primary_display_remove_output(_wdma_fence_release_callback,
						      DISP_SESSION_DIRECT_LINK_MIRROR_MODE);
			pgc->need_trigger_ovl1to2 = 0;
		}
	} else if (pgc->session_mode == DISP_SESSION_DECOUPLE_MODE) {
		_trigger_ovl_to_memory(pgc->ovl2mem_path_handle, pgc->cmdq_handle_ovl1to2_config,
				       (CmdqAsyncFlushCB)_ovl_fence_release_callback, DISP_SESSION_DECOUPLE_MODE);
	} else if (pgc->session_mode == DISP_SESSION_DECOUPLE_MIRROR_MODE) {
		if (need_merge == 0 || primary_is_sec()) {
			trigger_decouple_mirror();
		} else {
			/* because only one of MM/UI thread will set output */
			/* we have to merge it up, then trigger at next vsync */
			/* see decouple_trigger_worker_thread below */
			atomic_set(&decouple_trigger_event, 1);
			wake_up(&decouple_trigger_wq);
		}
	}

	dprec_logger_done(DPREC_LOGGER_PRIMARY_TRIGGER, 0, 0);

	smart_ovl_try_switch_mode_nolock();

	atomic_set(&delayed_trigger_kick, 1);
done:
	if ((primary_trigger_cnt > 1) && aee_kernel_Powerkey_is_press()) {
		aee_kernel_wdt_kick_Powkey_api("primary_display_trigger", WDT_SETBY_Display);
		primary_trigger_cnt = 0;
	}

	if (pgc->session_id > 0)
		update_frm_seq_info(0, 0, 0, FRM_TRIGGER);

	return ret;
}

int primary_display_trigger(int blocking, void *callback, int need_merge)
{
	int ret;

	_primary_path_lock(__func__);
	ret = primary_display_trigger_nolock(blocking, callback, need_merge);

	atomic_set(&hwc_configing, 0);

	_primary_path_unlock(__func__);
	return ret;
}

/* the function will trigger dc and wake up dc thread for merge status; */
/* decouple_trigger thread->trigger mirror->decouple_update_rdma_config_thread */
static int decouple_trigger_worker_thread(void *data)
{
	struct sched_param param = {.sched_priority = 94 };

	sched_setscheduler(current, SCHED_RR, &param);

	while (1) {
		wait_event(decouple_trigger_wq, atomic_read(&decouple_trigger_event));
		dpmgr_wait_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);

		_primary_path_lock(__func__);
		trigger_decouple_mirror();
		atomic_set(&decouple_trigger_event, 0);
		wake_up(&decouple_trigger_wq);
		_primary_path_unlock(__func__);
		if (kthread_should_stop()) {
			DISPERR("error: stop %s as demond\n", __func__);
			break;
		}
	}
	return 0;
}

static int config_wdma_output(disp_path_handle disp_handle,
			      cmdqRecHandle cmdq_handle, disp_output_config *output)
{
	disp_ddp_path_config *pconfig = NULL;

	ASSERT(output != NULL);
	pconfig = dpmgr_path_get_last_config(disp_handle);
	pconfig->wdma_config.dstAddress = (unsigned long)output->pa;
	pconfig->wdma_config.srcHeight = disp_helper_get_option(DISP_OPT_FAKE_LCM_HEIGHT);
	pconfig->wdma_config.srcWidth = disp_helper_get_option(DISP_OPT_FAKE_LCM_WIDTH);
	pconfig->wdma_config.clipX = output->x;
	pconfig->wdma_config.clipY = output->y;
	pconfig->wdma_config.clipHeight = output->height;
	pconfig->wdma_config.clipWidth = output->width;
	pconfig->wdma_config.outputFormat = disp_fmt_to_unified_fmt(output->fmt);
	pconfig->wdma_config.useSpecifiedAlpha = 1;
	pconfig->wdma_config.alpha = 0xFF;
	pconfig->wdma_config.dstPitch = output->pitch * UFMT_GET_Bpp(pconfig->wdma_config.outputFormat);
	pconfig->wdma_config.security = output->security;
	pconfig->wdma_dirty = 1;

	return dpmgr_path_config(disp_handle, pconfig, cmdq_handle);
}

static int _convert_disp_output_to_memout(disp_output_config *src, disp_mem_output_config *dst)
{
	dst->fmt = disp_fmt_to_unified_fmt(src->fmt);

	dst->vaddr = (unsigned long)src->va;
	dst->security = src->security;
	dst->w = src->width;
	dst->h = src->height;

	dst->addr = (unsigned long)src->pa;

	dst->buff_idx = src->buff_idx;
	dst->interface_idx = src->interface_idx;

	dst->x = src->x;
	dst->y = src->y;
	dst->pitch = src->pitch * UFMT_GET_Bpp(dst->fmt);
	return 0;
}

static int primary_frame_cfg_output(struct disp_frame_cfg_t *cfg)
{
	int ret = 0;
	disp_path_handle disp_handle;
	cmdqRecHandle cmdq_handle = NULL;

	if (pgc->state == DISP_SLEPT) {
		DISPERR("mem out is already slept or mode wrong(%d)\n", pgc->session_mode);
		goto done;
	}

	if (!primary_display_is_mirror_mode()) {
		DISPERR("should not config output if not mirror mode!!\n");
		goto done;
	}

	if (primary_display_is_decouple_mode()) {
		disp_handle = pgc->ovl2mem_path_handle;
		cmdq_handle = pgc->cmdq_handle_ovl1to2_config;
	} else {
		disp_handle = pgc->dpmgr_handle;
		cmdq_handle = pgc->cmdq_handle_config;
	}

	if (primary_display_is_decouple_mode()) {
		pgc->need_trigger_dcMirror_out = 1;
	} else {
		/* direct link mirror mode should add memout first */
		dpmgr_path_add_memout(pgc->dpmgr_handle, DISP_MODULE_OVL0, cmdq_handle);
		pgc->need_trigger_ovl1to2 = 1;
	}

	ret = config_wdma_output(disp_handle, cmdq_handle, &cfg->output_cfg);

	if ((pgc->session_id > 0) && primary_display_is_decouple_mode())
		update_frm_seq_info((unsigned long)(cfg->output_cfg.pa), 0,
				    mtkfb_query_frm_seq_by_addr(pgc->session_id, 0, 0), FRM_CONFIG);

	_convert_disp_output_to_memout(&cfg->output_cfg, &mem_config);

	MMProfileLogEx(ddp_mmp_get_events()->primary_wdma_config, MMProfileFlagPulse,
		       cfg->output_cfg.buff_idx, (unsigned long)(cfg->output_cfg.pa));

done:
	return ret;
}

typedef enum {
	SVP_NOMAL = 0,
	SVP_IN_POINT,
	SVP_SEC,
	SVP_2_NOMAL,
	SVP_EXIT_POINT
} SVP_STATE;

static SVP_STATE svp_state = SVP_NOMAL;
static int svp_sum;

#ifndef OPT_BACKUP_NUM
	#define OPT_BACKUP_NUM 3
#endif

static DISP_HELPER_OPT opt_backup_name[OPT_BACKUP_NUM] = {
	DISP_OPT_SMART_OVL,
	DISP_OPT_IDLEMGR_SWTCH_DECOUPLE,
	DISP_OPT_BYPASS_OVL
};

static int opt_backup_value[OPT_BACKUP_NUM];
static unsigned int idlemgr_flag_backup;

static int disp_enter_svp(SVP_STATE state)
{
	int i;

	if (state == SVP_IN_POINT) {

		for (i = 0; i < OPT_BACKUP_NUM; i++) {
			opt_backup_value[i] = disp_helper_get_option(opt_backup_name[i]);
			disp_helper_set_option(opt_backup_name[i], 0);
		}

		if (primary_display_is_decouple_mode() && (!primary_display_is_mirror_mode())) {
			/* switch to DL */
			do_primary_display_switch_mode(DISP_SESSION_DIRECT_LINK_MODE, pgc->session_id, 0, NULL, 0);
		}
		idlemgr_flag_backup = set_idlemgr(0, 0);
	}

	return 0;
}

static int disp_leave_svp(SVP_STATE state)
{
	int i;

	if (state == SVP_EXIT_POINT) {

		for (i = 0; i < OPT_BACKUP_NUM; i++)
			disp_helper_set_option(opt_backup_name[i], opt_backup_value[i]);

		set_idlemgr(idlemgr_flag_backup, 0);
	}
	return 0;
}

static int setup_disp_sec(disp_ddp_path_config *data_config, cmdqRecHandle cmdq_handle,
			  int is_locked)
{
	int i, has_sec_layer = 0;

	for (i = 0; i < ARRAY_SIZE(data_config->ovl_config); i++) {
		if (data_config->ovl_config[i].layer_en && (data_config->ovl_config[i].security == DISP_SECURE_BUFFER))
			has_sec_layer = 1;
	}

	if (has_sec_layer != primary_is_sec()) {
		MMProfileLogEx(ddp_mmp_get_events()->sec, MMProfileFlagPulse, has_sec_layer, 0);
		/* sec/nonsec switch */

		cmdqRecReset(cmdq_handle);
		if (primary_display_is_decouple_mode())
			cmdqRecWait(cmdq_handle, CMDQ_EVENT_DISP_WDMA0_EOF);
		else
			_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);



		MMProfileLogEx(ddp_mmp_get_events()->sec, MMProfileFlagPulse, has_sec_layer, 1);

		if (has_sec_layer) {
			/* switch nonsec --> sec */
			svp_state = SVP_IN_POINT;
			disp_enter_svp(svp_state);
			MMProfileLogEx(ddp_mmp_get_events()->sec, MMProfileFlagStart, 0, 0);
		} else {
			/* switch sec --> nonsec */
			svp_state = SVP_2_NOMAL;
			MMProfileLogEx(ddp_mmp_get_events()->sec, MMProfileFlagEnd, 0, 0);
		}
	}

	if ((has_sec_layer == primary_is_sec()) && (primary_is_sec() == 0)) {
		if (svp_state == SVP_2_NOMAL) {
			svp_sum++;
			/*after 3 normal frame, we restore the normal environment*/
			if (svp_sum > 2) {
				svp_sum = 0;
				svp_state = SVP_EXIT_POINT;
				disp_leave_svp(svp_state);
			}
		} else {
			svp_state = SVP_NOMAL;
		}
	}

	if ((has_sec_layer == primary_is_sec()) && (primary_is_sec() == 1)) /* IN SVP now!*/
		svp_state = SVP_SEC;

	pgc->is_primary_sec = has_sec_layer;
	return 0;
}

static int can_bypass_ovl(disp_ddp_path_config *data_config, int *bypass_layer_id)
{
	int total_layer = 0;
	int i;
	unsigned int w, h;

#ifdef CONFIG_MTK_LCM_PHYSICAL_ROTATION_HW
	/* rdma don't support rotation */
	return 0;
#endif

	if (!disp_helper_get_option(DISP_OPT_BYPASS_OVL))
		return 0;

	for (i = 0; i < ARRAY_SIZE(data_config->ovl_config); i++) {
		if (data_config->ovl_config[i].layer_en) {
			total_layer++;
			*bypass_layer_id = i;
		}
	}

	if (total_layer != 1)
		return 0;

	/* rdma cannot process dim layer */
	if (data_config->ovl_config[*bypass_layer_id].source != OVL_LAYER_SOURCE_MEM)
		return 0;

	/* now we have only 1 layer */
	/* we need to check layer size, because rdma has output_valid_thres setting
	 * if (size < output_valid_thres) RDMA will hang !!*/
	h = data_config->ovl_config[*bypass_layer_id].dst_h;
	w = data_config->ovl_config[*bypass_layer_id].dst_w;
	if (w * h <= 512 * 16 / 2)
		return 0;

	return 1;
}

static int evaluate_bandwidth_save(disp_ddp_path_config *cfg, int *ori, int *act)
{
	int i = 0;
	int pixel = 0;
	int partial_pixel = 0;
	int save = 0;

	for (i = 0; i < TOTAL_OVL_LAYER_NUM; i++) {
		int layer_pixel = 0;
		struct disp_rect layer_roi = {0, 0, 0, 0};
		struct disp_rect layer_partial_roi = {0, 0, 0, 0};
		OVL_CONFIG_STRUCT *layer = &cfg->ovl_config[i];

		if (!layer->layer_en)
			continue;

		layer_pixel = layer->dst_w * layer->dst_h;
		pixel += layer_pixel;
		if (cfg->ovl_partial_dirty) {
			layer_roi.x = layer->dst_x;
			layer_roi.y = layer->dst_y;
			layer_roi.width = layer->dst_w;
			layer_roi.height = layer->dst_h;
			if (rect_intersect(&layer_roi, &cfg->ovl_partial_roi, &layer_partial_roi))
				partial_pixel += layer_partial_roi.width * layer_partial_roi.height;

		} else {
			partial_pixel += layer_pixel;
		}
	}

	if (pixel)
		save = (pixel - partial_pixel) * 100 / pixel;

	*ori = pixel;
	*act = partial_pixel;

	DISPDBG("frame partial save:%d, %d, %%%d\n", pixel, partial_pixel, save);
	return 0;
}

static int _config_ovl_input(struct disp_frame_cfg_t *cfg,
			     disp_path_handle disp_handle, cmdqRecHandle cmdq_handle)
{
	int ret = 0, i = 0, layer = 0;
	disp_ddp_path_config *data_config = NULL;
	int max_layer_id_configed = 0;
	int bypass, bypass_layer_id = 0;
	int hrt_level;
	struct disp_rect total_dirty_roi = {0, 0, 0, 0};
#ifdef DEBUG_OVL_CONFIG_TIME
	cmdqRecBackupRegisterToSlot(cmdq_handle, pgc->ovl_config_time, 0, 0x10008028);
#endif
	/*=== create new data_config for ovl input ===*/
	data_config = dpmgr_path_get_last_config(disp_handle);

	if (disp_partial_is_support()) {
		if (primary_display_is_directlink_mode())
			disp_partial_compute_ovl_roi(cfg, data_config, &total_dirty_roi);
		else
			assign_full_lcm_roi(&total_dirty_roi);
	}

	for (i = 0; i < cfg->input_layer_num; i++) {
		disp_input_config *input_cfg = &cfg->input_cfg[i];
		OVL_CONFIG_STRUCT *ovl_cfg;

		layer = input_cfg->layer_id;
		ovl_cfg = &(data_config->ovl_config[layer]);
		if (cfg->setter != SESSION_USER_AEE) {
			if (is_DAL_Enabled() && layer == primary_display_get_option("ASSERT_LAYER")) {
				DISPMSG("skip AEE layer %d\n", layer);
				continue;
			}
		} else {
			DISPMSG("set AEE layer %d\n", layer);
		}
		_convert_disp_input_to_ovl(ovl_cfg, input_cfg);

		dprec_logger_start(DPREC_LOGGER_PRIMARY_CONFIG,
				   ovl_cfg->layer | (ovl_cfg->layer_en << 16), ovl_cfg->addr);
		dprec_logger_done(DPREC_LOGGER_PRIMARY_CONFIG, ovl_cfg->src_x, ovl_cfg->src_y);

		dprec_mmp_dump_ovl_layer(ovl_cfg, layer, 1);

		if ((ovl_cfg->layer == 0) && (!primary_display_is_decouple_mode()))
			update_frm_seq_info(ovl_cfg->addr,
					    ovl_cfg->src_x * 4 +
					    ovl_cfg->src_y * ovl_cfg->src_pitch,
					    mtkfb_query_frm_seq_by_addr(pgc->session_id, 0, 0),
					    FRM_CONFIG);

		if (max_layer_id_configed < layer)
			max_layer_id_configed = layer;

		data_config->ovl_layer_dirty |= (1 << i);
	}
#ifdef CONFIG_MTK_DISPLAY_120HZ_SUPPORT
	hrt_level = HRT_LEVEL(cfg->overlap_layer_num);
#else
	hrt_level = cfg->overlap_layer_num;
#endif
	data_config->overlap_layer_num = hrt_level;

	if (hrt_level > HRT_LEVEL_HIGH)
		DISPCHECK("overlayed layer num is %d > %d\n", hrt_level, HRT_LEVEL_HIGH);

	if (hrt_level > HRT_LEVEL_LOW &&
		primary_display_is_directlink_mode()) {
		_request_dvfs_perf(HRT_LEVEL_HIGH);
		dvfs_last_ovl_req = HRT_LEVEL_HIGH;
	} else if (hrt_level > HRT_LEVEL_EXTREME_LOW) {
		dvfs_last_ovl_req = HRT_LEVEL_LOW;
	} else{
		dvfs_last_ovl_req = HRT_LEVEL_EXTREME_LOW;
	}

	if (disp_helper_get_option(DISP_OPT_SHOW_VISUAL_DEBUG_INFO)) {
		char msg[10];

		snprintf(msg, sizeof(msg), "HRT=%d,", hrt_level);
		screen_logger_add_message("HRT", MESSAGE_REPLACE, msg);
	}

	if (_should_wait_path_idle())
		dpmgr_wait_event_timeout(disp_handle, DISP_PATH_EVENT_FRAME_DONE, HZ * 1);

	if (cmdq_handle)
		setup_disp_sec(data_config, cmdq_handle, 1);

	/* check bypass ovl */
	bypass = can_bypass_ovl(data_config, &bypass_layer_id);
	if (bypass) {
		/* switch to rdma mode */
		if (pgc->session_mode == DISP_SESSION_DIRECT_LINK_MODE) {
			assign_full_lcm_roi(&total_dirty_roi);
			primary_display_config_full_roi(data_config, disp_handle, cmdq_handle);
			do_primary_display_switch_mode(DISP_SESSION_RDMA_MODE, pgc->session_id, 0,
						       cmdq_handle, 0);
		}
	} else {
		if (pgc->session_mode == DISP_SESSION_RDMA_MODE) {
			do_primary_display_switch_mode(DISP_SESSION_DIRECT_LINK_MODE, pgc->session_id, 0,
						       cmdq_handle, 0);
			assign_full_lcm_roi(&total_dirty_roi);
		}
	}

	if (pgc->session_mode != DISP_SESSION_RDMA_MODE) {
		data_config->ovl_dirty = 1;
	} else {
		ret = ddp_convert_ovl_input_to_rdma(&data_config->rdma_config,
						    &data_config->ovl_config[bypass_layer_id],
						    data_config->dst_w, data_config->dst_h);
		data_config->rdma_dirty = 1;

		/* no need ioctl because of rdma_dirty */
		set_is_dc(1);

		dynamic_debug_msg_print(data_config->rdma_config.address, data_config->rdma_config.width,
				data_config->rdma_config.height, data_config->rdma_config.pitch,
				UFMT_GET_Bpp(data_config->rdma_config.inputFormat));

	}

	if (disp_helper_get_option(DISP_OPT_DYNAMIC_SWITCH_MMSYSCLK)) {
		if (bypass) {
			if (set_one_layer(1))
				; /* mmdvfs_notify_mmclk_switch_request(MMDVFS_EVENT_OVL_SINGLE_LAYER_ENTER); */
		} else {
			if (set_one_layer(0))
				; /* mmdvfs_notify_mmclk_switch_request(MMDVFS_EVENT_OVL_SINGLE_LAYER_EXIT); */
		}
	}

	if (disp_partial_is_support() && primary_display_is_directlink_mode()) {
		disp_patial_lcm_validate_roi(pgc->plcm, &total_dirty_roi);
		if (is_equal_full_lcm(&total_dirty_roi))
			aal_request_partial_support(0);
		else
			aal_request_partial_support(1);

		if (!aal_is_partial_support())
			assign_full_lcm_roi(&total_dirty_roi);

		DISPDBG("frame partial roi(%d,%d,%d,%d)\n", total_dirty_roi.x, total_dirty_roi.y,
			total_dirty_roi.width, total_dirty_roi.height);

		if (!rect_equal(&total_dirty_roi, &data_config->ovl_partial_roi)) {
			/* update roi to lcm */
			disp_partial_update_roi_to_lcm(disp_handle, total_dirty_roi, cmdq_handle);
			data_config->ovl_partial_roi = total_dirty_roi;
			if (disp_helper_get_option(DISP_OPT_DYNAMIC_RDMA_GOLDEN_SETTING)) {
				/* update rdma goden settin */
				set_rdma_width_height(total_dirty_roi.width, total_dirty_roi.height);
				dpmgr_path_ioctl(disp_handle, cmdq_handle, DDP_RDMA_GOLDEN_SETTING, data_config);
			}
		}
		if (is_equal_full_lcm(&total_dirty_roi))
			data_config->ovl_partial_dirty = 0;
		else
			data_config->ovl_partial_dirty = 1;

		if (1) {
			static long long total_ori;
			static long long total_partial;

			if (ddp_debug_partial_statistic()) {
				int frame_ori = 0;
				int frame_partial = 0;
				long long save = 0;

				evaluate_bandwidth_save(data_config, &frame_ori, &frame_partial);
				total_ori += frame_ori;
				total_partial += frame_partial;

				if (total_ori) {
					/*save = (total_ori - total_partial) * 100 / total_ori;*/
					unsigned long long tmp = (total_ori - total_partial) * 100;
					do_div(tmp, total_ori);
					save = tmp;
				}

				DISPDBG("total partial save:%%%lld\n", save);
			} else {
				total_ori = 0;
				total_partial = 0;
			}
		}
	}

	ret = dpmgr_path_config(disp_handle, data_config, cmdq_handle);

#ifdef DEBUG_OVL_CONFIG_TIME
	cmdqRecBackupRegisterToSlot(cmdq_handle, pgc->ovl_config_time, 1, 0x10008028);
#endif
	if (!cmdq_handle)
		goto done;

	/* write fence_id/enable to DRAM using cmdq
	 * it will be used when release fence (put these after config registers done)*/
	for (i = 0; i < cfg->input_layer_num; i++) {
		unsigned int last_fence, cur_fence, sub;
		disp_input_config *input_cfg = &cfg->input_cfg[i];

		layer = input_cfg->layer_id;

		cmdqBackupReadSlot(pgc->cur_config_fence, layer, &last_fence);
		cur_fence = input_cfg->next_buff_idx;

		if (cur_fence != -1 && cur_fence > last_fence)
			cmdqRecBackupUpdateSlot(cmdq_handle, pgc->cur_config_fence, layer, cur_fence);

		/* for dim_layer/disable_layer/no_fence_layer, just release all fences configured */
		/* for other layers, release current_fence-1 */
		if (input_cfg->buffer_source == DISP_BUFFER_ALPHA
		    || input_cfg->layer_enable == 0 || cur_fence == -1)
			sub = 0;
		else
			sub = 1;

		/* store overlap layer to layer0's subtractor_when_free : bit[31:16] */
		if (layer == 0)
			sub |= hrt_level << 16;
		cmdqRecBackupUpdateSlot(cmdq_handle, pgc->subtractor_when_free, layer, sub);
	}
	if (primary_display_is_video_mode() && !primary_display_is_decouple_mode()) {
		unsigned long ovl_base = ovl_base_addr(DISP_MODULE_OVL1_2L);

		cmdqRecBackupRegisterToSlot(cmdq_handle, pgc->ovl_status_info,
					    0, disp_addr_convert(DISP_REG_OVL_STA + ovl_base));
	}

done:
#ifdef DEBUG_OVL_CONFIG_TIME
	cmdqRecBackupRegisterToSlot(cmdq_handle, pgc->ovl_config_time, 2, 0x10008028);
#endif
	return ret;
}

/* notes: primary lock should be held when call this func */
static int primary_frame_cfg_input(struct disp_frame_cfg_t *cfg)
{
	int ret = 0;
	unsigned int wdma_mva = 0;
	disp_path_handle disp_handle;
	cmdqRecHandle cmdq_handle;

	if (gTriggerDispMode > 0)
		return 0;

	primary_display_idlemgr_kick(__func__, 0);

	if (primary_display_is_decouple_mode()) {
		disp_handle = pgc->ovl2mem_path_handle;
		cmdq_handle = pgc->cmdq_handle_ovl1to2_config;
	} else {
		disp_handle = pgc->dpmgr_handle;
		cmdq_handle = pgc->cmdq_handle_config;
	}

	if (pgc->state == DISP_SLEPT) {
		DISPERR("%s, skip because primary dipslay is slept\n", __func__);

		if (is_DAL_Enabled() &&
			cfg->setter == SESSION_USER_AEE &&
			cfg->input_cfg[0].layer_id == primary_display_get_option("ASSERT_LAYER")) {
			disp_ddp_path_config *data_config = dpmgr_path_get_last_config(disp_handle);
			int layer = cfg->input_cfg[0].layer_id;

			ret = _convert_disp_input_to_ovl(&(data_config->ovl_config[layer]),
					&cfg->input_cfg[0]);
		}

		goto done;
	}

	fps_ctx_update(&primary_fps_ctx);
	if (disp_helper_get_option(DISP_OPT_SHOW_VISUAL_DEBUG_INFO))
		primary_show_basic_debug_info(cfg);

	_config_ovl_input(cfg, disp_handle, cmdq_handle);

	if (primary_display_is_decouple_mode() && !primary_display_is_mirror_mode()) {
		pgc->dc_buf_id++;
		pgc->dc_buf_id %= DISP_INTERNAL_BUFFER_COUNT;
		wdma_mva = pgc->dc_buf[pgc->dc_buf_id];
		if (wdma_mva == 0) {
			DISPERR("%s, dc buffer does not exist\n", __func__);
			ret = -1;
			goto done;
		}
		decouple_wdma_config.dstAddress = wdma_mva;
		_config_wdma_output(&decouple_wdma_config, pgc->ovl2mem_path_handle,
				    pgc->cmdq_handle_ovl1to2_config);
		mem_config.addr = wdma_mva;
		mem_config.buff_idx = -1;
		mem_config.interface_idx = -1;
		mem_config.security = DISP_NORMAL_BUFFER;
		mem_config.pitch = decouple_wdma_config.dstPitch;
		mem_config.fmt = decouple_wdma_config.outputFormat;
		MMProfileLogEx(ddp_mmp_get_events()->primary_wdma_config, MMProfileFlagPulse,
			       pgc->dc_buf_id, wdma_mva);
	}
done:
	return ret;
}

int primary_display_config_input_multiple(disp_session_input_config *session_input)
{
	int ret = 0;
	struct disp_frame_cfg_t *frame_cfg;

	BUG_ON(sizeof(session_input->config) != sizeof(frame_cfg->input_cfg));

	frame_cfg = kzalloc(sizeof(struct disp_frame_cfg_t), GFP_KERNEL);
	if (frame_cfg == NULL)
		return -ENOMEM;

	frame_cfg->session_id = session_input->session_id;
	frame_cfg->setter = session_input->setter;
	frame_cfg->input_layer_num = session_input->config_layer_num;
	frame_cfg->overlap_layer_num = 4;
	memcpy(frame_cfg->input_cfg, session_input->config, sizeof(frame_cfg->input_cfg));

	_primary_path_lock(__func__);

	atomic_set(&hwc_configing, 1);

	ret = primary_frame_cfg_input(frame_cfg);

	_primary_path_unlock(__func__);

	kfree(frame_cfg);
	return ret;
}

int primary_display_frame_cfg(struct disp_frame_cfg_t *cfg)
{
	int ret = 0;
	disp_session_sync_info *session_info = disp_get_session_sync_info_for_debug(cfg->session_id);
	dprec_logger_event *input_event, *output_event, *trigger_event;

	if (session_info) {
		input_event = &session_info->event_setinput;
		output_event = &session_info->event_setoutput;
		trigger_event = &session_info->event_trigger;
	} else {
		input_event = output_event = trigger_event = NULL;
	}

	_primary_path_lock(__func__);

#ifdef CONFIG_MTK_DISPLAY_120HZ_SUPPORT
	if (pgc->request_fps && HRT_FPS(cfg->overlap_layer_num) == 120)
		_display_set_lcm_refresh_rate(pgc->request_fps);
#endif

	/* set input */
	dprec_start(input_event, cfg->overlap_layer_num, cfg->input_layer_num);
	primary_frame_cfg_input(cfg);
	dprec_done(input_event, 0, 0);

	if (cfg->output_en) {
		dprec_start(output_event, cfg->output_cfg.buff_idx, 0);
		primary_frame_cfg_output(cfg);
		dprec_done(output_event, 0, 0);
	}

	if (trigger_event) {
		/* to debug UI thread or MM thread */
		unsigned int proc_name = (current->comm[0] << 24) |
		    (current->comm[1] << 16) | (current->comm[2] << 8) | (current->comm[3] << 0);
		dprec_start(trigger_event, proc_name, 0);
	}

	if (cfg->present_fence_idx != (unsigned int)-1)
		primary_display_update_present_fence(cfg->present_fence_idx);

	primary_display_trigger_nolock(0, NULL, 0);

	dprec_done(trigger_event, 0, 0);

	_primary_path_unlock(__func__);
	return ret;
}


int primary_display_user_cmd(unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	cmdqRecHandle handle = NULL;
	int cmdqsize = 0;

	MMProfileLogEx(ddp_mmp_get_events()->primary_display_cmd, MMProfileFlagStart, (unsigned long)handle, 0);

	if (cmd == DISP_IOCTL_AAL_GET_HIST) {
		_primary_path_lock(__func__);

		if (disp_helper_get_option(DISP_OPT_USE_CMDQ)) {
			ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &handle);
			cmdqRecReset(handle);
			_cmdq_insert_wait_frame_done_token_mira(handle);
			cmdqsize = cmdqRecGetInstructionCount(handle);
		}

		if (pgc->state == DISP_SLEPT && handle) {
			cmdqRecDestroy(handle);
			handle = NULL;
		}
		_primary_path_unlock(__func__);

		/* only cmd mode & with disable mmsys clk will kick */
		if (disp_helper_get_option(DISP_OPT_IDLEMGR_ENTER_ULPS) && !primary_display_is_video_mode())
			primary_display_idlemgr_kick(__func__, 1);

		ret = dpmgr_path_user_cmd(pgc->dpmgr_handle, cmd, arg, handle);

		if (handle) {
			if (cmdqRecGetInstructionCount(handle) > cmdqsize) {
				_primary_path_lock(__func__);
				if (pgc->state == DISP_ALIVE) {
					/* do not set dirty here, just write register. */
					/* if set dirty needed, will be implemented by dpmgr_module_notify() */
					/* _cmdq_set_config_handle_dirty_mira(handle); */
					/* use non-blocking flush here to avoid primary path is locked for too long */
					_cmdq_flush_config_handle_mira(handle, 0);
				}
				_primary_path_unlock(__func__);
			}

			cmdqRecDestroy(handle);
		}
	} else {
		_primary_path_switch_dst_lock();
		_primary_path_lock(__func__);

		if (disp_helper_get_option(DISP_OPT_USE_CMDQ)) {
			ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &handle);
			cmdqRecReset(handle);
			_cmdq_insert_wait_frame_done_token_mira(handle);
			cmdqsize = cmdqRecGetInstructionCount(handle);
		}

		if (pgc->state == DISP_SLEPT && handle) {
			cmdqRecDestroy(handle);
			handle = NULL;
			goto user_cmd_unlock;
		}
		/* only cmd mode & with disable mmsys clk will kick */
		if (disp_helper_get_option(DISP_OPT_IDLEMGR_ENTER_ULPS) && !primary_display_is_video_mode())
			primary_display_idlemgr_kick(__func__, 0);

		ret = dpmgr_path_user_cmd(pgc->dpmgr_handle, cmd, arg, handle);

		if (handle) {
			if (cmdqRecGetInstructionCount(handle) > cmdqsize) {
				if (pgc->state == DISP_ALIVE) {
					/* do not set dirty here, just write register. */
					/* if set dirty needed, will be implemented by dpmgr_module_notify() */
					/* _cmdq_set_config_handle_dirty_mira(handle); */
					/* use non-blocking flush here to avoid primary path is locked for too long */
					_cmdq_flush_config_handle_mira(handle, 0);
				}
			}

			cmdqRecDestroy(handle);
		}
user_cmd_unlock:
		_primary_path_unlock(__func__);
		_primary_path_switch_dst_unlock();

	}
	MMProfileLogEx(ddp_mmp_get_events()->primary_display_cmd, MMProfileFlagEnd, (unsigned long)handle,
		       cmdqsize);

	return ret;
}

int do_primary_display_switch_mode(int sess_mode, unsigned int session, int need_lock,
					cmdqRecHandle handle, int block)
{
	int ret = 0, sw_only = 0;

	DISPCHECK("primary_display_switch_mode require sess_mode %s\n",
		  session_mode_spy(sess_mode));
	if (need_lock)
		_primary_path_lock(__func__);

	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_mode, MMProfileFlagStart,
		       pgc->session_mode, sess_mode);

	if (pgc->session_mode == sess_mode)
		goto done;


	if (pgc->state == DISP_SLEPT) {
		DISPERR("primary display switch from %s to %s in suspend state!!!\n",
			session_mode_spy(pgc->session_mode), session_mode_spy(sess_mode));
		sw_only = 1;
	}

	DISPMSG("primary display will switch from %s to %s\n", session_mode_spy(pgc->session_mode),
		session_mode_spy(sess_mode));

	if (pgc->session_mode == DISP_SESSION_DIRECT_LINK_MODE
	    && sess_mode == DISP_SESSION_DECOUPLE_MODE) {
		/* dl to dc */
		DL_switch_to_DC_fast(sw_only);
	} else if (pgc->session_mode == DISP_SESSION_DECOUPLE_MODE
		   && sess_mode == DISP_SESSION_DIRECT_LINK_MODE) {
		/* dc to dl */
		DC_switch_to_DL_fast(sw_only);
		/* primary_display_diagnose(); */
	} else if (pgc->session_mode == DISP_SESSION_DIRECT_LINK_MODE
		   && sess_mode == DISP_SESSION_DIRECT_LINK_MIRROR_MODE) {
		/* dl to dl mirror */
	} else if (pgc->session_mode == DISP_SESSION_DIRECT_LINK_MIRROR_MODE
		   && sess_mode == DISP_SESSION_DIRECT_LINK_MODE) {
		/* dl mirror to dl */
	} else if (pgc->session_mode == DISP_SESSION_DIRECT_LINK_MODE
		   && sess_mode == DISP_SESSION_DECOUPLE_MIRROR_MODE) {
		/* dl to dc mirror  mirror */
		DL_switch_to_DC_fast(sw_only);
	} else if (pgc->session_mode == DISP_SESSION_DECOUPLE_MIRROR_MODE
		   && sess_mode == DISP_SESSION_DIRECT_LINK_MODE) {
		/* dc mirror  to dl */
		DC_switch_to_DL_fast(sw_only);
	} else if (pgc->session_mode == DISP_SESSION_DECOUPLE_MIRROR_MODE &&
			sess_mode == DISP_SESSION_DECOUPLE_MODE){
		/* do nothing */
		/* just switch mode */
	} else if (pgc->session_mode == DISP_SESSION_DECOUPLE_MODE &&
			sess_mode == DISP_SESSION_DECOUPLE_MIRROR_MODE){
		/* do nothing */
		/* just switch mode */
	} else if (pgc->session_mode == DISP_SESSION_DIRECT_LINK_MODE && sess_mode == DISP_SESSION_RDMA_MODE) {
		ret = DL_switch_to_rdma_mode(handle, block);
		if (ret)
			goto err;
	} else if (pgc->session_mode == DISP_SESSION_RDMA_MODE && sess_mode == DISP_SESSION_DIRECT_LINK_MODE) {
		ret = rdma_mode_switch_to_DL(handle, block);
		if (ret)
			goto err;
	} else if (pgc->session_mode == DISP_SESSION_RDMA_MODE && sess_mode == DISP_SESSION_DECOUPLE_MIRROR_MODE) {
		/* switch to DL mode first */
		ret = rdma_mode_switch_to_DL(NULL, 0);
		if (ret)
			goto err;
		MMProfileLogEx(ddp_mmp_get_events()->primary_switch_mode, MMProfileFlagPulse, pgc->session_mode, 0);
		DL_switch_to_DC_fast(0);
	} else {
		DISPERR("invalid mode switch from %s to %s\n", session_mode_spy(pgc->session_mode),
			session_mode_spy(sess_mode));
		BUG();
	}
done:
	MMProfileLogEx(ddp_mmp_get_events()->primary_mode[pgc->session_mode],
				MMProfileFlagEnd, pgc->session_mode, sess_mode);
	MMProfileLogEx(ddp_mmp_get_events()->primary_mode[sess_mode],
				MMProfileFlagStart, pgc->session_mode, sess_mode);

	pgc->session_mode = sess_mode;
	DISPMSG("primary display is %s mode now\n", session_mode_spy(pgc->session_mode));
	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_mode, MMProfileFlagPulse, pgc->session_mode, sess_mode);
	pgc->session_id = session;
	screen_logger_add_message("sess_mode", MESSAGE_REPLACE, (char *)session_mode_spy(sess_mode));
err:
	MMProfileLogEx(ddp_mmp_get_events()->primary_switch_mode, MMProfileFlagEnd, pgc->session_mode, sess_mode);

	if (need_lock)
		_primary_path_unlock(__func__);
	pgc->session_id = session;
	return ret;
}

int primary_display_switch_mode(int sess_mode, unsigned int session, int force)
{
	int ret = 0;

	_primary_path_lock(__func__);
	primary_display_idlemgr_kick(__func__, 0);

	if (!force && primary_display_is_mirror_mode() == _is_mirror_mode(sess_mode)) {
		/* HWC only needs to control mirror/not mirror
		 * it doesn't need to control DL/DC */
		 goto done;
	}

	if (pgc->session_mode == sess_mode)
		goto done;

	while (primary_get_state() == DISP_BLANK) {
		_primary_path_unlock(__func__);
		DISPMSG("%s wait for leave TUI\n", __func__);
		primary_display_wait_not_state(DISP_BLANK, MAX_SCHEDULE_TIMEOUT);
		_primary_path_lock(__func__);
	}

	ret = do_primary_display_switch_mode(sess_mode, session, 0, NULL, 0);

done:
	_primary_path_unlock(__func__);

	return ret;
}

static int smart_ovl_try_switch_mode_nolock(void)
{
	unsigned int hwc_fps, lcm_fps;
	unsigned long long ovl_sz, rdma_sz;
	disp_path_handle disp_handle = NULL;
	disp_ddp_path_config *data_config = NULL;
	int i, stable;
	unsigned long long DL_bw, DC_bw, bw_th;

	if (!disp_helper_get_option(DISP_OPT_SMART_OVL))
		return 0;

	if (!primary_display_is_video_mode())
		return 0;

	if (pgc->session_mode != DISP_SESSION_DIRECT_LINK_MODE &&
		pgc->session_mode != DISP_SESSION_DECOUPLE_MODE)
		return 0;

	if (pgc->state != DISP_ALIVE)
		return 0;

	lcm_fps = pgc->lcm_fps / 100;
	fps_ctx_get_fps(&primary_fps_ctx, &hwc_fps, &stable);

	/* we switch DL->DC only when fps is stable ! */
	if (pgc->session_mode == DISP_SESSION_DIRECT_LINK_MODE) {
		if (!stable)
			return 0;
	}

	if (hwc_fps > lcm_fps)
		hwc_fps = lcm_fps;

	if (pgc->session_mode == DISP_SESSION_DECOUPLE_MODE)
		disp_handle = pgc->ovl2mem_path_handle;
	else
		disp_handle = pgc->dpmgr_handle;

	data_config = dpmgr_path_get_last_config(disp_handle);

	/* calc wdma/rdma data size */
	rdma_sz = data_config->dst_h * data_config->dst_w * 3;

	/* calc ovl data size */
	ovl_sz = 0;
	for (i = 0; i < ARRAY_SIZE(data_config->ovl_config); i++) {
		OVL_CONFIG_STRUCT *ovl_cfg = &(data_config->ovl_config[i]);

		if (ovl_cfg->layer_en) {
			unsigned int Bpp = UFMT_GET_Bpp(ovl_cfg->fmt);

			ovl_sz += ovl_cfg->dst_w * ovl_cfg->dst_h * Bpp;
		}
	}

	/* switch criteria is:  (DL) V.S. (DC):
	 *   (ovl * lcm_fps) V.S. (ovl * hwc_fps + wdma * hwc_fps + rdma * lcm_fps)
	 */
	DL_bw = ovl_sz * lcm_fps;
	DC_bw = (ovl_sz + rdma_sz) * hwc_fps + rdma_sz * lcm_fps;

	if (pgc->session_mode == DISP_SESSION_DIRECT_LINK_MODE) {
		bw_th = DL_bw*4;
		do_div(bw_th, 5);
		if (DC_bw < bw_th) {
			/* switch to DC */
			do_primary_display_switch_mode(DISP_SESSION_DECOUPLE_MODE, pgc->session_id, 0, NULL, 0);
		}
	} else {
		bw_th = DC_bw*4;
		do_div(bw_th, 5);
		if (DL_bw < bw_th) {
			/* switch to DL */
			do_primary_display_switch_mode(DISP_SESSION_DIRECT_LINK_MODE, pgc->session_id, 0, NULL, 0);
		}
	}

	return 0;
}

int primary_display_is_alive(void)
{
	unsigned int temp = 0;

	/* DISPFUNC(); */
	_primary_path_lock(__func__);

	if (pgc->state == DISP_ALIVE)
		temp = 1;


	_primary_path_unlock(__func__);

	return temp;
}

int primary_display_is_sleepd(void)
{
	unsigned int temp = 0;

	/* DISPFUNC(); */
	_primary_path_lock(__func__);

	if (pgc->state == DISP_SLEPT)
		temp = 1;


	_primary_path_unlock(__func__);

	return temp;
}

int primary_display_get_width(void)
{
	if (pgc->plcm == NULL) {
		DISPERR("lcm handle is null\n");
		return 0;
	}

	if (pgc->plcm->params)
		return pgc->plcm->params->width;

	DISPERR("lcm_params is null!\n");
	return 0;
}

int primary_display_get_height(void)
{
	if (pgc->plcm == NULL) {
		DISPERR("lcm handle is null\n");
		return 0;
	}

	if (pgc->plcm->params)
		return pgc->plcm->params->height;

	DISPERR("lcm_params is null!\n");
	return 0;
}

int primary_display_get_virtual_width(void)
{
	if (pgc->plcm == NULL) {
		DISPERR("lcm handle is null\n");
		return 0;
	}

	if (pgc->plcm->params)
		return pgc->plcm->params->virtual_width;

	DISPERR("lcm_params is null!\n");
	return 0;
}

int primary_display_get_virtual_height(void)
{
	if (pgc->plcm == NULL) {
		DISPERR("lcm handle is null\n");
		return 0;
	}

	if (pgc->plcm->params)
		return pgc->plcm->params->virtual_height;

	DISPERR("lcm_params is null!\n");
	return 0;
}

int primary_display_get_original_width(void)
{
	if (pgc->plcm == NULL) {
		DISPERR("lcm handle is null\n");
		return 0;
	}

	if (pgc->plcm->params)
		return pgc->plcm->lcm_original_width;

	DISPERR("lcm_params is null!\n");
	return 0;
}

int primary_display_get_original_height(void)
{
	if (pgc->plcm == NULL) {
		DISPERR("lcm handle is null\n");
		return 0;
	}

	if (pgc->plcm->params)
		return pgc->plcm->lcm_original_height;

	DISPERR("lcm_params is null!\n");
	return 0;
}

int primary_display_get_bpp(void)
{
	return 32;
}

void primary_display_set_max_layer(int maxlayer)
{
	pgc->max_layer = maxlayer;
}

int primary_display_get_max_layer(void)
{
	return pgc->max_layer;
}

int primary_display_get_info(disp_session_info *info)
{
	disp_session_info *dispif_info = (disp_session_info *)info;
	LCM_PARAMS *lcm_param = disp_lcm_get_params(pgc->plcm);

	if (lcm_param == NULL) {
		DISPERR("lcm_param is null\n");
		return -1;
	}

	memset((void *)dispif_info, 0, sizeof(disp_session_info));

	if (is_DAL_Enabled())
		dispif_info->maxLayerNum = pgc->max_layer - 1;
	else
		dispif_info->maxLayerNum = pgc->max_layer;

	dispif_info->const_layer_num = 0;

	switch (lcm_param->type) {
	case LCM_TYPE_DBI:
		{
			dispif_info->displayType = DISP_IF_TYPE_DBI;
			dispif_info->displayMode = DISP_IF_MODE_COMMAND;
			dispif_info->isHwVsyncAvailable = 1;
			/* DISPMSG("DISP Info: DBI, CMD Mode, HW Vsync enable\n"); */
			break;
		}
	case LCM_TYPE_DPI:
		{
			dispif_info->displayType = DISP_IF_TYPE_DPI;
			dispif_info->displayMode = DISP_IF_MODE_VIDEO;
			dispif_info->isHwVsyncAvailable = 1;
			/* DISPMSG("DISP Info: DPI, VDO Mode, HW Vsync enable\n"); */
			break;
		}
	case LCM_TYPE_DSI:
		{
			dispif_info->displayType = DISP_IF_TYPE_DSI0;
			if (lcm_param->dsi.mode == CMD_MODE) {
				dispif_info->displayMode = DISP_IF_MODE_COMMAND;
				dispif_info->isHwVsyncAvailable = 1;
				/* DISPMSG("DISP Info: DSI, CMD Mode, HW Vsync enable\n"); */
			} else {
				dispif_info->displayMode = DISP_IF_MODE_VIDEO;
				dispif_info->isHwVsyncAvailable = 1;
				/* DISPMSG("DISP Info: DSI, VDO Mode, HW Vsync enable\n"); */
			}

			break;
		}
	default:
		break;
	}


	dispif_info->displayFormat = DISP_IF_FORMAT_RGB888;

	dispif_info->displayWidth = primary_display_get_width();
	dispif_info->displayHeight = primary_display_get_height();

	dispif_info->physicalWidth = DISP_GetActiveWidth();
	dispif_info->physicalHeight = DISP_GetActiveHeight();

	dispif_info->vsyncFPS = pgc->lcm_fps;

	dispif_info->isConnected = 1;

	fps_ctx_get_fps(&primary_fps_ctx, &dispif_info->updateFPS, &dispif_info->is_updateFPS_stable);
	dispif_info->updateFPS *= 100;

	return 0;
}

int primary_display_get_pages(void)
{
	return 3;
}

int primary_display_is_video_mode(void)
{
	/* TODO: we should store the video/cmd mode in runtime, because ROME will support cmd/vdo dynamic switch */
	return disp_lcm_is_video_mode(pgc->plcm);
}

int primary_display_diagnose(void)
{
	int ret = 0;

	DISPMSG("==== %s ===>\n", __func__);
	dpmgr_check_status(pgc->dpmgr_handle);

	if (primary_display_is_decouple_mode()) {
		/* to prevent race condition with DC->DL switch */
		dpmgr_check_status_by_scenario(DDP_SCENARIO_PRIMARY_OVL_MEMOUT);
	}
	DISPMSG("==== %s ===<\n", __func__);
	return ret;
}

CMDQ_SWITCH primary_display_cmdq_enabled(void)
{
	return disp_helper_get_option(DISP_OPT_USE_CMDQ);
}

int primary_display_manual_lock(void)
{
	_primary_path_lock(__func__);

	return 0;
}

int primary_display_manual_unlock(void)
{
	_primary_path_unlock(__func__);

	return 0;
}

void primary_display_reset(void)
{
	dpmgr_path_reset(pgc->dpmgr_handle, CMDQ_DISABLE);
}

unsigned int primary_display_get_fps_nolock(void)
{
	return pgc->lcm_fps;
}

unsigned int primary_display_get_fps(void)
{
	unsigned int fps = 0;

	_primary_path_lock(__func__);
	fps = pgc->lcm_fps;
	_primary_path_unlock(__func__);

	return fps;
}

int primary_display_force_set_fps(unsigned int keep, unsigned int skip)
{
	int ret = 0;

	DISPMSG("force set fps to keep %d, skip %d\n", keep, skip);
	_primary_path_lock(__func__);

	pgc->force_fps_keep_count = keep;
	pgc->force_fps_skip_count = skip;

	g_keep = 0;
	g_skip = 0;
	_primary_path_unlock(__func__);

	return ret;
}

int primary_display_force_set_vsync_fps(unsigned int fps)
{
	int ret = 0;

	DISPMSG("force set fps to %d\n", fps);
	_primary_path_lock(__func__);

	if (fps == pgc->lcm_fps) {
		pgc->vsync_drop = 0;
		ret = 0;
	} else if (fps == 30) {
		pgc->vsync_drop = 1;
		ret = 0;
	} else {
		ret = -1;
	}

	_primary_path_unlock(__func__);

	return ret;
}

int primary_display_vsync_switch(int method)
{
	int ret = 0;

	if (method == 0) {
		pr_debug("Vsync map RDMA %d\n", method);
		dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
				       DDP_IRQ_RDMA0_DONE);
	} else if (method == 1) {
		pr_debug("Vsync map DSI TE %d\n", method);
		dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
				       DDP_IRQ_DSI0_EXT_TE);
	} else if (method == 2) {
		pr_debug("Vsync map DSI FRAME DONE %d\n", method);
		dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
				       DDP_IRQ_DSI0_FRAME_DONE);
	}

	return ret;
}

int _set_backlight_by_cmdq(unsigned int level)
{
	int ret = 0;
	cmdqRecHandle cmdq_handle_backlight = NULL;

	MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl, MMProfileFlagPulse, 1, 1);
	ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &cmdq_handle_backlight);
	DISPDBG("primary backlight, handle=%p\n", cmdq_handle_backlight);
	if (ret != 0) {
		DISPERR("fail to create primary cmdq handle for backlight\n");
		return -1;
	}

	if (primary_display_is_video_mode()) {
		MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl, MMProfileFlagPulse, 1, 2);
		cmdqRecReset(cmdq_handle_backlight);
		disp_lcm_set_backlight(pgc->plcm, cmdq_handle_backlight, level);
		_cmdq_flush_config_handle_mira(cmdq_handle_backlight, 1);
		DISPCHECK("[BL]_set_backlight_by_cmdq ret=%d\n", ret);
	} else {
		MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl, MMProfileFlagPulse, 1, 3);
		cmdqRecReset(cmdq_handle_backlight);
		cmdqRecWait(cmdq_handle_backlight, CMDQ_SYNC_TOKEN_CABC_EOF);
		_cmdq_handle_clear_dirty(cmdq_handle_backlight);
		_cmdq_insert_wait_frame_done_token_mira(cmdq_handle_backlight);
		disp_lcm_set_backlight(pgc->plcm, cmdq_handle_backlight, level);
		cmdqRecSetEventToken(cmdq_handle_backlight, CMDQ_SYNC_TOKEN_CONFIG_DIRTY);
		cmdqRecSetEventToken(cmdq_handle_backlight, CMDQ_SYNC_TOKEN_CABC_EOF);
		MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl, MMProfileFlagPulse, 1, 4);
		_cmdq_flush_config_handle_mira(cmdq_handle_backlight, 1);
		MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl, MMProfileFlagPulse, 1, 6);
		DISPCHECK("[BL]_set_backlight_by_cmdq ret=%d\n", ret);
	}
	cmdqRecDestroy(cmdq_handle_backlight);
	cmdq_handle_backlight = NULL;
	MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl, MMProfileFlagPulse, 1, 5);

	return ret;
}

int _set_backlight_by_cpu(unsigned int level)
{
	int ret = 0;

	if (disp_helper_get_stage() != DISP_HELPER_STAGE_NORMAL) {
		DISPMSG("%s skip due to stage %s\n", __func__, disp_helper_stage_spy());
		return 0;
	}

	MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl, MMProfileFlagPulse, 0, 1);
	if (primary_display_is_video_mode()) {
		disp_lcm_set_backlight(pgc->plcm, NULL, level);
	} else {
		DISPCHECK("[BL]display cmdq trigger loop stop[begin]\n");
		if (primary_display_cmdq_enabled())
			_cmdq_stop_trigger_loop();

		DISPCHECK("[BL]display cmdq trigger loop stop[end]\n");

		if (dpmgr_path_is_busy(pgc->dpmgr_handle)) {
			DISPCHECK("[BL]primary display path is busy\n");
			ret = dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE,
						     HZ * 1);
			DISPCHECK("[BL]wait frame done ret:%d\n", ret);
		}

		DISPCHECK("[BL]stop dpmgr path[begin]\n");
		dpmgr_path_stop(pgc->dpmgr_handle, CMDQ_DISABLE);
		DISPCHECK("[BL]stop dpmgr path[end]\n");
		if (dpmgr_path_is_busy(pgc->dpmgr_handle)) {
			DISPCHECK("[BL]primary display path is busy after stop\n");
			dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE,
						 HZ * 1);
			DISPCHECK("[BL]wait frame done ret:%d\n", ret);
		}
		DISPCHECK("[BL]reset display path[begin]\n");
		dpmgr_path_reset(pgc->dpmgr_handle, CMDQ_DISABLE);
		DISPCHECK("[BL]reset display path[end]\n");

		MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl, MMProfileFlagPulse, 0, 2);

		disp_lcm_set_backlight(pgc->plcm, NULL, level);

		MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl, MMProfileFlagPulse, 0, 3);

		DISPCHECK("[BL]start dpmgr path[begin]\n");
		dpmgr_path_start(pgc->dpmgr_handle, CMDQ_DISABLE);
		DISPCHECK("[BL]start dpmgr path[end]\n");

		if (primary_display_cmdq_enabled()) {
			DISPCHECK("[BL]start cmdq trigger loop[begin]\n");
			_cmdq_start_trigger_loop();
		}
		DISPCHECK("[BL]start cmdq trigger loop[end]\n");
	}
	MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl, MMProfileFlagPulse, 0, 7);
	return ret;
}

int primary_display_setbacklight(unsigned int level)
{
	int ret = 0;
	static unsigned int last_level;

	DISPFUNC();
	if (disp_helper_get_stage() != DISP_HELPER_STAGE_NORMAL) {
		DISPMSG("%s skip due to stage %s\n", __func__, disp_helper_stage_spy());
		return 0;
	}

	if (last_level == level)
		return 0;

	MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl, MMProfileFlagStart, 0, 0);

	_primary_path_switch_dst_lock();

	_primary_path_lock(__func__);
	if (pgc->state == DISP_SLEPT) {
		DISPERR("Sleep State set backlight invald\n");
	} else {
		primary_display_idlemgr_kick(__func__, 0);
		if (primary_display_cmdq_enabled()) {
			if (primary_display_is_video_mode()) {
				MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl,
					       MMProfileFlagPulse, 0, 7);
				disp_lcm_set_backlight(pgc->plcm, NULL, level);
			} else {
				_set_backlight_by_cmdq(level);
			}
			atomic_set(&delayed_trigger_kick, 1);
		} else {
			_set_backlight_by_cpu(level);
		}
		last_level = level;
	}
	_primary_path_unlock(__func__);

	_primary_path_switch_dst_unlock();

	MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl, MMProfileFlagEnd, 0, 0);
	return ret;
}

int _set_lcm_cmd_by_cmdq(unsigned int *lcm_cmd, unsigned int *lcm_count, unsigned int *lcm_value)
{
	int ret = 0;
	cmdqRecHandle cmdq_handle_lcm_cmd = NULL;

	MMProfileLogEx(ddp_mmp_get_events()->primary_set_cmd, MMProfileFlagPulse, 1, 1);
	ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &cmdq_handle_lcm_cmd);
	DISPDBG("primary set lcm cmd, handle=%p\n", cmdq_handle_lcm_cmd);
	if (ret != 0) {
		DISPERR("fail to create primary cmdq handle for setlcmcmd\n");
		return -1;
	}

	if (primary_display_is_video_mode()) {
		MMProfileLogEx(ddp_mmp_get_events()->primary_set_cmd, MMProfileFlagPulse, 1, 2);
		cmdqRecReset(cmdq_handle_lcm_cmd);
		disp_lcm_set_lcm_cmd(pgc->plcm, cmdq_handle_lcm_cmd, lcm_cmd, lcm_count, lcm_value);
		_cmdq_flush_config_handle_mira(cmdq_handle_lcm_cmd, 1);
		DISPCHECK("[CMD]_set_lcm_cmd_by_cmdq ret=%d\n", ret);
	} else {
		MMProfileLogEx(ddp_mmp_get_events()->primary_set_bl, MMProfileFlagPulse, 1, 3);
		cmdqRecReset(cmdq_handle_lcm_cmd);
		_cmdq_handle_clear_dirty(cmdq_handle_lcm_cmd);
		_cmdq_insert_wait_frame_done_token_mira(cmdq_handle_lcm_cmd);

		disp_lcm_set_lcm_cmd(pgc->plcm, cmdq_handle_lcm_cmd, lcm_cmd, lcm_count, lcm_value);
		cmdqRecSetEventToken(cmdq_handle_lcm_cmd, CMDQ_SYNC_TOKEN_CONFIG_DIRTY);
		MMProfileLogEx(ddp_mmp_get_events()->primary_set_cmd, MMProfileFlagPulse, 1, 4);
		_cmdq_flush_config_handle_mira(cmdq_handle_lcm_cmd, 1);
		MMProfileLogEx(ddp_mmp_get_events()->primary_set_cmd, MMProfileFlagPulse, 1, 6);
		DISPCHECK("[CMD]_set_lcm_cmd_by_cmdq ret=%d\n", ret);
	}
	cmdqRecDestroy(cmdq_handle_lcm_cmd);
	cmdq_handle_lcm_cmd = NULL;
	MMProfileLogEx(ddp_mmp_get_events()->primary_set_cmd, MMProfileFlagPulse, 1, 5);
	return ret;

}

int primary_display_setlcm_cmd(unsigned int *lcm_cmd, unsigned int *lcm_count,
			       unsigned int *lcm_value)
{
	int ret = 0;

	DISPFUNC();
	if (disp_helper_get_stage() != DISP_HELPER_STAGE_NORMAL) {
		DISPMSG("%s skip due to stage %s\n", __func__, disp_helper_stage_spy());
		return 0;
	}

	MMProfileLogEx(ddp_mmp_get_events()->primary_set_cmd, MMProfileFlagStart, 0, 0);

	_primary_path_switch_dst_lock();

	_primary_path_lock(__func__);
	if (pgc->state == DISP_SLEPT) {
		DISPCHECK("Sleep State set backlight invald\n");
	} else {
		if (primary_display_cmdq_enabled()) {
			if (primary_display_is_video_mode()) {
				MMProfileLogEx(ddp_mmp_get_events()->primary_set_cmd,
					       MMProfileFlagPulse, 0, 7);
				_set_lcm_cmd_by_cmdq(lcm_cmd, lcm_count, lcm_value);
			} else {
				_set_lcm_cmd_by_cmdq(lcm_cmd, lcm_count, lcm_value);
			}
		} else {
			/* cpu */
		}
	}
	_primary_path_unlock(__func__);

	_primary_path_switch_dst_lock();

	MMProfileLogEx(ddp_mmp_get_events()->primary_set_cmd, MMProfileFlagEnd, 0, 0);

	return ret;
}

int primary_display_mipi_clk_change(unsigned int clk_value)
{
	cmdqRecHandle cmdq_handle = NULL;

	if (pgc->state == DISP_SLEPT) {
		DISPCHECK("Sleep State clk change invald\n");
		return 0;
	}

	_primary_path_lock(__func__);

	if (!primary_display_is_video_mode()) {
		DISPCHECK("clk change CMD Mode return\n");
		return 0;
	}

	cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &cmdq_handle);
	cmdqRecReset(cmdq_handle);

	_cmdq_insert_wait_frame_done_token_mira(cmdq_handle);
	pgc->plcm->params->dsi.PLL_CLOCK = clk_value;

	dpmgr_path_build_cmdq(pgc->dpmgr_handle,
			cmdq_handle, CMDQ_STOP_VDO_MODE, 0);

	dpmgr_path_ioctl(primary_get_dpmgr_handle(), cmdq_handle,
		DDP_PHY_CLK_CHANGE,
		&clk_value);

	dpmgr_path_build_cmdq(pgc->dpmgr_handle,
		cmdq_handle, CMDQ_START_VDO_MODE, 0);

	cmdqRecClearEventToken(cmdq_handle, CMDQ_EVENT_MUTEX0_STREAM_EOF);
	cmdqRecClearEventToken(cmdq_handle, CMDQ_EVENT_DISP_RDMA0_EOF);

	dpmgr_path_trigger(pgc->dpmgr_handle, cmdq_handle, CMDQ_ENABLE);
	ddp_mutex_set_sof_wait(dpmgr_path_get_mutex(pgc->dpmgr_handle), pgc->cmdq_handle_config_esd, 0);
	_cmdq_flush_config_handle_mira(cmdq_handle, 1);

	cmdqRecDestroy(cmdq_handle);
	cmdq_handle = NULL;

	DISPCHECK("primary_display_mipi_clk_change return\n");

	_primary_path_unlock(__func__);

	return 0;
}

/***********************/
/*****Legacy DISP API*****/
/***********************/
UINT32 DISP_GetScreenWidth(void)
{
	return primary_display_get_width();
}

UINT32 DISP_GetScreenHeight(void)
{
	return primary_display_get_height();
}

UINT32 DISP_GetActiveHeight(void)
{
	if (pgc->plcm == NULL) {
		DISPERR("lcm handle is null\n");
		return 0;
	}

	if (pgc->plcm->params)
		return pgc->plcm->params->physical_height;

	DISPERR("lcm_params is null!\n");
	return 0;
}

UINT32 DISP_GetActiveWidth(void)
{
	if (pgc->plcm == NULL) {
		DISPERR("lcm handle is null\n");
		return 0;
	}

	if (pgc->plcm->params)
		return pgc->plcm->params->physical_width;

	DISPERR("lcm_params is null!\n");
	return 0;
}

LCM_PARAMS *DISP_GetLcmPara(void)
{
	if (pgc->plcm == NULL) {
		DISPERR("lcm handle is null\n");
		return NULL;
	}

	if (pgc->plcm->params)
		return pgc->plcm->params;
	else
		return NULL;
}

LCM_DRIVER *DISP_GetLcmDrv(void)
{
	if (pgc->plcm == NULL) {
		DISPERR("lcm handle is null\n");
		return NULL;
	}

	if (pgc->plcm->drv)
		return pgc->plcm->drv;
	else
		return NULL;
}

static int _screen_cap_by_cmdq(unsigned int mva, enum UNIFIED_COLOR_FMT ufmt, DISP_MODULE_ENUM after_eng)
{
	int ret = 0;
	cmdqRecHandle cmdq_handle = NULL;
	cmdqRecHandle cmdq_wait_handle = NULL;
	disp_ddp_path_config *pconfig = NULL;
	unsigned int w_xres = primary_display_get_width();
	unsigned int h_yres = primary_display_get_height();

	/* create config thread */
	ret = cmdqRecCreate(CMDQ_SCENARIO_PRIMARY_DISP, &cmdq_handle);
	if (ret != 0) {
		DISPCHECK("primary capture:Fail to create primary cmdq handle for capture\n");
		ret = -1;
		goto out;
	}
	cmdqRecReset(cmdq_handle);

	/* create wait thread */
	ret = cmdqRecCreate(CMDQ_SCENARIO_DISP_SCREEN_CAPTURE, &cmdq_wait_handle);
	if (ret != 0) {
		DISPCHECK
		    ("primary capture:Fail to create primary cmdq wait handle for capture\n");
		ret = -1;
		goto out;
	}
	cmdqRecReset(cmdq_wait_handle);

	dpmgr_path_memout_clock(pgc->dpmgr_handle, 1);

	_cmdq_handle_clear_dirty(cmdq_handle);
	_cmdq_insert_wait_frame_done_token_mira(cmdq_handle);

	_primary_path_lock(__func__);

	primary_display_idlemgr_kick(__func__, 0);
	dpmgr_path_add_memout(pgc->dpmgr_handle, after_eng, cmdq_handle);

	pconfig = dpmgr_path_get_last_config(pgc->dpmgr_handle);
	pconfig->wdma_dirty = 1;
	pconfig->ovl_dirty = 1;
	pconfig->dst_dirty = 1;
	pconfig->rdma_dirty = 1;
	pconfig->wdma_config.dstAddress = mva;
	pconfig->wdma_config.srcHeight = h_yres;
	pconfig->wdma_config.srcWidth = w_xres;
	pconfig->wdma_config.clipX = 0;
	pconfig->wdma_config.clipY = 0;
	pconfig->wdma_config.clipHeight = h_yres;
	pconfig->wdma_config.clipWidth = w_xres;
	pconfig->wdma_config.outputFormat = ufmt;
	pconfig->wdma_config.useSpecifiedAlpha = 1;
	pconfig->wdma_config.alpha = 0xFF;
	pconfig->wdma_config.dstPitch = w_xres * UFMT_GET_bpp(ufmt) / 8;
	ret = dpmgr_path_config(pgc->dpmgr_handle, pconfig, cmdq_handle);
	pconfig->wdma_dirty = 0;

	_cmdq_set_config_handle_dirty_mira(cmdq_handle);
	_cmdq_flush_config_handle_mira(cmdq_handle, 0);
	DISPMSG("primary capture:Flush add memout mva(0x%x)\n", mva);
	/* wait wdma0 sof */
	cmdqRecWait(cmdq_wait_handle, CMDQ_EVENT_DISP_WDMA0_SOF);
	cmdqRecFlush(cmdq_wait_handle);
	DISPMSG("primary capture:Flush wait wdma sof\n");
	cmdqRecReset(cmdq_handle);
	_cmdq_handle_clear_dirty(cmdq_handle);
	_cmdq_insert_wait_frame_done_token_mira(cmdq_handle);

	dpmgr_path_remove_memout(pgc->dpmgr_handle, cmdq_handle);

	cmdqRecClearEventToken(cmdq_handle, CMDQ_EVENT_DISP_WDMA0_SOF);
	_cmdq_set_config_handle_dirty_mira(cmdq_handle);
	/* flush remove memory to cmdq */
	_cmdq_flush_config_handle_mira(cmdq_handle, 1);
	DISPMSG("primary capture: Flush remove memout\n");

	dpmgr_path_memout_clock(pgc->dpmgr_handle, 0);
	_primary_path_unlock(__func__);

out:
	cmdqRecDestroy(cmdq_handle);
	cmdqRecDestroy(cmdq_wait_handle);
	return 0;
}

static int _screen_cap_by_cpu(unsigned int mva, enum UNIFIED_COLOR_FMT ufmt, DISP_MODULE_ENUM after_eng)
{
	int ret = 0;
	disp_ddp_path_config *pconfig = NULL;
	unsigned int w_xres = primary_display_get_width();
	unsigned int h_yres = primary_display_get_height();

	dpmgr_path_memout_clock(pgc->dpmgr_handle, 1);

	if (_should_wait_path_idle()) {
		ret = dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ * 1);
		if (ret <= 0)
			primary_display_diagnose();
	}

	_primary_path_lock(__func__);
	primary_display_idlemgr_kick(__func__, 1);

	dpmgr_path_add_memout(pgc->dpmgr_handle, after_eng, NULL);

	pconfig = dpmgr_path_get_last_config(pgc->dpmgr_handle);
	pconfig->wdma_dirty = 1;
	pconfig->wdma_config.dstAddress = mva;
	pconfig->wdma_config.srcHeight = h_yres;
	pconfig->wdma_config.srcWidth = w_xres;
	pconfig->wdma_config.clipX = 0;
	pconfig->wdma_config.clipY = 0;
	pconfig->wdma_config.clipHeight = h_yres;
	pconfig->wdma_config.clipWidth = w_xres;
	pconfig->wdma_config.outputFormat = ufmt;
	pconfig->wdma_config.useSpecifiedAlpha = 1;
	pconfig->wdma_config.alpha = 0xFF;
	pconfig->wdma_config.dstPitch = w_xres * UFMT_GET_bpp(ufmt) / 8;
	ret = dpmgr_path_config(pgc->dpmgr_handle, pconfig, NULL);
	pconfig->wdma_dirty = 0;

	_trigger_display_interface(1, NULL, 0);
	msleep(20);
	if (_should_wait_path_idle()) {
		ret = dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ * 1);
		if (ret <= 0)
			primary_display_diagnose();
	}

	dpmgr_path_remove_memout(pgc->dpmgr_handle, NULL);

	dpmgr_path_memout_clock(pgc->dpmgr_handle, 0);
	_primary_path_unlock(__func__);
	return 0;
}

int primary_display_capture_framebuffer_ovl(unsigned long pbuf, enum UNIFIED_COLOR_FMT ufmt)
{
	int ret = 0;
	m4u_client_t *m4uClient = NULL;
	unsigned int mva = 0;
	unsigned int w_xres = primary_display_get_width();
	unsigned int h_yres = primary_display_get_height();
	unsigned int pixel_byte = primary_display_get_bpp() / 8;
	int buffer_size = h_yres * w_xres * pixel_byte;
	DISP_MODULE_ENUM after_eng = DISP_MODULE_OVL0;
	int tmp;

	DISPMSG("primary capture: begin\n");

	disp_sw_mutex_lock(&(pgc->capture_lock));

	if (primary_display_is_sleepd()) {
		memset((void *)pbuf, 0, buffer_size);
		DISPMSG("primary capture: Fail black End\n");
		goto out;
	}

	m4uClient = m4u_create_client();
	if (m4uClient == NULL) {
		DISPERR("primary capture:Fail to alloc  m4uClient\n");
		ret = -1;
		goto out;
	}

	ret = m4u_alloc_mva(m4uClient, M4U_PORT_DISP_WDMA0, pbuf, NULL, buffer_size,
			  M4U_PROT_READ | M4U_PROT_WRITE, 0, &mva);
	if (ret != 0) {
		DISPERR("primary capture:Fail to allocate mva\n");
		ret = -1;
		goto out;
	}

	ret = m4u_cache_sync(m4uClient, M4U_PORT_DISP_WDMA0, pbuf, buffer_size, mva,
			   M4U_CACHE_FLUSH_ALL);
	if (ret != 0) {
		DISPERR("primary capture:Fail to cach sync\n");
		ret = -1;
		goto out;
	}
	tmp = disp_helper_get_option(DISP_OPT_SCREEN_CAP_FROM_DITHER);
	if (tmp == 0)
		after_eng = DISP_MODULE_OVL0;
	else if (tmp == 1)
		after_eng = DISP_MODULE_DITHER0;
	else if (tmp == 2)
		after_eng = DISP_MODULE_UFOE;

	if (primary_display_cmdq_enabled())
		_screen_cap_by_cmdq(mva, ufmt, after_eng);
	else
		_screen_cap_by_cpu(mva, ufmt, after_eng);

	ret = m4u_cache_sync(m4uClient, M4U_PORT_DISP_WDMA0, pbuf, buffer_size, mva,
			   M4U_CACHE_INVALID_BY_RANGE);
out:
	if (mva > 0)
		m4u_dealloc_mva(m4uClient, M4U_PORT_DISP_WDMA0, mva);

	if (m4uClient != 0)
		m4u_destroy_client(m4uClient);

	disp_sw_mutex_unlock(&(pgc->capture_lock));
	DISPMSG("primary capture: end\n");
	return ret;
}

int primary_display_capture_framebuffer(unsigned long pbuf)
{
	unsigned int fb_layer_id = primary_display_get_option("FB_LAYER");
	unsigned int w_xres = primary_display_get_width();
	unsigned int h_yres = primary_display_get_height();
	unsigned int pixel_bpp = primary_display_get_bpp() / 8;	/* bpp is either 32 or 16, can not be other value */
	unsigned int w_fb = ALIGN_TO(w_xres, MTK_FB_ALIGNMENT);
	unsigned int fbsize = w_fb * h_yres * pixel_bpp;	/* frame buffer size */
	unsigned long fbaddress =
	    dpmgr_path_get_last_config(pgc->dpmgr_handle)->ovl_config[fb_layer_id].addr;
	void *fbv = 0;
	unsigned int i;
	unsigned long ttt;

	DISPMSG("w_res=%d, h_yres=%d, pixel_bpp=%d, w_fb=%d, fbsize=%d, fbaddress=0x%lx\n", w_xres,
		h_yres, pixel_bpp, w_fb, fbsize, fbaddress);
	fbv = ioremap(fbaddress, fbsize);
	DISPMSG("w_xres = %d, h_yres = %d, w_fb = %d, pixel_bpp = %d, fbsize = %d, fbaddress = 0x%08lx\n",
		w_xres, h_yres, w_fb, pixel_bpp, fbsize, fbaddress);
	if (!fbv) {
		DISPMSG
		    ("[FB Driver], Unable to allocate memory for frame buffer: address=0x%lx, size=0x%08x\n",
		     fbaddress, fbsize);
		return -1;
	}

	ttt = get_current_time_us();
	for (i = 0; i < h_yres; i++) {
		memcpy((void *)(pbuf + i * w_xres * pixel_bpp),
		       (void *)(fbv + i * w_fb * pixel_bpp), w_xres * pixel_bpp);
	}
	DISPMSG("capture framebuffer cost %ld us\n", get_current_time_us() - ttt);
	iounmap((void *)fbv);
	return -1;
}

static UINT32 disp_fb_bpp = 32;
static UINT32 disp_fb_pages = 3;

UINT32 DISP_GetScreenBpp(void)
{
	return disp_fb_bpp;
}

UINT32 DISP_GetPages(void)
{
	return disp_fb_pages;
}

UINT32 DISP_GetFBRamSize(void)
{
	return ALIGN_TO(DISP_GetScreenWidth(), MTK_FB_ALIGNMENT) *
	    ALIGN_TO(DISP_GetScreenHeight(), MTK_FB_ALIGNMENT) *
	    ((DISP_GetScreenBpp() + 7) >> 3) * DISP_GetPages();
}

UINT32 DISP_GetVRamSize(void)
{
#if 0
	/* Use a local static variable to cache the calculated vram size */
	/*  */
	static UINT32 vramSize;

	if (0 == vramSize) {
		disp_drv_init_context();

		/* /get framebuffer size */
		vramSize = DISP_GetFBRamSize();

		/* /get DXI working buffer size */
		vramSize += disp_if_drv->get_working_buffer_size();

		/* get assertion layer buffer size */
		vramSize += DAL_GetLayerSize();

		/* Align vramSize to 1MB */
		/*  */
		vramSize = ALIGN_TO_POW_OF_2(vramSize, 0x100000);

		DISP_LOG("DISP_GetVRamSize: %u bytes\n", vramSize);
	}

	return vramSize;
#endif

	return 0;
}

UINT32 DISP_GetVRamSizeBoot(char *cmdline)
{
	unsigned int vramsize;

	vramsize = mtkfb_get_fb_size();
	BUG_ON(!vramsize);
	DISPCHECK("[DT]display vram size = 0x%08x|%d\n", vramsize, vramsize);
	return vramsize;
}

int disp_hal_allocate_framebuffer(phys_addr_t pa_start, phys_addr_t pa_end, unsigned long *va,
				  unsigned long *mva)
{
	int ret = 0;

	*va = (unsigned long)ioremap_nocache(pa_start, pa_end - pa_start + 1);
	pr_debug("disphal_allocate_fb, pa_start=0x%pa, pa_end=0x%pa, va=0x%lx\n", &pa_start,
		 &pa_end, *va);

	if (disp_helper_get_option(DISP_OPT_USE_M4U)) {
		m4u_client_t *client;

		struct sg_table *sg_table = &table;

		sg_alloc_table(sg_table, 1, GFP_KERNEL);

		sg_dma_address(sg_table->sgl) = pa_start;
		sg_dma_len(sg_table->sgl) = (pa_end - pa_start + 1);
		client = m4u_create_client();
		if (IS_ERR_OR_NULL(client))
			DISPERR("create client fail!\n");


		*mva = pa_start & 0xffffffffULL;
		ret = m4u_alloc_mva(client, M4U_PORT_DISP_OVL0, 0, sg_table, (pa_end - pa_start + 1),
				    M4U_PROT_READ | M4U_PROT_WRITE, M4U_FLAGS_FIX_MVA, (unsigned int *)mva);
		/* m4u_alloc_mva(M4U_PORT_DISP_OVL0, pa_start, (pa_end - pa_start + 1), 0, 0, mva); */
		if (ret)
			DISPERR("m4u_alloc_mva returns fail: %d\n", ret);

		pr_debug("[DISPHAL] FB MVA is 0x%lx PA is 0x%pa\n", *mva, &pa_start);

	} else {
		*mva = pa_start & 0xffffffffULL;
	}

	return 0;
}


unsigned int primary_display_get_option(const char *option)
{
	if (!strcmp(option, "FB_LAYER"))
		return 0;
	if (!strcmp(option, "ASSERT_LAYER"))
		return PRIMARY_SESSION_INPUT_LAYER_COUNT - 1;
	if (!strcmp(option, "M4U_ENABLE"))
		return disp_helper_get_option(DISP_OPT_USE_M4U);
	ASSERT(0);
}

int primary_display_lcm_ATA(void)
{
	DISP_STATUS ret = DISP_STATUS_OK;

	DISPFUNC();
	primary_display_esd_check_enable(0);
	_primary_path_lock(__func__);
	disp_irq_esd_cust_bycmdq(0);
	if (pgc->state == 0) {
		DISPCHECK("ATA_LCM, primary display path is already sleep, skip\n");
		goto done;
	}

	DISPCHECK("[ATA_LCM]primary display path stop[begin]\n");
	if (primary_display_is_video_mode())
		dpmgr_path_ioctl(pgc->dpmgr_handle, NULL, DDP_STOP_VIDEO_MODE, NULL);

	DISPCHECK("[ATA_LCM]primary display path stop[end]\n");
	ret = disp_lcm_ATA(pgc->plcm);
	dpmgr_path_start(pgc->dpmgr_handle, CMDQ_DISABLE);
	if (primary_display_is_video_mode()) {
		/* for video mode, we need to force trigger here */
		/* for cmd mode, just set DPREC_EVENT_CMDQ_SET_EVENT_ALLOW when trigger loop start */
		dpmgr_path_trigger(pgc->dpmgr_handle, NULL, CMDQ_DISABLE);
	}
done:
	disp_irq_esd_cust_bycmdq(1);
	_primary_path_unlock(__func__);
	primary_display_esd_check_enable(1);
	return ret;
}

static int Panel_Master_primary_display_config_dsi(const char *name, UINT32 config_value)
{
	int ret = 0;
	disp_ddp_path_config *data_config;

	/* all dirty should be cleared in dpmgr_path_get_last_config() */
	data_config = dpmgr_path_get_last_config(pgc->dpmgr_handle);
	/* modify below for config dsi */
	if (!strcmp(name, "PM_CLK")) {
		pr_debug("Pmaster_config_dsi: PM_CLK:%d\n", config_value);
		data_config->dispif_config.dsi.PLL_CLOCK = config_value;
	} else if (!strcmp(name, "PM_SSC")) {
		data_config->dispif_config.dsi.ssc_range = config_value;
	}
	pr_debug("Pmaster_config_dsi: will Run path_config()\n");
	ret = dpmgr_path_config(pgc->dpmgr_handle, data_config, NULL);

	return ret;
}

int fbconfig_get_esd_check_test(UINT32 dsi_id, UINT32 cmd, UINT8 *buffer, UINT32 num)
{
	int ret = 0;

	_primary_path_lock(__func__);
	if (pgc->state == DISP_SLEPT) {
		DISPCHECK("[ESD]primary display path is slept?? -- skip esd check\n");
		_primary_path_unlock(__func__);
		goto done;
	}
	primary_display_esd_check_enable(0);
	disp_irq_esd_cust_bycmdq(0);
	/* / 1: stop path */
	_cmdq_stop_trigger_loop();
	if (dpmgr_path_is_busy(pgc->dpmgr_handle))
		DISPCHECK("[ESD]wait frame done ret:%d\n", ret);

	dpmgr_path_stop(pgc->dpmgr_handle, CMDQ_DISABLE);
	DISPCHECK("[ESD]stop dpmgr path[end]\n");
	if (dpmgr_path_is_busy(pgc->dpmgr_handle))
		DISPCHECK("[ESD]wait frame done ret:%d\n", ret);

	dpmgr_path_reset(pgc->dpmgr_handle, CMDQ_DISABLE);
	ret = fbconfig_get_esd_check(dsi_id, cmd, buffer, num);
	dpmgr_path_start(pgc->dpmgr_handle, CMDQ_DISABLE);
	DISPCHECK("[ESD]start dpmgr path[end]\n");
	if (primary_display_is_video_mode()) {
		/* for video mode, we need to force trigger here */
		/* for cmd mode, just set DPREC_EVENT_CMDQ_SET_EVENT_ALLOW when trigger loop start */
		dpmgr_path_trigger(pgc->dpmgr_handle, NULL, CMDQ_DISABLE);
	}
	_cmdq_start_trigger_loop();
	/* when we stop trigger loop
	 * if no other thread is running, cmdq may disable its clock
	 * all cmdq event will be cleared after suspend */
	cmdqCoreSetEvent(CMDQ_EVENT_DISP_WDMA0_EOF);
	DISPCHECK("[ESD]start cmdq trigger loop[end]\n");
	disp_irq_esd_cust_bycmdq(1);
	primary_display_esd_check_enable(1);
	_primary_path_unlock(__func__);

done:
	return ret;
}

int Panel_Master_dsi_config_entry(const char *name, void *config_value)
{
	int ret = 0;
	int force_trigger_path = 0;
	UINT32 *config_dsi = (UINT32 *)config_value;
	LCM_PARAMS *lcm_param = NULL;
	LCM_DRIVER *pLcm_drv = DISP_GetLcmDrv();

	DISPFUNC();
	if (!strcmp(name, "DRIVER_IC_RESET") || !strcmp(name, "PM_DDIC_CONFIG")) {
		primary_display_esd_check_enable(0);
		msleep(2500);
	}
	_primary_path_lock(__func__);

	lcm_param = disp_lcm_get_params(pgc->plcm);
	if (pgc->state == DISP_SLEPT) {
		DISPERR("[Pmaster]Panel_Master: primary display path is slept??\n");
		goto done;
	}
	/* Esd Check : Read from lcm
	 * the following code is to
	 * 0: lock path
	 * 1: stop path
	 * 2: do esd check (!!!)
	 * 3: start path
	 * 4: unlock path
	 * 1: stop path
	 */
	_cmdq_stop_trigger_loop();

	if (dpmgr_path_is_busy(pgc->dpmgr_handle)) {
		int event_ret = dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ * 1);

		DISPCHECK("[ESD]wait frame done ret:%d\n", ret);
		if (event_ret <= 0) {
			DISPERR("wait frame done in suspend timeout\n");
			primary_display_diagnose();
			ret = -1;
		}
	}
	dpmgr_path_stop(pgc->dpmgr_handle, CMDQ_DISABLE);
	DISPCHECK("[ESD]stop dpmgr path[end]\n");

	if (dpmgr_path_is_busy(pgc->dpmgr_handle))
		DISPCHECK("[ESD]wait frame done ret:%d\n", ret);

	dpmgr_path_reset(pgc->dpmgr_handle, CMDQ_DISABLE);
	if ((!strcmp(name, "PM_CLK")) || (!strcmp(name, "PM_SSC")))
		Panel_Master_primary_display_config_dsi(name, *config_dsi);
	else if (!strcmp(name, "PM_DDIC_CONFIG")) {
		Panel_Master_DDIC_config();
		force_trigger_path = 1;
	} else if (!strcmp(name, "DRIVER_IC_RESET")) {
		if (pLcm_drv && pLcm_drv->init_power)
			pLcm_drv->init_power();
		if (pLcm_drv)
			pLcm_drv->init();
		else
			ret = -1;
		force_trigger_path = 1;
	}
	dpmgr_path_start(pgc->dpmgr_handle, CMDQ_DISABLE);
	if (primary_display_is_video_mode()) {
		/* for video mode, we need to force trigger here */
		/* for cmd mode, just set DPREC_EVENT_CMDQ_SET_EVENT_ALLOW when trigger loop start */
		dpmgr_path_trigger(pgc->dpmgr_handle, NULL, CMDQ_DISABLE);
		force_trigger_path = 0;
	}
	_cmdq_start_trigger_loop();

	/* when we stop trigger loop
	 * if no other thread is running, cmdq may disable its clock
	 * all cmdq event will be cleared after suspend */
	cmdqCoreSetEvent(CMDQ_EVENT_DISP_WDMA0_EOF);

	DISPCHECK("[Pmaster]start cmdq trigger loop\n");
done:
	_primary_path_unlock(__func__);

	if (force_trigger_path) {
		primary_display_trigger(0, NULL, 0);
		DISPCHECK("[Pmaster]force trigger display path\r\n");
	}

	return ret;
}

int primary_display_switch_dst_mode(int mode)
{
	DISP_STATUS ret = DISP_STATUS_ERROR;
	disp_path_handle disp_handle = NULL;
	disp_ddp_path_config *pconfig = NULL;
	void *lcm_cmd = NULL;
	int temp_mode = 0;

	if (!disp_helper_get_option(DISP_OPT_CV_BYSUSPEND))
		return 0;

	DISPFUNC();
	_primary_path_switch_dst_lock();
	disp_sw_mutex_lock(&(pgc->capture_lock));
	_primary_path_lock(__func__);
	MMProfileLogEx(ddp_mmp_get_events()->primary_display_switch_dst_mode,
		MMProfileFlagStart, primary_display_cur_dst_mode, mode);
	DISPMSG("C2V]cur_mode:%d, dst_mode:%d\n", primary_display_cur_dst_mode, mode);

	if (pgc->plcm->params->type != LCM_TYPE_DSI) {
		MMProfileLogEx(ddp_mmp_get_events()->primary_display_switch_dst_mode,
				MMProfileFlagPulse, 5, pgc->plcm->params->type);
		DISPERR("[C2V]dst mode switch only support DSI IF\n");
		goto done;
	}
	if (pgc->state == DISP_SLEPT) {
		MMProfileLogEx(ddp_mmp_get_events()->primary_display_switch_dst_mode,
			MMProfileFlagPulse, 6, pgc->state);
		DISPCHECK
		    ("[primary_display_switch_dst_mode], primary display path is already sleep, skip\n");
		goto done;
	}

	if (pgc->lcm_refresh_rate != 120) {
		DISPCHECK("Only support 120HZ CV switch. But lcm_refresh_rate is:%d\n",
			pgc->lcm_refresh_rate);
		goto done;
	}

	if (mode == primary_display_cur_dst_mode) {
		MMProfileLogEx(ddp_mmp_get_events()->primary_display_switch_dst_mode,
			MMProfileFlagPulse, 7, mode);
		DISPCHECK
		    ("[primary_display_switch_dst_mode]not need switch,cur_mode:%d, switch_mode:%d\n",
		     primary_display_cur_dst_mode, mode);
		goto done;
	}

	/* get c2v switch lcm cmd */
	lcm_cmd = disp_lcm_switch_mode(pgc->plcm, mode);
	if (lcm_cmd == NULL) {
		MMProfileLogEx(ddp_mmp_get_events()->primary_display_switch_dst_mode,
			MMProfileFlagPulse, 8, mode);
		DISPCHECK
		    ("[primary_display_switch_dst_mode]get lcm cmd fail primary_display_cur_dst_mode=%d mode=%d\n",
		     primary_display_cur_dst_mode, mode);
		goto done;
	}

	/* When switch to vdo mode, go back to DL mode if display path change to DC mode by SMART OVL */
	if (disp_helper_get_option(DISP_OPT_SMART_OVL) && !primary_display_is_video_mode()) {
		/* switch to the mode before idle */
		do_primary_display_switch_mode(DISP_SESSION_DIRECT_LINK_MODE,
			primary_get_sess_id(), 0, NULL, 0);

		set_is_dc(0);
	}

#ifndef CONFIG_FPGA_EARLY_PORTING /* just to fix build error, please remove me. */
	/* set power down mode forbidden */
	if (disp_helper_get_option(DISP_OPT_SODI_SUPPORT))
		spm_sodi_mempll_pwr_mode(1);
#endif
	MMProfileLogEx(ddp_mmp_get_events()->primary_display_switch_dst_mode,
		MMProfileFlagPulse, 4, 0);
	_cmdq_reset_config_handle();

	/* 1. modify lcm mode - sw */
	MMProfileLogEx(ddp_mmp_get_events()->primary_display_switch_dst_mode,
		MMProfileFlagPulse, 4, 1);
	temp_mode = (int)(pgc->plcm->params->dsi.mode);
	pgc->plcm->params->dsi.mode = pgc->plcm->params->dsi.switch_mode;
	pgc->plcm->params->dsi.switch_mode = temp_mode;
	dpmgr_path_set_video_mode(pgc->dpmgr_handle, primary_display_is_video_mode());

	/* 2.Change PLL CLOCK parameter and build fps lcm command */
	disp_lcm_adjust_fps(pgc->cmdq_handle_config, pgc->plcm, pgc->lcm_refresh_rate);
	disp_handle = pgc->dpmgr_handle;
	pconfig = dpmgr_path_get_last_config(disp_handle);
	pconfig->dispif_config.dsi.PLL_CLOCK = pgc->plcm->params->dsi.PLL_CLOCK;

	/* 3.Change PLL LCM clock parameter */
	dpmgr_path_ioctl(pgc->dpmgr_handle, pgc->cmdq_handle_config, DDP_UPDATE_PLL_CLK_ONLY,
		&pgc->plcm->params->dsi.PLL_CLOCK);

	/* 4.Switch mode and change DSI clock */
	if (0 != dpmgr_path_ioctl(pgc->dpmgr_handle, pgc->cmdq_handle_config,
		DDP_SWITCH_DSI_MODE, lcm_cmd)) {
		MMProfileLogEx(ddp_mmp_get_events()->primary_display_switch_dst_mode,
			MMProfileFlagPulse, 9, 0);
		/* DISPERR("[C2V]switch dsi mode fail, return directly\n"); */
		ret = -1;
	}

	/* 5. rebuild trigger loop */
	MMProfileLogEx(ddp_mmp_get_events()->primary_display_switch_dst_mode,
		MMProfileFlagPulse, 4, 2);
	_cmdq_build_trigger_loop();
	_cmdq_stop_trigger_loop();
	_cmdq_start_trigger_loop();
	_cmdq_reset_config_handle();
	_cmdq_insert_wait_frame_done_token_mira(pgc->cmdq_handle_config);

	MMProfileLogEx(ddp_mmp_get_events()->primary_display_switch_dst_mode,
		MMProfileFlagPulse, 4, 3);
	primary_display_cur_dst_mode = mode;
	DISPMSG("primary_display_cur_dst_mode %d\n", primary_display_cur_dst_mode);
	if (primary_display_is_video_mode())
		dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
		   DDP_IRQ_RDMA0_DONE);
	else
		dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC,
			DDP_IRQ_DSI0_EXT_TE);

	ret = DISP_STATUS_OK;
done:
	MMProfileLogEx(ddp_mmp_get_events()->primary_display_switch_dst_mode,
		MMProfileFlagEnd, primary_display_cur_dst_mode, mode);
	_primary_path_unlock(__func__);
	disp_sw_mutex_unlock(&(pgc->capture_lock));
	_primary_path_switch_dst_unlock();
	return ret;
}


/* extern void DSI_ForceConfig(int forceconfig);	*/
/* extern int DSI_set_roi(int x,int y);			*/
/* extern int DSI_check_roi(void);			*/

static int width_array[] = {2560, 1440, 1920, 1280, 1200, 800, 960, 640};
static int heigh_array[] = {1440, 2560, 1200, 800, 1920, 1280, 640, 960};
static int array_id[] = {6,   2,   7,   4,   3,    0,   5,   1};
LCM_PARAMS *lcm_param2 = NULL;
disp_ddp_path_config data_config2;

int primary_display_te_test(void)
{
	int ret = 0;
	int try_cnt = 3;
	int time_interval = 0;
	int time_interval_max = 0;
	long long time_te = 0;
	long long time_framedone = 0;

	DISPMSG("display_test te begin\n");
	if (primary_display_is_video_mode()) {
		DISPMSG("Video Mode No TE\n");
		return ret;
	}

	while (try_cnt >= 0) {
		try_cnt--;
		ret = dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC, HZ*1);
		time_te = sched_clock();

		dpmgr_path_trigger(pgc->dpmgr_handle, NULL, CMDQ_DISABLE);
		ret = dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ*1);
		time_framedone = sched_clock();
		time_interval = (int) (time_framedone - time_te);
		time_interval = time_interval / 1000;
		if (time_interval > time_interval_max)
			time_interval_max = time_interval;
	}
	if (time_interval_max > 20000)
		ret = 0;
	else
		ret = -1;

	if (ret >= 0)
		DISPMSG("[display_test_result]==>Force On TE Open!(%d)\n", time_interval_max);
	else
		DISPMSG("[display_test_result]==>Force On TE Closed!(%d)\n", time_interval_max);

	DISPMSG("display_test te  end\n");
	return ret;
}


int primary_display_roi_test(int x, int y)
{
	int ret = 0;

	DISPMSG("display_test roi begin\n");
	DISPMSG("display_test roi set roi %d, %d\n", x, y);
	DSI_set_roi(x, y);
	msleep(50);
	dpmgr_path_trigger(pgc->dpmgr_handle, NULL, CMDQ_DISABLE);
	msleep(50);
	DISPMSG("display_test DSI_check_roi\n");
	ret = DSI_check_roi();
	msleep(20);
	if (ret == 0)
		DISPMSG("[display_test_result]==>DSI_ROI limit!\n");
	else
		DISPMSG("[display_test_result]==>DSI_ROI Normal!\n");

	DISPMSG("display_test set roi %d, %d\n", 0, 0);

	DSI_set_roi(0, 0);

	msleep(20);
	DISPCHECK("display_test end\n");
	return ret;
}

int primary_display_resolution_test(void)
{
	int ret = 0;
	int i = 0;
	unsigned int w_backup = 0;
	unsigned int h_backup = 0;
	int dst_width = 0;
	int dst_heigh = 0;
	LCM_DSI_MODE_CON dsi_mode_backup = primary_display_is_video_mode();

	memset((void *)&data_config2, 0, sizeof(data_config2));
	lcm_param2 = NULL;
	memcpy((void *)&data_config2,
		(void *)dpmgr_path_get_last_config(pgc->dpmgr_handle),
		sizeof(disp_ddp_path_config));
	w_backup = data_config2.dst_w;
	h_backup = data_config2.dst_h;
	DISPCHECK("[display_test resolution]w_backup %d h_backup %d dsi_mode_backup %d\n",
		w_backup, h_backup, dsi_mode_backup);
	/* for dsi config */
	DSI_ForceConfig(1);
	for (i = 0; i < sizeof(width_array)/sizeof(int); i++) {
		dst_width = width_array[i];
		dst_heigh = heigh_array[i];
		DISPCHECK("[display_test resolution] width %d, heigh %d\n", dst_width, dst_heigh);
		lcm_param2 = disp_lcm_get_params(pgc->plcm);
		lcm_param2->dsi.mode = CMD_MODE;
		lcm_param2->dsi.horizontal_active_pixel = dst_width;
		lcm_param2->dsi.vertical_active_line = dst_heigh;

		data_config2.dispif_config.dsi.mode = CMD_MODE;
		data_config2.dispif_config.dsi.horizontal_active_pixel = dst_width;
		data_config2.dispif_config.dsi.vertical_active_line = dst_heigh;


		data_config2.dst_w = dst_width;
		data_config2.dst_h = dst_heigh;

		data_config2.ovl_config[0].layer    = 0;
		data_config2.ovl_config[0].layer_en = 0;
		data_config2.ovl_config[1].layer    = 1;
		data_config2.ovl_config[1].layer_en = 0;
		data_config2.ovl_config[2].layer    = 2;
		data_config2.ovl_config[2].layer_en = 0;
		data_config2.ovl_config[3].layer    = 3;
		data_config2.ovl_config[3].layer_en = 0;

		data_config2.dst_dirty = 1;
		data_config2.ovl_dirty = 1;

		dpmgr_path_set_video_mode(pgc->dpmgr_handle, primary_display_is_video_mode());

		dpmgr_path_config(pgc->dpmgr_handle, &data_config2, CMDQ_DISABLE);
		data_config2.dst_dirty = 0;
		data_config2.ovl_dirty = 0;

		dpmgr_path_start(pgc->dpmgr_handle, CMDQ_DISABLE);

		if (dpmgr_path_is_busy(pgc->dpmgr_handle))
			DISPERR("[display_test]==>Fatal error, we didn't trigger display path but it's already busy\n");

		dpmgr_path_trigger(pgc->dpmgr_handle, NULL, CMDQ_DISABLE);

		ret = dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ*1);
		if (ret > 0) {
			if (!dpmgr_path_is_busy(pgc->dpmgr_handle)) {
				if (i == 0)
					DISPCHECK("[display_test resolution] display_result 0x%x unlimited!\n",
							array_id[i]);
				else if (i == 1)
					DISPCHECK("[display_test resolution] display_result 0x%x unlimited (W<H)\n",
							array_id[i]);
				else
					DISPCHECK("[display_test resolution] display_result 0x%x(%d x %d)\n",
							array_id[i], dst_width, dst_heigh);
				break;
			}
		}
		dpmgr_path_reset(pgc->dpmgr_handle, CMDQ_DISABLE);
	}
	dpmgr_path_stop(pgc->dpmgr_handle, CMDQ_DISABLE);
	lcm_param2 = disp_lcm_get_params(pgc->plcm);
	lcm_param2->dsi.mode = dsi_mode_backup;
	lcm_param2->dsi.vertical_active_line = h_backup;
	lcm_param2->dsi.horizontal_active_pixel = w_backup;
	data_config2.dispif_config.dsi.vertical_active_line = h_backup;
	data_config2.dispif_config.dsi.horizontal_active_pixel = w_backup;
	data_config2.dispif_config.dsi.mode = dsi_mode_backup;
	data_config2.dst_w = w_backup;
	data_config2.dst_h = h_backup;
	data_config2.dst_dirty = 1;
	dpmgr_path_set_video_mode(pgc->dpmgr_handle, primary_display_is_video_mode());
	dpmgr_path_connect(pgc->dpmgr_handle, CMDQ_DISABLE);
	dpmgr_path_config(pgc->dpmgr_handle, &data_config2, CMDQ_DISABLE);
	data_config2.dst_dirty = 0;
	DSI_ForceConfig(0);
	return ret;
}

int primary_display_check_test(void)
{
	int ret = 0;
	int esd_backup = 0;

	DISPCHECK("[display_test]Display test[Start]\n");
	_primary_path_lock(__func__);
	/* disable esd check */
	if (1) {
		esd_backup = 1;
		primary_display_esd_check_enable(0);
		msleep(2000);
		DISPCHECK("[display_test]Disable esd check end\n");
	}

	/* if suspend => return */
	if (pgc->state == DISP_SLEPT) {
		DISPCHECK("[display_test_result]======================================\n");
		DISPCHECK("[display_test_result]==>Test Fail : primary display path is slept\n");
		DISPCHECK("[display_test_result]======================================\n");
		goto done;
	}

	/* stop trigger loop */
	DISPCHECK("[display_test]Stop trigger loop[begin]\n");
	_cmdq_stop_trigger_loop();
	if (dpmgr_path_is_busy(pgc->dpmgr_handle)) {
		DISPCHECK("[display_test]==>primary display path is busy\n");
		ret = dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ*1);
		if (ret <= 0)
			dpmgr_path_reset(pgc->dpmgr_handle, CMDQ_DISABLE);

		DISPCHECK("[display_test]==>wait frame done ret:%d\n", ret);
	}
	DISPCHECK("[display_test]Stop trigger loop[end]\n");

	/* test force te */
	/* primary_display_te_test(); */

	/* test roi */
	/* primary_display_roi_test(30, 30); */

	/* test resolution test */
	primary_display_resolution_test();

	DISPCHECK("[display_test]start dpmgr path[begin]\n");
	dpmgr_path_start(pgc->dpmgr_handle, CMDQ_DISABLE);
	if (dpmgr_path_is_busy(pgc->dpmgr_handle))
		DISPERR("[display_test]==>Fatal error, we didn't trigger display path but it's already busy\n");

	DISPCHECK("[display_test]start dpmgr path[end]\n");

	DISPCHECK("[display_test]Start trigger loop[begin]\n");
	_cmdq_start_trigger_loop();
	DISPCHECK("[display_test]Start trigger loop[end]\n");

done:
	/* restore esd */
	if (esd_backup == 1) {
		primary_display_esd_check_enable(1);
		DISPCHECK("[display_test]Restore esd check\n");
	}
	/* unlock path */
	_primary_path_unlock(__func__);
	DISPCHECK("[display_test]Display test[End]\n");
	return ret;
}

OPT_BACKUP tui_opt_backup[3] = {
	{DISP_OPT_IDLEMGR_SWTCH_DECOUPLE, 0},
	{DISP_OPT_SMART_OVL, 0},
	{DISP_OPT_BYPASS_OVL, 0}
};

void stop_smart_ovl_nolock(void)
{
	int i;

	for (i = 0; i < (sizeof(tui_opt_backup) / sizeof((tui_opt_backup)[0])); i++) {
		tui_opt_backup[i].value = disp_helper_get_option(tui_opt_backup[i].option);
		disp_helper_set_option(tui_opt_backup[i].option, 0);
	}
	/* primary_display_esd_check_enable(0);*/
}

void restart_smart_ovl_nolock(void)
{
	int i;

	for (i = 0; i < (sizeof(tui_opt_backup) / sizeof((tui_opt_backup)[0])); i++)
		disp_helper_set_option(tui_opt_backup[i].option, tui_opt_backup[i].value);

}

static DISP_POWER_STATE tui_power_stat_backup;
static int tui_session_mode_backup;

int display_enter_tui(void)
{
	msleep(500);
	DISPMSG("TDDP: %s\n", __func__);

	MMProfileLogEx(ddp_mmp_get_events()->tui, MMProfileFlagStart, 0, 0);

	_primary_path_lock(__func__);

	if (primary_get_state() != DISP_ALIVE) {
		DISPERR("Can't enter tui: current_stat=%d is not alive\n", primary_get_state());
		goto err0;
	}

	tui_power_stat_backup = primary_set_state(DISP_BLANK);

	primary_display_idlemgr_kick(__func__, 0);

	if (primary_display_is_mirror_mode()) {
		DISPERR("Can't enter tui: current_mode=%s\n", session_mode_spy(pgc->session_mode));
		goto err1;
	}

	stop_smart_ovl_nolock();

	tui_session_mode_backup = pgc->session_mode;

	do_primary_display_switch_mode(DISP_SESSION_DECOUPLE_MODE, pgc->session_id, 0, NULL, 0);

	MMProfileLogEx(ddp_mmp_get_events()->tui, MMProfileFlagPulse, 0, 1);

	_primary_path_unlock(__func__);
	return 0;

err1:
	primary_set_state(tui_power_stat_backup);

err0:
	MMProfileLogEx(ddp_mmp_get_events()->tui, MMProfileFlagEnd, 0, 0);
	_primary_path_unlock(__func__);

	return -1;
}

int display_exit_tui(void)
{
	pr_info("[TUI-HAL]  display_exit_tui() start\n");
	MMProfileLogEx(ddp_mmp_get_events()->tui, MMProfileFlagPulse, 1, 1);

	_primary_path_lock(__func__);
	primary_set_state(tui_power_stat_backup);

	/* trigger rdma to display last normal buffer */
	_decouple_update_rdma_config_nolock();
	/* workaround: wait until this frame triggered to lcm */
	msleep(32);
	do_primary_display_switch_mode(tui_session_mode_backup, pgc->session_id, 0, NULL, 0);
	/* DISP_REG_SET(NULL, DISP_REG_RDMA_INT_ENABLE, 0xffffffff); */

	restart_smart_ovl_nolock();
	_primary_path_unlock(__func__);

	MMProfileLogEx(ddp_mmp_get_events()->tui, MMProfileFlagEnd, 0, 0);
	DISPMSG("TDDP: %s\n", __func__);
	pr_info("[TUI-HAL]  display_exit_tui() done\n");
	return 0;

}


void ddp_irq_callback(DISP_MODULE_ENUM module, unsigned int reg_value)
{
	/* set config dirty, it will keep trigger loop busy refresh */
	cmdqCoreSetEvent(CMDQ_SYNC_TOKEN_CONFIG_DIRTY);
}


static int self_refresh_idlemgr_status_backup;
static int primary_display_enter_self_refresh(void)
{
	_primary_path_lock(__func__);

	MMProfileLogEx(ddp_mmp_get_events()->self_refresh, MMProfileFlagStart, 0, 0);

	if (primary_display_is_mirror_mode()) {
		/* we only accept non-mirror mode */
		disp_aee_print("enter self-refresh mode fail\n");
		goto out;
	}

	/* disable idle manager */
	self_refresh_idlemgr_status_backup = set_idlemgr(0, 0);

	/* stop smart ovl / bypass ovl */
	stop_smart_ovl_nolock();
	/* switch to directlink mode */
	do_primary_display_switch_mode(DISP_SESSION_DIRECT_LINK_MODE, pgc->session_id, 0, NULL, 1);

	if (!primary_display_is_video_mode())
		disp_register_irq_callback(ddp_irq_callback);

	pgc->primary_display_scenario = DISP_SCENARIO_SELF_REFRESH;
out:
	_primary_path_unlock(__func__);
	return 0;
}

static int primary_display_exit_self_refresh(void)
{
	_primary_path_lock(__func__);

	if (primary_display_is_mirror_mode()) {
		/* we only accept non-mirror mode */
		disp_aee_print("enter self-refresh mode fail\n");
		goto out;
	}

	/* disable idle manager */
	set_idlemgr(self_refresh_idlemgr_status_backup, 0);

	/* restart smart ovl */
	restart_smart_ovl_nolock();

	if (!primary_display_is_video_mode())
		disp_unregister_irq_callback(ddp_irq_callback);

	pgc->primary_display_scenario = DISP_SCENARIO_NORMAL;
out:
	_primary_path_unlock(__func__);
	MMProfileLogEx(ddp_mmp_get_events()->self_refresh, MMProfileFlagEnd, 0, 0);
	return 0;
}

int primary_display_set_scenario(int scenario)
{
	int ret = 0;

	if (scenario != DISP_SCENARIO_NORMAL &&
		pgc->primary_display_scenario != DISP_SCENARIO_NORMAL) {
		/* every scenario should start from NORMAL !! */
		pr_err("%s set scenario %d fail ! current scenario is %d\n",
			__func__, scenario, pgc->primary_display_scenario);
		return -EINVAL;
	}

	if (scenario == DISP_SCENARIO_SELF_REFRESH)
		ret = primary_display_enter_self_refresh();

	if (scenario == DISP_SCENARIO_NORMAL) {
		if (pgc->primary_display_scenario == DISP_SCENARIO_SELF_REFRESH)
			ret = primary_display_exit_self_refresh();
	}

	return ret;
}


