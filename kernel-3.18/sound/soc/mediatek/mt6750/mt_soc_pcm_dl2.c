




#include "AudDrv_Common.h"
#include "AudDrv_Def.h"
#include "AudDrv_Afe.h"
#include "AudDrv_Ana.h"
#include "AudDrv_Clk.h"
#include "AudDrv_Kernel.h"
#include "mt_soc_afe_control.h"
#include "mt_soc_digital_type.h"
#include "mt_soc_pcm_common.h"
#include "AudDrv_Gpio.h"
#include <linux/ftrace.h>


static int fast_dl_hdoutput;
static AFE_MEM_CONTROL_T *pMemControl;
static int mPlaybackSramState;
static struct snd_dma_buffer *Dl2_Playback_dma_buf;

static uint32 UnderflowTime;

static bool StartCheckTime;
static unsigned long PrevTime;
static unsigned long NowTime;

#ifdef AUDIO_DL2_ISR_COPY_SUPPORT
static const int ISRCopyMaxSize = 256*2*4;     /* 256 frames, stereo, 32bit */
static AFE_DL_ISR_COPY_T ISRCopyBuffer = {0};
#endif

const char * const fast_dl_hd_output[] = {"Off", "On"};

static const struct soc_enum fast_dl_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(fast_dl_hd_output), fast_dl_hd_output),
};

static int fast_dl_hdoutput_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s() = %d\n", __func__, fast_dl_hdoutput);
	ucontrol->value.integer.value[0] = fast_dl_hdoutput;
	return 0;
}

static int fast_dl_hdoutput_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	/* pr_debug("%s()\n", __func__); */
	if (ucontrol->value.enumerated.item[0] >
	    ARRAY_SIZE(fast_dl_hd_output)) {
		pr_warn("%s(), return -EINVAL\n", __func__);
		return -EINVAL;
	}

	fast_dl_hdoutput = ucontrol->value.integer.value[0];

	if (GetMemoryPathEnable(Soc_Aud_Digital_Block_MEM_HDMI) == true) {
		pr_debug("return HDMI enabled\n");
		return 0;
	}

	return 0;
}

static int dataTransfer(void *dest, const void *src, uint32_t size);

enum DEBUG_DL2 {
	DEBUG_DL2_LOG               = 1,
	DEBUG_DL2_LOG_DETECT_DTAT   = 2,
	DEBUG_DL2_AEE_UNDERFLOW     = 4,
	DEBUG_DL2_AEE_OTHERS        = 8
};


#define PRINTK_DEBUG_LOG(format, args...) \
{\
	if (unlikely(get_LowLatencyDebug() & DEBUG_DL2_LOG)) \
		pr_debug(format, ##args); \
}


/* void StartAudioPcmHardware(void); */
/* void StopAudioPcmHardware(void); */
static int mtk_soc_dl2_probe(struct platform_device *pdev);
static int mtk_soc_pcm_dl2_close(struct snd_pcm_substream *substream);
static int mtk_asoc_pcm_dl2_new(struct snd_soc_pcm_runtime *rtd);
static int mtk_asoc_dl2_probe(struct snd_soc_platform *platform);

static bool mPrepareDone;
static bool mPlaybackUseSram;

#define USE_RATE        (SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000)
#define USE_RATE_MIN        8000
#define USE_RATE_MAX        192000
#define USE_CHANNELS_MIN     1
#define USE_CHANNELS_MAX    2
#define USE_PERIODS_MIN     512
#define USE_PERIODS_MAX     8192

static const struct snd_kcontrol_new fast_dl_controls[] = {
	SOC_ENUM_EXT("fast_dl_hd_Switch", fast_dl_enum[0],
		    fast_dl_hdoutput_get, fast_dl_hdoutput_set),
};

static struct snd_pcm_hardware mtk_pcm_dl2_hardware = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_RESUME | SNDRV_PCM_INFO_MMAP_VALID),
	.formats = SND_SOC_ADV_MT_FMTS,
	.rates = SOC_HIGH_USE_RATE,
	.rate_min = SOC_HIGH_USE_RATE_MIN,
	.rate_max = SOC_HIGH_USE_RATE_MAX,
	.channels_min = SOC_NORMAL_USE_CHANNELS_MIN,
	.channels_max = SOC_NORMAL_USE_CHANNELS_MAX,
	.buffer_bytes_max = Dl2_MAX_BUFFER_SIZE,
	.period_bytes_max = Dl2_MAX_PERIOD_SIZE,
	.periods_min = SOC_NORMAL_USE_PERIODS_MIN,
	.periods_max = SOC_NORMAL_USE_PERIODS_MAX,
	.fifo_size = 0,
};

