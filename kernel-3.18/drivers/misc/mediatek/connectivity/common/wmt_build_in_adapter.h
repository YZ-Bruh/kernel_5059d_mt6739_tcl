
#ifndef WMT_BUILD_IN_ADAPTER_H
#define WMT_BUILD_IN_ADAPTER_H

#include <mtk_wcn_cmb_stub.h>


typedef int (*wmt_bridge_thermal_query_cb)(void);
#if MTK_WCN_CMB_FOR_SDIO_1V_AUTOK
typedef int (*wmt_bridge_1vautok_for_dvfs_cb)(void);
#endif

struct wmt_platform_bridge {
	wmt_bridge_thermal_query_cb thermal_query_cb;
#if MTK_WCN_CMB_FOR_SDIO_1V_AUTOK
	wmt_bridge_1vautok_for_dvfs_cb autok_cb;
#endif
};

void wmt_export_platform_bridge_register(struct wmt_platform_bridge *cb);
void wmt_export_platform_bridge_unregister(void);


extern unsigned int wifi_irq;
extern pm_callback_t mtk_wcn_cmb_sdio_pm_cb;
extern void *mtk_wcn_cmb_sdio_pm_data;

void wmt_export_mtk_wcn_cmb_sdio_disable_eirq(void);
int wmt_export_mtk_wcn_sdio_irq_flag_set(int flag);

#endif /* WMT_BUILD_IN_ADAPTER_H */
