#include <linux/kthread.h>
/*#include <linux/rtpm_prio.h>*/

#include "extd_multi_control.h"
#include "disp_drv_platform.h"
#include "external_display.h"
#include "extd_platform.h"
#include "extd_log.h"
#include "mtk_ovl.h"
#ifdef CONFIG_SINGLE_PANEL_OUTPUT
#include "extd_hdmi.h"
#include <linux/delay.h>
#include <linux/ktime.h>
#include "primary_display.h"
#include "leds_drv.h"
struct task_struct *primary_resume_task = NULL;
wait_queue_head_t primary_resume_wq;
atomic_t primary_resume_event = ATOMIC_INIT(0);
unsigned int g_suspend_flag = 0;
#endif

static const struct EXTD_DRIVER  *extd_driver[DEV_MAX_NUM-1];
struct SWITCH_MODE_INFO_STRUCT path_info;

struct task_struct *disp_switch_mode_task = NULL;
wait_queue_head_t switch_mode_wq;
atomic_t switch_mode_event = ATOMIC_INIT(0);

static int extd_create_path(enum EXT_DISP_PATH_MODE mode, unsigned int session)
{
	int ret = 0;

	MULTI_COTRL_LOG("extd_create_path session:%08x, mode:%d", session, mode);

	ext_disp_path_set_mode(mode, session);
	ret = ext_disp_init(NULL, session);

	return ret;
}


static int extd_get_device_type(unsigned int session)
{
	int ret = 0;
	int device_id = (session & 0x0FF) - 1;

	if (device_id == DEV_MHL && extd_driver[DEV_MHL]->ioctl) {
		/*for mhl device*/
		ret = extd_driver[DEV_MHL]->ioctl(GET_DEV_TYPE_CMD, 0, 0, NULL);
	} else if (device_id == DEV_EINK && extd_driver[DEV_EINK]->ioctl) {
		/*for eink device*/
		ret = extd_driver[DEV_EINK]->ioctl(GET_DEV_TYPE_CMD, 0, 0, NULL);
	}

	MULTI_COTRL_LOG("device type is:%d\n", ret);
	return ret;
}

static void extd_set_layer_num(int layer_num, unsigned int session)
{
	int device_id = (session & 0x0FF) - 1;

	if (device_id == DEV_MHL && extd_driver[DEV_MHL]->ioctl) {
		/*for mhl device*/
		extd_driver[DEV_MHL]->ioctl(SET_LAYER_NUM_CMD, layer_num, 0, NULL);
	} else if (device_id == DEV_EINK && extd_driver[DEV_EINK]->ioctl) {
		/*for eink device*/
		extd_driver[DEV_EINK]->ioctl(SET_LAYER_NUM_CMD, layer_num, 0, NULL);
	}
}

static int create_external_display_path(unsigned int session, int mode)
{
	int ret = 0;
	int extd_type = DISP_IF_MHL;

	pr_warn("create_external_display_path session:%08x, mode:%d\n", session, mode);

	if (DISP_SESSION_TYPE(session) == DISP_SESSION_MEMORY && EXTD_OVERLAY_CNT > 0) {
		if (mode < DISP_SESSION_DIRECT_LINK_MIRROR_MODE
		&& (path_info.old_mode[DEV_WFD] >= DISP_SESSION_DIRECT_LINK_MIRROR_MODE
		|| path_info.old_session[DEV_WFD] != DISP_SESSION_MEMORY)) {

			if (ext_disp_wait_ovl_available(0) > 0) {
				ovl2mem_init(session);
				ovl2mem_setlayernum(4);
			} else {
				MULTI_COTRL_ERR("mhl path: OVL1 can not be split out!\n");
				ret = -1;
			}
		} else if (mode >= DISP_SESSION_DIRECT_LINK_MIRROR_MODE
			&& path_info.old_session[DEV_WFD] == DISP_SESSION_MEMORY
			&& path_info.old_mode[DEV_WFD] < DISP_SESSION_DIRECT_LINK_MIRROR_MODE) {

			ovl2mem_deinit();
			ovl2mem_setlayernum(0);
			ext_disp_path_change(EXTD_OVL_IDLE_REQ, session);
		}
	} else if (DISP_SESSION_TYPE(session) == DISP_SESSION_EXTERNAL) {
		int device_id = DISP_SESSION_DEV(session) - 1;

		extd_type = extd_get_device_type(session);

		if (path_info.old_session[device_id] == DISP_SESSION_EXTERNAL) {
			ext_disp_resume(session);

			if (EXTD_OVERLAY_CNT < 1) {
				/*external display has no OVL to use, so no actions for mode switch */
				return ret;
			}
		} else {
			extd_create_path(EXTD_RDMA_DPI_MODE, session);
			extd_set_layer_num(1, session);
		}
#if 0
		if (extd_type != DISP_IF_EPD && (mode < DISP_SESSION_DIRECT_LINK_MIRROR_MODE
		|| extd_type == DISP_IF_HDMI_SMARTBOOK)) {

			if (ext_disp_wait_ovl_available(0) > 0) {
				if (path_info.old_session[device_id] == DISP_SESSION_EXTERNAL
				&& extd_type != DISP_IF_HDMI_SMARTBOOK) {
					/*insert OVL to external dispaly path*/
					ext_disp_path_change(EXTD_OVL_INSERT_REQ, session);
				} else {
					/*create external display path with OVL*/
					extd_create_path(EXTD_DIRECT_LINK_MODE, session);
				}

				extd_set_layer_num(4, session);
			} else {
				extd_create_path(EXTD_RDMA_DPI_MODE, session);
				extd_set_layer_num(1, session);
			}
		} else {
			if (path_info.old_session[device_id] == DISP_SESSION_EXTERNAL) {
				ext_disp_path_change(EXTD_OVL_REMOVE_REQ, session);
				extd_set_layer_num(1, session);
			} else {
				extd_create_path(EXTD_RDMA_DPI_MODE, session);
				extd_set_layer_num(1, session);
			}
		}
#endif




	} else if (DISP_SESSION_TYPE(session) == DISP_SESSION_MEMORY && EXTD_OVERLAY_CNT == 0) {
		MULTI_COTRL_ERR("memory session and ovl time sharing!\n");
		ovl2mem_setlayernum(4);
	}

	return ret;
}

