

#ifndef AUDIO_GLOBAL_H
#define AUDIO_GLOBAL_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <mach/irqs.h>
#include <mt-plat/sync_write.h>
/*#include <linux/xlog.h>*/
/*#include <mach/mt_typedefs.h>*/
#include <linux/types.h>
#include "AudDrv_Def.h"
#include <linux/clk.h>
/* TODO: KC: don't declare unnecessary typdef, just use it */

#define DL_ABNORMAL_CONTROL_MAX (5)

typedef	uint8_t kal_uint8;
typedef	int8_t kal_int8;
typedef	uint32_t kal_uint32;
typedef	int32_t kal_int32;
typedef	uint64_t kal_uint64;
typedef	int64_t kal_int64;

typedef struct {
	kal_uint32 pucPhysBufAddr;
	kal_uint8 *pucVirtBufAddr;
	kal_int32 u4BufferSize;
	kal_int32 u4DataRemained;
	kal_uint32 u4SampleNumMask;	/* sample number mask */
	kal_uint32 u4SamplesPerInt;	/* number of samples to play before interrupting */
	kal_int32 u4WriteIdx;	/* Previous Write Index. */
	kal_int32 u4DMAReadIdx;	/* Previous DMA Read Index. */
	kal_uint32 u4MaxCopySize;
	kal_uint32 u4fsyncflag;
	kal_uint32 uResetFlag;
} AFE_BLOCK_T;

typedef struct substreamList {
	struct snd_pcm_substream *substream;
	volatile kal_uint32 u4MaxCopySize;
	struct substreamList *next;
} substreamList;


typedef struct {
	struct file *flip;
	substreamList *substreamL;
	AFE_BLOCK_T rBlock;
	kal_uint32 MemIfNum;
	bool interruptTrigger;
	spinlock_t substream_lock;
} AFE_MEM_CONTROL_T;

struct pcm_afe_info {
	struct AFE_BLOCK_T *mAfeBlock;
	struct snd_pcm_substream *substream;
};


typedef struct {
	kal_int32 u4BufferSize[DL_ABNORMAL_CONTROL_MAX];
	kal_int32 u4DataRemained[DL_ABNORMAL_CONTROL_MAX];
	kal_int32 u4WriteIdx[DL_ABNORMAL_CONTROL_MAX];          /* Previous Write Index. */
	kal_int32 u4DMAReadIdx[DL_ABNORMAL_CONTROL_MAX];        /* Previous DMA Read Index. */
	kal_int32 u4ConsumedBytes[DL_ABNORMAL_CONTROL_MAX];
	kal_int32 u4HwMemoryIndex[DL_ABNORMAL_CONTROL_MAX];
	kal_int32 pucPhysBufAddr[DL_ABNORMAL_CONTROL_MAX];
	kal_int32 u4UnderflowCnt;
	kal_uint32 MemIfNum[DL_ABNORMAL_CONTROL_MAX];
	unsigned long long IrqLastTimeNs[DL_ABNORMAL_CONTROL_MAX];
	unsigned long long IrqCurrentTimeNs[DL_ABNORMAL_CONTROL_MAX];
	unsigned long long IrqIntervalNs[DL_ABNORMAL_CONTROL_MAX];
	kal_uint32 IrqIntervalLimitMs[DL_ABNORMAL_CONTROL_MAX];
	bool IrqDelayCnt;

} AFE_DL_ABNORMAL_CONTROL_T;

struct mtk_dai {
	bool enable;
	uint32_t sample_rate;
};

typedef struct {
	kal_int8 *pBufferBase;
	kal_int8 *pBufferIndx;
	kal_uint32 u4BufferSize;
	kal_uint32 u4BufferSizeMax;

	kal_int32 u4IsrConsumeSize;
} AFE_DL_ISR_COPY_T;

#endif
