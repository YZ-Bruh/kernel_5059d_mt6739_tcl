
#ifndef _MT_ISP_H
#define _MT_ISP_H

#include <linux/ioctl.h>

#ifndef CONFIG_OF
extern void mt_irq_set_sens(unsigned int irq, unsigned int sens);
extern void mt_irq_set_polarity(unsigned int irq, unsigned int polarity);
#endif

m4u_callback_ret_t ISP_M4U_TranslationFault_callback(int port, unsigned int mva, void *data);

#define KERNEL_LOG
#define ISR_LOG_ON

#define SIG_ERESTARTSYS 512
#define ISP_DEV_MAJOR_NUMBER    251
#define ISP_MAGIC               'k'

#define CAM_A_BASE_HW   0x1A004000
#define CAM_B_BASE_HW   0x1A005000
#define CAMSV_0_BASE_HW 0x1A050000
#define CAMSV_1_BASE_HW 0x1A051000
#define CAMSV_2_BASE_HW 0x1A052000
#define CAMSV_3_BASE_HW 0x1A053000
#define CAMSV_4_BASE_HW 0x1A054000
#define CAMSV_5_BASE_HW 0x1A055000
#define DIP_A_BASE_HW   0x15022000
#define UNI_A_BASE_HW   0x1A003000
#define SENINF_BASE_HW  0x1A040000
#define MIPI_RX_BASE_HW 0x10217000
#define GPIO_BASE_HW    0x10002000

#define ISP_REG_RANGE           (PAGE_SIZE)
#define ISP_REG_PER_DIP_RANGE   (PAGE_SIZE*5)

/* In order with the suquence of device nodes defined in dtsi */
typedef enum {
	ISP_IMGSYS_CONFIG_IDX = 0,
	ISP_DIP_A_IDX, /* Remider: Add this device node manually in .dtsi */
	ISP_CAMSYS_CONFIG_IDX,
	ISP_UNI_A_IDX, /* CAMTOP in .dtsi */
	ISP_CAM_A_IDX,
	ISP_CAM_B_IDX,
	ISP_CAMSV0_IDX,
	ISP_CAMSV1_IDX,
	ISP_CAMSV2_IDX,
	ISP_CAMSV3_IDX,
	ISP_CAMSV4_IDX,
	ISP_CAMSV5_IDX,
	ISP_DEV_NODE_NUM
} ISP_DEV_NODE_ENUM;

/* defined if want to support multiple dequne and enque or camera 3.0 */
/* #define _rtbc_buf_que_2_0_ */
typedef enum {
	CAM_FST_NORMAL = 0, CAM_FST_DROP_FRAME = 1, CAM_FST_LAST_WORKING_FRAME = 2,
} CAM_FrameST;

typedef enum {
	ISP_IRQ_CLEAR_NONE, /* non-clear wait, clear after wait */
	ISP_IRQ_CLEAR_WAIT, /* clear wait, clear before and after wait */
	ISP_IRQ_CLEAR_STATUS, /* clear specific status only */
	ISP_IRQ_CLEAR_ALL /* clear all status */
} ISP_IRQ_CLEAR_ENUM;

typedef enum {
	ISP_IRQ_TYPE_INT_CAM_A_ST,
	ISP_IRQ_TYPE_INT_CAM_B_ST,
	ISP_IRQ_TYPE_INT_DIP_A_ST,
	ISP_IRQ_TYPE_INT_CAMSV_0_ST,
	ISP_IRQ_TYPE_INT_CAMSV_1_ST,
	ISP_IRQ_TYPE_INT_CAMSV_2_ST,
	ISP_IRQ_TYPE_INT_CAMSV_3_ST,
	ISP_IRQ_TYPE_INT_CAMSV_4_ST,
	ISP_IRQ_TYPE_INT_CAMSV_5_ST,
	ISP_IRQ_TYPE_INT_UNI_A_ST,
	ISP_IRQ_TYPE_AMOUNT
} ISP_IRQ_TYPE_ENUM;

typedef enum {
	SIGNAL_INT = 0, DMA_INT, ISP_IRQ_ST_AMOUNT
} ISP_ST_ENUM;