static void destroy_external_display_path(unsigned int session, int mode)
{
	int device_id = DISP_SESSION_DEV(session) - 1;

	MULTI_COTRL_LOG("destroy_external_display_path session:%08x\n", session);

	if ((path_info.old_session[device_id] == DISP_SESSION_PRIMARY)
	|| (path_info.old_session[device_id] == DISP_SESSION_MEMORY
	&& path_info.old_mode[device_id] >= DISP_SESSION_DIRECT_LINK_MIRROR_MODE)) {
		/*discard for memory session in mirror mode*/
		return;
	}

	if (path_info.old_session[device_id] == DISP_SESSION_EXTERNAL) {
		ext_disp_deinit(session);
		extd_set_layer_num(0, session);
		ext_disp_path_change(EXTD_OVL_IDLE_REQ, session);
	} else if (path_info.old_session[device_id] == DISP_SESSION_MEMORY && EXTD_OVERLAY_CNT > 0) {
		ovl2mem_deinit();
		ovl2mem_setlayernum(0);
		ext_disp_path_change(EXTD_OVL_IDLE_REQ, session);
	}
}

static int disp_switch_mode_kthread(void *data)
{
	int ret = 0;
	struct sched_param param = { .sched_priority = 94 }; /*RTPM_PRIO_SCRN_UPDATE*/

	sched_setscheduler(current, SCHED_RR, &param);

	MULTI_COTRL_LOG("disp_switch_mode_kthread in!\n");

	for (;;) {
		wait_event_interruptible(switch_mode_wq, atomic_read(&switch_mode_event));
		atomic_set(&switch_mode_event, 0);

		MULTI_COTRL_LOG("switch mode, create or change path, mode:%d, session:0x%x\n",
				path_info.cur_mode, path_info.ext_sid);
		ret = create_external_display_path(path_info.ext_sid, path_info.cur_mode);
		if (ret == 0) {
			path_info.old_session[path_info.switching] = DISP_SESSION_TYPE(path_info.ext_sid);
			path_info.old_mode[path_info.switching]    = path_info.cur_mode;
		}

		path_info.switching = DEV_MAX_NUM;
		path_info.ext_sid   = 0;

		if (kthread_should_stop()) {
			/*thread exit*/
			break;
		}
	}

	return 0;
}
#ifdef CONFIG_SINGLE_PANEL_OUTPUT
static int primary_resume_kthread(void *data)
{
	struct sched_param param = { .sched_priority = 94 };

	sched_setscheduler(current, SCHED_RR, &param);
	pr_warn("primary_resume_kthread in!\n");
	for ( ; ; ) {
		wait_event_interruptible(primary_resume_wq, atomic_read(&primary_resume_event));
		if (is_hdmi_plug_out_valid() && !is_early_suspended) {
			g_suspend_flag = 1;
			clr_hdmi_plug_out_valid();
			pr_warn("primary_resume_kthread: hdmi plug out,call primary_resume\n");
			msleep(30);
			primary_display_resume();
			atomic_set(&primary_resume_event, 0);
			msleep(700);
			g_suspend_flag = 0;
#if  defined(CONFIG_MTK_LEDS)
			mt65xx_leds_brightness_set(MT65XX_LED_TYPE_LCD, LED_HALF);
#endif
		} else {
			atomic_set(&primary_resume_event, 0);
			pr_warn("primary_resume_kthread: hdmi not plug out--skip primary_resume\n");
		}
		if (kthread_should_stop())
			break;
	}
	return 0;
}
#endif

