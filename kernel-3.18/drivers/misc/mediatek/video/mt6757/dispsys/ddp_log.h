
#ifndef _H_DDP_LOG_
#define _H_DDP_LOG_
#include "mt-plat/aee.h"
#include "display_recorder.h"
#include "ddp_debug.h"
#ifndef LOG_TAG
#define LOG_TAG
#endif

#if 0 /* set 1 to output log to mobilelog */
#define DISP_LOG_D(fmt, args...)   pr_debug("[DDP/"LOG_TAG"]"fmt, ##args)
#define DISP_LOG_I(fmt, args...)   pr_debug("[DDP/"LOG_TAG"]"fmt, ##args)
#define DISP_LOG_W(fmt, args...)   pr_warn("[DDP/"LOG_TAG"]"fmt, ##args)

#define DISP_LOG_E(fmt, args...) \
do { \
	pr_err("[DDP/"LOG_TAG"]error:"fmt, ##args); \
	dprec_logger_pr(DPREC_LOGGER_ERROR, fmt, ##args); \
} while (0)

#define DISP_LOG_V(fmt, args...) \
do { \
	if (ddp_debug_dbg_log_level() >= 2) \
		DISP_LOG_I(fmt, ##args); \
} while (0)

#define DDPIRQ(fmt, args...) \
do { \
	if (ddp_debug_irq_log_level()) \
		DISP_LOG_I(fmt, ##args); \
} while (0)

#define DDPDBG(fmt, args...) \
do { \
	if (ddp_debug_dbg_log_level()) \
		DISP_LOG_I(fmt, ##args); \
} while (0)

#define DDPMSG(fmt, args...) DISP_LOG_I(fmt, ##args)
#define DDPERR(fmt, args...) DISP_LOG_E(fmt, ##args)
#define DDPDUMP(fmt, args...) \
do { \
	if (ddp_debug_analysis_to_buffer()) \
			dprec_logger_vdump(fmt, ##args); \
	else \
			pr_warn("[DDP/"LOG_TAG"]"fmt, ##args);\
} while (0)

#ifndef ASSERT
#define ASSERT(expr)                             \
do {                                         \
	if (expr) \
		break;                          \
	pr_err("DDP ASSERT FAILED %s, %d\n", __FILE__, __LINE__); \
	BUG();\
} while (0)
#endif

#define DDPAEE(string, args...) \
do {\
	char str[200]; \
	snprintf(str, 199, "DDP:"string, ##args); \
	aee_kernel_warning_api(__FILE__, __LINE__, DB_OPT_DEFAULT | DB_OPT_MMPROFILE_BUFFER, str, string, ##args);  \
	pr_err("[DDP Error]"string, ##args);  \
} while (0)
#else

#define DISP_LOG_I(fmt, args...)					\
	do {								\
		dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##args);	\
		if (g_mobilelog)					\
			pr_warn("[DDP/"LOG_TAG"]"fmt, ##args);		\
	} while (0)

#define DISP_LOG_V(fmt, args...)					\
	do {								\
		if (ddp_debug_dbg_log_level() >= 2) {			\
			DISP_LOG_I(fmt, ##args);			\
		}							\
	} while (0)

#define DISP_LOG_D(fmt, args...)					\
	do {								\
		if (1/*ddp_debug_dbg_log_level()*/) {			\
			DISP_LOG_I(fmt, ##args);			\
		}							\
	} while (0)

#define DISP_LOG_W(fmt, args...)					\
	do {								\
		dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##args);	\
		pr_warn("[DDP/"LOG_TAG"]warn:"fmt, ##args);		\
	} while (0)

#define DISP_LOG_E(fmt, args...)					\
	do {								\
		dprec_logger_pr(DPREC_LOGGER_ERROR, fmt, ##args);	\
		pr_err("[DDP/"LOG_TAG"]error:"fmt, ##args);		\
	} while (0)

#define DDPIRQ(fmt, args...)						\
	do {								\
		if (ddp_debug_irq_log_level()) {			\
			DISP_LOG_I(fmt, ##args);			\
		}							\
	} while (0)

#define DDPDBG(fmt, args...) DISP_LOG_D(fmt, ##args)

#define DDPMSG(fmt, args...) DISP_LOG_I(fmt, ##args)

#define DDPERR(fmt, args...) DISP_LOG_E(fmt, ##args)


#define DDPDUMP(fmt, ...)						\
	do {								\
		if (ddp_debug_analysis_to_buffer()) {			\
			char log[512] = {'\0'};				\
			scnprintf(log, 511, fmt, ##__VA_ARGS__);	\
			dprec_logger_dump(log);				\
		} else {						\
			dprec_logger_pr(DPREC_LOGGER_DUMP, fmt, ##__VA_ARGS__);	\
			if (g_mobilelog)				\
				pr_warn("[DDP/"LOG_TAG"]"fmt, ##__VA_ARGS__);	\
		}							\
	} while (0)

#ifndef ASSERT
#define ASSERT(expr)					\
	do {						\
		if (expr)				\
			break;				\
		pr_err("DDP ASSERT FAILED %s, %d\n",	\
			__FILE__, __LINE__); BUG();	\
	} while (0)
#endif

#define DDPAEE(string, args...)						\
	do {								\
		char str[200];						\
		snprintf(str, 199, "DDP:"string, ##args);		\
		aee_kernel_warning_api(__FILE__, __LINE__,		\
				DB_OPT_DEFAULT | DB_OPT_MMPROFILE_BUFFER, str, string, ##args);	\
		pr_err("[DDP Error]"string, ##args);			\
	} while (0)
#endif

#endif
