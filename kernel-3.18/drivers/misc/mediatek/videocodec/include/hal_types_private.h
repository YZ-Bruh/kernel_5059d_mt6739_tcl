
#ifndef _HAL_TYPES_PRIVATE_H_
#define _HAL_TYPES_PRIVATE_H_

#define DumpReg__   /* /< Dump Reg for debug */
#ifdef DumpReg__
#include <stdio.h>
#endif

#include "val_types_private.h"
#include "hal_types_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADD_QUEUE(queue, index, q_type, q_address, q_offset, q_value, q_mask)       \
	{                                                                                   \
		queue[index].type     = q_type;                                                   \
		queue[index].address  = q_address;                                                \
		queue[index].offset   = q_offset;                                                 \
		queue[index].value    = q_value;                                                  \
		queue[index].mask     = q_mask;                                                   \
		index = index + 1;                                                                \
	}       /* /< ADD QUEUE command */


typedef enum __HAL_CODEC_TYPE_T {
	HAL_CODEC_TYPE_VDEC,                /* /< VDEC */
	HAL_CODEC_TYPE_VENC,                /* /< VENC */
	HAL_CODEC_TYPE_MAX = 0xFFFFFFFF     /* /< MAX Value */
}
HAL_CODEC_TYPE_T;


typedef enum _HAL_CMD_T {
	HAL_CMD_SET_CMD_QUEUE,              /* /< set command queue */
	HAL_CMD_SET_POWER,                  /* /< set power */
	HAL_CMD_SET_ISR,                    /* /< set ISR */
	HAL_CMD_GET_CACHE_CTRL_ADDR,        /* /< get cahce control address */
	HAL_CMD_MAX = 0xFFFFFFFF            /* /< MAX value */
} HAL_CMD_T;


typedef enum _REGISTER_GROUP_T {
	VDEC_SYS,           /* /< VDEC_SYS */
	VDEC_MISC,          /* /< VDEC_MISC */
	VDEC_VLD,           /* /< VDEC_VLD */
	VDEC_VLD_TOP,       /* /< VDEC_VLD_TOP */
	VDEC_MC,            /* /< VDEC_MC */
	VDEC_AVC_VLD,       /* /< VDEC_AVC_VLD */
	VDEC_AVC_MV,        /* /< VDEC_AVC_MV */
	VDEC_HEVC_VLD,      /* /< VDEC_HEVC_VLD */
	VDEC_HEVC_MV,       /* /< VDEC_HEVC_MV */
	VDEC_PP,            /* /< VDEC_PP */
	/* VDEC_SQT, */
	VDEC_VP8_VLD,       /* /< VDEC_VP8_VLD */
	VDEC_VP6_VLD,       /* /< VDEC_VP6_VLD */
	VDEC_VP8_VLD2,      /* /< VDEC_VP8_VLD2 */
	VENC_HW_BASE,       /* /< VENC_HW_BASE */
	VENC_LT_HW_BASE,    /* /< VENC_HW_LT_BASE */
	VENC_MP4_HW_BASE,   /* /< VENC_MP4_HW_BASE */
	VDEC_VP9_VLD,       /* /< VDEC_VP9_VLD*/
	VDEC_UFO,           /* /< VDEC_UFO*/
	VCODEC_MAX          /* /< VCODEC_MAX */
} REGISTER_GROUP_T;


typedef enum _VCODEC_DRV_CMD_TYPE {
	ENABLE_HW_CMD,              /* /< ENABLE_HW_CMD */
	DISABLE_HW_CMD,             /* /< DISABLE_HW_CMD */
	WRITE_REG_CMD,              /* /< WRITE_REG_CMD */
	READ_REG_CMD,               /* /< READ_REG_CMD */
	WRITE_SYSRAM_CMD,           /* /< WRITE_SYSRAM_CMD */
	READ_SYSRAM_CMD,            /* /< READ_SYSRAM_CMD */
	MASTER_WRITE_CMD,           /* /< MASTER_WRITE_CMD */
	WRITE_SYSRAM_RANGE_CMD,     /* /< WRITE_SYSRAM_RANGE_CMD */
	READ_SYSRAM_RANGE_CMD,      /* /< READ_SYSRAM_RANGE_CMD */
	SETUP_ISR_CMD,              /* /< SETUP_ISR_CMD */
	WAIT_ISR_CMD,               /* /< WAIT_ISR_CMD */
	TIMEOUT_CMD,                /* /< TIMEOUT_CMD */
	MB_CMD,                     /* /< MB_CMD */
	POLL_REG_STATUS_CMD,        /* /< POLL_REG_STATUS_CMD */
	END_CMD                     /* /< END_CMD */
} VCODEC_DRV_CMD_TYPE;


typedef struct __VCODEC_DRV_CMD_T *P_VCODEC_DRV_CMD_T;


typedef struct __VCODEC_DRV_CMD_T {
	VAL_UINT32_T type;          /* /< type */
	VAL_ULONG_T  address;       /* /< address */
	VAL_ULONG_T  offset;        /* /< offset */
	VAL_ULONG_T  value;         /* /< value */
	VAL_ULONG_T  mask;          /* /< mask */
} VCODEC_DRV_CMD_T;


typedef struct _HAL_HANDLE_T_ {
	VAL_INT32_T     fd_vdec;            /* /< fd_vdec */
	VAL_INT32_T     fd_venc;            /* /< fd_venc */
	VAL_MEMORY_T    rHandleMem;         /* /< rHandleMem */
	VAL_ULONG_T     mmap[VCODEC_MAX];   /* /< mmap[VCODEC_MAX] */
	VAL_DRIVER_TYPE_T    driverType;    /* /< driverType */
	VAL_UINT32_T    u4TimeOut;          /* /< u4TimeOut */
	VAL_UINT32_T    u4FrameCount;       /* /< u4FrameCount */
#ifdef DumpReg__
	FILE *pf_out;
#endif
	VAL_BOOL_T      bProfHWTime;        /* /< bProfHWTime */
	VAL_UINT64_T    u8HWTime[2];        /* /< u8HWTime */
} HAL_HANDLE_T;


#ifdef __cplusplus
}
#endif

#endif /* #ifndef _HAL_TYPES_PRIVATE_H_ */