typedef struct {
	unsigned int tLastSig_sec; /* time stamp of the latest occurring signal*/
	unsigned int tLastSig_usec; /* time stamp of the latest occurring signal*/
	unsigned int tMark2WaitSig_sec; /* time period from marking a signal to user try to wait and get the signal*/
	unsigned int tMark2WaitSig_usec; /* time period from marking a signal to user try to wait and get the signal*/
	unsigned int tLastSig2GetSig_sec; /* time period from latest signal to user try to wait and get the signal*/
	unsigned int tLastSig2GetSig_usec; /* time period from latest signal to user try to wait and get the signal*/
	int passedbySigcnt; /* the count for the signal passed by  */
} ISP_IRQ_TIME_STRUCT;

typedef struct {
	ISP_IRQ_CLEAR_ENUM Clear;
	ISP_ST_ENUM St_type;
	unsigned int Status; /*ref. enum:ENUM_CAM_INT / ENUM_CAM_DMA_INT ...etc in isp_drv_stddef.h*/
	int UserKey; /* user key for doing interrupt operation */
	unsigned int Timeout;
	ISP_IRQ_TIME_STRUCT TimeInfo;
} ISP_WAIT_IRQ_ST;

typedef struct {
	ISP_IRQ_TYPE_ENUM Type;
	unsigned int bDumpReg;
	ISP_WAIT_IRQ_ST EventInfo;
} ISP_WAIT_IRQ_STRUCT;

typedef struct {
	int userKey;
	char userName[32]; /* this size must the same as the icamiopipe api - registerIrq(...) */
} ISP_REGISTER_USERKEY_STRUCT;

typedef struct {
	int UserKey; /* user key for doing interrupt operation */
	ISP_ST_ENUM St_type;
	unsigned int Status;
} ISP_CLEAR_IRQ_ST;

typedef struct {
	ISP_IRQ_TYPE_ENUM Type;
	ISP_CLEAR_IRQ_ST EventInfo;
} ISP_CLEAR_IRQ_STRUCT;

typedef struct {
	unsigned int module;
	unsigned int Addr; /* register's addr */
	unsigned int Val; /* register's value */
} ISP_REG_STRUCT;

typedef struct {
	ISP_REG_STRUCT *pData; /* pointer to ISP_REG_STRUCT */
	unsigned int Count; /* count */
} ISP_REG_IO_STRUCT;

#ifdef CONFIG_COMPAT
typedef struct {
	compat_uptr_t pData;
	unsigned int Count; /* count */
} compat_ISP_REG_IO_STRUCT;
#endif

/* length of the two memory areas */
#define P1_DEQUE_CNT    1
#define RT_BUF_TBL_NPAGES 16
#define ISP_RT_BUF_SIZE 16
#define ISP_RT_CQ0C_BUF_SIZE (ISP_RT_BUF_SIZE)/* (ISP_RT_BUF_SIZE>>1) */
/* pass1 setting sync index */
#define ISP_REG_P1_CFG_IDX 0x4090

typedef enum {
	_cam_tg_ = 0,
	_cam_tg2_,
	_camsv_tg_,
	_camsv2_tg_,
	_cam_tg_max_
} _isp_tg_enum_;


typedef enum {
	_imgi_ = 0,
	_imgbi_,
	_imgci_,
	_vipi_,
	_vip2i_,
	_vip3i_, /* 5 */
	_ufdi_,
	_lcei_,
	_dmgi_,
	_depi_,
	_img2o_, /* 10 */
	_img2bo_,
	_img3o_,
	_img3bo_,
	_img3co_,
	_mfbo_, /* 15 */
	_feo_,
	_wrot_,
	_wdma_,
	_jpeg_,
	_venc_stream_, /* 20 */
	_dip_max,
	_rt_dma_max_ = _dip_max,

	_imgo_ = 0,
	_rrzo_,
	_ufeo_,
	_aao_,
	_afo_,
	_lcso_, /* 5 */
	_pdo_,
	_eiso_,
	_flko_,
	_rsso_,
	_cam_max_,

	_camsv_imgo_ = 0,
	_camsv_max_,
} _isp_dma_enum_;

