
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/string.h>
#ifdef CONFIG_MTK_DCS
#include <mt-plat/mtk_meminfo.h>
#endif
#include "layering_rule.h"

static disp_layer_info layering_info;
static int debug_resolution_level;
static struct layering_rule_info_t *l_rule_info;
static struct layering_rule_ops *l_rule_ops;

bool is_ext_path(disp_layer_info *disp_info)
{
	if (disp_info->layer_num[HRT_SECONDARY] > 0)
		return true;
	else
		return false;

}

bool is_decouple_path(disp_layer_info *disp_info)
{
	if (disp_info->disp_mode[HRT_PRIMARY] != 1)
		return true;
	else
		return false;
}

static int get_bpp(DISP_FORMAT format)
{
	int bpp;

	bpp = format & 0xFF;

	if (bpp > 4) {
		DISPERR("Invalid color format: 0x%x, bpp > 4\n", format);
		bpp = 4;
	}
	return bpp;
}

bool is_argb_fmt(DISP_FORMAT format)
{
	switch (format) {
	case DISP_FORMAT_ARGB8888:
	case DISP_FORMAT_ABGR8888:
	case DISP_FORMAT_RGBA8888:
	case DISP_FORMAT_BGRA8888:
		return true;
	default:
		return false;
	}
}

bool is_yuv(DISP_FORMAT format)
{
	switch (format) {
	case DISP_FORMAT_YUV422:
	case DISP_FORMAT_UYVY:
	case DISP_FORMAT_YUV420_P:
	case DISP_FORMAT_YV12:
		return true;
	default:
		return false;
	}
}


bool is_gles_layer(disp_layer_info *disp_info, int disp_idx, int layer_idx)
{
	if (layer_idx >= disp_info->gles_head[disp_idx] &&
		layer_idx <= disp_info->gles_tail[disp_idx])
		return true;
	else
		return false;
}

inline bool has_layer_cap(layer_config *layer_info, enum LAYERING_CAPS l_caps)
{
	if (layer_info->layer_caps & l_caps)
		return true;
	return false;
}

static int get_ovl_layer_cnt(disp_layer_info *disp_info, int disp_idx)
{
	int total_cnt = 0;

	if (disp_info->layer_num[disp_idx] != -1) {
		total_cnt = disp_info->layer_num[disp_idx];

		if (disp_info->gles_head[disp_idx] >= 0)
			total_cnt -= (disp_info->gles_tail[disp_idx] - disp_info->gles_head[disp_idx]);
	}
	return total_cnt;
}

static int is_overlap_on_yaxis(layer_config *lhs, layer_config *rhs)
{
	if ((lhs->dst_offset_y + lhs->dst_height <= rhs->dst_offset_y) ||
			(rhs->dst_offset_y + rhs->dst_height <= lhs->dst_offset_y))
		return 0;
	return 1;
}

bool is_layer_across_each_pipe(layer_config *layer_info)
{
	int dst_x, dst_w;

	if (!disp_helper_get_option(DISP_OPT_DUAL_PIPE))
		return true;

	dst_x = layer_info->dst_offset_x;
	dst_w = layer_info->dst_width;
	if ((dst_x + dst_w <= primary_display_get_width() / 2) ||
		(dst_x > primary_display_get_width() / 2))
		return false;
	return true;
}

static inline bool is_extended_layer(layer_config *layer_info)
{
	return (layer_info->ext_sel_layer != -1);
}

static int is_continuous_ext_layer_overlap(layer_config *configs, int curr)
{
	int overlapped;
	layer_config *src_info, *dst_info;
	int i;

	overlapped = 0;
	dst_info = &configs[curr];
	for (i = curr-1; i >= 0; i--) {
		src_info = &configs[i];
		if (is_extended_layer(src_info)) {
			overlapped |= is_overlap_on_yaxis(src_info, dst_info);
			if (overlapped)
				break;
		} else {
			if (i == 0 && is_yuv(src_info->src_fmt))
				overlapped |= 1;
			else
				overlapped |= is_overlap_on_yaxis(src_info, dst_info);

			/**
			 * Under dual pipe, if the layer is not included in each pipes, it cannot
			 * use as a base layer for extended layer as extended layer would not
			 * find base layer in one of display pipe. So always Mark this specific layer
			 * as overlap to avoid the fail case.
			 **/
			if (!is_layer_across_each_pipe(src_info))
				overlapped |= 1;
			break;
		}
	}
	return overlapped;
}

int get_phy_ovl_layer_cnt(disp_layer_info *disp_info, int disp_idx)
{
	int total_cnt = 0;
	int i;
	layer_config *layer_info;

	if (disp_info->layer_num[disp_idx] != -1) {
		total_cnt = disp_info->layer_num[disp_idx];

		if (disp_info->gles_head[disp_idx] >= 0)
			total_cnt -= (disp_info->gles_tail[disp_idx] - disp_info->gles_head[disp_idx]);

		if (disp_helper_get_option(DISP_OPT_OVL_EXT_LAYER)) {
			for (i = 0 ; i < disp_info->layer_num[disp_idx]; i++) {
				layer_info = &disp_info->input_config[disp_idx][i];
				if (is_extended_layer(layer_info) && !is_gles_layer(disp_info, disp_idx, i))
					total_cnt--;
			}
		}
	}
	return total_cnt;
}

int get_phy_layer_limit(int layer_map_tb, int disp_idx)
{
	int total_cnt = 0;
	int i;

	if (disp_idx)
		layer_map_tb >>= 16;
	layer_map_tb &= 0xFFFF;

	for (i = 0 ; i < 16 ; i++) {
		if (layer_map_tb & 0x1)
			total_cnt++;
		layer_map_tb >>= 1;
	}
	return total_cnt;
}

bool is_max_lcm_resolution(void)
{
	if (debug_resolution_level == 1)
		return true;
	else if (debug_resolution_level == 2)
		return false;

	if (primary_display_get_width() > 1080)
		return true;
	else
		return false;
}

static int get_ovl_idx_by_phy_layer(int layer_map_tb, int phy_layer_idx)
{
	int i, ovl_mapping_tb;
	int ovl_idx = 0, layer_idx = 0;

	ovl_mapping_tb = l_rule_ops->get_mapping_table(DISP_HW_OVL_TB, 0);
	for (layer_idx = 0 ; layer_idx < MAX_PHY_OVL_CNT ; layer_idx++) {
		if (layer_map_tb & 0x1) {
			if (phy_layer_idx == 0)
				break;
			phy_layer_idx--;
		}
		layer_map_tb >>= 1;
	}

	if (layer_idx == MAX_PHY_OVL_CNT) {
		DISPERR("%s fail, phy_layer_idx:%d\n", __func__, phy_layer_idx);
		return -1;
	}

	for (i = 0 ; i < layer_idx  ; i++) {
		if (ovl_mapping_tb & 0x1)
			ovl_idx++;
		ovl_mapping_tb >>= 1;
	}
#ifdef HRT_DEBUG_LEVEL2
	DISPMSG("%s, phy_layer_idx:%d, layer_map_tb:0x%x, layer_idx:%d ovl_idx:%d, ovl_mapping_tb:0x%x\n",
		__func__, phy_layer_idx, layer_map_tb, layer_idx, ovl_idx, ovl_mapping_tb);
#endif
	return ovl_idx;
}

static int get_phy_ovl_index(int layer_idx)
{
	int ovl_mapping_tb = l_rule_ops->get_mapping_table(DISP_HW_OVL_TB, 0);
	int phy_layer_cnt, layer_flag;

	phy_layer_cnt = 0;
	layer_flag = 1 << layer_idx;
	while (layer_idx) {
		layer_idx--;
		layer_flag >>= 1;
		if (ovl_mapping_tb & layer_flag)
			break;
		phy_layer_cnt++;
	}

	return phy_layer_cnt;
}