static int mtk_pcm_dl2_stop(struct snd_pcm_substream *substream)
{
	PRINTK_DEBUG_LOG("%s\n", __func__);

	StartCheckTime = false;
	if (unlikely(get_LowLatencyDebug())) {
		AFE_BLOCK_T *Afe_Block = &pMemControl->rBlock;

		if (Afe_Block->u4DataRemained < 0) {
			pr_warn("%s, dl2 underflow\n", __func__);
			if (get_LowLatencyDebug() & DEBUG_DL2_AEE_UNDERFLOW)
				AUDIO_AEE("mtk_pcm_dl2_stop - dl2 underflow");
		}
	}

	SetMemoryPathEnable(Soc_Aud_Digital_Block_MEM_DL2, false);

	/* SetIrqMcuCounter(Soc_Aud_IRQ_MCU_MODE_IRQ1_MCU_MODE, 0); */
	irq_remove_user(substream, Soc_Aud_IRQ_MCU_MODE_IRQ1_MCU_MODE);

	/* here start digital part */
	SetConnection(Soc_Aud_InterCon_DisConnect, Soc_Aud_InterConnectionInput_I07,
		      Soc_Aud_InterConnectionOutput_O03);
	SetConnection(Soc_Aud_InterCon_DisConnect, Soc_Aud_InterConnectionInput_I08,
		      Soc_Aud_InterConnectionOutput_O04);

	ClearMemBlock(Soc_Aud_Digital_Block_MEM_DL2);
	return 0;
}

static snd_pcm_uframes_t mtk_pcm_dl2_pointer(struct snd_pcm_substream *substream)
{
	kal_int32 HW_memory_index = 0;
	kal_int32 HW_Cur_ReadIdx = 0;
	kal_uint32 Frameidx = 0;
	kal_int32 Afe_consumed_bytes = 0;
	AFE_BLOCK_T *Afe_Block = &pMemControl->rBlock;
	unsigned long flags;

	/* struct snd_pcm_runtime *runtime = substream->runtime; */
	PRINTK_AUD_DL2(" %s Afe_Block->u4DMAReadIdx = 0x%x\n", __func__, Afe_Block->u4DMAReadIdx);

	spin_lock_irqsave(&pMemControl->substream_lock, flags);

	/* get total bytes to copy */
	/* Frameidx = audio_bytes_to_frame(substream , Afe_Block->u4DMAReadIdx); */
	/* return Frameidx; */

	if (GetMemoryPathEnable(Soc_Aud_Digital_Block_MEM_DL2) == true) {
		HW_Cur_ReadIdx = Afe_Get_Reg(AFE_DL2_CUR);
		if (HW_Cur_ReadIdx == 0) {
			pr_warn("[Auddrv] HW_Cur_ReadIdx ==0\n");
			HW_Cur_ReadIdx = Afe_Block->pucPhysBufAddr;
		}

		HW_memory_index = (HW_Cur_ReadIdx - Afe_Block->pucPhysBufAddr);
		if (HW_memory_index >= Afe_Block->u4DMAReadIdx) {
			Afe_consumed_bytes = HW_memory_index - Afe_Block->u4DMAReadIdx;
		} else {
			Afe_consumed_bytes = Afe_Block->u4BufferSize + HW_memory_index -
			    Afe_Block->u4DMAReadIdx;
		}

#ifdef AUDIO_64BYTE_ALIGN	/* no need to do 64byte align */
		Afe_consumed_bytes = Align64ByteSize(Afe_consumed_bytes);
#endif

		PRINTK_DEBUG_LOG
		("+%s DataRemained:%d, consumed_bytes:%d, HW_memory_index = %d, ReadIdx:%d, WriteIdx:%d\n",
		__func__, Afe_Block->u4DataRemained, Afe_consumed_bytes, HW_memory_index,
		Afe_Block->u4DMAReadIdx, Afe_Block->u4WriteIdx);

		Afe_Block->u4DataRemained -= Afe_consumed_bytes;
		Afe_Block->u4DMAReadIdx += Afe_consumed_bytes;
		Afe_Block->u4DMAReadIdx %= Afe_Block->u4BufferSize;

		PRINTK_DEBUG_LOG
		("-%s DataRemained:%d, consumed_bytes:%d, HW_memory_index = %d, ReadIdx:%d, WriteIdx:%d\n",
		__func__, Afe_Block->u4DataRemained, Afe_consumed_bytes, HW_memory_index,
		Afe_Block->u4DMAReadIdx, Afe_Block->u4WriteIdx);

		if (Afe_Block->u4DataRemained < 0)
			PRINTK_DEBUG_LOG("[AudioWarn] u4DataRemained=0x%x\n", Afe_Block->u4DataRemained);

		spin_unlock_irqrestore(&pMemControl->substream_lock, flags);

		return audio_bytes_to_frame(substream, Afe_Block->u4DMAReadIdx);
	}

	Frameidx = audio_bytes_to_frame(substream, Afe_Block->u4DMAReadIdx);
	spin_unlock_irqrestore(&pMemControl->substream_lock, flags);
	return Frameidx;
}