/* for keep ion handle */
typedef enum {
	_dma_cq0i_ = 0,/* 0168 */
	_dma_cq1i_,    /* 0180 */
	_dma_cq2i_,    /* 018c */
	_dma_cq3i_,    /* 0198 */
	_dma_cq4i_,    /* 01a4 */
	_dma_cq5i_,    /* 01b0 *//*5*/
	_dma_cq6i_,    /* 01bc */
	_dma_cq7i_,    /* 01c8 */
	_dma_cq8i_,    /* 01d4 */
	_dma_cq9i_,    /* 01e0 */
	_dma_cq10i_,   /* 01ec *//*10*/
	_dma_cq11i_,   /* 01f8 */
	_dma_bpci_,    /* 0370 */
	_dma_caci_,    /* 03a0 */
	_dma_lsci_,    /* 03d0 */
	_dma_imgo_,    /* 0220 *//*15*/
	_dma_rrzo_,    /* 0250 */
	_dma_aao_,     /* 0280 */
	_dma_afo_,     /* 02b0 */
	_dma_lcso_,    /* 02e0 */
	_dma_ufeo_,    /* 0310 *//*20*/
	_dma_pdo_,     /* 0340 */
	_dma_eiso_,    /* 0220 */
	_dma_flko_,    /* 0250 */
	_dma_rsso_,    /* 0280 */
	_dma_imgo_fh_, /* 0c04 *//*25*/
	_dma_rrzo_fh_, /* 0c08 */
	_dma_aao_fh_,  /* 0c0c */
	_dma_afo_fh_,  /* 0c10 */
	_dma_lcso_fh_, /* 0c14 */
	_dma_ufeo_fh_, /* 0c18 *//*30*/
	_dma_pdo_fh_,  /* 0c1c */
	_dma_eiso_fh_,  /* 03C4 */
	_dma_flko_fh_, /* 03C8 */
	_dma_rsso_fh_, /* 03CC */
	_dma_max_wr_             /*35*/
} ISP_WRDMA_ENUM;

typedef struct {
	unsigned int       devNode;
	ISP_WRDMA_ENUM     dmaPort;
	int                memID;
} ISP_DEV_ION_NODE_STRUCT;

typedef struct {
	unsigned int w; /* tg size */
	unsigned int h;
	unsigned int xsize; /* dmao xsize */
	unsigned int stride;
	unsigned int fmt;
	unsigned int pxl_id;
	unsigned int wbn;
	unsigned int ob;
	unsigned int lsc;
	unsigned int rpg;
	unsigned int m_num_0;
	unsigned int frm_cnt;
	unsigned int bus_size;
} ISP_RT_IMAGE_INFO_STRUCT;

typedef struct {
	unsigned int srcX; /* crop window start point */
	unsigned int srcY;
	unsigned int srcW; /* crop window size */
	unsigned int srcH;
	unsigned int dstW; /* rrz out size */
	unsigned int dstH;
} ISP_RT_RRZ_INFO_STRUCT;

typedef struct {
	unsigned int x; /* in pix */
	unsigned int y; /* in pix */
	unsigned int w; /* in byte */
	unsigned int h; /* in byte */
} ISP_RT_DMAO_CROPPING_STRUCT;

typedef struct {
	unsigned int memID;
	unsigned int size;
	long long base_vAddr;
	unsigned int base_pAddr;
	unsigned int timeStampS;
	unsigned int timeStampUs;
	unsigned int bFilled;
	unsigned int bProcessRaw;
	ISP_RT_IMAGE_INFO_STRUCT image;
	ISP_RT_RRZ_INFO_STRUCT rrzInfo;
	ISP_RT_DMAO_CROPPING_STRUCT dmaoCrop; /* imgo */
	unsigned int bDequeued;
	signed int bufIdx; /* used for replace buffer */
} ISP_RT_BUF_INFO_STRUCT;