static int get_larb_idx_by_ovl_idx(int ovl_idx, int disp_idx)
{
	int larb_mapping_tb, larb_idx;

	larb_mapping_tb = l_rule_ops->get_mapping_table(DISP_HW_LARB_TB, 0);
	if (disp_idx == HRT_SECONDARY)
		larb_mapping_tb >>= 16;

	larb_idx = (larb_mapping_tb >> ovl_idx * 4) & 0xF;

	return larb_idx;
}

static char *get_scale_name(int scale)
{
	switch (scale) {
	case HRT_SCALE_NONE:
		return "NA";
	case HRT_SCALE_133:
		return "133";
	case HRT_SCALE_150:
		return "150";
	case HRT_SCALE_200:
		return "200";
	case HRT_SCALE_266:
		return "266";
	default:
		return "unknown";
	}
}

static void dump_disp_info(disp_layer_info *disp_info, enum DISP_DEBUG_LEVEL debug_level)
{
	int i, j;
	layer_config *layer_info;

	DISPMSG("HRT hrt_num:%d/fps:%d/dal:%d/p:%d/r:%s/layer_tb:%d/bound_tb:%d\n",
		disp_info->hrt_num, l_rule_info->primary_fps, l_rule_info->dal_enable,
		HRT_GET_PATH_ID(l_rule_info->disp_path), get_scale_name(l_rule_info->scale_rate),
		l_rule_info->layer_tb_idx, l_rule_info->bound_tb_idx);

	for (i = 0 ; i < 2 ; i++) {
		DISPMSG("HRT D%d/M%d/LN%d/hrt_num:%d/G(%d,%d)\n",
			i, disp_info->disp_mode[i], disp_info->layer_num[i], disp_info->hrt_num,
			disp_info->gles_head[i], disp_info->gles_tail[i]);

		for (j = 0 ; j < disp_info->layer_num[i] ; j++) {
			layer_info = &disp_info->input_config[i][j];
			DISPMSG("L%d->%d/of(%d,%d)/swh(%d,%d)/dwh(%d,%d)/fmt:0x%x/ext:%d/caps:0x%x\n",
				j, layer_info->ovl_id, layer_info->dst_offset_x, layer_info->dst_offset_y,
				layer_info->src_width, layer_info->src_height,
				layer_info->dst_width, layer_info->dst_height,
				layer_info->src_fmt, layer_info->ext_sel_layer,
				layer_info->layer_caps);
		}
	}
}

static void print_disp_info_to_log_buffer(disp_layer_info *disp_info)
{
	char *status_buf;
	int i, j, n;
	layer_config *layer_info;

	status_buf = get_dprec_status_ptr(0);
	if (status_buf == NULL)
		return;

	n = 0;
	n += snprintf(status_buf + n, LOGGER_BUFFER_SIZE - n,
		"Last hrt query data[start]\n");
	for (i = 0 ; i < 2 ; i++) {
		n += snprintf(status_buf + n, LOGGER_BUFFER_SIZE - n,
			"HRT D%d/M%d/LN%d/hrt_num:%d/G(%d,%d)/fps:%d\n",
			i, disp_info->disp_mode[i], disp_info->layer_num[i], disp_info->hrt_num,
			disp_info->gles_head[i], disp_info->gles_tail[i], l_rule_info->primary_fps);

		for (j = 0 ; j < disp_info->layer_num[i] ; j++) {
			layer_info = &disp_info->input_config[i][j];
			n += snprintf(status_buf + n, LOGGER_BUFFER_SIZE - n,
				"L%d->%d/of(%d,%d)/wh(%d,%d)/fmt:0x%x\n",
				j, layer_info->ovl_id, layer_info->dst_offset_x,
				layer_info->dst_offset_y, layer_info->dst_width, layer_info->dst_height,
				layer_info->src_fmt);
		}
	}
	n += snprintf(status_buf + n, LOGGER_BUFFER_SIZE - n,
		"Last hrt query data[end]\n");

}

static bool support_partial_gles_layer(enum HRT_PATH_SCENARIO path_scenario)
{
	if (HRT_GET_PATH_RSZ_TYPE(path_scenario) == HRT_PATH_RSZ_NONE)
		return true;
	else
		return false;
}

int rollback_all_resize_layer_to_GPU(disp_layer_info *disp_info, int disp_idx)
{
	int curr_ovl_num, i;
	layer_config *layer_info;

	if (disp_info->layer_num[disp_idx] <= 0)
		return 0;

	curr_ovl_num = 0;
	for (i = 0 ; i < disp_info->layer_num[disp_idx] ; i++) {
		layer_info = &disp_info->input_config[disp_idx][i];
		if ((layer_info->src_height != layer_info->dst_height) ||
					(layer_info->src_width != layer_info->dst_width)) {
			if (disp_info->gles_head[disp_idx] == -1 || disp_info->gles_head[disp_idx] > i)
				disp_info->gles_head[disp_idx] = i;
			if (disp_info->gles_tail[disp_idx] == -1 || disp_info->gles_tail[disp_idx] < i)
				disp_info->gles_tail[disp_idx] = i;
		}
	}

	if (disp_info->gles_head[disp_idx] != -1) {
		for (i = disp_info->gles_head[disp_idx] ; i <= disp_info->gles_tail[disp_idx] ; i++) {
			layer_info = &disp_info->input_config[disp_idx][i];
			layer_info->ext_sel_layer = -1;
		}
	}

	if (disp_idx == HRT_SECONDARY)
		return 0;

	if (l_rule_ops->rsz_by_gpu_info_change)
		l_rule_ops->rsz_by_gpu_info_change();
	else
		pr_warn("%s, rsz_by_gpu_info_change not defined\n", __func__);

	return 0;
}

static int _rollback_to_GPU_bottom_up(disp_layer_info *disp_info, int disp_idx, int ovl_limit)
{
	int available_ovl_num, i, j;
	layer_config *layer_info;

	available_ovl_num = ovl_limit;
	for (i = 0 ; i < disp_info->layer_num[disp_idx] ; i++) {
		layer_info = &disp_info->input_config[disp_idx][i];
		if (is_extended_layer(layer_info))
			continue;
		available_ovl_num--;

		if (is_gles_layer(disp_info, disp_idx, i)) {
			disp_info->gles_head[disp_idx] = i;
			if (disp_info->gles_tail[disp_idx] == -1) {
				disp_info->gles_tail[disp_idx] = i;
				for (j = i + 1 ; j < disp_info->layer_num[disp_idx] ; j++) {
					layer_info = &disp_info->input_config[disp_idx][j];
					if (is_extended_layer(layer_info))
						disp_info->gles_tail[disp_idx] = j;
					else
						break;
				}
			}
			break;
		} else if (available_ovl_num == 0) {
			disp_info->gles_head[disp_idx] = i;
			disp_info->gles_tail[disp_idx] = disp_info->layer_num[disp_idx] - 1;
			break;
		}
	}

	if (available_ovl_num < 0)
		DISPERR("%s available_ovl_num invalid:%d\n", __func__, available_ovl_num);

	return available_ovl_num;
}

static int _rollback_to_GPU_top_down(disp_layer_info *disp_info, int disp_idx, int ovl_limit)
{
	int available_ovl_num, i;
	int tmp_ext_id = -1;
	layer_config *layer_info;

	available_ovl_num = ovl_limit;
	for (i = disp_info->layer_num[disp_idx] - 1 ; i > disp_info->gles_tail[disp_idx] ; i--) {
		layer_info = &disp_info->input_config[disp_idx][i];
		if (!is_extended_layer(layer_info)) {

			if (is_gles_layer(disp_info, disp_idx, i))
				break;
			if (available_ovl_num == 0) {
				if (tmp_ext_id == -1)
					disp_info->gles_tail[disp_idx] = i;
				else
					disp_info->gles_tail[disp_idx] = tmp_ext_id;
				break;
			}
			tmp_ext_id = -1;
			available_ovl_num--;
		} else {
			if (tmp_ext_id == -1)
				tmp_ext_id = i;
		}
	}

	if (available_ovl_num < 0)
		DISPERR("%s available_ovl_num invalid:%d\n", __func__, available_ovl_num);

	return available_ovl_num;
}