static void SetDL2Buffer(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	AFE_BLOCK_T *pblock = &pMemControl->rBlock;

	pblock->pucPhysBufAddr = runtime->dma_addr;
	pblock->pucVirtBufAddr = runtime->dma_area;
	pblock->u4BufferSize = runtime->dma_bytes;
	pblock->u4SampleNumMask = 0x001f;	/* 32 byte align */
	pblock->u4WriteIdx = 0;
	pblock->u4DMAReadIdx = 0;
	pblock->u4DataRemained = 0;
	pblock->u4fsyncflag = false;
	pblock->uResetFlag = true;
	pr_warn("SetDL2Buffer u4BufferSize = %d pucVirtBufAddr = %p pucPhysBufAddr = 0x%x\n",
	       pblock->u4BufferSize, pblock->pucVirtBufAddr, pblock->pucPhysBufAddr);
	/* set dram address top hardware */
	Afe_Set_Reg(AFE_DL2_BASE, pblock->pucPhysBufAddr, 0xffffffff);
	Afe_Set_Reg(AFE_DL2_END, pblock->pucPhysBufAddr + (pblock->u4BufferSize - 1), 0xffffffff);
	memset_io((void *)pblock->pucVirtBufAddr, 0, pblock->u4BufferSize);

}

static int mtk_pcm_dl2_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *hw_params)
{
	/* struct snd_dma_buffer *dma_buf = &substream->dma_buffer; */
	int ret = 0;

	PRINTK_AUD_DL2("mtk_pcm_dl2_params\n");

	/* runtime->dma_bytes has to be set manually to allow mmap */
	substream->runtime->dma_bytes = params_buffer_bytes(hw_params);

		substream->runtime->dma_bytes = params_buffer_bytes(hw_params);
		substream->runtime->dma_area = Dl2_Playback_dma_buf->area;
		substream->runtime->dma_addr = Dl2_Playback_dma_buf->addr;
		SetHighAddr(Soc_Aud_Digital_Block_MEM_DL2, true);
		SetDL2Buffer(substream, hw_params);

	PRINTK_AUD_DL2("dma_bytes = %zu dma_area = %p dma_addr = 0x%lx\n",
		      substream->runtime->dma_bytes, substream->runtime->dma_area,
		      (long)substream->runtime->dma_addr);
	return ret;
}

static int mtk_pcm_dl2_hw_free(struct snd_pcm_substream *substream)
{
	PRINTK_AUD_DL2("mtk_pcm_dl2_hw_free\n");
	return 0;
}


static struct snd_pcm_hw_constraint_list constraints_sample_rates = {
	.count = ARRAY_SIZE(soc_high_supported_sample_rates),
	.list = soc_high_supported_sample_rates,
	.mask = 0,
};

static int mtk_pcm_dl2_open(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;

	PRINTK_AUD_DL2("mtk_pcm_dl2_open\n");

		mtk_pcm_dl2_hardware.buffer_bytes_max = GetPLaybackDramSize();
		mPlaybackSramState = SRAM_STATE_PLAYBACKDRAM;
		mPlaybackUseSram = false;
	if (mPlaybackSramState == SRAM_STATE_PLAYBACKDRAM)
		AudDrv_Emi_Clk_On();

	pr_warn("mtk_pcm_dl2_hardware.buffer_bytes_max = %zu mPlaybackSramState = %d\n",
	       mtk_pcm_dl2_hardware.buffer_bytes_max, mPlaybackSramState);
	runtime->hw = mtk_pcm_dl2_hardware;

	AudDrv_Clk_On();
	memcpy((void *)(&(runtime->hw)), (void *)&mtk_pcm_dl2_hardware,
	       sizeof(struct snd_pcm_hardware));
	pMemControl = Get_Mem_ControlT(Soc_Aud_Digital_Block_MEM_DL2);

	ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					 &constraints_sample_rates);

	if (ret < 0)
		pr_err("snd_pcm_hw_constraint_integer failed\n");

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		pr_warn("SNDRV_PCM_STREAM_PLAYBACK mtkalsa_dl2playback_constraints\n");
	else
		pr_warn("SNDRV_PCM_STREAM_CAPTURE mtkalsa_dl2playback_constraints\n");

	if (ret < 0) {
		pr_err("ret < 0 mtk_soc_pcm_dl2_close\n");
		mtk_soc_pcm_dl2_close(substream);
		return ret;
	}

#ifdef AUDIO_DL2_ISR_COPY_SUPPORT
	if (!ISRCopyBuffer.pBufferBase) {
		ISRCopyBuffer.pBufferBase = kmalloc(ISRCopyMaxSize, GFP_KERNEL);
		if (!ISRCopyBuffer.pBufferBase)
			pr_err("%s alloc ISRCopyBuffer fail\n", __func__);
		else
			ISRCopyBuffer.u4BufferSizeMax = ISRCopyMaxSize;
	}
#endif

	/* PRINTK_AUD_DL2("mtk_pcm_dl2_open return\n"); */
	return 0;
}