typedef struct {
	unsigned int count;
	unsigned int sof_cnt; /* cnt for current sof */
	unsigned int img_cnt; /* cnt for mapping to which sof */
	/* support only deque 1 image at a time */
	/* ISP_RT_BUF_INFO_STRUCT  data[ISP_RT_BUF_SIZE]; */
	ISP_RT_BUF_INFO_STRUCT data[P1_DEQUE_CNT];
} ISP_DEQUE_BUF_INFO_STRUCT;

typedef struct {
	unsigned int start; /* current DMA accessing buffer */
	unsigned int total_count; /* total buffer number.Include Filled and empty */
	unsigned int empty_count; /* total empty buffer number include current DMA accessing buffer */
	unsigned int pre_empty_count; /* previous total empty buffer number include current DMA accessing buffer */
	unsigned int active;
	unsigned int read_idx;
	unsigned int img_cnt; /* cnt for mapping to which sof */
	ISP_RT_BUF_INFO_STRUCT data[ISP_RT_BUF_SIZE];
} ISP_RT_RING_BUF_INFO_STRUCT;

typedef enum {
	ISP_RT_BUF_CTRL_DMA_EN, ISP_RT_BUF_CTRL_CLEAR, ISP_RT_BUF_CTRL_MAX
} ISP_RT_BUF_CTRL_ENUM;

typedef enum {
	ISP_RTBC_STATE_INIT,
	ISP_RTBC_STATE_SOF,
	ISP_RTBC_STATE_DONE,
	ISP_RTBC_STATE_MAX
} ISP_RTBC_STATE_ENUM;

typedef enum {
	ISP_RTBC_BUF_EMPTY,
	ISP_RTBC_BUF_FILLED,
	ISP_RTBC_BUF_LOCKED,
} ISP_RTBC_BUF_STATE_ENUM;

typedef enum {
	ISP_RROCESSED_RAW,
	ISP_PURE_RAW,
} ISP_RAW_TYPE_ENUM;

typedef struct {
	ISP_RTBC_STATE_ENUM state;
	unsigned long dropCnt;
	ISP_RT_RING_BUF_INFO_STRUCT ring_buf[_cam_max_];
} ISP_RT_BUF_STRUCT;

typedef struct {
	ISP_RT_BUF_CTRL_ENUM ctrl;
	ISP_IRQ_TYPE_ENUM module;
	_isp_dma_enum_ buf_id;
	/* unsigned int            data_ptr; */
	/* unsigned int            ex_data_ptr; exchanged buffer */
	ISP_RT_BUF_INFO_STRUCT *data_ptr;
	ISP_RT_BUF_INFO_STRUCT *ex_data_ptr; /* exchanged buffer */
	unsigned char *pExtend;
} ISP_BUFFER_CTRL_STRUCT;

/* reference count */
#define _use_kernel_ref_cnt_

typedef enum {
	ISP_REF_CNT_GET,
	ISP_REF_CNT_INC,
	ISP_REF_CNT_DEC,
	ISP_REF_CNT_DEC_AND_RESET_P1_P2_IF_LAST_ONE,
	ISP_REF_CNT_DEC_AND_RESET_P1_IF_LAST_ONE,
	ISP_REF_CNT_DEC_AND_RESET_P2_IF_LAST_ONE,
	ISP_REF_CNT_MAX
} ISP_REF_CNT_CTRL_ENUM;

typedef enum {
	ISP_REF_CNT_ID_IMEM,
	ISP_REF_CNT_ID_ISP_FUNC,
	ISP_REF_CNT_ID_GLOBAL_PIPE,
	ISP_REF_CNT_ID_P1_PIPE,
	ISP_REF_CNT_ID_P2_PIPE,
	ISP_REF_CNT_ID_MAX,
} ISP_REF_CNT_ID_ENUM;

typedef struct {
	ISP_REF_CNT_CTRL_ENUM ctrl;
	ISP_REF_CNT_ID_ENUM id;
	signed int *data_ptr;
} ISP_REF_CNT_CTRL_STRUCT;