static int rollback_to_GPU(disp_layer_info *disp_info, int disp_idx, int available)
{
	int available_ovl_num, i;
	bool has_gles_layer = false;
	layer_config *layer_info;

	available_ovl_num = available;
	if (!support_partial_gles_layer(l_rule_info->disp_path)) {
		rollback_all_resize_layer_to_GPU(disp_info, disp_idx);
		available_ovl_num = get_phy_layer_limit(
			l_rule_ops->get_mapping_table(DISP_HW_LAYER_TB, MAX_PHY_OVL_CNT - 1), disp_idx);
		if (l_rule_info->dal_enable)
			available_ovl_num--;
	}

	if (disp_info->gles_head[disp_idx] != -1)
		has_gles_layer = true;

	available_ovl_num = _rollback_to_GPU_bottom_up(disp_info, disp_idx, available_ovl_num);
	if (has_gles_layer)
		available_ovl_num = _rollback_to_GPU_top_down(disp_info, disp_idx, available_ovl_num);

	/* Clear extended layer for all GLES layer */
	for (i = disp_info->gles_head[disp_idx] ; i <= disp_info->gles_tail[disp_idx] ; i++) {
		layer_info = &disp_info->input_config[disp_idx][i];
		layer_info->ext_sel_layer = -1;
	}

	if (disp_info->gles_tail[disp_idx] + 1 < disp_info->layer_num[disp_idx]) {
		layer_info = &disp_info->input_config[disp_idx][disp_info->gles_tail[disp_idx] + 1];
		if (is_extended_layer(layer_info))
			layer_info->ext_sel_layer = -1;
	}

	return available_ovl_num;
}

static int _filter_by_ovl_cnt(disp_layer_info *disp_info, int disp_idx)
{
	int ovl_num_limit, phy_ovl_cnt;

	if (disp_info->layer_num[disp_idx] <= 0)
		return 0;

	phy_ovl_cnt = get_phy_ovl_layer_cnt(disp_info, disp_idx);
#ifdef HRT_DEBUG_LEVEL2
	DISPMSG("layer_tb_idx:%d, layer_mapping_table:0x%x\n",
		l_rule_info->layer_tb_idx, l_rule_ops->get_mapping_table(DISP_HW_LAYER_TB, MAX_PHY_OVL_CNT - 1));
#endif
	ovl_num_limit = get_phy_layer_limit(
		l_rule_ops->get_mapping_table(DISP_HW_LAYER_TB, MAX_PHY_OVL_CNT - 1), disp_idx);
	if (disp_idx == 0 && l_rule_info->dal_enable)
		ovl_num_limit--;

#ifdef HRT_DEBUG_LEVEL2
	DISPMSG("phy_ovl_cnt:%d, ovl_num_limit:%d\n", phy_ovl_cnt, ovl_num_limit);
#endif
	if (phy_ovl_cnt <= ovl_num_limit)
		return 0;

	rollback_to_GPU(disp_info, disp_idx, ovl_num_limit);
	return 0;
}

static int ext_id_tunning(disp_layer_info *disp_info, int disp_idx)
{
	int ovl_mapping_tb, layer_mapping_tb, phy_ovl_cnt, i;
	int ext_cnt = 0, cur_phy_cnt = 0;
	layer_config *layer_info;

	_filter_by_ovl_cnt(disp_info, disp_idx);
	phy_ovl_cnt = get_phy_ovl_layer_cnt(disp_info, disp_idx);
	if (phy_ovl_cnt > MAX_PHY_OVL_CNT) {
		DISPERR("phy_ovl_cnt(%d) over OVL count limit\n", phy_ovl_cnt);
		phy_ovl_cnt = MAX_PHY_OVL_CNT;
	}

	ovl_mapping_tb = l_rule_ops->get_mapping_table(DISP_HW_OVL_TB, 0);
	layer_mapping_tb = l_rule_ops->get_mapping_table(DISP_HW_LAYER_TB, phy_ovl_cnt - 1);
	if (l_rule_info->dal_enable) {
		layer_mapping_tb = l_rule_ops->get_mapping_table(DISP_HW_LAYER_TB, MAX_PHY_OVL_CNT - 1);
		layer_mapping_tb &= HRT_AEE_LAYER_MASK;
	}
	for (i = 0 ; i < disp_info->layer_num[disp_idx] ; i++) {
		layer_info = &disp_info->input_config[disp_idx][i];
		if (is_extended_layer(layer_info)) {
			ext_cnt++;
			if (ext_cnt > 3) {
				int j;

				for (j = i ; j < i + 3 ; j++) {
					layer_info = &disp_info->input_config[disp_idx][j];
					if (j == (disp_info->layer_num[disp_idx] - 1) ||
						!is_extended_layer(&disp_info->input_config[disp_idx][j+1])) {
						layer_info->ext_sel_layer = -1;
						break;
					}
				}
#ifdef HRT_DEBUG_LEVEL2
				DISPMSG("[%s]cannot feet current layer layout\n", __func__);
				dump_disp_info(disp_info, DISP_DEBUG_LEVEL_ERR);
#endif
				ext_id_tunning(disp_info, disp_idx);
				break;
			}
		} else {
#ifdef HRT_DEBUG_LEVEL2
			DISPMSG("i:%d, cur_phy_cnt:%d\n", i, cur_phy_cnt);
#endif
			if (is_gles_layer(disp_info, disp_idx, i) && (i != disp_info->gles_head[disp_idx])) {
#ifdef HRT_DEBUG_LEVEL2
				DISPMSG("is gles layer, continue\n");
#endif
				continue;
			}
			if (cur_phy_cnt > 0) {
				if (get_ovl_idx_by_phy_layer(layer_mapping_tb, cur_phy_cnt) !=
					get_ovl_idx_by_phy_layer(layer_mapping_tb, cur_phy_cnt - 1))
					ext_cnt = 0;
			}
			cur_phy_cnt++;
		}
	}

	return 0;
}

static int filter_by_ovl_cnt(disp_layer_info *disp_info)
{
	int ret, disp_idx;

	/* 0->primary display, 1->secondary display */
	for (disp_idx = 0 ; disp_idx < 2 ; disp_idx++) {
		if (disp_helper_get_option(DISP_OPT_OVL_EXT_LAYER))
			ret = ext_id_tunning(disp_info, disp_idx);
		else
			ret = _filter_by_ovl_cnt(disp_info, disp_idx);
	}

#ifdef HRT_DEBUG_LEVEL2
	DISPMSG("[%s result]\n", __func__);
	dump_disp_info(disp_info, DISP_DEBUG_LEVEL_INFO);
#endif
	return ret;
}

struct hrt_sort_entry *x_entry_list, *y_entry_list;

int dump_entry_list(bool sort_by_y)
{
	struct hrt_sort_entry *temp;
	layer_config *layer_info;

	if (sort_by_y)
		temp = y_entry_list;
	else
		temp = x_entry_list;

	DISPMSG("%s, sort_by_y:%d, addr:0x%p\n", __func__, sort_by_y, temp);
	while (temp != NULL) {
		layer_info = temp->layer_info;
		DISPMSG("key:%d, offset(%d, %d), w/h(%d, %d), overlap_w:%d\n",
			temp->key, layer_info->dst_offset_x, layer_info->dst_offset_y,
			layer_info->dst_width, layer_info->dst_height, temp->overlap_w);
		temp = temp->tail;
	}
	DISPMSG("dump_entry_list end\n");
	return 0;
}

static int insert_entry(struct hrt_sort_entry **head, struct hrt_sort_entry *sort_entry)
{
	struct hrt_sort_entry *temp;

	temp = *head;
	while (temp != NULL) {
		if (sort_entry->key < temp->key ||
			((sort_entry->key == temp->key) && (sort_entry->overlap_w > 0))) {
			sort_entry->head = temp->head;
			sort_entry->tail = temp;
			if (temp->head != NULL)
				temp->head->tail = sort_entry;
			else
				*head = sort_entry;
			temp->head = sort_entry;
			break;
		}

		if (temp->tail == NULL) {
			temp->tail = sort_entry;
			sort_entry->head = temp;
			sort_entry->tail = NULL;
			break;
		}
		temp = temp->tail;
	}

	return 0;
}