static int mtk_soc_pcm_dl2_close(struct snd_pcm_substream *substream)
{
	pr_debug("%s\n", __func__);

	if (mPrepareDone == true) {
		/* stop DAC output */
		SetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_OUT_DAC, false);
		if (GetI2SDacEnable() == false) {
			SetI2SADDAEnable(false);
			SetI2SDacEnable(false);
		}


		RemoveMemifSubStream(Soc_Aud_Digital_Block_MEM_DL2, substream);

		EnableAfe(false);

		if (fast_dl_hdoutput == true) {
			/* here to open APLL */
			EnableI2SDivPower(AUDIO_APLL12_DIV2, false);
			EnableI2SDivPower(AUDIO_APLL12_DIV4, false);
			if (!mtk_soc_always_hd)
				DisableALLbySampleRate(substream->runtime->rate);
		}

		mPrepareDone = false;
	}

	if (mPlaybackSramState == SRAM_STATE_PLAYBACKDRAM)
		AudDrv_Emi_Clk_Off();

	AfeControlSramLock();
	ClearSramState(mPlaybackSramState);
	mPlaybackSramState = GetSramState();
	AfeControlSramUnLock();
	AudDrv_Clk_Off();

#ifdef AUDIO_DL2_ISR_COPY_SUPPORT
	kfree(ISRCopyBuffer.pBufferBase);
	memset(&ISRCopyBuffer, 0, sizeof(ISRCopyBuffer));
#endif

	return 0;
}

static int mtk_pcm_dl2_prepare(struct snd_pcm_substream *substream)
{
	bool mI2SWLen = Soc_Aud_I2S_WLEN_WLEN_16BITS;
	struct snd_pcm_runtime *runtime = substream->runtime;

	pr_debug("%s\n", __func__);

	if (mPrepareDone == false) {
		pr_info(
			"%s format = %d SNDRV_PCM_FORMAT_S32_LE = %d SNDRV_PCM_FORMAT_U32_LE = %d, fast_dl_hdoutput = %d\n",
			__func__, runtime->format, SNDRV_PCM_FORMAT_S32_LE,
			SNDRV_PCM_FORMAT_U32_LE, fast_dl_hdoutput);
		SetMemifSubStream(Soc_Aud_Digital_Block_MEM_DL2, substream);

		if (runtime->format == SNDRV_PCM_FORMAT_S32_LE ||
		    runtime->format == SNDRV_PCM_FORMAT_U32_LE) {
			/* not support 24bit +++ */
			SetMemIfFetchFormatPerSample(Soc_Aud_Digital_Block_MEM_DL2,
						     AFE_WLEN_32_BIT_ALIGN_8BIT_0_24BIT_DATA);
			SetoutputConnectionFormat(OUTPUT_DATA_FORMAT_24BIT,
						  Soc_Aud_InterConnectionOutput_O03);
			SetoutputConnectionFormat(OUTPUT_DATA_FORMAT_24BIT,
						  Soc_Aud_InterConnectionOutput_O04);
			/* not support 24bit --- */
			mI2SWLen = Soc_Aud_I2S_WLEN_WLEN_32BITS;
		} else {
			/* not support 24bit +++ */
			SetMemIfFetchFormatPerSample(Soc_Aud_Digital_Block_MEM_DL2,
						     AFE_WLEN_16_BIT);
			SetoutputConnectionFormat(OUTPUT_DATA_FORMAT_16BIT,
						  Soc_Aud_InterConnectionOutput_O03);
			SetoutputConnectionFormat(OUTPUT_DATA_FORMAT_16BIT,
						  Soc_Aud_InterConnectionOutput_O04);
			/* not support 24bit --- */
			mI2SWLen = Soc_Aud_I2S_WLEN_WLEN_16BITS;
		}

		SetSampleRate(Soc_Aud_Digital_Block_MEM_I2S, runtime->rate);

		if (fast_dl_hdoutput == true) {
			/* here to open APLL */
			if (!mtk_soc_always_hd)
				EnableALLbySampleRate(runtime->rate);

			SetCLkMclk(Soc_Aud_I2S1, runtime->rate);
			SetCLkMclk(Soc_Aud_I2S3, runtime->rate);
		}

		/* start I2S DAC out */
		if (GetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_OUT_DAC) == false) {
			SetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_OUT_DAC, true);
			SetI2SDacOut(substream->runtime->rate, fast_dl_hdoutput, mI2SWLen);
			SetI2SDacEnable(true);
		} else {
			SetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_OUT_DAC, true);
		}

		if (fast_dl_hdoutput == true) {
			EnableI2SDivPower(AUDIO_APLL12_DIV2, true);
			EnableI2SDivPower(AUDIO_APLL12_DIV4, true);
		}

		EnableAfe(true);
		mPrepareDone = true;
	}
	return 0;
}


