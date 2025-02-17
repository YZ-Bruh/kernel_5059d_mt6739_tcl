
#ifndef __DDP_AAL_H__
#define __DDP_AAL_H__

#if defined(CONFIG_ARCH_MT6755)
#define AAL_SUPPORT_KERNEL_API            (1)
#endif

#define AAL_HIST_BIN        33	/* [0..32] */
#define AAL_DRE_POINT_NUM   29

#define AAL_SERVICE_FORCE_UPDATE 0x1

enum AAL_ESS_UD_MODE {
	CONFIG_BY_CUSTOM_LIB = 0,
	CONFIG_TO_LCD = 1,
	CONFIG_TO_AMOLED = 2
};

enum AAL_DRE_MODE {
	DRE_EN_BY_CUSTOM_LIB = 0xFFFF,
	DRE_OFF = 0,
	DRE_ON = 1
};

enum AAL_ESS_MODE {
	ESS_EN_BY_CUSTOM_LIB = 0xFFFF,
	ESS_OFF = 0,
	ESS_ON = 1
};

enum AAL_ESS_LEVEL {
	ESS_LEVEL_BY_CUSTOM_LIB = 0xFFFF
};

typedef struct {
	/* DRE */
	int dre_map_bypass;
	/* ESS */
	int cabc_gainlmt[33];
} DISP_AAL_INITREG;

typedef struct {
	unsigned int serviceFlags;
	int backlight;
	int colorHist;
	unsigned int maxHist[AAL_HIST_BIN];
	int requestPartial;
#ifdef AAL_SUPPORT_KERNEL_API
	unsigned int panel_type;
	int essStrengthIndex;
	int ess_enable;
	int dre_enable;
#endif

} DISP_AAL_HIST;

enum DISP_AAL_REFRESH_LATENCY {
	AAL_REFRESH_17MS = 17,
	AAL_REFRESH_33MS = 33
};

typedef struct {
	int DREGainFltStatus[AAL_DRE_POINT_NUM];
	int cabc_fltgain_force;	/* 10-bit ; [0,1023] */
	int cabc_gainlmt[33];
	int FinalBacklight;	/* 10-bit ; [0,1023] */
	int allowPartial;
	int refreshLatency;	/* DISP_AAL_REFRESH_LATENCY */
} DISP_AAL_PARAM;


void disp_aal_on_end_of_frame(void);

extern int aal_dbg_en;
void aal_test(const char *cmd, char *debug_output);
int aal_is_partial_support(void);
int aal_request_partial_support(int partial);

void disp_aal_notify_backlight_changed(int bl_1024);

int aal_is_need_lock(void);

void disp_aal_set_lcm_type(unsigned int panel_type);

#endif