static int add_layer_entry(layer_config *layer_info, bool sort_by_y, int overlap_w)
{
	struct hrt_sort_entry *begin_t, *end_t;
	struct hrt_sort_entry **p_entry;

	begin_t = kzalloc(sizeof(struct hrt_sort_entry), GFP_KERNEL);
	end_t = kzalloc(sizeof(struct hrt_sort_entry), GFP_KERNEL);

	begin_t->head = NULL;
	begin_t->tail = NULL;
	end_t->head = NULL;
	end_t->tail = NULL;
	if (sort_by_y) {
		begin_t->key = layer_info->dst_offset_y;
		end_t->key = layer_info->dst_offset_y + layer_info->dst_height - 1;
		p_entry = &y_entry_list;
	} else {
		begin_t->key = layer_info->dst_offset_x;
		end_t->key = layer_info->dst_offset_x + layer_info->dst_width - 1;
		p_entry = &x_entry_list;
	}

	begin_t->overlap_w = overlap_w;
	begin_t->layer_info = layer_info;
	end_t->overlap_w = -overlap_w;
	end_t->layer_info = layer_info;

	if (*p_entry == NULL) {
		*p_entry = begin_t;
		begin_t->head = NULL;
		begin_t->tail = end_t;
		end_t->head = begin_t;
		end_t->tail = NULL;
	} else {
		/* Inser begin entry */
		insert_entry(p_entry, begin_t);
#ifdef HRT_DEBUG_LEVEL2
		DISPMSG("Insert key:%d\n", begin_t->key);
		dump_entry_list(sort_by_y);
#endif
		/* Inser end entry */
		insert_entry(p_entry, end_t);
#ifdef HRT_DEBUG_LEVEL2
		DISPMSG("Insert key:%d\n", end_t->key);
		dump_entry_list(sort_by_y);
#endif
	}

	return 0;
}

static int remove_layer_entry(layer_config *layer_info, bool sort_by_y)
{
	struct hrt_sort_entry *temp, *free_entry;

	if (sort_by_y)
		temp = y_entry_list;
	else
		temp = x_entry_list;

	while (temp != NULL) {
		if (temp->layer_info == layer_info) {
			free_entry = temp;
			temp = temp->tail;
			if (free_entry->head == NULL) {
				/* Free head entry */
				if (temp != NULL)
					temp->head = NULL;
				if (sort_by_y)
					y_entry_list = temp;
				else
					x_entry_list = temp;
				kfree(free_entry);
			} else {
				free_entry->head->tail = free_entry->tail;
				if (temp)
					temp->head = free_entry->head;
				kfree(free_entry);
			}
		} else {
			temp = temp->tail;
		}
	}
	return 0;
}


static int free_all_layer_entry(bool sort_by_y)
{
	struct hrt_sort_entry *cur_entry, *next_entry;

	if (sort_by_y)
		cur_entry = y_entry_list;
	else
		cur_entry = x_entry_list;

	while (cur_entry != NULL) {
		next_entry = cur_entry->tail;
		kfree(cur_entry);
		cur_entry = next_entry;
	}

	if (sort_by_y)
		y_entry_list = NULL;
	else
		x_entry_list = NULL;

	return 0;
}

static int scan_x_overlap(disp_layer_info *disp_info, int disp_index, int ovl_overlap_limit_w)
{
	struct hrt_sort_entry *tmp_entry;
	int overlap_w_sum, max_overlap;

	overlap_w_sum = 0;
	max_overlap = 0;
	tmp_entry = x_entry_list;
	while (tmp_entry != NULL) {
		overlap_w_sum += tmp_entry->overlap_w;
		max_overlap = (overlap_w_sum > max_overlap) ? overlap_w_sum : max_overlap;
		tmp_entry = tmp_entry->tail;
	}
	return max_overlap;
}

static int scan_y_overlap(disp_layer_info *disp_info, int disp_index, int ovl_overlap_limit_w)
{
	struct hrt_sort_entry *tmp_entry;
	int overlap_w_sum, tmp_overlap, max_overlap;

	overlap_w_sum = 0;
	tmp_overlap = 0;
	max_overlap = 0;
	tmp_entry = y_entry_list;
	while (tmp_entry != NULL) {
		overlap_w_sum += tmp_entry->overlap_w;
		if (tmp_entry->overlap_w > 0)
			add_layer_entry(tmp_entry->layer_info, false, tmp_entry->overlap_w);
		else
			remove_layer_entry(tmp_entry->layer_info, false);

		if (overlap_w_sum > ovl_overlap_limit_w && overlap_w_sum > max_overlap)
			tmp_overlap = scan_x_overlap(disp_info, disp_index, ovl_overlap_limit_w);
		else
			tmp_overlap = overlap_w_sum;

		max_overlap = (tmp_overlap > max_overlap) ? tmp_overlap : max_overlap;
		tmp_entry = tmp_entry->tail;
	}

	return max_overlap;
}

static int get_hrt_level(int sum_overlap_w, int is_larb)
{
	int hrt_level;
	int *bound_table;

	if (is_larb)
		bound_table = l_rule_ops->get_bound_table(DISP_HW_LARB_BOUND_TB);
	else
		bound_table = l_rule_ops->get_bound_table(DISP_HW_EMI_BOUND_TB);
	for (hrt_level = 0 ; hrt_level < HRT_LEVEL_NUM ; hrt_level++) {
		if (bound_table[hrt_level] != -1 && (sum_overlap_w <= bound_table[hrt_level] * 240))
			return hrt_level;
	}
	return hrt_level;
}

static bool has_hrt_limit(disp_layer_info *disp_info, int disp_idx)
{
	if (disp_info->layer_num[disp_idx] <= 0 ||
		disp_info->disp_mode[disp_idx] == DISP_SESSION_DECOUPLE_MIRROR_MODE ||
		disp_info->disp_mode[disp_idx] == DISP_SESSION_DECOUPLE_MODE)
		return false;

	return true;
}

static int get_layer_weight(int disp_idx, layer_config *layer_info)
{
	int bpp, weight;

	if (layer_info)
		bpp = get_bpp(layer_info->src_fmt);
	else
		bpp = 4;
#ifdef CONFIG_MTK_HDMI_SUPPORT
	if (disp_idx == HRT_SECONDARY) {
		struct disp_session_info dispif_info;

		/* For seconary display, set the wight 4K@30 as 2K@60.	*/
		hdmi_get_dev_info(true, &dispif_info);

		if (dispif_info.displayWidth > 2560)
			weight = 120;
		else if (dispif_info.displayWidth > 1920)
			weight = 60;
		else
			weight = 30;

		if (dispif_info.vsyncFPS <= 30)
			weight /= 2;

		return weight * bpp;
	}
#endif

	/* Resize layer weight adjustment */
	if (layer_info && layer_info->dst_width != layer_info->src_width) {
		switch (l_rule_info->scale_rate) {
	/* Do not adjust hrt weight for resize layer unless the resize golden setting ready.*/
#if 0
		case HRT_SCALE_200:
			weight = 23;
			break;
		case HRT_SCALE_200:
			weight = 30;
			break;
		case HRT_SCALE_150:
			weight = 40;
			break;
		case HRT_SCALE_133:
			weight = 45;
			break;
#endif
		default:
			weight = 60;
			break;
		}
	} else {
		weight = 60;
	}

	return weight * bpp;
}