int external_display_path_change_without_cascade(DISP_MODE mode, unsigned int session_id,
		unsigned int device_id, unsigned int change_flag)
{
	int ret = -1;
	unsigned int session = 0;

	MULTI_COTRL_FUNC();
	if (1 == change_flag) {
#ifdef CONFIG_SINGLE_PANEL_OUTPUT
		if (primary_display_is_alive()) {
			g_suspend_flag = 1;
#if defined(CONFIG_MTK_LEDS)
			mt65xx_leds_brightness_set(MT65XX_LED_TYPE_LCD, LED_OFF);
#endif
			primary_display_suspend();
			g_suspend_flag = 0;
		}
#endif
	}
	/*destroy external display path*/
	if (session_id == 0 && path_info.old_session[device_id] != DISP_SESSION_PRIMARY) {
		if (device_id == DEV_WFD) {
			/*make memory session for WFD*/
			session = MAKE_DISP_SESSION(DISP_SESSION_MEMORY, device_id);
		} else {
			/*make external session*/
			session = MAKE_DISP_SESSION(DISP_SESSION_EXTERNAL, device_id + 1);
		}
		destroy_external_display_path(session, DISP_SESSION_DIRECT_LINK_MODE);
#ifdef CONFIG_SINGLE_PANEL_OUTPUT
		atomic_set(&primary_resume_event, 1);
		wake_up_interruptible(&primary_resume_wq);
#endif
		path_info.old_session[device_id] = DISP_SESSION_PRIMARY;
		path_info.old_mode[device_id]    = DISP_SESSION_DIRECT_LINK_MODE;
		path_info.switching = DEV_MAX_NUM;

		return 1;
	}

	/*create path or change path*/
	if ((session_id > 0 && path_info.old_session[device_id] == DISP_SESSION_PRIMARY)
	|| (mode != path_info.old_mode[device_id] && path_info.old_session[device_id] != DISP_SESSION_PRIMARY)) {
		ret = create_external_display_path(session_id, mode);
		if (ret == 0) {
			path_info.old_session[device_id] = DISP_SESSION_TYPE(session_id);
			path_info.old_mode[device_id]    = mode;
		}

		path_info.switching = DEV_MAX_NUM;

		return 1;
	}

	return 0;
}


void external_display_control_init(void)
{
	int i = 0;

	MULTI_COTRL_FUNC();
	memset(&path_info, 0, sizeof(struct SWITCH_MODE_INFO_STRUCT));
	path_info.switching = DEV_MAX_NUM;

	for (i = 0; i < DEV_MAX_NUM; i++) {
		path_info.old_mode[i]    = DISP_SESSION_DIRECT_LINK_MODE;
		path_info.old_session[i] = DISP_SESSION_PRIMARY;
	}

	extd_driver[DEV_MHL]  = EXTD_HDMI_Driver();
	extd_driver[DEV_EINK] = EXTD_EPD_Driver();
#ifdef CONFIG_SINGLE_PANEL_OUTPUT
	init_waitqueue_head(&primary_resume_wq);
	primary_resume_task = kthread_create(primary_resume_kthread, NULL, "primary_resume_kthread");
	wake_up_process(primary_resume_task);
#endif

	init_waitqueue_head(&switch_mode_wq);
	disp_switch_mode_task = kthread_create(disp_switch_mode_kthread, NULL, "disp_switch_mode_kthread");
	wake_up_process(disp_switch_mode_task);

	ext_disp_probe();
}

int external_display_config_input(disp_session_input_config *input, int idx, unsigned int session)
{
	int ret = 0;

	if (ext_disp_path_get_mode(session) == EXTD_RDMA_DPI_MODE) {
		/*recompule background*/
		/*extd_recompute_bg(input->src_w, input->src_h, session);*/
	}

	ret = ext_disp_config_input_multiple(input, idx, session);

	return ret;
}