/* struct for enqueue/dequeue control in ihalpipe wrapper */
typedef enum {
	ISP_P2_BUFQUE_CTRL_ENQUE_FRAME = 0, /* 0,signal that a specific buffer is enqueued */
	ISP_P2_BUFQUE_CTRL_WAIT_DEQUE, /* 1,a dequeue thread is waiting to do dequeue */
	ISP_P2_BUFQUE_CTRL_DEQUE_SUCCESS, /* 2,signal that a buffer is dequeued (success) */
	ISP_P2_BUFQUE_CTRL_DEQUE_FAIL, /* 3,signal that a buffer is dequeued (fail) */
	ISP_P2_BUFQUE_CTRL_WAIT_FRAME, /* 4,wait for a specific buffer */
	ISP_P2_BUFQUE_CTRL_WAKE_WAITFRAME, /* 5,wake all slept users to check buffer is dequeued or not */
	ISP_P2_BUFQUE_CTRL_CLAER_ALL, /* 6,free all recored dequeued buffer */
	ISP_P2_BUFQUE_CTRL_MAX
} ISP_P2_BUFQUE_CTRL_ENUM;

typedef enum {
	ISP_P2_BUFQUE_PROPERTY_DIP = 0,
	ISP_P2_BUFQUE_PROPERTY_NUM = 1,
	ISP_P2_BUFQUE_PROPERTY_WARP
} ISP_P2_BUFQUE_PROPERTY;
typedef struct {
	ISP_P2_BUFQUE_CTRL_ENUM ctrl;
	ISP_P2_BUFQUE_PROPERTY property;
	unsigned int processID; /* judge multi-process */
	unsigned int callerID; /* judge multi-thread and different kinds of buffer type */
	int frameNum; /* total frame number in the enque request */
	int cQIdx; /* cq index */
	int dupCQIdx; /* dup cq index */
	int burstQIdx; /* burst queue index */
	unsigned int timeoutIns; /* timeout for wait buffer */
} ISP_P2_BUFQUE_STRUCT;

typedef enum {
	ISP_P2_BUF_STATE_NONE = -1,
	ISP_P2_BUF_STATE_ENQUE = 0,
	ISP_P2_BUF_STATE_RUNNING,
	ISP_P2_BUF_STATE_WAIT_DEQUE_FAIL,
	ISP_P2_BUF_STATE_DEQUE_SUCCESS,
	ISP_P2_BUF_STATE_DEQUE_FAIL
} ISP_P2_BUF_STATE_ENUM;

typedef enum {
	ISP_P2_BUFQUE_LIST_TAG_PACKAGE = 0, ISP_P2_BUFQUE_LIST_TAG_UNIT
} ISP_P2_BUFQUE_LIST_TAG;

typedef enum {
	ISP_P2_BUFQUE_MATCH_TYPE_WAITDQ = 0, /* waiting for deque */
	ISP_P2_BUFQUE_MATCH_TYPE_WAITFM, /* wait frame from user */
	ISP_P2_BUFQUE_MATCH_TYPE_FRAMEOP, /* frame operaetion */
	ISP_P2_BUFQUE_MATCH_TYPE_WAITFMEQD /* wait frame enqueued for deque */
} ISP_P2_BUFQUE_MATCH_TYPE;

#define _rtbc_use_cq0c_

#define _MAGIC_NUM_ERR_HANDLING_

#ifdef CONFIG_COMPAT


typedef struct {
	ISP_RT_BUF_CTRL_ENUM ctrl;
	ISP_IRQ_TYPE_ENUM module;
	_isp_dma_enum_ buf_id;
	compat_uptr_t data_ptr;
	compat_uptr_t ex_data_ptr; /* exchanged buffer */
	compat_uptr_t pExtend;
} compat_ISP_BUFFER_CTRL_STRUCT;

typedef struct {
	ISP_REF_CNT_CTRL_ENUM ctrl;
	ISP_REF_CNT_ID_ENUM id;
	compat_uptr_t data_ptr;
} compat_ISP_REF_CNT_CTRL_STRUCT;

#endif