static int _calc_hrt_num(disp_layer_info *disp_info, int disp_index,
				int hrt_type, bool force_scan_y, bool has_dal_layer)
{
	int i, sum_overlap_w, overlap_lower_bound, layer_map;
	int overlap_w, layer_idx, phy_layer_idx, ovl_cnt;
	bool has_gles = false;
	layer_config *layer_info;

/* 1.Initial overlap conditions. */
	sum_overlap_w = 0;
	overlap_lower_bound = l_rule_ops->get_hrt_bound(0, 0) * 240;

	layer_idx = -1;
	ovl_cnt = get_phy_ovl_layer_cnt(disp_info, disp_index);
	layer_map = l_rule_ops->get_mapping_table(DISP_HW_LAYER_TB, ovl_cnt - 1);
	if (l_rule_info->dal_enable) {
		layer_map = l_rule_ops->get_mapping_table(DISP_HW_LAYER_TB, MAX_PHY_OVL_CNT - 1);
		layer_map &= HRT_AEE_LAYER_MASK;
	}

	for (i = 0 ; i < disp_info->layer_num[disp_index] ; i++) {
		int ovl_idx;

		layer_info = &disp_info->input_config[disp_index][i];
		if (disp_info->gles_head[disp_index] == -1 ||
			(i < disp_info->gles_head[disp_index] ||
			i > disp_info->gles_tail[disp_index])) {

			if (hrt_type != HRT_TYPE_EMI) {
				if (layer_idx == -1)
					layer_idx = 0;
				else if (!is_extended_layer(layer_info))
					layer_idx++;

				phy_layer_idx = get_phy_ovl_index(layer_idx);
				ovl_idx = get_ovl_idx_by_phy_layer(layer_map, layer_idx);
				if (get_larb_idx_by_ovl_idx(ovl_idx, disp_index) != hrt_type)
					continue;
			}
			overlap_w = get_layer_weight(disp_index, layer_info);
			sum_overlap_w += overlap_w;
			add_layer_entry(layer_info, true, overlap_w);
		} else if (i == disp_info->gles_head[disp_index]) {
			/* Add GLES layer */
			if (hrt_type != HRT_TYPE_EMI) {
				if (layer_idx == -1)
					layer_idx = 0;
				else if (!is_extended_layer(layer_info))
					layer_idx++;

				phy_layer_idx = get_phy_ovl_index(layer_idx);
				ovl_idx = get_ovl_idx_by_phy_layer(layer_map, layer_idx);

				if (get_larb_idx_by_ovl_idx(ovl_idx, disp_index) != hrt_type)
					continue;
			}
			has_gles = true;
		}
	}
/* Add overlap weight of Gles layer and Assert layer. */
	if (has_gles)
		sum_overlap_w += get_layer_weight(disp_index, NULL);

	if (has_dal_layer)
		sum_overlap_w += 120;

	if (sum_overlap_w > overlap_lower_bound ||
		has_hrt_limit(disp_info, HRT_SECONDARY) ||
		force_scan_y) {
		sum_overlap_w = scan_y_overlap(disp_info, disp_index, overlap_lower_bound);
		/* Add overlap weight of Gles layer and Assert layer. */
		if (has_gles)
			sum_overlap_w += get_layer_weight(disp_index, NULL);
		if (has_dal_layer)
			sum_overlap_w += 120;
	}

#ifdef HRT_DEBUG_LEVEL1
	DISPMSG("%s disp_index:%d, disp_index:%d, hrt_type:%d, sum_overlap_w:%d\n",
		__func__, disp_index, disp_index, hrt_type, sum_overlap_w);
#endif

	free_all_layer_entry(true);
	return sum_overlap_w;
}

#ifdef HAS_LARB_HRT
static int calc_larb_hrt_level(disp_layer_info *disp_info)
{
	int larb_hrt_level, i, sum_overlap_w;

	larb_hrt_level = 0;
	for (i = HRT_TYPE_LARB0 ; i <= HRT_TYPE_LARB1 ; i++) {
		int tmp_hrt_level;

		sum_overlap_w = _calc_hrt_num(disp_info, HRT_PRIMARY, i, true, l_rule_info->dal_enable);
		sum_overlap_w += _calc_hrt_num(disp_info, HRT_SECONDARY, i, true, false);
		tmp_hrt_level = get_hrt_level(sum_overlap_w, true);
		if (tmp_hrt_level > larb_hrt_level)
			larb_hrt_level = tmp_hrt_level;
	}

	return larb_hrt_level;
}
#endif

static int calc_hrt_num(disp_layer_info *disp_info)
{
	int emi_hrt_level;
	int sum_overlap_w = 0;
#ifdef HAS_LARB_HRT
	int larb_hrt_level;
#endif
	/* Calculate HRT for EMI level */
	if (has_hrt_limit(disp_info, HRT_PRIMARY))
		sum_overlap_w = _calc_hrt_num(disp_info, HRT_PRIMARY, HRT_TYPE_EMI, false, l_rule_info->dal_enable);
	if (has_hrt_limit(disp_info, HRT_SECONDARY))
		sum_overlap_w += _calc_hrt_num(disp_info, HRT_SECONDARY, HRT_TYPE_EMI, false, false);


	emi_hrt_level = get_hrt_level(sum_overlap_w, false);
	disp_info->hrt_num = emi_hrt_level;
#ifdef HRT_DEBUG_LEVEL1
	DISPMSG("EMI hrt level2:%d, overlap_w:%d\n", emi_hrt_level, sum_overlap_w);
#endif

#ifdef HAS_LARB_HRT
	/* Need to calculate larb hrt for HRT_LEVEL_LOW level. */
	/* TOBE: Should revise larb calculate statement here */
	/* if (hrt_level != HRT_LEVEL_NUM - 2) */
	/*	return hrt_level; */

	/* Check Larb Bound here */
	larb_hrt_level = calc_larb_hrt_level(disp_info);



#ifdef HRT_DEBUG_LEVEL1
	DISPMSG("Larb hrt level:%d\n", larb_hrt_level);
#endif

	if (emi_hrt_level < larb_hrt_level)
		disp_info->hrt_num = larb_hrt_level;
	else
#endif
		disp_info->hrt_num = emi_hrt_level;

#ifdef HAS_LARB_HRT
	MMProfileLogEx(ddp_mmp_get_events()->hrt, MMProfileFlagPulse, emi_hrt_level, larb_hrt_level);
#else
	MMProfileLogEx(ddp_mmp_get_events()->hrt, MMProfileFlagPulse, emi_hrt_level, 0);
#endif
	return disp_info->hrt_num;
}

static int ext_layer_grouping(disp_layer_info *disp_info)
{
	int cont_ext_layer_cnt = 0, ext_layer_idx = 0;
	int is_ext_layer, disp_idx, i;
	layer_config *src_info, *dst_info;
	int available_layers = 0;

	for (disp_idx = 0 ; disp_idx < 2 ; disp_idx++) {

		/* initialize ext layer info */
		for (i = 0 ; i < disp_info->layer_num[disp_idx]; i++)
			disp_info->input_config[disp_idx][i].ext_sel_layer = -1;

		if (!disp_helper_get_option(DISP_OPT_OVL_EXT_LAYER))
			continue;

		for (i = 1 ; i < disp_info->layer_num[disp_idx]; i++) {
			dst_info = &disp_info->input_config[disp_idx][i];
			src_info = &disp_info->input_config[disp_idx][i-1];
			/* skip other GPU layers */
			if (is_gles_layer(disp_info, disp_idx, i) || is_gles_layer(disp_info, disp_idx, i - 1)) {
				cont_ext_layer_cnt = 0;
				if (i > disp_info->gles_tail[disp_idx])
					ext_layer_idx =
						i - (disp_info->gles_tail[disp_idx] - disp_info->gles_head[disp_idx]);
				continue;
			}

			is_ext_layer = !is_continuous_ext_layer_overlap(disp_info->input_config[disp_idx], i);

			/* The yuv layer is not supported as extended layer as the HWC has a special for
			 * yuv content.
			 */
			if (is_yuv(dst_info->src_fmt))
				is_ext_layer = false;

			if (is_ext_layer && cont_ext_layer_cnt < 3) {
				++cont_ext_layer_cnt;
				dst_info->ext_sel_layer = ext_layer_idx;
			} else {
				cont_ext_layer_cnt = 0;
				ext_layer_idx = i;
				if (i > disp_info->gles_tail[disp_idx])
					ext_layer_idx -=
						(disp_info->gles_tail[disp_idx] - disp_info->gles_head[disp_idx]);
			}
		}
	}

#ifdef HRT_DEBUG_LEVEL1
	DISPMSG("[ext layer grouping]\n");
	dump_disp_info(disp_info, DISP_DEBUG_LEVEL_INFO);
#endif

	return available_layers;
}