int external_display_trigger(EXTD_TRIGGER_MODE trigger, unsigned int session)
{
	int ret = 0;
	enum EXTD_OVL_REQ_STATUS ovl_status = EXTD_OVL_NO_REQ;

	if (trigger == TRIGGER_RESUME) {
		ext_disp_resume(session);
		if (DISP_SESSION_TYPE(session) == DISP_SESSION_EXTERNAL && DISP_SESSION_DEV(session) == DEV_EINK+1) {
			if (extd_driver[DEV_EINK]->power_enable) {
				/*1 for power on, 0 for power off*/
				extd_driver[DEV_EINK]->power_enable(1);
			}
		}
	}

	ret = ext_disp_trigger(0, NULL, 0, session);

	if (trigger == TRIGGER_SUSPEND) {
		ext_disp_suspend_trigger(NULL, 0, session);
		if (DISP_SESSION_TYPE(session) == DISP_SESSION_EXTERNAL && DISP_SESSION_DEV(session) == DEV_EINK+1) {
			if (extd_driver[DEV_EINK]->power_enable) {
				/*1 for power on, 0 for power off*/
				extd_driver[DEV_EINK]->power_enable(0);
			}
		}
	}

	ovl_status = ext_disp_get_ovl_req_status(session);
	if (ovl_status == EXTD_OVL_REMOVING) {
		/*the new buffer configured, ovl can be removed*/
		ext_disp_path_change(EXTD_OVL_REMOVED, session);
	} else if (ovl_status == EXTD_OVL_INSERT_REQ) {
		/*the new buffer configured, ovl already is inserted in the path*/
		ext_disp_path_change(EXTD_OVL_INSERTED, session);
	}

	return ret;
}

int external_display_wait_for_vsync(void *config, unsigned int session)
{
	int ret = 0;

	ret = ext_disp_wait_for_vsync(config, session);

	return ret;
}

int external_display_get_info(void *info, unsigned int session)
{
	int ret = 0;
	int device_id = (session & 0x0FF) - 1;

	if (device_id == DEV_MHL && extd_driver[DEV_MHL]->get_dev_info) {
		/*get device info for MHL*/
		ret = extd_driver[DEV_MHL]->get_dev_info(SF_GET_INFO, info);
	} else if (device_id == DEV_EINK  && extd_driver[DEV_EINK]->get_dev_info) {
		/*get device info for EINK*/
		ret = extd_driver[DEV_EINK]->get_dev_info(SF_GET_INFO, info);
	}

	return ret;
}

int external_display_switch_mode(DISP_MODE mode, unsigned int *session_created, unsigned int session)
{
	int i = 0;
	int j = 0;
	int ret = -1;
	int switching = 0;
	static unsigned int change_flag;
	int session_id[DEV_MAX_NUM] = {0};

	MULTI_COTRL_FUNC();

	if (session_created == NULL) {
		/*error,no session can be compared*/
		return ret;
	}

	if (path_info.switching < DEV_MAX_NUM
#ifdef CONFIG_SINGLE_PANEL_OUTPUT
		|| (atomic_read(&primary_resume_event) == 1)
#endif
		) {
		/*mode is switching, return directly*/
		return ret;
	}

	path_info.switching = DEV_MAX_NUM - 1;

	for (i = 0; i < MAX_SESSION_COUNT; i++) {
		if (session_created[i] == MAKE_DISP_SESSION(DISP_SESSION_EXTERNAL, DEV_MHL+1)) {
			/*it has MHL session*/
			session_id[DEV_MHL] = session_created[i];
		}

		if (session_created[i] == MAKE_DISP_SESSION(DISP_SESSION_EXTERNAL, DEV_EINK+1)) {
			/*it has EINK session*/
			session_id[DEV_EINK] = session_created[i];
		}

		if (session_created[i] == MAKE_DISP_SESSION(DISP_SESSION_MEMORY, DEV_WFD)) {
			/*it has WFD session*/
			session_id[DEV_WFD] = session_created[i];
		}
	}

	for (j = 0; j < DEV_MAX_NUM; j++) {
#ifndef OVL_CASCADE_SUPPORT
#ifdef CONFIG_SINGLE_PANEL_OUTPUT
		if ((session_id[j] == 0 && path_info.old_session[j] != DISP_SESSION_PRIMARY) ||
			(session_id[j] > 0 && path_info.old_session[j] == DISP_SESSION_PRIMARY) ||
			(mode != path_info.old_mode[j] && path_info.old_session[j] != DISP_SESSION_PRIMARY)) {
			pr_warn("need change path, set change flag 1\n");
			change_flag = 1;
		}
#endif
		switching = external_display_path_change_without_cascade(mode, session_id[j], j, change_flag);
#else
		switching = path_change_with_cascade(mode, session_id[j], j);
#endif
		change_flag = 0;


		if (switching == 1) {
			/*session switching one by one*/
			break;
		}
	}

	path_info.switching = (switching == 0) ? DEV_MAX_NUM : path_info.switching;

	return 0;
}