typedef enum {
	ISP_CMD_RESET_CAM_P1, /* Reset */
	ISP_CMD_RESET_CAM_P2,
	ISP_CMD_RESET_CAMSV,
	ISP_CMD_RESET_CAMSV2,
	ISP_CMD_RESET_BY_HWMODULE,
	ISP_CMD_READ_REG, /* Read register from driver */
	ISP_CMD_WRITE_REG, /* Write register to driver */
	ISP_CMD_WAIT_IRQ, /* Wait IRQ */
	ISP_CMD_CLEAR_IRQ, /* Clear IRQ */
	ISP_CMD_DUMP_REG, /* Dump ISP registers , for debug usage */
	ISP_CMD_RT_BUF_CTRL, /* for pass buffer control */
	ISP_CMD_REF_CNT, /* get imem reference count */
	ISP_CMD_DEBUG_FLAG, /* Dump message level */
	ISP_CMD_P2_BUFQUE_CTRL,
	ISP_CMD_UPDATE_REGSCEN,
	ISP_CMD_QUERY_REGSCEN,
	ISP_CMD_UPDATE_BURSTQNUM,
	ISP_CMD_QUERY_BURSTQNUM,
	ISP_CMD_DUMP_ISR_LOG, /* dump isr log */
	ISP_CMD_GET_CUR_SOF,
	ISP_CMD_GET_DMA_ERR,
	ISP_CMD_GET_INT_ERR,
	ISP_CMD_GET_DROP_FRAME, /* dump current frame informaiton, 1 for drop frmae, 2 for last working frame */
	ISP_CMD_WAKELOCK_CTRL,
	ISP_CMD_REGISTER_IRQ_USER_KEY, /* register for a user key to do irq operation */
	ISP_CMD_MARK_IRQ_REQUEST, /* mark for a specific register before wait for the interrupt if needed */
	ISP_CMD_GET_MARK2QUERY_TIME, /* query time information between read and mark */
	ISP_CMD_FLUSH_IRQ_REQUEST, /* flush signal */
	ISP_CMD_GET_START_TIME,
	ISP_CMD_VF_LOG, /* dbg only, prt log on kernel when vf_en is driven */
	ISP_CMD_GET_VSYNC_CNT,
	ISP_CMD_RESET_VSYNC_CNT,
	ISP_CMD_ION_IMPORT, /* get ion handle */
	ISP_CMD_ION_FREE,  /* free ion handle */
	ISP_CMD_CQ_SW_PATCH,  /* sim cq update behavior as atomic behavior */
	ISP_CMD_ION_FREE_BY_HWMODULE  /* free all ion handle */
} ISP_CMD_ENUM;

typedef enum {
	ISP_HALT_DMA_IMGO = 0,
	ISP_HALT_DMA_RRZO,
	ISP_HALT_DMA_AAO,
	ISP_HALT_DMA_AFO,
	ISP_HALT_DMA_LSCI,
	ISP_HALT_DMA_LSC3I,
	ISP_HALT_DMA_RSSO,
	ISP_HALT_DMA_CAMSV0,
	ISP_HALT_DMA_CAMSV1,
	ISP_HALT_DMA_CAMSV2,
	ISP_HALT_DMA_LSCO,
	ISP_HALT_DMA_UFEO,
	ISP_HALT_DMA_BPCI,
	ISP_HALT_DMA_PDO,
	ISP_HALT_DMA_RAWI,
	ISP_HALT_DMA_CCUI,
	ISP_HALT_DMA_CCUO,
	ISP_HALT_DMA_AMOUNT
} ISP_HALT_DMA_ENUM;


/* Everest reset ioctl */
#define ISP_RESET_CAM_P1         _IOWR(ISP_MAGIC, ISP_CMD_RESET_CAM_P1, unsigned int)
#define ISP_RESET_BY_HWMODULE    _IOW(ISP_MAGIC, ISP_CMD_RESET_BY_HWMODULE, unsigned long)

/* read phy reg  */
#define ISP_READ_REGISTER        _IOWR(ISP_MAGIC, ISP_CMD_READ_REG, ISP_REG_IO_STRUCT)

/* write phy reg */
#define ISP_WRITE_REGISTER       _IOWR(ISP_MAGIC, ISP_CMD_WRITE_REG, ISP_REG_IO_STRUCT)