static int dispatch_ovl_id(disp_layer_info *disp_info)
{
	int disp_idx, i, j;
	layer_config *layer_info;
	bool has_second_disp;

	if (disp_info->layer_num[0] <= 0 && disp_info->layer_num[1] <= 0)
		return 0;

	if (disp_info->layer_num[1] > 0)
		has_second_disp = true;
	else
		has_second_disp = false;

	/* Dispatch gles range if necessary */
	if (disp_info->hrt_num > HRT_LEVEL_NUM - 1) {
		int valid_ovl_cnt = l_rule_ops->get_hrt_bound(0, HRT_LEVEL_NUM - 1);

		if (l_rule_info->dal_enable)
			valid_ovl_cnt--;

		if (has_hrt_limit(disp_info, HRT_SECONDARY)) {
			int phy_ovl_cnt;

			phy_ovl_cnt = get_ovl_layer_cnt(disp_info, HRT_SECONDARY);
			if (valid_ovl_cnt > phy_ovl_cnt) {
				valid_ovl_cnt -= phy_ovl_cnt;
			} else {
				/* TODO: Adjust gles layer by valid ovl count for seconard display */
				valid_ovl_cnt = 1;
			}
		}

		if (has_hrt_limit(disp_info, HRT_PRIMARY))
			rollback_to_GPU(disp_info, HRT_PRIMARY, valid_ovl_cnt);
		disp_info->hrt_num = HRT_LEVEL_NUM - 1;
	}

	/* Dispatch OVL id */
	for (disp_idx = 0 ; disp_idx < 2 ; disp_idx++) {
		int ovl_cnt, layer_map, layer_idx, ext_cnt, gles_cnt;

		if (disp_info->layer_num[disp_idx] <= 0)
			continue;
		ovl_cnt = get_phy_ovl_layer_cnt(disp_info, disp_idx);
		layer_map = l_rule_ops->get_mapping_table(DISP_HW_LAYER_TB, ovl_cnt - 1);
		if (l_rule_info->dal_enable) {
			layer_map = l_rule_ops->get_mapping_table(DISP_HW_LAYER_TB, MAX_PHY_OVL_CNT - 1);
			layer_map &= HRT_AEE_LAYER_MASK;
		}

		layer_idx = 0;
		ext_cnt = 0;
		gles_cnt = 0;

		if (disp_idx == 0)
			layer_map &= 0x0000FFFF;
		else
			layer_map = (layer_map & 0xFFFF0000) >> 16;

		for (i = 0 ; i < TOTAL_OVL_LAYER_NUM ; i++) {
			if ((layer_map & 0x1) == 0) {
				layer_map >>= 1;
				continue;
			}

			layer_info = &disp_info->input_config[disp_idx][layer_idx];
			layer_info->ovl_id = i + ext_cnt;
			if (is_gles_layer(disp_info, disp_idx, layer_idx)) {
				layer_config *gles_layer_info;

				for (j = disp_info->gles_head[disp_idx] ; j <= disp_info->gles_tail[disp_idx] ; j++) {
					gles_layer_info = &disp_info->input_config[disp_idx][j];
					gles_layer_info->ovl_id = layer_info->ovl_id;
				}
				layer_idx += (disp_info->gles_tail[disp_idx] - disp_info->gles_head[disp_idx]) + 1;
			} else {
				int phy_layer_idx;

				layer_idx++;
				phy_layer_idx = get_phy_ovl_index(i);
				for (j = 0 ; j < 3 ; j++) {
					if (layer_idx >= disp_info->layer_num[disp_idx])
						break;

					layer_info = &disp_info->input_config[disp_idx][layer_idx];
					if (is_extended_layer(layer_info)) {
						ext_cnt++;
						layer_info->ovl_id = i + ext_cnt;
						layer_idx++;
						layer_info->ext_sel_layer = phy_layer_idx;
					} else {
						break;
					}
				}
			}
			if (layer_idx >= disp_info->layer_num[disp_idx])
				break;

			layer_map >>= 1;
		}
	}
	return 0;
}

int check_disp_info(disp_layer_info *disp_info)
{
	int disp_idx;

	if (disp_info == NULL) {
		DISPERR("[HRT]disp_info is empty\n");
		return -1;
	}

	for (disp_idx = 0 ; disp_idx < 2 ; disp_idx++) {

		if (disp_info->layer_num[disp_idx] > 0 &&
			disp_info->input_config[disp_idx] == NULL) {
			DISPERR("[HRT]Has input layer, but input config is empty, disp_idx:%d, layer_num:%d\n",
				disp_idx, disp_info->layer_num[disp_idx]);
			return -1;
		}

		if ((disp_info->gles_head[disp_idx] < 0 && disp_info->gles_tail[disp_idx] >= 0) ||
			(disp_info->gles_tail[disp_idx] < 0 && disp_info->gles_head[disp_idx] >= 0)) {
			dump_disp_info(disp_info, DISP_DEBUG_LEVEL_ERR);
			DISPERR("[HRT]gles layer invalid, disp_idx:%d, head:%d, tail:%d\n",
				disp_idx, disp_info->gles_head[disp_idx], disp_info->gles_tail[disp_idx]);
			return -1;
		}
	}

	return 0;
}

int set_disp_info(disp_layer_info *disp_info_user, int debug_mode)
{

	memcpy(&layering_info, disp_info_user, sizeof(disp_layer_info));

	if (layering_info.layer_num[0]) {
		layering_info.input_config[0] =
			kzalloc(sizeof(layer_config) * layering_info.layer_num[0], GFP_KERNEL);

		if (layering_info.input_config[0] == NULL) {
			DISPERR("[HRT]: alloc input config 0 fail, layer_num:%d\n",
				layering_info.layer_num[0]);
			return -EFAULT;
		}

		if (debug_mode) {
			memcpy(layering_info.input_config[0], disp_info_user->input_config[0],
				sizeof(layer_config) * layering_info.layer_num[0]);
		} else {
			if (copy_from_user(layering_info.input_config[0], disp_info_user->input_config[0],
				sizeof(layer_config) * layering_info.layer_num[0])) {
				DISPERR("[FB]: copy_to_user failed! line:%d\n", __LINE__);
				return -EFAULT;
			}
		}
	}

	if (layering_info.layer_num[1]) {
		layering_info.input_config[1] =
			kzalloc(sizeof(layer_config) * layering_info.layer_num[1], GFP_KERNEL);
		if (layering_info.input_config[1] == NULL) {
			DISPERR("[HRT]: alloc input config 1 fail, layer_num:%d\n",
				layering_info.layer_num[1]);
			return -EFAULT;
		}

		if (debug_mode) {
			memcpy(layering_info.input_config[1], disp_info_user->input_config[1],
				sizeof(layer_config) * layering_info.layer_num[1]);
		} else {
			if (copy_from_user(layering_info.input_config[1], disp_info_user->input_config[1],
				sizeof(layer_config) * layering_info.layer_num[1])) {
				DISPERR("[FB]: copy_to_user failed! line:%d\n", __LINE__);
				return -EFAULT;
			}
		}
	}

	l_rule_info->disp_path = HRT_PATH_UNKNOWN;
	return 0;
}