static int mtk_pcm_dl2_start(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	pr_debug("%s\n", __func__);
	/* here start digital part */

	SetConnection(Soc_Aud_InterCon_Connection, Soc_Aud_InterConnectionInput_I07,
		      Soc_Aud_InterConnectionOutput_O03);
	SetConnection(Soc_Aud_InterCon_Connection, Soc_Aud_InterConnectionInput_I08,
		      Soc_Aud_InterConnectionOutput_O04);

#ifdef CONFIG_MTK_FPGA
	/* set loopback test interconnection */
	SetConnection(Soc_Aud_InterCon_Connection, Soc_Aud_InterConnectionInput_I07,
		      Soc_Aud_InterConnectionOutput_O09);
	SetConnection(Soc_Aud_InterCon_Connection, Soc_Aud_InterConnectionInput_I08,
		      Soc_Aud_InterConnectionOutput_O10);
#endif

	/* here to set interrupt */
	irq_add_user(substream,
		     Soc_Aud_IRQ_MCU_MODE_IRQ1_MCU_MODE,
		     runtime->rate, runtime->period_size);

	SetSampleRate(Soc_Aud_Digital_Block_MEM_DL2, runtime->rate);
	SetChannels(Soc_Aud_Digital_Block_MEM_DL2, runtime->channels);
	SetMemoryPathEnable(Soc_Aud_Digital_Block_MEM_DL2, true);

	EnableAfe(true);
	if (GetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_OUT_DAC) == true)
		SetI2SADDAEnable(true);

	StartCheckTime = true;
	PrevTime = NowTime = 0;

	UnderflowTime = runtime->period_size * runtime->periods * 1000000000 / runtime->rate;

	return 0;
}

static int mtk_pcm_dl2_trigger(struct snd_pcm_substream *substream, int cmd)
{
	PRINTK_AUD_DL2("mtk_pcm_trigger cmd = %d\n", cmd);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		return mtk_pcm_dl2_start(substream);
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		return mtk_pcm_dl2_stop(substream);
	}
	return -EINVAL;
}

static int mtk_pcm_dl2_copy_(void __user *dst, snd_pcm_uframes_t *size, AFE_BLOCK_T *Afe_Block, bool bCopy)
{
	int copy_size = 0, Afe_WriteIdx_tmp;
	unsigned long flags;
	/* struct snd_pcm_runtime *runtime = substream->runtime; */
	char *data_w_ptr = (char *)dst;

	snd_pcm_uframes_t count = *size;

	PRINTK_AUD_DL2("AudDrv_write WriteIdx=0x%x, ReadIdx=0x%x, DataRemained=0x%x\n",
		       Afe_Block->u4WriteIdx, Afe_Block->u4DMAReadIdx, Afe_Block->u4DataRemained);

	if (Afe_Block->u4BufferSize == 0) {
		pr_err("AudDrv_write: u4BufferSize=0 Error");
		return 0;
	}

	spin_lock_irqsave(&pMemControl->substream_lock, flags);
	copy_size = Afe_Block->u4BufferSize - Afe_Block->u4DataRemained;	/* free space of the buffer */
	spin_unlock_irqrestore(&pMemControl->substream_lock, flags);
	if (count <= copy_size) {
		if (copy_size < 0)
			copy_size = 0;
		else
			copy_size = count;
	} else {
		if (unlikely(get_LowLatencyDebug() & DEBUG_DL2_AEE_OTHERS)) {
			pr_warn("%s, Insufficient data !\n", __func__);
			AUDIO_AEE("ISRCopy has remaining data !!");
		}
	}

#ifdef AUDIO_64BYTE_ALIGN	/* no need to do 64byte align */
	copy_size = Align64ByteSize(copy_size);
#endif
	*size = copy_size;
	PRINTK_DEBUG_LOG("%s, copy_size=%d, count=%d, bCopy %d, %pf %pf %pf %pf\n", __func__, copy_size,
		(unsigned int)count, bCopy, (void *)CALLER_ADDR0, (void *)CALLER_ADDR1,
		(void *)CALLER_ADDR2, (void *)CALLER_ADDR3);

	PRINTK_DEBUG_LOG("AudDrv_write DataRemained:%d, ReadIdx=%d, WriteIdx:%d\r\n",
		Afe_Block->u4DataRemained, Afe_Block->u4DMAReadIdx, Afe_Block->u4WriteIdx);


	if (copy_size != 0) {
		spin_lock_irqsave(&pMemControl->substream_lock, flags);
		Afe_WriteIdx_tmp = Afe_Block->u4WriteIdx;
		spin_unlock_irqrestore(&pMemControl->substream_lock, flags);

		PRINTK_AUD_DL2("Afe_WriteIdx_tmp %d, copy_size %d, u4BufferSize %d\n",
			Afe_WriteIdx_tmp, copy_size, Afe_Block->u4BufferSize);

		if (Afe_WriteIdx_tmp + copy_size < Afe_Block->u4BufferSize) {	/* copy once */
			if (bCopy) {
				if (dataTransfer((Afe_Block->pucVirtBufAddr + Afe_WriteIdx_tmp), data_w_ptr,
						copy_size) == -1)
					return -1;
			}

			spin_lock_irqsave(&pMemControl->substream_lock, flags);
			Afe_Block->u4DataRemained += copy_size;
			Afe_Block->u4WriteIdx = Afe_WriteIdx_tmp + copy_size;
			Afe_Block->u4WriteIdx %= Afe_Block->u4BufferSize;
			spin_unlock_irqrestore(&pMemControl->substream_lock, flags);
			data_w_ptr += copy_size;
			count -= copy_size;

			PRINTK_DEBUG_LOG("AudDrv_write finish1, DataRemained:%d, ReadIdx=%d, WriteIdx:%d\r\n",
				Afe_Block->u4DataRemained, Afe_Block->u4DMAReadIdx, Afe_Block->u4WriteIdx);

		} else {	/* copy twice */
			kal_uint32 size_1 = 0, size_2 = 0;
#ifdef AUDIO_64BYTE_ALIGN	/* no need to do 64byte align */
			size_1 = Align64ByteSize((Afe_Block->u4BufferSize - Afe_WriteIdx_tmp));
			size_2 = Align64ByteSize((copy_size - size_1));
#else
			size_1 = Afe_Block->u4BufferSize - Afe_WriteIdx_tmp;
			size_2 = copy_size - size_1;
#endif
			PRINTK_AUD_DL2("size_1=0x%x, size_2=0x%x\n", size_1, size_2);
			if (bCopy) {
				if (dataTransfer((Afe_Block->pucVirtBufAddr + Afe_WriteIdx_tmp),
						data_w_ptr, size_1) == -1)
					return -1;
			}
			spin_lock_irqsave(&pMemControl->substream_lock, flags);
			Afe_Block->u4DataRemained += size_1;
			Afe_Block->u4WriteIdx = Afe_WriteIdx_tmp + size_1;
			Afe_Block->u4WriteIdx %= Afe_Block->u4BufferSize;
			Afe_WriteIdx_tmp = Afe_Block->u4WriteIdx;
			spin_unlock_irqrestore(&pMemControl->substream_lock, flags);

			if (bCopy) {
				if (dataTransfer((Afe_Block->pucVirtBufAddr + Afe_WriteIdx_tmp),
						(data_w_ptr + size_1), size_2) == -1)
					return -1;
			}
			spin_lock_irqsave(&pMemControl->substream_lock, flags);

			Afe_Block->u4DataRemained += size_2;
			Afe_Block->u4WriteIdx = Afe_WriteIdx_tmp + size_2;
			Afe_Block->u4WriteIdx %= Afe_Block->u4BufferSize;
			spin_unlock_irqrestore(&pMemControl->substream_lock, flags);
			count -= copy_size;
			data_w_ptr += copy_size;

			PRINTK_DEBUG_LOG("AudDrv_write finish2, DataRemained:%d, ReadIdx=%d, WriteIdx:%d\r\n",
				Afe_Block->u4DataRemained, Afe_Block->u4DMAReadIdx, Afe_Block->u4WriteIdx);
		}
	}
	return 0;
}