#define ISP_WAIT_IRQ        _IOW(ISP_MAGIC, ISP_CMD_WAIT_IRQ,      ISP_WAIT_IRQ_STRUCT)
#define ISP_CLEAR_IRQ       _IOW(ISP_MAGIC, ISP_CMD_CLEAR_IRQ,     ISP_CLEAR_IRQ_STRUCT)
#define ISP_DUMP_REG        _IO(ISP_MAGIC, ISP_CMD_DUMP_REG)
#define ISP_BUFFER_CTRL     _IOWR(ISP_MAGIC, ISP_CMD_RT_BUF_CTRL,   ISP_BUFFER_CTRL_STRUCT)
#define ISP_REF_CNT_CTRL    _IOWR(ISP_MAGIC, ISP_CMD_REF_CNT,       ISP_REF_CNT_CTRL_STRUCT)
#define ISP_DEBUG_FLAG      _IOW(ISP_MAGIC, ISP_CMD_DEBUG_FLAG,    unsigned char*)
#define ISP_P2_BUFQUE_CTRL     _IOWR(ISP_MAGIC, ISP_CMD_P2_BUFQUE_CTRL, ISP_P2_BUFQUE_STRUCT)
#define ISP_UPDATE_REGSCEN     _IOWR(ISP_MAGIC, ISP_CMD_UPDATE_REGSCEN, unsigned int)
#define ISP_QUERY_REGSCEN     _IOR(ISP_MAGIC, ISP_CMD_QUERY_REGSCEN,   unsigned int)
#define ISP_UPDATE_BURSTQNUM _IOW(ISP_MAGIC, ISP_CMD_UPDATE_BURSTQNUM,   int)
#define ISP_QUERY_BURSTQNUM _IOR(ISP_MAGIC, ISP_CMD_QUERY_BURSTQNUM,    int)
#define ISP_DUMP_ISR_LOG    _IOWR(ISP_MAGIC, ISP_CMD_DUMP_ISR_LOG,         unsigned long)
#define ISP_GET_CUR_SOF     _IOWR(ISP_MAGIC, ISP_CMD_GET_CUR_SOF,          unsigned char*)
#define ISP_GET_DMA_ERR     _IOWR(ISP_MAGIC, ISP_CMD_GET_DMA_ERR,          unsigned char*)
#define ISP_GET_INT_ERR     _IOR(ISP_MAGIC, ISP_CMD_GET_INT_ERR,        unsigned char*)
#define ISP_GET_DROP_FRAME  _IOWR(ISP_MAGIC, ISP_CMD_GET_DROP_FRAME,    unsigned long)
#define ISP_GET_START_TIME  _IOWR(ISP_MAGIC, ISP_CMD_GET_START_TIME,    unsigned char*)

#define ISP_REGISTER_IRQ_USER_KEY   _IOR(ISP_MAGIC, ISP_CMD_REGISTER_IRQ_USER_KEY, ISP_REGISTER_USERKEY_STRUCT)

#define ISP_MARK_IRQ_REQUEST        _IOWR(ISP_MAGIC, ISP_CMD_MARK_IRQ_REQUEST, ISP_WAIT_IRQ_STRUCT)
#define ISP_GET_MARK2QUERY_TIME     _IOWR(ISP_MAGIC, ISP_CMD_GET_MARK2QUERY_TIME, ISP_WAIT_IRQ_STRUCT)
#define ISP_FLUSH_IRQ_REQUEST       _IOW(ISP_MAGIC, ISP_CMD_FLUSH_IRQ_REQUEST, ISP_WAIT_IRQ_STRUCT)

