
#ifndef PPG_CONTROL_SETTING_H
#define PPG_CONTROL_SETTING_H

/* Function configure */
#define PPG_CTRL_DRIVER_ON
/* #define LOG_PPG_CONTROL_ENABLE */

/* Version Control */
#define PPG_CTRL_VER_CODE_1     1
#define PPG_CTRL_VER_CODE_2     0
#define PPG_CTRL_VER_CODE_3     0
#define PPG_CTRL_VER_CODE_4     3

/* Configure */
#define PPG_CTRL_CH_MAX 2

/* TIA */
#define PPG_TIA_GAIN_500K 0
#define PPG_TIA_GAIN_250K 1
#define PPG_TIA_GAIN_100K 2
#define PPG_TIA_GAIN_050K 3
#define PPG_TIA_GAIN_025K 4
#define PPG_TIA_GAIN_010K 5
#define PPG_TIA_GAIN_001M 6

/* DC edge */
#define PPG_DC_POS_EDGE     30240 /* The real PPG saturation edge (1476mV) */
#define PPG_DC_NEG_EDGE    -32260 /* The real PPG saturation edge (-1575mV) */
#define PPG_DC_MAXP         27000 /* upper tuning bound */
#define PPG_DC_MAXN        -29000 /* lower tuning bound */

/* minimal/maximal range */
#define PPG_MAX_LED_CURRENT     96  /* IC MAX: 255 */
#define PPG_MIN_LED_CURRENT     8   /* IC MIN: 1 */
#define PPG_MAX_TIA_GAIN        PPG_TIA_GAIN_100K /* IC MAX: 1M */
#define PPG_MIN_TIA_GAIN        PPG_TIA_GAIN_100K /* IC MIN: 10K */
#define PPG_MAX_AMBDAC_CURRENT  7   /* IC MAX: 7 -> 6.5uA */
#define PPG_MIN_AMBDAC_CURRENT  1   /* IC MIN: 0uA */

/* initial setting */
#define PPG_INIT_LED1       40 /*0x25 */
#define PPG_INIT_LED2       40 /* 0x2A */
#define PPG_INIT_AMBDAC1    PPG_MAX_AMBDAC_CURRENT /* LED phase */
#define PPG_INIT_AMBDAC2    PPG_MIN_AMBDAC_CURRENT /* AMB phase */
#define PPG_INIT_TIA1       PPG_TIA_GAIN_100K
#define PPG_INIT_TIA2       PPG_TIA_GAIN_100K

/* ppg_control parameter */
#define PPG_CTRL_FS_OPERATE 16
#define PPG_CTRL_BUF_SIZE (PPG_CTRL_FS_OPERATE) /* 1.0-sec buffer */
#define PPG_SATURATE_HANDLE_COUNT ((PPG_CTRL_FS_OPERATE >> 2) - 1)

#define PPG_DC_ENLARGE_BOUND (PPG_DC_POS_EDGE>>2)
#define PPG_AC_TARGET        256

#define PPG_DC_STEP_H    4000 /* @100K */
#define PPG_DC_STEP      3000 /* PPG DC step/LED DAC code (@100K) */
#define PPG_DC_STEP_L    2000 /* @100K */

#define PPG_LED_STEP_COARSE 8
#define PPG_LED_STEP_FINE   4
#define PPG_LED_STEP_MIN    2

/* Timer */
#define PPG_CONTROL_TIME_LIMIT 14
#define PPG_CONTROL_ALWAYS_ON (-1)

/* States */
#define PPG_CONTROL_STATE_RESET   0
#define PPG_CONTROL_STATE_SAT_P   1
#define PPG_CONTROL_STATE_SAT_N   2
#define PPG_CONTROL_STATE_NON_SAT 3
#define PPG_CONTROL_STATE_INC     4
#define PPG_CONTROL_STATE_DEC     5
#define PPG_CONTROL_STATE_DC_BOUNDARY 6

/* Filter */
#define PPG_CONTROL_HPF_ORDER 2

/*-------------------------*/
/*-- Function definition --*/
/*-------------------------*/
/* call drivers API */
#if defined(PPG_CTRL_DRIVER_ON)
void ppg_ctrl_set_driver_ch(INT32 ch);
#endif /* #if defined(PPG_CTRL_DRIVER_ON) */




#endif /* PPG_CONTROL_SETTING_H */