int copy_layer_info_to_user(disp_layer_info *disp_info_user, int debug_mode)
{
	int ret = 0;

	disp_info_user->hrt_num = layering_info.hrt_num;
	if (layering_info.layer_num[0] > 0) {
		disp_info_user->gles_head[0] = layering_info.gles_head[0];
		disp_info_user->gles_tail[0] = layering_info.gles_tail[0];

		if (debug_mode) {
			memcpy(disp_info_user->input_config[0], layering_info.input_config[0],
				sizeof(layer_config) * disp_info_user->layer_num[0]);
		} else {
			if (copy_to_user(disp_info_user->input_config[0], layering_info.input_config[0],
				sizeof(layer_config) * layering_info.layer_num[0])) {
				DISPERR("[FB]: copy_to_user failed! line:%d\n", __LINE__);
				ret = -EFAULT;
			}
			kfree(layering_info.input_config[0]);
		}
	}

	if (layering_info.layer_num[1] > 0) {
		disp_info_user->gles_head[1] = layering_info.gles_head[1];
		disp_info_user->gles_tail[1] = layering_info.gles_tail[1];
		if (debug_mode) {
			memcpy(disp_info_user->input_config[1], layering_info.input_config[1],
			sizeof(layer_config) * disp_info_user->layer_num[1]);
		} else {
			if (copy_to_user(disp_info_user->input_config[1], layering_info.input_config[1],
				sizeof(layer_config) * layering_info.layer_num[1])) {
				DISPERR("[FB]: copy_to_user failed! line:%d\n", __LINE__);
				ret = -EFAULT;
			}
			kfree(layering_info.input_config[1]);
		}
	}

	return ret;
}

int set_hrt_state(enum HRT_SYS_STATE sys_state, int en)
{
	switch (sys_state) {
	case DISP_HRT_MJC_ON:
		if (en)
			l_rule_info->hrt_sys_state |= (1 << sys_state);
		else
			l_rule_info->hrt_sys_state &= ~(1 << sys_state);
		break;
	case DISP_HRT_FORCE_DUAL_OFF:
		if (en)
			l_rule_info->hrt_sys_state |= (1 << sys_state);
		else
			l_rule_info->hrt_sys_state &= ~(1 << sys_state);
		break;
	case DISP_HRT_MULTI_TUI_ON:
		if (en)
			l_rule_info->hrt_sys_state |= (1 << sys_state);
		else
			l_rule_info->hrt_sys_state &= ~(1 << sys_state);
		break;
	default:
		DISPERR("unknown hrt scenario\n");
	}

	DISPMSG("Set hrt sys_state:%d, en:%d\n", sys_state, en);
	return 0;
}

void register_layering_rule_ops(struct layering_rule_ops *ops, struct layering_rule_info_t *info)
{
	l_rule_ops = ops;
	l_rule_info = info;
}

int layering_rule_start(disp_layer_info *disp_info_user, int debug_mode)
{
	int ret;

	if (l_rule_ops == NULL || l_rule_info == NULL) {
		DISPERR("Layering rule has not been initialize.\n");
		return -EFAULT;
	}

	if (check_disp_info(disp_info_user) < 0) {
		DISPERR("check_disp_info fail\n");
		return -EFAULT;
	}
	if (set_disp_info(disp_info_user, debug_mode))
		return -EFAULT;

	print_disp_info_to_log_buffer(&layering_info);
#ifdef HRT_DEBUG_LEVEL1
	DISPMSG("[Input data]\n");
	dump_disp_info(&layering_info, DISP_DEBUG_LEVEL_INFO);
#endif
	l_rule_info->disp_path = HRT_PATH_UNKNOWN;

	l_rule_info->dal_enable = is_DAL_Enabled();

	if (l_rule_ops->rollback_to_gpu_by_hw_limitation)
		ret = l_rule_ops->rollback_to_gpu_by_hw_limitation(&layering_info);

	/* Check and choose the Resize Scenario */
	if (disp_helper_get_option(DISP_OPT_RSZ)) {
		if (l_rule_ops->resizing_rule)
			ret = l_rule_ops->resizing_rule(&layering_info);
		else
			DISPERR("RSZ feature on, but no resizing rule be implement.\n");
	} else {
		l_rule_info->scale_rate = HRT_SCALE_NONE;
	}

	/* Layer Grouping */
	ret = ext_layer_grouping(&layering_info);
	/* Initial HRT conditions */
	l_rule_ops->scenario_decision(&layering_info);
	/* GLES adjustment and ext layer checking */
	ret = filter_by_ovl_cnt(&layering_info);


	calc_hrt_num(&layering_info);

	ret = dispatch_ovl_id(&layering_info);
#ifdef HRT_DEBUG_LEVEL1
	dump_disp_info(&layering_info, DISP_DEBUG_LEVEL_INFO);
#endif
	HRT_SET_PATH_SCENARIO(layering_info.hrt_num, l_rule_info->disp_path);
	HRT_SET_SCALE_SCENARIO(layering_info.hrt_num, l_rule_info->scale_rate);
	HRT_SET_AEE_FLAG(layering_info.hrt_num, l_rule_info->dal_enable);

	ret = copy_layer_info_to_user(disp_info_user, debug_mode);
	return ret;
}

/**** UT Program ****/
#ifdef HRT_UT_DEBUG
static void debug_set_layer_data(disp_layer_info *disp_info, int disp_id, int data_type, int value)
{
	static int layer_id = -1;
	layer_config *layer_info = NULL;

	if (layer_id != -1)
		layer_info = &disp_info->input_config[disp_id][layer_id];
	else
		return;

	switch (data_type) {
	case HRT_LAYER_DATA_ID:
		layer_id = value;
		break;
	case HRT_LAYER_DATA_SRC_FMT:
		layer_info->src_fmt = value;
		break;
	case HRT_LAYER_DATA_DST_OFFSET_X:
		layer_info->dst_offset_x = value;
		break;
	case HRT_LAYER_DATA_DST_OFFSET_Y:
		layer_info->dst_offset_y = value;
		break;
	case HRT_LAYER_DATA_DST_WIDTH:
		layer_info->dst_width = value;
		break;
	case HRT_LAYER_DATA_DST_HEIGHT:
		layer_info->dst_height = value;
		break;
	case HRT_LAYER_DATA_SRC_WIDTH:
		layer_info->src_width = value;
		break;
	case HRT_LAYER_DATA_SRC_HEIGHT:
		layer_info->src_height = value;
		break;
	default:
		break;
	}
}

static char *parse_hrt_data_value(char *start, long int *value)
{
	char *tok_start = NULL, *tok_end = NULL;
	int ret;

	tok_start = strchr(start + 1, ']');
	tok_end = strchr(tok_start + 1, '[');
	if (tok_end)
		*tok_end = 0;
	ret = kstrtol(tok_start + 1, 10, value);
	if (ret)
		DISPERR("Parsing error gles_num:%d, p:%s, ret:%d\n", (int)*value, tok_start + 1, ret);

	return tok_end;
}