#define ISP_WAKELOCK_CTRL           _IOWR(ISP_MAGIC, ISP_CMD_WAKELOCK_CTRL, unsigned long)
#define ISP_VF_LOG                  _IOW(ISP_MAGIC, ISP_CMD_VF_LOG,         unsigned char*)
#define ISP_GET_VSYNC_CNT           _IOWR(ISP_MAGIC, ISP_CMD_GET_VSYNC_CNT,      unsigned int)
#define ISP_RESET_VSYNC_CNT         _IOW(ISP_MAGIC, ISP_CMD_RESET_VSYNC_CNT,      unsigned int)
#define ISP_ION_IMPORT              _IOW(ISP_MAGIC, ISP_CMD_ION_IMPORT, ISP_DEV_ION_NODE_STRUCT)
#define ISP_ION_FREE                _IOW(ISP_MAGIC, ISP_CMD_ION_FREE,   ISP_DEV_ION_NODE_STRUCT)
#define ISP_ION_FREE_BY_HWMODULE    _IOW(ISP_MAGIC, ISP_CMD_ION_FREE_BY_HWMODULE, unsigned int)
#define ISP_CQ_SW_PATCH             _IOW(ISP_MAGIC, ISP_CMD_CQ_SW_PATCH, unsigned int)

#ifdef CONFIG_COMPAT
#define COMPAT_ISP_READ_REGISTER    _IOWR(ISP_MAGIC, ISP_CMD_READ_REG,      compat_ISP_REG_IO_STRUCT)
#define COMPAT_ISP_WRITE_REGISTER   _IOWR(ISP_MAGIC, ISP_CMD_WRITE_REG,     compat_ISP_REG_IO_STRUCT)
#define COMPAT_ISP_DEBUG_FLAG      _IOW(ISP_MAGIC, ISP_CMD_DEBUG_FLAG,     compat_uptr_t)
#define COMPAT_ISP_GET_DMA_ERR     _IOWR(ISP_MAGIC, ISP_CMD_GET_DMA_ERR,   compat_uptr_t)
#define COMPAT_ISP_GET_INT_ERR     _IOR(ISP_MAGIC, ISP_CMD_GET_INT_ERR,  compat_uptr_t)

#define COMPAT_ISP_BUFFER_CTRL     _IOWR(ISP_MAGIC, ISP_CMD_RT_BUF_CTRL,    compat_ISP_BUFFER_CTRL_STRUCT)
#define COMPAT_ISP_REF_CNT_CTRL    _IOWR(ISP_MAGIC, ISP_CMD_REF_CNT,        compat_ISP_REF_CNT_CTRL_STRUCT)
#define COMPAT_ISP_GET_START_TIME  _IOWR(ISP_MAGIC, ISP_CMD_GET_START_TIME, compat_uptr_t)

#define COMPAT_ISP_WAKELOCK_CTRL    _IOWR(ISP_MAGIC, ISP_CMD_WAKELOCK_CTRL, compat_uptr_t)
#define COMPAT_ISP_GET_DROP_FRAME   _IOWR(ISP_MAGIC, ISP_CMD_GET_DROP_FRAME, compat_uptr_t)
#define COMPAT_ISP_GET_CUR_SOF      _IOWR(ISP_MAGIC, ISP_CMD_GET_CUR_SOF,   compat_uptr_t)
#define COMPAT_ISP_DUMP_ISR_LOG     _IOWR(ISP_MAGIC, ISP_CMD_DUMP_ISR_LOG,  compat_uptr_t)
#define COMPAT_ISP_RESET_BY_HWMODULE _IOW(ISP_MAGIC, ISP_CMD_RESET_BY_HWMODULE, compat_uptr_t)
#define COMPAT_ISP_VF_LOG           _IOW(ISP_MAGIC, ISP_CMD_VF_LOG,         compat_uptr_t)
#define COMPAT_ISP_CQ_SW_PATCH      _IOW(ISP_MAGIC, ISP_CMD_CQ_SW_PATCH,         compat_uptr_t)
#endif

int32_t ISP_MDPClockOnCallback(uint64_t engineFlag);
int32_t ISP_MDPDumpCallback(uint64_t engineFlag, int level);
int32_t ISP_MDPResetCallback(uint64_t engineFlag);

int32_t ISP_MDPClockOffCallback(uint64_t engineFlag);

int32_t ISP_BeginGCECallback(uint32_t taskID, uint32_t *regCount, uint32_t **regAddress);
int32_t ISP_EndGCECallback(uint32_t taskID, uint32_t regCount, uint32_t *regValues);

#endif