#ifdef AUDIO_DL2_ISR_COPY_SUPPORT

static void detectData(char *ptr, unsigned long count)
{
	if (get_LowLatencyDebug() & DEBUG_DL2_LOG_DETECT_DTAT) {
		int i;

		for (i = 0; i < count; i++) {
			if (ptr[i]) {
				pr_debug("mtk_pcm_dl2_copy has data, i %d /%ld\n", i, count);
				break;
			}
		}
		if (i == count)
			pr_debug("mtk_pcm_dl2_copy no data\n");
	}
}

static int dataTransfer(void *dest, const void *src, uint32_t size)
{
	if (unlikely(!dest || !src)) {
		pr_err("%s, fail. dest %p, src %p\n", __func__, dest, src);
		return 0;
	}

	memcpy(dest, src, size);
	return 0;
}

void mtk_dl2_copy2buffer(const void *addr, uint32_t size)
{
	bool again = false;

	PRINTK_DEBUG_LOG("%s, addr 0x%p 0x%p, size %d %d\n", __func__, addr,
			ISRCopyBuffer.pBufferBase, size, ISRCopyBuffer.u4BufferSize);

#ifdef AUDIO_64BYTE_ALIGN	/* no need to do 64byte align */
	size = Align64ByteSize(size);
#endif

	Auddrv_Dl2_Spinlock_lock();
retry:

	if (unlikely(ISRCopyBuffer.u4BufferSize)) {
		pr_warn("%s, remaining data %d\n", __func__, ISRCopyBuffer.u4BufferSize);
		if (unlikely(get_LowLatencyDebug() & DEBUG_DL2_AEE_OTHERS))
			AUDIO_AEE("ISRCopy has remaining data !!");
	}

	if (unlikely(!ISRCopyBuffer.pBufferBase || size > ISRCopyBuffer.u4BufferSizeMax)) {
		if (!again) {
			/* realloc memory */
			kfree(ISRCopyBuffer.pBufferBase);
			ISRCopyBuffer.pBufferBase = kmalloc(size, GFP_ATOMIC);
			if (!ISRCopyBuffer.pBufferBase)
				pr_err("%s, alloc ISRCopyBuffer fail, size %d\n", __func__, size);
			else
				ISRCopyBuffer.u4BufferSizeMax = size;

			again = true;
			goto retry;
		}
		pr_err("%s, alloc ISRCopyBuffer fail, again %d!!\n", __func__, again);
		goto exit;
	}

	if (unlikely(copy_from_user(ISRCopyBuffer.pBufferBase, (char *)addr, size))) {
		pr_warn("%s Fail copy from user !!\n", __func__);
		goto exit;
	}
	ISRCopyBuffer.pBufferIndx = ISRCopyBuffer.pBufferBase;
	ISRCopyBuffer.u4BufferSize = size;
	ISRCopyBuffer.u4IsrConsumeSize = 0;    /* Restart */
exit:
	Auddrv_Dl2_Spinlock_unlock();
}