static int load_hrt_test_data(disp_layer_info *disp_info)
{
	char filename[] = "/sdcard/hrt_data.txt";
	char line_buf[512];
	char *tok;
	struct file *filp;
	mm_segment_t oldfs;
	int ret, pos, i;
	long int disp_id, test_case;
	bool is_end = false, is_test_pass = false;

	pos = 0;
	test_case = -1;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	filp = filp_open(filename, O_RDONLY, 0777);
	if (IS_ERR(filp)) {
		DISPERR("File open error:%s\n", filename);
		return -1;
	}

	if (!filp->f_op) {
		DISPERR("File Operation Method Error!!\n");
		return -1;
	}

	while (1) {
		ret = filp->f_op->llseek(filp, filp->f_pos, pos);
		memset(line_buf, 0x0, sizeof(line_buf));
		ret = filp->f_op->read(filp, line_buf, sizeof(line_buf), &filp->f_pos);
		tok = strchr(line_buf, '\n');
		if (tok != NULL)
			*tok = '\0';
		else
			is_end = true;

		pos += strlen(line_buf) + 1;
		filp->f_pos = pos;

		if (strncmp(line_buf, "#", 1) == 0) {
			continue;
		} else if (strncmp(line_buf, "[layer_num]", 11) == 0) {
			unsigned long int layer_num = 0;

			tok = parse_hrt_data_value(line_buf, &layer_num);
			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &disp_id);
			if (layer_num != 0)
				disp_info->input_config[disp_id] =
					kzalloc(sizeof(layer_config) * layer_num, GFP_KERNEL);
			disp_info->layer_num[disp_id] = layer_num;

			if (disp_info->input_config[disp_id] == NULL)
				return 0;
		} else if (strncmp(line_buf, "[set_layer]", 11) == 0) {
			unsigned long int tmp_info;

			tok = strchr(line_buf, ']');
			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &disp_id);
			for (i = 0 ; i < HRT_LAYER_DATA_NUM ; i++) {
				tok = parse_hrt_data_value(tok, &tmp_info);
				debug_set_layer_data(disp_info, disp_id, i, tmp_info);
			}
		} else if (strncmp(line_buf, "[test_start]", 12) == 0) {
			tok = parse_hrt_data_value(line_buf, &test_case);
			layering_rule_start(disp_info, 1);
			is_test_pass = true;
		} else if (strncmp(line_buf, "[test_end]", 10) == 0) {
			kfree(disp_info->input_config[0]);
			kfree(disp_info->input_config[1]);
			memset(disp_info, 0x0, sizeof(disp_layer_info));
			is_end = true;
		} else if (strncmp(line_buf, "[print_out_test_result]", 23) == 0) {
			DISPERR("Test case %d is %s\n", (int)test_case, is_test_pass?"Pass":"Fail");
		} else if (strncmp(line_buf, "[layer_result]", 14) == 0) {
			long int layer_result = 0, layer_id;

			tok = strchr(line_buf, ']');
			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &disp_id);
			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &layer_id);
			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &layer_result);
			if (layer_result != disp_info->input_config[disp_id][layer_id].ovl_id) {
				DISPERR("Test case:%d, ovl_id incorrect, real is %d, expect is %d\n",
					(int)test_case, disp_info->input_config[disp_id][layer_id].ovl_id,
					(int)layer_result);
				is_test_pass = false;
			}
			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &layer_result);
			if (layer_result != disp_info->input_config[disp_id][layer_id].ext_sel_layer) {
				DISPERR("Test case:%d, ext_sel_layer incorrect, real is %d, expect is %d\n",
					(int)test_case, disp_info->input_config[disp_id][layer_id].ext_sel_layer,
					(int)layer_result);
				is_test_pass = false;
			}
		} else if (strncmp(line_buf, "[gles_result]", 13) == 0) {
			long int gles_num = 0;

			tok = strchr(line_buf, ']');
			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &disp_id);
			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &gles_num);
			if (gles_num != disp_info->gles_head[disp_id]) {
				DISPERR("Test case:%d, gles head incorrect, gles head is %d, expect is %d\n",
					(int)test_case, disp_info->gles_head[disp_id], (int)gles_num);
				is_test_pass = false;
			}

			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &gles_num);
			if (gles_num != disp_info->gles_tail[disp_id]) {
				DISPERR("Test case:%d, gles tail incorrect, gles tail is %d, expect is %d\n",
					(int)test_case, disp_info->gles_tail[disp_id], (int)gles_num);
				is_test_pass = false;
			}
		} else if (strncmp(line_buf, "[hrt_result]", 12) == 0) {
			unsigned long int hrt_num = 0;

			tok = parse_hrt_data_value(line_buf, &hrt_num);
			if (hrt_num != HRT_GET_DVFS_LEVEL(disp_info->hrt_num))
				DISPERR("Test case:%d, hrt num incorrect, hrt_num is %d, expect is %d\n",
					(int)test_case, HRT_GET_DVFS_LEVEL(disp_info->hrt_num), (int)hrt_num);

			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &hrt_num);
			if (hrt_num != (HRT_GET_PATH_SCENARIO(disp_info->hrt_num) & 0x1F)) {
				DISPERR("Test case:%d, hrt path incorrect, disp_path is %d, expect is %d\n",
					(int)test_case, HRT_GET_PATH_SCENARIO(disp_info->hrt_num) & 0x1F, (int)hrt_num);
				is_test_pass = false;
			}

			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &hrt_num);
			if (hrt_num != HRT_GET_SCALE_SCENARIO(disp_info->hrt_num)) {
				DISPERR("Test case:%d, hrt scale scenario incorrect, hrt scale is %d, expect is %d\n",
					(int)test_case, HRT_GET_SCALE_SCENARIO(disp_info->hrt_num), (int)hrt_num);
				is_test_pass = false;
			}

		} else if (strncmp(line_buf, "[change_layer_num]", 18) == 0) {
			unsigned long int layer_num = 0;

			tok = parse_hrt_data_value(line_buf, &layer_num);
			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &disp_id);
			disp_info->layer_num[disp_id] = layer_num;
		} else if (strncmp(line_buf, "[force_dual_pipe_off]", 21) == 0) {
			unsigned long int force_off = 0;

			tok = parse_hrt_data_value(line_buf, &force_off);
			set_hrt_state(DISP_HRT_FORCE_DUAL_OFF, force_off);
		} else if (strncmp(line_buf, "[resolution_level]", 18) == 0) {
			unsigned long int resolution_level = 0;

			tok = parse_hrt_data_value(line_buf, &resolution_level);
			debug_resolution_level = resolution_level;
		} else if (strncmp(line_buf, "[set_gles]", 10) == 0) {
			long int gles_num = 0;

			tok = strchr(line_buf, ']');
			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &disp_id);
			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &gles_num);
			disp_info->gles_head[disp_id] = gles_num;

			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &gles_num);
			disp_info->gles_tail[disp_id] = gles_num;
		} else if (strncmp(line_buf, "[disp_mode]", 11) == 0) {
			unsigned long int disp_mode = 0;

			tok = parse_hrt_data_value(line_buf, &disp_mode);
			if (!tok)
				goto end;
			tok = parse_hrt_data_value(tok, &disp_id);
			disp_info->disp_mode[disp_id] = disp_mode;
		}

		if (is_end)
			break;
	}

end:
	filp_close(filp, NULL);
	set_fs(oldfs);
	DISPMSG("end set_fs\n");
	return 0;
}
#endif

int gen_hrt_pattern(void)
{
#ifdef HRT_UT_DEBUG
	disp_layer_info disp_info;
	layer_config *layer_info;
	int i;

	memset(&disp_info, 0x0, sizeof(disp_layer_info));
	disp_info.gles_head[0] = -1;
	disp_info.gles_head[1] = -1;
	disp_info.gles_tail[0] = -1;
	disp_info.gles_tail[1] = -1;
	if (!load_hrt_test_data(&disp_info))
		return 0;

	/* Primary Display */
	disp_info.disp_mode[0] = DISP_SESSION_DIRECT_LINK_MODE;
	disp_info.layer_num[0] = 5;
	disp_info.gles_head[0] = 3;
	disp_info.gles_tail[0] = 5;
	disp_info.input_config[0] = kzalloc(sizeof(layer_config) * 5, GFP_KERNEL);
	layer_info = disp_info.input_config[0];
	for (i = 0 ; i < disp_info.layer_num[0] ; i++)
		layer_info[i].src_fmt = DISP_FORMAT_ARGB8888;

	layer_info = disp_info.input_config[0];
	layer_info[0].dst_offset_x = 0;
	layer_info[0].dst_offset_y = 0;
	layer_info[0].dst_width = 1080;
	layer_info[0].dst_height = 1920;
	layer_info[1].dst_offset_x = 0;
	layer_info[1].dst_offset_y = 0;
	layer_info[1].dst_width = 1080;
	layer_info[1].dst_height = 1920;
	layer_info[2].dst_offset_x = 269;
	layer_info[2].dst_offset_y = 72;
	layer_info[2].dst_width = 657;
	layer_info[2].dst_height = 612;
	layer_info[3].dst_offset_x = 0;
	layer_info[3].dst_offset_y = 0;
	layer_info[3].dst_width = 1080;
	layer_info[3].dst_height = 72;
	layer_info[4].dst_offset_x = 1079;
	layer_info[4].dst_offset_y = 72;
	layer_info[4].dst_width = 1;
	layer_info[4].dst_height = 1704;

	/* Secondary Display */
	disp_info.disp_mode[1] = DISP_SESSION_DIRECT_LINK_MODE;
	disp_info.layer_num[1] = 0;
	disp_info.gles_head[1] = -1;
	disp_info.gles_tail[1] = -1;

	DISPMSG("free test pattern\n");
	kfree(disp_info.input_config[0]);
	msleep(50);
#endif
	return 0;
}
/**** UT Program end ****/