void mtk_dl2_copy_l(void)
{
	AFE_BLOCK_T Afe_Block = pMemControl->rBlock;
	snd_pcm_uframes_t count = ISRCopyBuffer.u4BufferSize;

	if (unlikely(!ISRCopyBuffer.u4BufferSize || !ISRCopyBuffer.pBufferIndx))
		return;

	/* for debug */
	detectData((char *)ISRCopyBuffer.pBufferIndx, count);

	mtk_pcm_dl2_copy_((void *)ISRCopyBuffer.pBufferIndx, &count, &Afe_Block, true);

	ISRCopyBuffer.pBufferIndx += count;
	ISRCopyBuffer.u4BufferSize -= count;
	ISRCopyBuffer.u4IsrConsumeSize += count;
}

static int mtk_pcm_dl2_copy(struct snd_pcm_substream *substream,
			int channel, snd_pcm_uframes_t pos,
			void __user *dst, snd_pcm_uframes_t count)
{
	AFE_BLOCK_T *Afe_Block = &pMemControl->rBlock;
	int remainCount = 0;
	int ret = 0;
	int retryCount = 0;

	PRINTK_AUD_DL2("%s pos = %lu count = %lu, BufferSize %d, ConsumeSize %d\n", __func__, pos,
		count, ISRCopyBuffer.u4BufferSize, ISRCopyBuffer.u4IsrConsumeSize);
	/* get total bytes to copy */
	count = audio_frame_to_bytes(substream, count);

	Auddrv_Dl2_Spinlock_lock();

	if (unlikely(!ISRCopyBuffer.pBufferIndx))
		goto exit;

	if (unlikely(get_LowLatencyDebug())) {
		/* check underflow */
		if (StartCheckTime) {
			if (PrevTime == 0)
				PrevTime = sched_clock();
			NowTime = sched_clock();

			if ((NowTime - PrevTime) > UnderflowTime) {
				pr_warn("%s, dl2 underflow, UnderflowTime %d, start %ld, end %ld\n",
					__func__, UnderflowTime, PrevTime, NowTime);
				if (get_LowLatencyDebug() & DEBUG_DL2_AEE_UNDERFLOW)
					AUDIO_AEE("mtk_pcm_dl2_copy - dl2 underflow");
			}
			PrevTime = NowTime;
		}
	}

retry:
	if (ISRCopyBuffer.u4IsrConsumeSize <= 0) {
		if (!ISRCopyBuffer.u4BufferSize)
			goto exit;

		if (unlikely(ISRCopyBuffer.u4BufferSize < count))
			count = ISRCopyBuffer.u4BufferSize;

		/* for debug */
		detectData((char *)ISRCopyBuffer.pBufferIndx, count);

		ret = mtk_pcm_dl2_copy_((void *)ISRCopyBuffer.pBufferIndx, &count, Afe_Block, true);

		ISRCopyBuffer.pBufferIndx += count;
		ISRCopyBuffer.u4BufferSize -= count;
	} else {
		if (unlikely(ISRCopyBuffer.u4IsrConsumeSize < count)) {
			remainCount = count - ISRCopyBuffer.u4IsrConsumeSize;
			count = ISRCopyBuffer.u4IsrConsumeSize;
		}

		ret = mtk_pcm_dl2_copy_((void *)ISRCopyBuffer.pBufferIndx, &count, Afe_Block, false);
		ISRCopyBuffer.u4IsrConsumeSize -= count;
		if (ISRCopyBuffer.u4IsrConsumeSize < 0)
			ISRCopyBuffer.u4IsrConsumeSize = 0;

		if (unlikely(remainCount)) {
			if ((++retryCount) > 1) {
				pr_debug("%s, retryCount %d, remainCount %d, count %d\n", __func__,
					retryCount, remainCount, (int)count);
				if (retryCount > 5)
					goto exit;
			}
			count = remainCount;
			goto retry;
		}
	}

exit:
	Auddrv_Dl2_Spinlock_unlock();

	return ret;
}
#else

static int dataTransfer(void *dest, const void *src, uint32_t size)
{
	int ret = 0;

	if (unlikely(!access_ok(VERIFY_READ, src, size))) {
		PRINTK_AUD_DL2("AudDrv_write 0ptr invalid data_w_ptr=%p, size=%d\n", src, size);
	} else {
		PRINTK_AUD_DL2
			("memcpy VirtBufAddr+Afe_WriteIdx= %p,data_w_ptr = %p copy_size = 0x%x\n",
			dest, src, size);
		if (unlikely(copy_from_user(dest, src, size))) {
			PRINTK_AUD_DL2("AudDrv_write Fail copy from user\n");
			ret = -1;
		}
	}
	return ret;
}

static int mtk_pcm_dl2_copy(struct snd_pcm_substream *substream,
			int channel, snd_pcm_uframes_t pos,
			void __user *dst, snd_pcm_uframes_t count)
{
	AFE_BLOCK_T *Afe_Block = &pMemControl->rBlock;

	PRINTK_AUD_DL2("mtk_pcm_dl2_copy pos = %lu count = %lu\n", pos, count);
	/* get total bytes to copy */
	count = audio_frame_to_bytes(substream, count);
	PRINTK_AUD_DL2("mtk_pcm_dl2_copy+ pos = %lu count = %lu\n", pos, count);
	return mtk_pcm_dl2_copy_(dst, &count, Afe_Block, true);
}

#endif

static int mtk_pcm_dl2_silence(struct snd_pcm_substream *substream,
			   int channel, snd_pcm_uframes_t pos, snd_pcm_uframes_t count)
{
	PRINTK_AUD_DL2("%s\n", __func__);
	return 0;		/* do nothing */
}

static void *dummy_page[2];

static struct page *mtk_pcm_dl2_page(struct snd_pcm_substream *substream, unsigned long offset)
{
	PRINTK_AUD_DL2("%s\n", __func__);
	return virt_to_page(dummy_page[substream->stream]);	/* the same page */
}

static struct snd_pcm_ops mtk_dl2_ops = {
	.open = mtk_pcm_dl2_open,
	.close = mtk_soc_pcm_dl2_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = mtk_pcm_dl2_params,
	.hw_free = mtk_pcm_dl2_hw_free,
	.prepare = mtk_pcm_dl2_prepare,
	.trigger = mtk_pcm_dl2_trigger,
	.pointer = mtk_pcm_dl2_pointer,
	.copy = mtk_pcm_dl2_copy,
	.silence = mtk_pcm_dl2_silence,
	.page = mtk_pcm_dl2_page,
};

static struct snd_soc_platform_driver mtk_soc_platform = {
	.ops = &mtk_dl2_ops,
	.pcm_new = mtk_asoc_pcm_dl2_new,
	.probe = mtk_asoc_dl2_probe,
};

#ifdef CONFIG_OF

static const struct of_device_id mt_soc_pcm_dl2_of_ids[] = {
	{.compatible = "mediatek,mt_soc_pcm_dl2",},
	{}
};

#endif

static int mtk_soc_dl2_probe(struct platform_device *pdev)
{
	PRINTK_AUD_DL2("%s\n", __func__);

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(64);
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	if (pdev->dev.of_node) {
		dev_set_name(&pdev->dev, "%s", MT_SOC_DL2_PCM);
	} else {
		pr_err("%s invalid of_node\n", __func__);
		return -ENODEV;
	}

	pr_warn("%s: dev name %s\n", __func__, dev_name(&pdev->dev));


	return snd_soc_register_platform(&pdev->dev, &mtk_soc_platform);
}

static int mtk_asoc_pcm_dl2_new(struct snd_soc_pcm_runtime *rtd)
{
	int ret = 0;

	PRINTK_AUD_DL2("%s\n", __func__);
	return ret;
}


static int mtk_asoc_dl2_probe(struct snd_soc_platform *platform)
{
	PRINTK_AUD_DL2("mtk_asoc_dl2_probe\n");

	snd_soc_add_platform_controls(platform, fast_dl_controls,
				      ARRAY_SIZE(fast_dl_controls));

	/* allocate dram */
	AudDrv_Allocate_mem_Buffer(platform->dev, Soc_Aud_Digital_Block_MEM_DL2,
				   Dl2_MAX_BUFFER_SIZE);
	Dl2_Playback_dma_buf = Get_Mem_Buffer(Soc_Aud_Digital_Block_MEM_DL2);
	return 0;
}

static int mtk_soc_dl2_remove(struct platform_device *pdev)
{
	PRINTK_AUD_DL2("%s\n", __func__);
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver mtk_dl2_driver = {
	.driver = {
		   .name = MT_SOC_DL2_PCM,
		   .owner = THIS_MODULE,
#ifdef CONFIG_OF
		   .of_match_table = mt_soc_pcm_dl2_of_ids,
#endif
		   },
	.probe = mtk_soc_dl2_probe,
	.remove = mtk_soc_dl2_remove,
};

#ifndef CONFIG_OF
static struct platform_device *soc_mtkdl2_dev;
#endif

static int __init mtk_dl2_soc_platform_init(void)
{
	int ret;

	PRINTK_AUD_DL2("%s\n", __func__);

#ifndef CONFIG_OF
	soc_mtkdl2_dev = platform_device_alloc(MT_SOC_DL2_PCM, -1);
	if (!soc_mtkdl2_dev)
		return -ENOMEM;


	ret = platform_device_add(soc_mtkdl2_dev);
	if (ret != 0) {
		platform_device_put(soc_mtkdl2_dev);
		return ret;
	}
#endif
	ret = platform_driver_register(&mtk_dl2_driver);
	return ret;

}
module_init(mtk_dl2_soc_platform_init);

static void __exit mtk_dl2_soc_platform_exit(void)
{
	PRINTK_AUD_DL2("%s\n", __func__);

	platform_driver_unregister(&mtk_dl2_driver);
}
module_exit(mtk_dl2_soc_platform_exit);

MODULE_DESCRIPTION("AFE PCM module platform driver");
MODULE_LICENSE("GPL");
