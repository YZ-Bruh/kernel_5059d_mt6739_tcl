
#include <linux/of.h>
#include <linux/of_address.h>

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/clkdev.h>

#include "clk-mtk-v1.h"
#include "clk-mt6757-pg.h"

#include <dt-bindings/clock/mt6757-clk.h>

/*#define TOPAXI_PROTECT_LOCK*/

#if !defined(MT_CCF_DEBUG) || !defined(MT_CCF_BRINGUP)
#define MT_CCF_DEBUG	0
#define MT_CCF_BRINGUP  1
#define CONTROL_LIMIT 1
#endif

#define	CHECK_PWR_ST	1

#ifndef GENMASK
#define GENMASK(h, l)	(((U32_C(1) << ((h) - (l) + 1)) - 1) << (l))
#endif

#ifdef CONFIG_ARM64
#define IOMEM(a)	((void __force __iomem *)((a)))
#endif

#define clk_readl(addr)		readl(addr)
#define clk_writel(val, addr)	\
	do { writel(val, addr); wmb(); } while (0)	/* sync_write */
#define clk_setl(mask, addr)	clk_writel(clk_readl(addr) | (mask), addr)
#define clk_clrl(mask, addr)	clk_writel(clk_readl(addr) & ~(mask), addr)

#define mt_reg_sync_writel(v, a) \
	do { \
		__raw_writel((v), IOMEM(a)); \
		mb(); } \
while (0)
#define spm_read(addr)			__raw_readl(IOMEM(addr))
#define spm_write(addr, val)		mt_reg_sync_writel(val, addr)


#define STA_POWER_DOWN	0
#define STA_POWER_ON	1
#define SUBSYS_PWR_DOWN		0
#define SUBSYS_PWR_ON		1

	struct subsys;

struct subsys_ops {
	int (*enable)(struct subsys *sys);
	int (*disable)(struct subsys *sys);
	int (*get_state)(struct subsys *sys);
};

struct subsys {
	const char *name;
	uint32_t sta_mask;
	void __iomem *ctl_addr;
	uint32_t sram_pdn_bits;
	uint32_t sram_pdn_ack_bits;
	uint32_t bus_prot_mask;
	struct subsys_ops *ops;
};

/*static struct subsys_ops general_sys_ops;*/
static struct subsys_ops MD1_sys_ops;
static struct subsys_ops CONN_sys_ops;
static struct subsys_ops DIS_sys_ops;
static struct subsys_ops MFG_sys_ops;
static struct subsys_ops ISP_sys_ops;
static struct subsys_ops VDE_sys_ops;
static struct subsys_ops VEN_sys_ops;
static struct subsys_ops MFG_ASYNC_sys_ops;
static struct subsys_ops AUDIO_sys_ops;
static struct subsys_ops CAM_sys_ops;
static struct subsys_ops C2K_sys_ops;
static struct subsys_ops MDSYS_INTF_INFRA_sys_ops;
static struct subsys_ops MFG_CORE1_sys_ops;
static struct subsys_ops MFG_CORE0_sys_ops;


static void __iomem *infracfg_base;/*infracfg_ao*/
static void __iomem *spm_base;
static void __iomem *infra_base;/*infracfg*/


#define INFRACFG_REG(offset)		(infracfg_base + offset)
#define SPM_REG(offset)			(spm_base + offset)
#define INFRA_REG(offset)	(infra_base + offset)
#ifdef TOPAXI_PROTECT_LOCK
static DEFINE_SPINLOCK(spm_noncpu_lock);

#define spm_mtcmos_noncpu_lock(flags)   spin_lock_irqsave(&spm_noncpu_lock, flags)

#define spm_mtcmos_noncpu_unlock(flags) spin_unlock_irqrestore(&spm_noncpu_lock, flags)
#endif

#if 1
#define POWERON_CONFIG_EN			SPM_REG(0x0000)
#define PWR_STATUS			SPM_REG(0x0180)
#define PWR_STATUS_2ND	SPM_REG(0x0184)
#define MD1_PWR_CON			SPM_REG(0x0320)
#define C2K_PWR_CON			SPM_REG(0x0328)
#define CONN_PWR_CON		SPM_REG(0x032c)
#define VDE_PWR_CON			SPM_REG(0x0300)
#define VEN_PWR_CON			SPM_REG(0x0304)
#define ISP_PWR_CON			SPM_REG(0x0308)
#define DIS_PWR_CON			SPM_REG(0x030c)
#define AUDIO_PWR_CON			SPM_REG(0x0314)
#define MFG_ASYNC_PWR_CON			SPM_REG(0x0334)
#define MFG_PWR_CON			SPM_REG(0x0338)
#define MFG_CORE0_PWR_CON        SPM_REG(0x033C)
#define MFG_CORE1_PWR_CON			SPM_REG(0x0340)
#define CAM_PWR_CON                    SPM_REG(0x344)
#define MDSYS_INTF_INFRA_PWR_CON       SPM_REG(0x360)

#define MD_EXT_BUCK_ISO     SPM_REG(0x0390)

#define MD1_OUTPUT_PISO_S_EN_IZ        SPM_REG(0x3B4)
#define SPM_SW_RSV_7                   SPM_REG(0x650)


#define INFRA_TOPAXI_PROTECTEN		INFRACFG_REG(0x0220)
#define INFRA_TOPAXI_PROTECTSTA1	INFRACFG_REG(0x0228)
#define INFRA_TOPAXI_PROTECTEN_1   INFRACFG_REG(0x0250)
#define INFRA_TOPAXI_PROTECTSTA1_1 INFRACFG_REG(0x0258)


#define INFRA_BUS_IDLE_STA5	INFRA_REG(0x0190)
#endif

#define  SPM_PROJECT_CODE    0xB16
/* Define MTCMOS power control */
#define PWR_RST_B                        (0x1 << 0)
#define PWR_ISO                          (0x1 << 1)
#define PWR_ON                           (0x1 << 2)
#define PWR_ON_2ND                       (0x1 << 3)
#define PWR_CLK_DIS                      (0x1 << 4)
#define SRAM_CKISO                       (0x1 << 5)
#define SRAM_ISOINT_B                    (0x1 << 6)
#define SLPB_CLAMP                       (0x1 << 7)

/* Define MTCMOS Bus Protect Mask */
#define MD1_PROT_BIT_MASK                ((0x1 << 16) \
						  |(0x1 << 17) \
						  |(0x1 << 18) \
						  |(0x1 << 19) \
						  |(0x1 << 20) \
						  |(0x1 << 21))
#define MD1_PROT_BIT_ACK_MASK            ((0x1 << 16) \
						  |(0x1 << 17) \
						  |(0x1 << 18) \
						  |(0x1 << 19) \
						  |(0x1 << 20) \
						  |(0x1 << 21))
#define CONN_PROT_BIT_MASK               ((0x1 << 13) \
						  |(0x1 << 14))
#define CONN_PROT_BIT_ACK_MASK           ((0x1 << 13) \
						  |(0x1 << 14))
#define DIS_PROT_BIT_MASK                ((0x1 << 1) \
						  |(0x1 << 2))
#define DIS_PROT_BIT_ACK_MASK            ((0x1 << 1) \
						  |(0x1 << 2))
#define MFG_PROT_BIT_MASK                ((0x1 << 21) \
						  |(0x1 << 22))
#define MFG_PROT_BIT_ACK_MASK            ((0x1 << 21) \
						  |(0x1 << 22))
#define C2K_PROT_BIT_MASK                ((0x1 << 22) \
						  |(0x1 << 23) \
						  |(0x1 << 24))
#define C2K_PROT_BIT_ACK_MASK            ((0x1 << 22) \
						  |(0x1 << 23) \
						  |(0x1 << 24))
#define MDSYS_INTF_INFRA_PROT_BIT_MASK   ((0x1 << 3) \
						  |(0x1 << 4))
#define MDSYS_INTF_INFRA_PROT_BIT_ACK_MASK   ((0x1 << 3) \
						  |(0x1 << 4))

/* Define MTCMOS Power Status Mask */

#define MD1_PWR_STA_MASK                 (0x1 << 0)
#define CONN_PWR_STA_MASK                (0x1 << 1)
#define DPY_PWR_STA_MASK                 (0x1 << 2)
#define DIS_PWR_STA_MASK                 (0x1 << 3)
#define MFG_PWR_STA_MASK                 (0x1 << 4)
#define ISP_PWR_STA_MASK                 (0x1 << 5)
#define IFR_PWR_STA_MASK                 (0x1 << 6)
#define VDE_PWR_STA_MASK                 (0x1 << 7)
#define VEN_PWR_STA_MASK                 (0x1 << 21)
#define MFG_ASYNC_PWR_STA_MASK           (0x1 << 23)
#define AUDIO_PWR_STA_MASK               (0x1 << 24)
#define CAM_PWR_STA_MASK                 (0x1 << 27)
#define C2K_PWR_STA_MASK                 (0x1 << 28)
#define MDSYS_INTF_INFRA_PWR_STA_MASK    (0x1 << 29)
#define MFG_CORE1_PWR_STA_MASK           (0x1 << 30)
#define MFG_CORE0_PWR_STA_MASK           (0x1 << 31)

	/* Define CPU SRAM Mask */

	/* Define Non-CPU SRAM Mask */
#define MD1_SRAM_PDN                     (0x1 << 8)
#define MD1_SRAM_PDN_ACK                 (0x0 << 12)
#define MD1_SRAM_PDN_ACK_BIT0            (0x1 << 12)
#define DPY_SRAM_PDN                     (0xF << 8)
#define DPY_SRAM_PDN_ACK                 (0xF << 12)
#define DPY_SRAM_PDN_ACK_BIT0            (0x1 << 12)
#define DPY_SRAM_PDN_ACK_BIT1            (0x1 << 13)
#define DPY_SRAM_PDN_ACK_BIT2            (0x1 << 14)
#define DPY_SRAM_PDN_ACK_BIT3            (0x1 << 15)
#define DIS_SRAM_PDN                     (0x1 << 8)
#define DIS_SRAM_PDN_ACK                 (0x1 << 12)
#define DIS_SRAM_PDN_ACK_BIT0            (0x1 << 12)
#define MFG_SRAM_PDN                     (0x1 << 8)
#define MFG_SRAM_PDN_ACK                 (0x1 << 16)
#define MFG_SRAM_PDN_ACK_BIT0            (0x1 << 16)
#define ISP_SRAM_PDN                     (0x3 << 8)
#define ISP_SRAM_PDN_ACK                 (0x3 << 12)
#define ISP_SRAM_PDN_ACK_BIT0            (0x1 << 12)
#define ISP_SRAM_PDN_ACK_BIT1            (0x1 << 13)
#define IFR_SRAM_PDN                     (0xF << 8)
#define IFR_SRAM_PDN_ACK                 (0xF << 12)
#define IFR_SRAM_PDN_ACK_BIT0            (0x1 << 12)
#define IFR_SRAM_PDN_ACK_BIT1            (0x1 << 13)
#define IFR_SRAM_PDN_ACK_BIT2            (0x1 << 14)
#define IFR_SRAM_PDN_ACK_BIT3            (0x1 << 15)
#define VDE_SRAM_PDN                     (0x1 << 8)
#define VDE_SRAM_PDN_ACK                 (0x1 << 12)
#define VDE_SRAM_PDN_ACK_BIT0            (0x1 << 12)
#define VEN_SRAM_PDN                     (0xF << 8)
#define VEN_SRAM_PDN_ACK                 (0xF << 12)
#define VEN_SRAM_PDN_ACK_BIT0            (0x1 << 12)
#define VEN_SRAM_PDN_ACK_BIT1            (0x1 << 13)
#define VEN_SRAM_PDN_ACK_BIT2            (0x1 << 14)
#define VEN_SRAM_PDN_ACK_BIT3            (0x1 << 15)
#define AUDIO_SRAM_PDN                   (0xF << 8)
#define AUDIO_SRAM_PDN_ACK               (0xF << 12)
#define AUDIO_SRAM_PDN_ACK_BIT0          (0x1 << 12)
#define AUDIO_SRAM_PDN_ACK_BIT1          (0x1 << 13)
#define AUDIO_SRAM_PDN_ACK_BIT2          (0x1 << 14)
#define AUDIO_SRAM_PDN_ACK_BIT3          (0x1 << 15)
#define CAM_SRAM_PDN                     (0x3 << 8)
#define CAM_SRAM_PDN_ACK                 (0x3 << 12)
#define CAM_SRAM_PDN_ACK_BIT0            (0x1 << 12)
#define CAM_SRAM_PDN_ACK_BIT1            (0x1 << 13)
#define MFG_CORE1_SRAM_PDN               (0x1 << 5)
#define MFG_CORE1_SRAM_PDN_ACK           (0x1 << 6)
#define MFG_CORE1_SRAM_PDN_ACK_BIT0      (0x1 << 6)
#define MFG_CORE0_SRAM_PDN               (0x1 << 5)
#define MFG_CORE0_SRAM_PDN_ACK           (0x1 << 6)
#define MFG_CORE0_SRAM_PDN_ACK_BIT0      (0x1 << 6)


static struct subsys syss[] =	/* NR_SYSS *//* FIXME: set correct value */
{
	[SYS_MD1] = {
		     .name = __stringify(SYS_MD1),
		     .sta_mask = MD1_PWR_STA_MASK,
		     /* .ctl_addr = NULL,  */
		     .sram_pdn_bits = MD1_SRAM_PDN,
		     .sram_pdn_ack_bits = 0,
		     .bus_prot_mask = 0,
		     .ops = &MD1_sys_ops,
		     },
	[SYS_CONN] = {
		      .name = __stringify(SYS_CONN),
		      .sta_mask = CONN_PWR_STA_MASK,
		     /* .ctl_addr = NULL,  */
		      .sram_pdn_bits = 0,
		      .sram_pdn_ack_bits = 0,
		      .bus_prot_mask = 0,
		      .ops = &CONN_sys_ops,
		      },
	[SYS_DIS] = {
		     .name = __stringify(SYS_DIS),
		     .sta_mask = DIS_PWR_STA_MASK,
		     /* .ctl_addr = NULL,  */
		     .sram_pdn_bits = 0,
		     .sram_pdn_ack_bits = 0,
		     .bus_prot_mask = 0,
		     .ops = &DIS_sys_ops,
		     },
	[SYS_MFG] = {
		     .name = __stringify(SYS_MFG),
		     .sta_mask = MFG_PWR_STA_MASK,
		     /* .ctl_addr = NULL,  */
		     .sram_pdn_bits = 0,
		     .sram_pdn_ack_bits = 0,
		     .bus_prot_mask = 0,
		     .ops = &MFG_sys_ops,
		     },
	[SYS_ISP] = {
		     .name = __stringify(SYS_ISP),
		     .sta_mask = ISP_PWR_STA_MASK,
		     /* .ctl_addr = NULL,  */
		     .sram_pdn_bits = 0,
		     .sram_pdn_ack_bits = 0,
		     .bus_prot_mask = 0,
		     .ops = &ISP_sys_ops,
		     },
	[SYS_VDE] = {
		     .name = __stringify(SYS_VDE),
		     .sta_mask = VDE_PWR_STA_MASK,
		     /* .ctl_addr = NULL,  */
		     .sram_pdn_bits = 0,
		     .sram_pdn_ack_bits = 0,
		     .bus_prot_mask = 0,
		     .ops = &VDE_sys_ops,
		     },
	[SYS_VEN] = {
		     .name = __stringify(SYS_VEN),
		     .sta_mask = VEN_PWR_STA_MASK,
		     /* .ctl_addr = NULL,  */
		     .sram_pdn_bits = 0,
		     .sram_pdn_ack_bits = 0,
		     .bus_prot_mask = 0,
		     .ops = &VEN_sys_ops,
		     },
	[SYS_MFG_ASYNC] = {
			   .name = __stringify(SYS_MFG_ASYNC),
			   .sta_mask = MFG_ASYNC_PWR_STA_MASK,
		     /* .ctl_addr = NULL,  */
			   .sram_pdn_bits = 0,
			   .sram_pdn_ack_bits = 0,
			   .bus_prot_mask = 0,
			   .ops = &MFG_ASYNC_sys_ops,
			   },
	[SYS_AUDIO] = {
		       .name = __stringify(SYS_AUDIO),
		       .sta_mask = AUDIO_PWR_STA_MASK,
		     /* .ctl_addr = NULL,  */
		       .sram_pdn_bits = 0,
		       .sram_pdn_ack_bits = 0,
		       .bus_prot_mask = 0,
		       .ops = &AUDIO_sys_ops,
		       },
	[SYS_CAM] = {
		       .name = __stringify(SYS_CAM),
		       .sta_mask = CAM_PWR_STA_MASK,
		     /* .ctl_addr = NULL,  */
		       .sram_pdn_bits = 0,
		       .sram_pdn_ack_bits = 0,
		       .bus_prot_mask = 0,
		       .ops = &CAM_sys_ops,
		       },
	[SYS_C2K] = {
		     .name = __stringify(SYS_C2K),
		     .sta_mask = C2K_PWR_STA_MASK,
		     /*.ctl_addr = NULL, SPM_C2K_PWR_CON, */
		     .sram_pdn_bits = 0,
		     .sram_pdn_ack_bits = 0,
		     .bus_prot_mask = 0,
		     .ops = &C2K_sys_ops,
		     },
	[SYS_MDSYS_INTF_INFRA] = {
			   .name = __stringify(SYS_MDSYS_INTF_INFRA),
			   .sta_mask = MDSYS_INTF_INFRA_PWR_STA_MASK,
		     /* .ctl_addr = NULL,  */
			   .sram_pdn_bits = 0,
			   .sram_pdn_ack_bits = 0,
			   .bus_prot_mask = 0,
			   .ops = &MDSYS_INTF_INFRA_sys_ops,
			   },
	[SYS_MFG_CORE1] = {
			   .name = __stringify(SYS_MFG_CORE1),
			   .sta_mask = MFG_CORE1_PWR_STA_MASK,
		     /* .ctl_addr = NULL,  */
			   .sram_pdn_bits = 0,
			   .sram_pdn_ack_bits = 0,
			   .bus_prot_mask = 0,
			   .ops = &MFG_CORE1_sys_ops,
			   },
	[SYS_MFG_CORE0] = {
			   .name = __stringify(SYS_MFG_CORE0),
			   .sta_mask = MFG_CORE0_PWR_STA_MASK,
		     /* .ctl_addr = NULL,  */
			   .sram_pdn_bits = 0,
			   .sram_pdn_ack_bits = 0,
			   .bus_prot_mask = 0,
			   .ops = &MFG_CORE0_sys_ops,
			   },
};

static struct pg_callbacks *g_pgcb;

struct pg_callbacks *register_pg_callback(struct pg_callbacks *pgcb)
{
	struct pg_callbacks *old_pgcb = g_pgcb;

	g_pgcb = pgcb;
	return old_pgcb;
}

#ifdef TOPAXI_PROTECT_LOCK
#define _TOPAXI_TIMEOUT_CNT_ 4000
int spm_topaxi_protect(unsigned int mask_value, int en)
{
	unsigned long flags;
	int count = 0;
	struct timeval tm_s, tm_e;
	unsigned int tm_val = 0;

	do_gettimeofday(&tm_s);
	spm_mtcmos_noncpu_lock(flags);

	if (en == 1) {
		spm_write(INFRA_TOPAXI_PROTECTEN, spm_read(INFRA_TOPAXI_PROTECTEN) | (mask_value));
		while ((spm_read(INFRA_TOPAXI_PROTECTSTA1) & (mask_value)) != (mask_value)) {
			#ifdef CONFIG_MTK_RAM_CONSOLE
			aee_rr_rec_clk(0, spm_read(INFRA_TOPAXI_PROTECTSTA1));
			#endif
			count++;
			if (count > _TOPAXI_TIMEOUT_CNT_)
				break;
		}
	} else {
		spm_write(INFRA_TOPAXI_PROTECTEN, spm_read(INFRA_TOPAXI_PROTECTEN) & ~(mask_value));
		while (spm_read(INFRA_TOPAXI_PROTECTSTA1) & (mask_value)) {
			#ifdef CONFIG_MTK_RAM_CONSOLE
			aee_rr_rec_clk(0, spm_read(INFRA_TOPAXI_PROTECTSTA1));
			#endif
			count++;
			if (count > _TOPAXI_TIMEOUT_CNT_)
				break;
		}
	}

	spm_mtcmos_noncpu_unlock(flags);

	if (count > _TOPAXI_TIMEOUT_CNT_) {
		do_gettimeofday(&tm_e);
		tm_val = (tm_e.tv_sec - tm_s.tv_sec) * 1000000 + (tm_e.tv_usec - tm_s.tv_usec);
		pr_err("TOPAXI Bus Protect Timeout Error (%d us)(%d) !!\n", tm_val, count);
		pr_err("INFRA_TOPAXI_PROTECTEN = 0x%x\n", clk_readl(INFRA_TOPAXI_PROTECTEN));
		pr_err("INFRA_TOPAXI_PROTECTSTA0 = 0x%x\n", clk_readl(INFRA_TOPAXI_PROTECTSTA0));
		pr_err("INFRA_TOPAXI_PROTECTSTA1 = 0x%x\n", clk_readl(INFRA_TOPAXI_PROTECTSTA1));
		pr_err("INFRA_BUS_IDLE_STA5 = 0x%x\n", clk_readl(INFRA_BUS_IDLE_STA5));
		BUG();
	}
	return 0;
}
#endif

static struct subsys *id_to_sys(unsigned int id)
{
	return id < NR_SYSS ? &syss[id] : NULL;
}

/* sync from mtcmos_ctrl.c  */
#ifdef CONFIG_MTK_RAM_CONSOLE
#if 0 /*add after early porting*/
static void aee_clk_data_rest(void)
{
	aee_rr_rec_clk(0, 0);
	aee_rr_rec_clk(1, 0);
	aee_rr_rec_clk(2, 0);
	aee_rr_rec_clk(3, 0);
}
#endif
#endif

/* auto-gen begin*/
int spm_mtcmos_ctrl_md1(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off MD1" */
		/* TINFO="Set bus protect" */
		spm_write(INFRA_TOPAXI_PROTECTEN_1, spm_read(INFRA_TOPAXI_PROTECTEN_1) | MD1_PROT_BIT_MASK);
#ifndef IGNORE_MTCMOS_CHECK
		while ((spm_read(INFRA_TOPAXI_PROTECTSTA1_1) & MD1_PROT_BIT_ACK_MASK) != MD1_PROT_BIT_ACK_MASK)
			;
#endif
		/* TINFO="Set SRAM_PDN = 1" */
		spm_write(MD1_PWR_CON, spm_read(MD1_PWR_CON) | MD1_SRAM_PDN);
#ifndef IGNORE_MTCMOS_CHECK
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(MD1_PWR_CON, spm_read(MD1_PWR_CON) | PWR_ISO);
		/* TINFO="MD1_OUTPUT_PISO_S_EN_IZ[0]=1"*/
		spm_write(MD1_OUTPUT_PISO_S_EN_IZ, spm_read(MD1_OUTPUT_PISO_S_EN_IZ) | (0x1 << 0));
		/* TINFO="SPM_SW_RSV_7[0]=1"*/
		spm_write(SPM_SW_RSV_7, spm_read(SPM_SW_RSV_7) | (0x1 << 0));
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(MD1_PWR_CON, spm_read(MD1_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(MD1_PWR_CON, spm_read(MD1_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(MD1_PWR_CON, spm_read(MD1_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(MD1_PWR_CON, spm_read(MD1_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & MD1_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & MD1_PWR_STA_MASK))
			;
#endif
		/* TINFO="MD_EXT_BUCK_ISO[0]=1"*/
		spm_write(MD_EXT_BUCK_ISO, spm_read(MD_EXT_BUCK_ISO) | (0x1 << 0));
		/* TINFO="Finish to turn off MD1" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on MD1" */
		/* TINFO="MD_EXT_BUCK_ISO[0]=0"*/
		spm_write(MD_EXT_BUCK_ISO, spm_read(MD_EXT_BUCK_ISO) & ~(0x1 << 0));
		/* TINFO="Set PWR_ON = 1" */
		spm_write(MD1_PWR_CON, spm_read(MD1_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(MD1_PWR_CON, spm_read(MD1_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & MD1_PWR_STA_MASK) != MD1_PWR_STA_MASK)
			   || ((spm_read(PWR_STATUS_2ND) & MD1_PWR_STA_MASK) != MD1_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(MD1_PWR_CON, spm_read(MD1_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="SPM_SW_RSV_7[0]=0"*/
		spm_write(SPM_SW_RSV_7, spm_read(SPM_SW_RSV_7) & ~(0x1 << 0));
		/* TINFO="MD1_OUTPUT_PISO_S_EN_IZ[0]=0"*/
		spm_write(MD1_OUTPUT_PISO_S_EN_IZ, spm_read(MD1_OUTPUT_PISO_S_EN_IZ) & ~(0x1 << 0));
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(MD1_PWR_CON, spm_read(MD1_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set SRAM_PDN = 0" */
		spm_write(MD1_PWR_CON, spm_read(MD1_PWR_CON) & ~(0x1 << 8));
		/* TINFO="Release bus protect" */
		spm_write(INFRA_TOPAXI_PROTECTEN_1, spm_read(INFRA_TOPAXI_PROTECTEN_1) & ~MD1_PROT_BIT_MASK);
#ifndef IGNORE_MTCMOS_CHECK
		while (spm_read(INFRA_TOPAXI_PROTECTSTA1_1) & MD1_PROT_BIT_ACK_MASK)
			;
#endif
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(MD1_PWR_CON, spm_read(MD1_PWR_CON) | PWR_RST_B);
		/* TINFO="Finish to turn on MD1" */
	}
	return err;
}

int spm_mtcmos_ctrl_conn(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off CONN" */
		/* TINFO="Set bus protect" */
		spm_write(INFRA_TOPAXI_PROTECTEN, spm_read(INFRA_TOPAXI_PROTECTEN) | CONN_PROT_BIT_MASK);
#ifndef IGNORE_MTCMOS_CHECK
		while ((spm_read(INFRA_TOPAXI_PROTECTSTA1) & CONN_PROT_BIT_ACK_MASK) != CONN_PROT_BIT_ACK_MASK)
			;
#endif
#ifndef IGNORE_MTCMOS_CHECK
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(CONN_PWR_CON, spm_read(CONN_PWR_CON) | PWR_ISO);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(CONN_PWR_CON, spm_read(CONN_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(CONN_PWR_CON, spm_read(CONN_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(CONN_PWR_CON, spm_read(CONN_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(CONN_PWR_CON, spm_read(CONN_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & CONN_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & CONN_PWR_STA_MASK))
			;
#endif
		/* TINFO="Finish to turn off CONN" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on CONN" */
		/* TINFO="Set PWR_ON = 1" */
		spm_write(CONN_PWR_CON, spm_read(CONN_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(CONN_PWR_CON, spm_read(CONN_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & CONN_PWR_STA_MASK) != CONN_PWR_STA_MASK)
			   || ((spm_read(PWR_STATUS_2ND) & CONN_PWR_STA_MASK) != CONN_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(CONN_PWR_CON, spm_read(CONN_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(CONN_PWR_CON, spm_read(CONN_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(CONN_PWR_CON, spm_read(CONN_PWR_CON) | PWR_RST_B);
#ifndef IGNORE_MTCMOS_CHECK
#endif
		/* TINFO="Release bus protect" */
		spm_write(INFRA_TOPAXI_PROTECTEN, spm_read(INFRA_TOPAXI_PROTECTEN) & ~CONN_PROT_BIT_MASK);
#ifndef IGNORE_MTCMOS_CHECK
		while (spm_read(INFRA_TOPAXI_PROTECTSTA1) & CONN_PROT_BIT_ACK_MASK)
			;
#endif
		/* TINFO="Finish to turn on CONN" */
	}
	return err;
}

int spm_mtcmos_ctrl_dis(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off DIS" */
		/* TINFO="Set bus protect" */
		spm_write(INFRA_TOPAXI_PROTECTEN, spm_read(INFRA_TOPAXI_PROTECTEN) | DIS_PROT_BIT_MASK);
#ifndef IGNORE_MTCMOS_CHECK
		while ((spm_read(INFRA_TOPAXI_PROTECTSTA1) & DIS_PROT_BIT_ACK_MASK) != DIS_PROT_BIT_ACK_MASK)
			;
#endif
		/* TINFO="Set SRAM_PDN = 1" */
		spm_write(DIS_PWR_CON, spm_read(DIS_PWR_CON) | DIS_SRAM_PDN);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until DIS_SRAM_PDN_ACK = 1" */
		/* Need hf_fmm_ck for SRAM PDN delay IP. */
		while ((spm_read(DIS_PWR_CON) & DIS_SRAM_PDN_ACK) != DIS_SRAM_PDN_ACK)
			;
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(DIS_PWR_CON, spm_read(DIS_PWR_CON) | PWR_ISO);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(DIS_PWR_CON, spm_read(DIS_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(DIS_PWR_CON, spm_read(DIS_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(DIS_PWR_CON, spm_read(DIS_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(DIS_PWR_CON, spm_read(DIS_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & DIS_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & DIS_PWR_STA_MASK))
			;
#endif
		/* TINFO="Finish to turn off DIS" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on DIS" */
		/* TINFO="Set PWR_ON = 1" */
		spm_write(DIS_PWR_CON, spm_read(DIS_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(DIS_PWR_CON, spm_read(DIS_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & DIS_PWR_STA_MASK) != DIS_PWR_STA_MASK)
			   || ((spm_read(PWR_STATUS_2ND) & DIS_PWR_STA_MASK) != DIS_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(DIS_PWR_CON, spm_read(DIS_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(DIS_PWR_CON, spm_read(DIS_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(DIS_PWR_CON, spm_read(DIS_PWR_CON) | PWR_RST_B);
		/* TINFO="Set SRAM_PDN = 0" */
		spm_write(DIS_PWR_CON, spm_read(DIS_PWR_CON) & ~(0x1 << 8));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until DIS_SRAM_PDN_ACK_BIT0 = 0" */
		/* Need hf_fmm_ck for SRAM PDN delay IP. */
		while (spm_read(DIS_PWR_CON) & DIS_SRAM_PDN_ACK_BIT0)
			;
#endif
		/* TINFO="Release bus protect" */
		spm_write(INFRA_TOPAXI_PROTECTEN, spm_read(INFRA_TOPAXI_PROTECTEN) & ~DIS_PROT_BIT_MASK);
#ifndef IGNORE_MTCMOS_CHECK
		while (spm_read(INFRA_TOPAXI_PROTECTSTA1) & DIS_PROT_BIT_ACK_MASK)
			;
#endif
		/* TINFO="Finish to turn on DIS" */
	}
	return err;
}

int spm_mtcmos_ctrl_mfg(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off MFG" */
		/* TINFO="Set bus protect" */
		spm_write(INFRA_TOPAXI_PROTECTEN, spm_read(INFRA_TOPAXI_PROTECTEN) | MFG_PROT_BIT_MASK);
#ifndef IGNORE_MTCMOS_CHECK
		while ((spm_read(INFRA_TOPAXI_PROTECTSTA1) & MFG_PROT_BIT_ACK_MASK) != MFG_PROT_BIT_ACK_MASK)
			;
#endif
		/* TINFO="Set SRAM_PDN = 1" */
		spm_write(MFG_PWR_CON, spm_read(MFG_PWR_CON) | MFG_SRAM_PDN);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until MFG_SRAM_PDN_ACK = 1" */
		/* Need f_fmfg_core_ck for SRAM PDN delay IP. */
		while ((spm_read(MFG_PWR_CON) & MFG_SRAM_PDN_ACK) != MFG_SRAM_PDN_ACK)
			;
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(MFG_PWR_CON, spm_read(MFG_PWR_CON) | PWR_ISO);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(MFG_PWR_CON, spm_read(MFG_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(MFG_PWR_CON, spm_read(MFG_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(MFG_PWR_CON, spm_read(MFG_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(MFG_PWR_CON, spm_read(MFG_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & MFG_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & MFG_PWR_STA_MASK))
			;
#endif
		/* TINFO="Finish to turn off MFG" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on MFG" */
		/* TINFO="Set PWR_ON = 1" */
		spm_write(MFG_PWR_CON, spm_read(MFG_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(MFG_PWR_CON, spm_read(MFG_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & MFG_PWR_STA_MASK) != MFG_PWR_STA_MASK)
			   || ((spm_read(PWR_STATUS_2ND) & MFG_PWR_STA_MASK) != MFG_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(MFG_PWR_CON, spm_read(MFG_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(MFG_PWR_CON, spm_read(MFG_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(MFG_PWR_CON, spm_read(MFG_PWR_CON) | PWR_RST_B);
		/* TINFO="Set SRAM_PDN = 0" */
		spm_write(MFG_PWR_CON, spm_read(MFG_PWR_CON) & ~(0x1 << 8));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until MFG_SRAM_PDN_ACK_BIT0 = 0" */
		/* Need f_fmfg_core_ck for SRAM PDN delay IP. */
		while (spm_read(MFG_PWR_CON) & MFG_SRAM_PDN_ACK_BIT0)
			;
#endif
		/* TINFO="Release bus protect" */
		spm_write(INFRA_TOPAXI_PROTECTEN, spm_read(INFRA_TOPAXI_PROTECTEN) & ~MFG_PROT_BIT_MASK);
#ifndef IGNORE_MTCMOS_CHECK
		while (spm_read(INFRA_TOPAXI_PROTECTSTA1) & MFG_PROT_BIT_ACK_MASK)
			;
#endif
		/* TINFO="Finish to turn on MFG" */
	}
	return err;
}

int spm_mtcmos_ctrl_isp(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off ISP" */
		/* TINFO="Set SRAM_PDN = 1" */
		spm_write(ISP_PWR_CON, spm_read(ISP_PWR_CON) | ISP_SRAM_PDN);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until ISP_SRAM_PDN_ACK = 1" */
		/* Need hf_fmm_ck for SRAM PDN delay IP. */
		while ((spm_read(ISP_PWR_CON) & ISP_SRAM_PDN_ACK) != ISP_SRAM_PDN_ACK)
			;
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(ISP_PWR_CON, spm_read(ISP_PWR_CON) | PWR_ISO);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(ISP_PWR_CON, spm_read(ISP_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(ISP_PWR_CON, spm_read(ISP_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(ISP_PWR_CON, spm_read(ISP_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(ISP_PWR_CON, spm_read(ISP_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & ISP_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & ISP_PWR_STA_MASK))
			;
#endif
		/* TINFO="Finish to turn off ISP" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on ISP" */
		/* TINFO="Set PWR_ON = 1" */
		spm_write(ISP_PWR_CON, spm_read(ISP_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(ISP_PWR_CON, spm_read(ISP_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & ISP_PWR_STA_MASK) != ISP_PWR_STA_MASK)
			   || ((spm_read(PWR_STATUS_2ND) & ISP_PWR_STA_MASK) != ISP_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(ISP_PWR_CON, spm_read(ISP_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(ISP_PWR_CON, spm_read(ISP_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(ISP_PWR_CON, spm_read(ISP_PWR_CON) | PWR_RST_B);
		/* TINFO="Set SRAM_PDN = 0" */
		spm_write(ISP_PWR_CON, spm_read(ISP_PWR_CON) & ~(0x1 << 8));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until ISP_SRAM_PDN_ACK_BIT0 = 0" */
		/* Need hf_fmm_ck for SRAM PDN delay IP. */
		while (spm_read(ISP_PWR_CON) & ISP_SRAM_PDN_ACK_BIT0)
			;
#endif
		spm_write(ISP_PWR_CON, spm_read(ISP_PWR_CON) & ~(0x1 << 9));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until ISP_SRAM_PDN_ACK_BIT1 = 0" */
		/* Need hf_fmm_ck for SRAM PDN delay IP. */
		while (spm_read(ISP_PWR_CON) & ISP_SRAM_PDN_ACK_BIT1)
			;
#endif
		/* TINFO="Finish to turn on ISP" */
	}
	return err;
}

int spm_mtcmos_ctrl_vde(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off VDE" */
		/* TINFO="Set SRAM_PDN = 1" */
		spm_write(VDE_PWR_CON, spm_read(VDE_PWR_CON) | VDE_SRAM_PDN);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until VDE_SRAM_PDN_ACK = 1" */
		/* Need hf_fmm_ck for SRAM PDN delay IP. */
		while ((spm_read(VDE_PWR_CON) & VDE_SRAM_PDN_ACK) != VDE_SRAM_PDN_ACK)
			;
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(VDE_PWR_CON, spm_read(VDE_PWR_CON) | PWR_ISO);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(VDE_PWR_CON, spm_read(VDE_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(VDE_PWR_CON, spm_read(VDE_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(VDE_PWR_CON, spm_read(VDE_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(VDE_PWR_CON, spm_read(VDE_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & VDE_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & VDE_PWR_STA_MASK))
			;
#endif
		/* TINFO="Finish to turn off VDE" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on VDE" */
		/* TINFO="Set PWR_ON = 1" */
		spm_write(VDE_PWR_CON, spm_read(VDE_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(VDE_PWR_CON, spm_read(VDE_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & VDE_PWR_STA_MASK) != VDE_PWR_STA_MASK)
			   || ((spm_read(PWR_STATUS_2ND) & VDE_PWR_STA_MASK) != VDE_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(VDE_PWR_CON, spm_read(VDE_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(VDE_PWR_CON, spm_read(VDE_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(VDE_PWR_CON, spm_read(VDE_PWR_CON) | PWR_RST_B);
		/* TINFO="Set SRAM_PDN = 0" */
		spm_write(VDE_PWR_CON, spm_read(VDE_PWR_CON) & ~(0x1 << 8));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until VDE_SRAM_PDN_ACK_BIT0 = 0" */
		/* Need hf_fmm_ck for SRAM PDN delay IP. */
		while (spm_read(VDE_PWR_CON) & VDE_SRAM_PDN_ACK_BIT0)
			;
#endif
		/* TINFO="Finish to turn on VDE" */
	}
	return err;
}

int spm_mtcmos_ctrl_ven(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off VEN" */
		/* TINFO="Set SRAM_PDN = 1" */
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) | VEN_SRAM_PDN);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until VEN_SRAM_PDN_ACK = 1" */
		/* Need hf_fmm_ck for SRAM PDN delay IP. */
		while ((spm_read(VEN_PWR_CON) & VEN_SRAM_PDN_ACK) != VEN_SRAM_PDN_ACK)
			;
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) | PWR_ISO);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & VEN_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & VEN_PWR_STA_MASK))
			;
#endif
		/* TINFO="Finish to turn off VEN" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on VEN" */
		/* TINFO="Set PWR_ON = 1" */
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & VEN_PWR_STA_MASK) != VEN_PWR_STA_MASK)
			   || ((spm_read(PWR_STATUS_2ND) & VEN_PWR_STA_MASK) != VEN_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) | PWR_RST_B);
		/* TINFO="Set SRAM_PDN = 0" */
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) & ~(0x1 << 8));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until VEN_SRAM_PDN_ACK_BIT0 = 0" */
		/* Need hf_fmm_ck for SRAM PDN delay IP. */
		while (spm_read(VEN_PWR_CON) & VEN_SRAM_PDN_ACK_BIT0)
			;
#endif
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) & ~(0x1 << 9));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until VEN_SRAM_PDN_ACK_BIT1 = 0" */
		/* Need hf_fmm_ck for SRAM PDN delay IP. */
		while (spm_read(VEN_PWR_CON) & VEN_SRAM_PDN_ACK_BIT1)
			;
#endif
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) & ~(0x1 << 10));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until VEN_SRAM_PDN_ACK_BIT2 = 0" */
		/* Need hf_fmm_ck for SRAM PDN delay IP. */
		while (spm_read(VEN_PWR_CON) & VEN_SRAM_PDN_ACK_BIT2)
			;
#endif
		spm_write(VEN_PWR_CON, spm_read(VEN_PWR_CON) & ~(0x1 << 11));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until VEN_SRAM_PDN_ACK_BIT3 = 0" */
		/* Need hf_fmm_ck for SRAM PDN delay IP. */
		while (spm_read(VEN_PWR_CON) & VEN_SRAM_PDN_ACK_BIT3)
			;
#endif
		/* TINFO="Finish to turn on VEN" */
	}
	return err;
}

int spm_mtcmos_ctrl_mfg_async(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off MFG_ASYNC" */
#ifndef IGNORE_MTCMOS_CHECK
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(MFG_ASYNC_PWR_CON, spm_read(MFG_ASYNC_PWR_CON) | PWR_ISO);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(MFG_ASYNC_PWR_CON, spm_read(MFG_ASYNC_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(MFG_ASYNC_PWR_CON, spm_read(MFG_ASYNC_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(MFG_ASYNC_PWR_CON, spm_read(MFG_ASYNC_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(MFG_ASYNC_PWR_CON, spm_read(MFG_ASYNC_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & MFG_ASYNC_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & MFG_ASYNC_PWR_STA_MASK))
			;
#endif
		/* TINFO="Finish to turn off MFG_ASYNC" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on MFG_ASYNC" */
		/* TINFO="Set PWR_ON = 1" */
		spm_write(MFG_ASYNC_PWR_CON, spm_read(MFG_ASYNC_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(MFG_ASYNC_PWR_CON, spm_read(MFG_ASYNC_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & MFG_ASYNC_PWR_STA_MASK) != MFG_ASYNC_PWR_STA_MASK)
			   || ((spm_read(PWR_STATUS_2ND) & MFG_ASYNC_PWR_STA_MASK) != MFG_ASYNC_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(MFG_ASYNC_PWR_CON, spm_read(MFG_ASYNC_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(MFG_ASYNC_PWR_CON, spm_read(MFG_ASYNC_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(MFG_ASYNC_PWR_CON, spm_read(MFG_ASYNC_PWR_CON) | PWR_RST_B);
#ifndef IGNORE_MTCMOS_CHECK
#endif
		/* TINFO="Finish to turn on MFG_ASYNC" */
	}
	return err;
}

int spm_mtcmos_ctrl_audio(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off AUDIO" */
		/* TINFO="Set SRAM_PDN = 1" */
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) | AUDIO_SRAM_PDN);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until AUDIO_SRAM_PDN_ACK = 1" */
		/* Need f_f26M_aud_ck for SRAM PDN delay IP. */
		while ((spm_read(AUDIO_PWR_CON) & AUDIO_SRAM_PDN_ACK) != AUDIO_SRAM_PDN_ACK)
			;
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) | PWR_ISO);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & AUDIO_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & AUDIO_PWR_STA_MASK))
			;
#endif
		/* TINFO="Finish to turn off AUDIO" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on AUDIO" */
		/* TINFO="Set PWR_ON = 1" */
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & AUDIO_PWR_STA_MASK) != AUDIO_PWR_STA_MASK)
			   || ((spm_read(PWR_STATUS_2ND) & AUDIO_PWR_STA_MASK) != AUDIO_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) | PWR_RST_B);
		/* TINFO="Set SRAM_PDN = 0" */
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) & ~(0x1 << 8));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until AUDIO_SRAM_PDN_ACK_BIT0 = 0" */
		/* Need f_f26M_aud_ck for SRAM PDN delay IP. */
		while (spm_read(AUDIO_PWR_CON) & AUDIO_SRAM_PDN_ACK_BIT0)
			;
#endif
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) & ~(0x1 << 9));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until AUDIO_SRAM_PDN_ACK_BIT1 = 0" */
		/* Need f_f26M_aud_ck for SRAM PDN delay IP. */
		while (spm_read(AUDIO_PWR_CON) & AUDIO_SRAM_PDN_ACK_BIT1)
			;
#endif
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) & ~(0x1 << 10));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until AUDIO_SRAM_PDN_ACK_BIT2 = 0" */
		while (spm_read(AUDIO_PWR_CON) & AUDIO_SRAM_PDN_ACK_BIT2)
			;
#endif
		spm_write(AUDIO_PWR_CON, spm_read(AUDIO_PWR_CON) & ~(0x1 << 11));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until AUDIO_SRAM_PDN_ACK_BIT3 = 0" */
		while (spm_read(AUDIO_PWR_CON) & AUDIO_SRAM_PDN_ACK_BIT3)
			;
#endif
		/* TINFO="Finish to turn on AUDIO" */
	}
	return err;
}

int spm_mtcmos_ctrl_cam(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off CAM" */
		/* TINFO="Set SRAM_PDN = 1" */
		spm_write(CAM_PWR_CON, spm_read(CAM_PWR_CON) | CAM_SRAM_PDN);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until CAM_SRAM_PDN_ACK = 1" */
		while ((spm_read(CAM_PWR_CON) & CAM_SRAM_PDN_ACK) != CAM_SRAM_PDN_ACK)
			;
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(CAM_PWR_CON, spm_read(CAM_PWR_CON) | PWR_ISO);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(CAM_PWR_CON, spm_read(CAM_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(CAM_PWR_CON, spm_read(CAM_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(CAM_PWR_CON, spm_read(CAM_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(CAM_PWR_CON, spm_read(CAM_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & CAM_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & CAM_PWR_STA_MASK))
			;
#endif
		/* TINFO="Finish to turn off CAM" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on CAM" */
		/* TINFO="Set PWR_ON = 1" */
		spm_write(CAM_PWR_CON, spm_read(CAM_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(CAM_PWR_CON, spm_read(CAM_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & CAM_PWR_STA_MASK) != CAM_PWR_STA_MASK)
			   || ((spm_read(PWR_STATUS_2ND) & CAM_PWR_STA_MASK) != CAM_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(CAM_PWR_CON, spm_read(CAM_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(CAM_PWR_CON, spm_read(CAM_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(CAM_PWR_CON, spm_read(CAM_PWR_CON) | PWR_RST_B);
		/* TINFO="Set SRAM_PDN = 0" */
		spm_write(CAM_PWR_CON, spm_read(CAM_PWR_CON) & ~(0x1 << 8));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until CAM_SRAM_PDN_ACK_BIT0 = 0" */
		while (spm_read(CAM_PWR_CON) & CAM_SRAM_PDN_ACK_BIT0)
			;
#endif
		spm_write(CAM_PWR_CON, spm_read(CAM_PWR_CON) & ~(0x1 << 9));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until CAM_SRAM_PDN_ACK_BIT1 = 0" */
		while (spm_read(CAM_PWR_CON) & CAM_SRAM_PDN_ACK_BIT1)
			;
#endif
		/* TINFO="Finish to turn on CAM" */
	}
	return err;
}

int spm_mtcmos_ctrl_c2k(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off C2K" */
		/* TINFO="Set bus protect" */
		spm_write(INFRA_TOPAXI_PROTECTEN_1, spm_read(INFRA_TOPAXI_PROTECTEN_1) | C2K_PROT_BIT_MASK);
#ifndef IGNORE_MTCMOS_CHECK
		while ((spm_read(INFRA_TOPAXI_PROTECTSTA1_1) & C2K_PROT_BIT_ACK_MASK) != C2K_PROT_BIT_ACK_MASK)
			;
#endif
#ifndef IGNORE_MTCMOS_CHECK
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(C2K_PWR_CON, spm_read(C2K_PWR_CON) | PWR_ISO);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(C2K_PWR_CON, spm_read(C2K_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(C2K_PWR_CON, spm_read(C2K_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(C2K_PWR_CON, spm_read(C2K_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(C2K_PWR_CON, spm_read(C2K_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & C2K_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & C2K_PWR_STA_MASK))
			;
#endif
		/* TINFO="Finish to turn off C2K" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on C2K" */
		/* TINFO="Set PWR_ON = 1" */
		spm_write(C2K_PWR_CON, spm_read(C2K_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(C2K_PWR_CON, spm_read(C2K_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & C2K_PWR_STA_MASK) != C2K_PWR_STA_MASK)
			   || ((spm_read(PWR_STATUS_2ND) & C2K_PWR_STA_MASK) != C2K_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(C2K_PWR_CON, spm_read(C2K_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(C2K_PWR_CON, spm_read(C2K_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(C2K_PWR_CON, spm_read(C2K_PWR_CON) | PWR_RST_B);
#ifndef IGNORE_MTCMOS_CHECK
#endif
		/* TINFO="Release bus protect" */
		spm_write(INFRA_TOPAXI_PROTECTEN_1, spm_read(INFRA_TOPAXI_PROTECTEN_1) & ~C2K_PROT_BIT_MASK);
#ifndef IGNORE_MTCMOS_CHECK
		while (spm_read(INFRA_TOPAXI_PROTECTSTA1_1) & C2K_PROT_BIT_ACK_MASK)
			;
#endif
		/* TINFO="Finish to turn on C2K" */
	}
	return err;
}

int spm_mtcmos_ctrl_mdsys_intf_infra(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off MDSYS_INTF_INFRA" */
		/* TINFO="Set bus protect" */
		spm_write(INFRA_TOPAXI_PROTECTEN, spm_read(INFRA_TOPAXI_PROTECTEN) | MDSYS_INTF_INFRA_PROT_BIT_MASK);
#ifndef IGNORE_MTCMOS_CHECK
		while ((spm_read(INFRA_TOPAXI_PROTECTSTA1) & MDSYS_INTF_INFRA_PROT_BIT_ACK_MASK)
			!= MDSYS_INTF_INFRA_PROT_BIT_ACK_MASK)
			;
#endif
#ifndef IGNORE_MTCMOS_CHECK
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(MDSYS_INTF_INFRA_PWR_CON, spm_read(MDSYS_INTF_INFRA_PWR_CON) | PWR_ISO);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(MDSYS_INTF_INFRA_PWR_CON, spm_read(MDSYS_INTF_INFRA_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(MDSYS_INTF_INFRA_PWR_CON, spm_read(MDSYS_INTF_INFRA_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(MDSYS_INTF_INFRA_PWR_CON, spm_read(MDSYS_INTF_INFRA_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(MDSYS_INTF_INFRA_PWR_CON, spm_read(MDSYS_INTF_INFRA_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & MDSYS_INTF_INFRA_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & MDSYS_INTF_INFRA_PWR_STA_MASK))
			;
#endif
		/* TINFO="Finish to turn off MDSYS_INTF_INFRA" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on MDSYS_INTF_INFRA" */
		/* TINFO="Set PWR_ON = 1" */
		spm_write(MDSYS_INTF_INFRA_PWR_CON, spm_read(MDSYS_INTF_INFRA_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(MDSYS_INTF_INFRA_PWR_CON, spm_read(MDSYS_INTF_INFRA_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & MDSYS_INTF_INFRA_PWR_STA_MASK) != MDSYS_INTF_INFRA_PWR_STA_MASK)
		|| ((spm_read(PWR_STATUS_2ND) & MDSYS_INTF_INFRA_PWR_STA_MASK) != MDSYS_INTF_INFRA_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(MDSYS_INTF_INFRA_PWR_CON, spm_read(MDSYS_INTF_INFRA_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(MDSYS_INTF_INFRA_PWR_CON, spm_read(MDSYS_INTF_INFRA_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(MDSYS_INTF_INFRA_PWR_CON, spm_read(MDSYS_INTF_INFRA_PWR_CON) | PWR_RST_B);
#ifndef IGNORE_MTCMOS_CHECK
#endif
		/* TINFO="Release bus protect" */
		spm_write(INFRA_TOPAXI_PROTECTEN, spm_read(INFRA_TOPAXI_PROTECTEN) & ~MDSYS_INTF_INFRA_PROT_BIT_MASK);
#ifndef IGNORE_MTCMOS_CHECK
		while (spm_read(INFRA_TOPAXI_PROTECTSTA1) & MDSYS_INTF_INFRA_PROT_BIT_ACK_MASK)
			;
#endif
		/* TINFO="Finish to turn on MDSYS_INTF_INFRA" */
	}
	return err;
}

int spm_mtcmos_ctrl_mfg_core1(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off MFG_CORE1" */
		/* TINFO="Set SRAM_PDN = 1" */
		spm_write(MFG_CORE1_PWR_CON, spm_read(MFG_CORE1_PWR_CON) | MFG_CORE1_SRAM_PDN);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until MFG_CORE1_SRAM_PDN_ACK = 1" */
		while ((spm_read(MFG_CORE1_PWR_CON) & MFG_CORE1_SRAM_PDN_ACK) != MFG_CORE1_SRAM_PDN_ACK)
			;
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(MFG_CORE1_PWR_CON, spm_read(MFG_CORE1_PWR_CON) | PWR_ISO);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(MFG_CORE1_PWR_CON, spm_read(MFG_CORE1_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(MFG_CORE1_PWR_CON, spm_read(MFG_CORE1_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(MFG_CORE1_PWR_CON, spm_read(MFG_CORE1_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(MFG_CORE1_PWR_CON, spm_read(MFG_CORE1_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & MFG_CORE1_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & MFG_CORE1_PWR_STA_MASK))
			;
#endif
		/* TINFO="Finish to turn off MFG_CORE1" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on MFG_CORE1" */
		/* TINFO="Set PWR_ON = 1" */
		spm_write(MFG_CORE1_PWR_CON, spm_read(MFG_CORE1_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(MFG_CORE1_PWR_CON, spm_read(MFG_CORE1_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & MFG_CORE1_PWR_STA_MASK) != MFG_CORE1_PWR_STA_MASK)
			   || ((spm_read(PWR_STATUS_2ND) & MFG_CORE1_PWR_STA_MASK) != MFG_CORE1_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(MFG_CORE1_PWR_CON, spm_read(MFG_CORE1_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(MFG_CORE1_PWR_CON, spm_read(MFG_CORE1_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(MFG_CORE1_PWR_CON, spm_read(MFG_CORE1_PWR_CON) | PWR_RST_B);
		/* TINFO="Set SRAM_PDN = 0" */
		spm_write(MFG_CORE1_PWR_CON, spm_read(MFG_CORE1_PWR_CON) & ~(0x1 << 5));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until MFG_CORE1_SRAM_PDN_ACK_BIT0 = 0" */
		while (spm_read(MFG_CORE1_PWR_CON) & MFG_CORE1_SRAM_PDN_ACK_BIT0)
			;
#endif
		/* TINFO="Finish to turn on MFG_CORE1" */
	}
	return err;
}

int spm_mtcmos_ctrl_mfg_core0(int state)
{
	int err = 0;

	/* TINFO="enable SPM register control" */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | (0x1 << 0));

	if (state == STA_POWER_DOWN) {
		/* TINFO="Start to turn off MFG_CORE0" */
		/* TINFO="Set SRAM_PDN = 1" */
		spm_write(MFG_CORE0_PWR_CON, spm_read(MFG_CORE0_PWR_CON) | MFG_CORE0_SRAM_PDN);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until MFG_CORE0_SRAM_PDN_ACK = 1" */
		while ((spm_read(MFG_CORE0_PWR_CON) & MFG_CORE0_SRAM_PDN_ACK) != MFG_CORE0_SRAM_PDN_ACK)
			;
#endif
		/* TINFO="Set PWR_ISO = 1" */
		spm_write(MFG_CORE0_PWR_CON, spm_read(MFG_CORE0_PWR_CON) | PWR_ISO);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		spm_write(MFG_CORE0_PWR_CON, spm_read(MFG_CORE0_PWR_CON) | PWR_CLK_DIS);
		/* TINFO="Set PWR_RST_B = 0" */
		spm_write(MFG_CORE0_PWR_CON, spm_read(MFG_CORE0_PWR_CON) & ~PWR_RST_B);
		/* TINFO="Set PWR_ON = 0" */
		spm_write(MFG_CORE0_PWR_CON, spm_read(MFG_CORE0_PWR_CON) & ~PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		spm_write(MFG_CORE0_PWR_CON, spm_read(MFG_CORE0_PWR_CON) & ~PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((spm_read(PWR_STATUS) & MFG_CORE0_PWR_STA_MASK)
			   || (spm_read(PWR_STATUS_2ND) & MFG_CORE0_PWR_STA_MASK))
			;
#endif
		/* TINFO="Finish to turn off MFG_CORE0" */
	} else {	/* STA_POWER_ON */
		/* TINFO="Start to turn on MFG_CORE0" */
		/* TINFO="Set PWR_ON = 1" */
		spm_write(MFG_CORE0_PWR_CON, spm_read(MFG_CORE0_PWR_CON) | PWR_ON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		spm_write(MFG_CORE0_PWR_CON, spm_read(MFG_CORE0_PWR_CON) | PWR_ON_2ND);
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((spm_read(PWR_STATUS) & MFG_CORE0_PWR_STA_MASK) != MFG_CORE0_PWR_STA_MASK)
				|| ((spm_read(PWR_STATUS_2ND) & MFG_CORE0_PWR_STA_MASK) != MFG_CORE0_PWR_STA_MASK))
			;
#endif
		/* TINFO="Set PWR_CLK_DIS = 0" */
		spm_write(MFG_CORE0_PWR_CON, spm_read(MFG_CORE0_PWR_CON) & ~PWR_CLK_DIS);
		/* TINFO="Set PWR_ISO = 0" */
		spm_write(MFG_CORE0_PWR_CON, spm_read(MFG_CORE0_PWR_CON) & ~PWR_ISO);
		/* TINFO="Set PWR_RST_B = 1" */
		spm_write(MFG_CORE0_PWR_CON, spm_read(MFG_CORE0_PWR_CON) | PWR_RST_B);
		/* TINFO="Set SRAM_PDN = 0" */
		spm_write(MFG_CORE0_PWR_CON, spm_read(MFG_CORE0_PWR_CON) & ~(0x1 << 5));
#ifndef IGNORE_MTCMOS_CHECK
		/* TINFO="Wait until MFG_CORE0_SRAM_PDN_ACK_BIT0 = 0" */
		while (spm_read(MFG_CORE0_PWR_CON) & MFG_CORE0_SRAM_PDN_ACK_BIT0)
			;
#endif
		/* TINFO="Finish to turn on MFG_CORE0" */
	}
	return err;
}


/* auto-gen end*/

/* enable op*/
static int MD1_sys_enable_op(struct subsys *sys)
{
	return spm_mtcmos_ctrl_md1(STA_POWER_ON);
}

static int CONN_sys_enable_op(struct subsys *sys)
{
	/*printk("CONN_sys_enable_op\r\n"); */
	return spm_mtcmos_ctrl_conn(STA_POWER_ON);
}

static int DIS_sys_enable_op(struct subsys *sys)
{
	/*printk("DIS_sys_enable_op\r\n"); */
	return spm_mtcmos_ctrl_dis(STA_POWER_ON);
}

static int MFG_sys_enable_op(struct subsys *sys)
{
	/*printk("MFG_sys_enable_op\r\n"); */
	return spm_mtcmos_ctrl_mfg(STA_POWER_ON);
}

static int ISP_sys_enable_op(struct subsys *sys)
{
	/*printk("ISP_sys_enable_op\r\n"); */
	return spm_mtcmos_ctrl_isp(STA_POWER_ON);
}

static int VDE_sys_enable_op(struct subsys *sys)
{
	/*printk("VDE_sys_enable_op\r\n"); */
	return spm_mtcmos_ctrl_vde(STA_POWER_ON);
}

static int VEN_sys_enable_op(struct subsys *sys)
{
	/*printk("VEN_sys_enable_op\r\n"); */
	return spm_mtcmos_ctrl_ven(STA_POWER_ON);
}

static int MFG_ASYNC_sys_enable_op(struct subsys *sys)
{
	/*printk("MFG_ASYNC_sys_enable_op\r\n"); */
	return spm_mtcmos_ctrl_mfg_async(STA_POWER_ON);
}

static int AUDIO_sys_enable_op(struct subsys *sys)
{
	/*printk("AUDIO_sys_enable_op\r\n"); */
	return spm_mtcmos_ctrl_audio(STA_POWER_ON);
}

static int CAM_sys_enable_op(struct subsys *sys)
{
	/*printk("CAM_sys_enable_op\r\n"); */
	return spm_mtcmos_ctrl_cam(STA_POWER_ON);
}

static int C2K_sys_enable_op(struct subsys *sys)
{
	/*printk("C2K_sys_enable_op\r\n"); */
	return spm_mtcmos_ctrl_c2k(STA_POWER_ON);
}

static int MDSYS_INTF_INFRA_sys_enable_op(struct subsys *sys)
{
	/*printk("MDSYS_INTF_INFRA_sys_enable_op\r\n"); */
	return spm_mtcmos_ctrl_mdsys_intf_infra(STA_POWER_ON);
}

static int MFG_CORE1_sys_enable_op(struct subsys *sys)
{
	/*printk("MFG_CORE1_sys_enable_op\r\n"); */
	return spm_mtcmos_ctrl_mfg_core1(STA_POWER_ON);
}

static int MFG_CORE0_sys_enable_op(struct subsys *sys)
{
	/*printk("MFG_CORE0_sys_enable_op\r\n"); */
	return spm_mtcmos_ctrl_mfg_core0(STA_POWER_ON);
}

/* disable op */
static int MD1_sys_disable_op(struct subsys *sys)
{
	return spm_mtcmos_ctrl_md1(STA_POWER_DOWN);
}

static int CONN_sys_disable_op(struct subsys *sys)
{
	/*printk("CONN_sys_disable_op\r\n"); */
	return spm_mtcmos_ctrl_conn(STA_POWER_DOWN);
}

static int DIS_sys_disable_op(struct subsys *sys)
{
	/*printk("DIS_sys_disable_op\r\n"); */
	return spm_mtcmos_ctrl_dis(STA_POWER_DOWN);
}

static int MFG_sys_disable_op(struct subsys *sys)
{
	/*printk("MFG_sys_disable_op\r\n"); */
	return spm_mtcmos_ctrl_mfg(STA_POWER_DOWN);
}

static int ISP_sys_disable_op(struct subsys *sys)
{
	/*printk("ISP_sys_disable_op\r\n"); */
	return spm_mtcmos_ctrl_isp(STA_POWER_DOWN);
}

static int VDE_sys_disable_op(struct subsys *sys)
{
	/*printk("VDE_sys_disable_op\r\n"); */
	return spm_mtcmos_ctrl_vde(STA_POWER_DOWN);
}

static int VEN_sys_disable_op(struct subsys *sys)
{
	/*printk("VEN_sys_disable_op\r\n"); */
	return spm_mtcmos_ctrl_ven(STA_POWER_DOWN);
}

static int MFG_ASYNC_sys_disable_op(struct subsys *sys)
{
	/*printk("MFG_ASYNC_sys_disable_op\r\n"); */
	return spm_mtcmos_ctrl_mfg_async(STA_POWER_DOWN);
}

static int AUDIO_sys_disable_op(struct subsys *sys)
{
	/*printk("AUDIO_sys_disable_op\r\n"); */
	return spm_mtcmos_ctrl_audio(STA_POWER_DOWN);
}

static int CAM_sys_disable_op(struct subsys *sys)
{
	/*printk("CAM_sys_disable_op\r\n"); */
	return spm_mtcmos_ctrl_cam(STA_POWER_DOWN);
}

static int C2K_sys_disable_op(struct subsys *sys)
{
	/*printk("C2K_sys_disable_op\r\n"); */
	return spm_mtcmos_ctrl_c2k(STA_POWER_DOWN);
}

static int MDSYS_INTF_INFRA_sys_disable_op(struct subsys *sys)
{
	/*printk("MDSYS_INTF_INFRA_sys_disable_op\r\n"); */
	return spm_mtcmos_ctrl_mdsys_intf_infra(STA_POWER_DOWN);
}

static int MFG_CORE1_sys_disable_op(struct subsys *sys)
{
	/*printk("MFG_CORE1_sys_disable_op\r\n"); */
	return spm_mtcmos_ctrl_mfg_core1(STA_POWER_DOWN);
}

static int MFG_CORE0_sys_disable_op(struct subsys *sys)
{
	/*printk("MFG_CORE0_sys_disable_op\r\n"); */
	return spm_mtcmos_ctrl_mfg_core0(STA_POWER_DOWN);
}

static int sys_get_state_op(struct subsys *sys)
{
	unsigned int sta = clk_readl(PWR_STATUS);
	unsigned int sta_s = clk_readl(PWR_STATUS_2ND);

	return (sta & sys->sta_mask) && (sta_s & sys->sta_mask);
}

/* ops */

static struct subsys_ops MD1_sys_ops = {
	.enable = MD1_sys_enable_op,
	.disable = MD1_sys_disable_op,
	.get_state = sys_get_state_op,
};

static struct subsys_ops CONN_sys_ops = {
	.enable = CONN_sys_enable_op,
	.disable = CONN_sys_disable_op,
	.get_state = sys_get_state_op,
};

static struct subsys_ops DIS_sys_ops = {
	.enable = DIS_sys_enable_op,
	.disable = DIS_sys_disable_op,
	.get_state = sys_get_state_op,
};

static struct subsys_ops MFG_sys_ops = {
	.enable = MFG_sys_enable_op,
	.disable = MFG_sys_disable_op,
	.get_state = sys_get_state_op,
};

static struct subsys_ops ISP_sys_ops = {
	.enable = ISP_sys_enable_op,
	.disable = ISP_sys_disable_op,
	.get_state = sys_get_state_op,
};

static struct subsys_ops VDE_sys_ops = {
	.enable = VDE_sys_enable_op,
	.disable = VDE_sys_disable_op,
	.get_state = sys_get_state_op,
};

static struct subsys_ops VEN_sys_ops = {
	.enable = VEN_sys_enable_op,
	.disable = VEN_sys_disable_op,
	.get_state = sys_get_state_op,
};

static struct subsys_ops MFG_ASYNC_sys_ops = {
	.enable = MFG_ASYNC_sys_enable_op,
	.disable = MFG_ASYNC_sys_disable_op,
	.get_state = sys_get_state_op,
};

static struct subsys_ops AUDIO_sys_ops = {
	.enable = AUDIO_sys_enable_op,
	.disable = AUDIO_sys_disable_op,
	.get_state = sys_get_state_op,
};

static struct subsys_ops CAM_sys_ops = {
	.enable = CAM_sys_enable_op,
	.disable = CAM_sys_disable_op,
	.get_state = sys_get_state_op,
};

static struct subsys_ops C2K_sys_ops = {
	.enable = C2K_sys_enable_op,
	.disable = C2K_sys_disable_op,
	.get_state = sys_get_state_op,
};

static struct subsys_ops MDSYS_INTF_INFRA_sys_ops = {
	.enable = MDSYS_INTF_INFRA_sys_enable_op,
	.disable = MDSYS_INTF_INFRA_sys_disable_op,
	.get_state = sys_get_state_op,
};

static struct subsys_ops MFG_CORE1_sys_ops = {
	.enable = MFG_CORE1_sys_enable_op,
	.disable = MFG_CORE1_sys_disable_op,
	.get_state = sys_get_state_op,
};

static struct subsys_ops MFG_CORE0_sys_ops = {
	.enable = MFG_CORE0_sys_enable_op,
	.disable = MFG_CORE0_sys_disable_op,
	.get_state = sys_get_state_op,
};


static int subsys_is_on(enum subsys_id id)
{
	int r;
	struct subsys *sys = id_to_sys(id);

	BUG_ON(!sys);

	r = sys->ops->get_state(sys);

#if MT_CCF_DEBUG
	pr_debug("[CCF] %s:%d, sys=%s, id=%d\n", __func__, r, sys->name, id);
#endif				/* MT_CCF_DEBUG */

	return r;
}

#if CONTROL_LIMIT
int allow[NR_SYSS] = {
1,	/*SYS_MD1 = 0,*/
1,	/*SYS_CONN = 1,*/
1,	/*SYS_DIS = 2,*/
1,	/*SYS_MFG = 3,*/
1,	/*SYS_ISP = 4,*/
1,	/*SYS_VDE = 5,*/
1,	/*SYS_VEN = 6,*/
1,	/*SYS_MFG_ASYNC = 7,*/
1,	/*SYS_AUDIO = 8,*/
1,	/*SYS_CAM = 9,*/
1,	/*SYS_C2K = 10,*/
1,	/*SYS_MDSYS_INTF_INFRA = 11,*/
1,	/*SYS_MFG_CORE1 = 12,*/
1,	/*SYS_MFG_CORE0 = 13,*/
};
#endif
static int enable_subsys(enum subsys_id id)
{
	int r;
	unsigned long flags;
	struct subsys *sys = id_to_sys(id);

	BUG_ON(!sys);

#if MT_CCF_BRINGUP
	pr_debug("[CCF] %s: sys=%s, id=%d\n", __func__, sys->name, id);
	switch (id) {
	case SYS_MD1:
		spm_mtcmos_ctrl_md1(STA_POWER_ON);
		break;
	case SYS_C2K:
		spm_mtcmos_ctrl_c2k(STA_POWER_ON);
		break;
	case SYS_CONN:
		spm_mtcmos_ctrl_conn(STA_POWER_ON);
		break;
	default:
		break;
	}
	return 0;
#endif				/* MT_CCF_BRINGUP */

#if CONTROL_LIMIT
	#if MT_CCF_DEBUG
	pr_debug("[CCF] %s: sys=%s, id=%d\n", __func__, sys->name, id);
	#endif
	if (allow[id] == 0) {
		#if MT_CCF_DEBUG
		pr_debug("[CCF] %s: do nothing return\n", __func__);
		#endif
		return 0;
	}
#endif


	mtk_clk_lock(flags);

#if CHECK_PWR_ST
	if (sys->ops->get_state(sys) == SUBSYS_PWR_ON) {
		mtk_clk_unlock(flags);
		return 0;
	}
#endif				/* CHECK_PWR_ST */

	r = sys->ops->enable(sys);
	WARN_ON(r);

	mtk_clk_unlock(flags);

	if (g_pgcb && g_pgcb->after_on)
		g_pgcb->after_on(id);

	return r;
}

static int disable_subsys(enum subsys_id id)
{
	int r;
	unsigned long flags;
	struct subsys *sys = id_to_sys(id);

	BUG_ON(!sys);

#if MT_CCF_BRINGUP
	pr_debug("[CCF] %s: sys=%s, id=%d\n", __func__, sys->name, id);
	switch (id) {
	case SYS_MD1:
		spm_mtcmos_ctrl_md1(STA_POWER_DOWN);
		break;
	case SYS_C2K:
		spm_mtcmos_ctrl_c2k(STA_POWER_DOWN);
		break;
	case SYS_CONN:
		spm_mtcmos_ctrl_conn(STA_POWER_DOWN);
		break;
	default:
		break;
	}
	return 0;
#endif				/* MT_CCF_BRINGUP */
#if CONTROL_LIMIT
	#if MT_CCF_DEBUG
	pr_debug("[CCF] %s: sys=%s, id=%d\n", __func__, sys->name, id);
	#endif
	if (allow[id] == 0) {
		#if MT_CCF_DEBUG
		pr_debug("[CCF] %s: do nothing return\n", __func__);
		#endif
		return 0;
	}
#endif



	/* TODO: check all clocks related to this subsys are off */
	/* could be power off or not */

	if (g_pgcb && g_pgcb->before_off)
		g_pgcb->before_off(id);

	mtk_clk_lock(flags);

#if CHECK_PWR_ST
	if (sys->ops->get_state(sys) == SUBSYS_PWR_DOWN) {
		mtk_clk_unlock(flags);
		return 0;
	}
#endif				/* CHECK_PWR_ST */

	r = sys->ops->disable(sys);
	WARN_ON(r);

	mtk_clk_unlock(flags);

	return r;
}


struct mt_power_gate {
	struct clk_hw hw;
	struct clk *pre_clk;
	enum subsys_id pd_id;
};

#define to_power_gate(_hw) container_of(_hw, struct mt_power_gate, hw)

static int pg_enable(struct clk_hw *hw)
{
	struct mt_power_gate *pg = to_power_gate(hw);

#if MT_CCF_DEBUG
	pr_debug("[CCF] %s: sys=%s, pd_id=%u\n", __func__,
		 __clk_get_name(hw->clk), pg->pd_id);
#endif				/* MT_CCF_DEBUG */

	return enable_subsys(pg->pd_id);
}

static void pg_disable(struct clk_hw *hw)
{
	struct mt_power_gate *pg = to_power_gate(hw);

#if MT_CCF_DEBUG
	pr_debug("[CCF] %s: sys=%s, pd_id=%u\n", __func__,
		 __clk_get_name(hw->clk), pg->pd_id);
#endif				/* MT_CCF_DEBUG */

	disable_subsys(pg->pd_id);
}

static int pg_is_enabled(struct clk_hw *hw)
{
	struct mt_power_gate *pg = to_power_gate(hw);
#if MT_CCF_BRINGUP
	return 1;
#endif				/* MT_CCF_BRINGUP */

	return subsys_is_on(pg->pd_id);
}

int pg_prepare(struct clk_hw *hw)
{
	int r;
	struct mt_power_gate *pg = to_power_gate(hw);

#if MT_CCF_DEBUG
	pr_debug("[CCF] %s: sys=%s, pre_sys=%s\n", __func__,
		 __clk_get_name(hw->clk),
		 pg->pre_clk ? __clk_get_name(pg->pre_clk) : "");
#endif				/* MT_CCF_DEBUG */

	if (pg->pre_clk) {
		r = clk_prepare_enable(pg->pre_clk);
		if (r)
			return r;
	}

	return pg_enable(hw);

}

void pg_unprepare(struct clk_hw *hw)
{
	struct mt_power_gate *pg = to_power_gate(hw);

#if MT_CCF_DEBUG
	pr_debug("[CCF] %s: clk=%s, pre_clk=%s\n", __func__,
		 __clk_get_name(hw->clk),
		 pg->pre_clk ? __clk_get_name(pg->pre_clk) : "");
#endif				/* MT_CCF_DEBUG */

	pg_disable(hw);

	if (pg->pre_clk)
		clk_disable_unprepare(pg->pre_clk);
}

static const struct clk_ops mt_power_gate_ops = {
	.prepare = pg_prepare,
	.unprepare = pg_unprepare,
	.is_enabled = pg_is_enabled,
};

struct clk *mt_clk_register_power_gate(const char *name,
				       const char *parent_name,
				       struct clk *pre_clk, enum subsys_id pd_id)
{
	struct mt_power_gate *pg;
	struct clk *clk;
	struct clk_init_data init;

	pg = kzalloc(sizeof(*pg), GFP_KERNEL);
	if (!pg)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = CLK_IGNORE_UNUSED;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;
	init.ops = &mt_power_gate_ops;

	pg->pre_clk = pre_clk;
	pg->pd_id = pd_id;
	pg->hw.init = &init;

	clk = clk_register(NULL, &pg->hw);
	if (IS_ERR(clk))
		kfree(pg);

	return clk;
}

#define pg_md1         "pg_md1"
#define pg_conn        "pg_conn"
#define pg_dis         "pg_dis"
#define pg_mfg         "pg_mfg"
#define pg_isp         "pg_isp"
#define pg_vde         "pg_vde"
#define pg_ven         "pg_ven"
#define pg_mfg_async   "pg_mfg_async"
#define pg_audio       "pg_audio"
#define pg_cam   "pg_cam"
#define pg_c2k         "pg_c2k"
#define pg_mdsys_intf_infra   "pg_mdsys_intf_infra"
#define pg_mfg_core1   "pg_mfg_core1"
#define pg_mfg_core0   "pg_mfg_core0"

#define mm_sel			"mm_sel"
#define f26m_sel	"f26m_sel"
#define infracfg_ao_audio_26m_bclk_ck "infracfg_ao_audio_26m_bclk_ck"
/* FIXME: set correct value: E */

struct mtk_power_gate {
	int id;
	const char *name;
	const char *parent_name;
	const char *pre_clk_name;
	enum subsys_id pd_id;
};

#define PGATE(_id, _name, _parent, _pre_clk, _pd_id) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.pre_clk_name = _pre_clk,		\
		.pd_id = _pd_id,			\
	}

/* FIXME: all values needed to be verified */
struct mtk_power_gate scp_clks[] __initdata = {
	PGATE(SCP_SYS_MD1, pg_md1, NULL, NULL, SYS_MD1),
	PGATE(SCP_SYS_CONN, pg_conn, NULL, NULL, SYS_CONN),
	PGATE(SCP_SYS_DIS, pg_dis, NULL, mm_sel, SYS_DIS),
	PGATE(SCP_SYS_MFG_ASYNC, pg_mfg_async, NULL, NULL, SYS_MFG_ASYNC),
	PGATE(SCP_SYS_MFG, pg_mfg, pg_mfg_async, NULL, SYS_MFG),
	PGATE(SCP_SYS_ISP, pg_isp, NULL, mm_sel, SYS_ISP),
	PGATE(SCP_SYS_VDE, pg_vde, NULL, mm_sel, SYS_VDE),
	PGATE(SCP_SYS_VEN, pg_ven, NULL, mm_sel, SYS_VEN),
	PGATE(SCP_SYS_AUDIO, pg_audio, NULL, infracfg_ao_audio_26m_bclk_ck, SYS_AUDIO),
	PGATE(SCP_SYS_CAM, pg_cam, NULL, mm_sel, SYS_CAM),
	PGATE(SCP_SYS_C2K, pg_c2k, NULL, NULL, SYS_C2K),
	PGATE(SCP_SYS_MDSYS_INTF_INFRA, pg_mdsys_intf_infra, NULL, NULL, SYS_MDSYS_INTF_INFRA),
	PGATE(SCP_SYS_MFG_CORE1, pg_mfg_core1, pg_mfg, NULL, SYS_MFG_CORE1),
	PGATE(SCP_SYS_MFG_CORE0, pg_mfg_core0, pg_mfg, NULL, SYS_MFG_CORE0),




};

static void __init init_clk_scpsys(void __iomem *infracfg_reg,
				   void __iomem *spm_reg, void __iomem *infra_reg, struct clk_onecell_data *clk_data)
{
	int i;
	struct clk *clk;
	struct clk *pre_clk;

	infracfg_base = infracfg_reg;
	spm_base = spm_reg;
	infra_base = infra_reg;

	syss[SYS_MD1].ctl_addr = MD1_PWR_CON;
	syss[SYS_CONN].ctl_addr = CONN_PWR_CON;
	syss[SYS_DIS].ctl_addr = DIS_PWR_CON;
	syss[SYS_MFG].ctl_addr = MFG_PWR_CON;
	syss[SYS_ISP].ctl_addr = ISP_PWR_CON;
	syss[SYS_VDE].ctl_addr = VDE_PWR_CON;
	syss[SYS_VEN].ctl_addr = VEN_PWR_CON;
	syss[SYS_MFG_ASYNC].ctl_addr = MFG_ASYNC_PWR_CON;
	syss[SYS_AUDIO].ctl_addr = AUDIO_PWR_CON;
	syss[SYS_CAM].ctl_addr = CAM_PWR_CON;
	syss[SYS_C2K].ctl_addr = C2K_PWR_CON;
	syss[SYS_MDSYS_INTF_INFRA].ctl_addr = MDSYS_INTF_INFRA_PWR_CON;
	syss[SYS_MFG_CORE1].ctl_addr = MFG_CORE1_PWR_CON;
	syss[SYS_MFG_CORE0].ctl_addr = MFG_CORE0_PWR_CON;

	for (i = 0; i < ARRAY_SIZE(scp_clks); i++) {
		struct mtk_power_gate *pg = &scp_clks[i];

		pre_clk = pg->pre_clk_name ? __clk_lookup(pg->pre_clk_name) : NULL;

		clk = mt_clk_register_power_gate(pg->name, pg->parent_name, pre_clk, pg->pd_id);

		if (IS_ERR(clk)) {
			pr_err("[CCF] %s: Failed to register clk %s: %ld\n",
			       __func__, pg->name, PTR_ERR(clk));
			continue;
		}

		if (clk_data)
			clk_data->clks[pg->id] = clk;

#if MT_CCF_DEBUG
		pr_debug("[CCF] %s: pgate %3d: %s\n", __func__, i, pg->name);
#endif				/* MT_CCF_DEBUG */
	}
}


/* TODO: remove this function */
static struct clk_onecell_data *alloc_clk_data(unsigned int clk_num)
{
	int i;
	struct clk_onecell_data *clk_data;

	clk_data = kzalloc(sizeof(clk_data), GFP_KERNEL);
	if (!clk_data)
		return NULL;

	clk_data->clks = kcalloc(clk_num, sizeof(struct clk *), GFP_KERNEL);
	if (!clk_data->clks) {
		kfree(clk_data);
		return NULL;
	}

	clk_data->clk_num = clk_num;

	for (i = 0; i < clk_num; ++i)
		clk_data->clks[i] = ERR_PTR(-ENOENT);

	return clk_data;
}

/* TODO: remove this function */
static void __iomem *get_reg(struct device_node *np, int index)
{
#if DUMMY_REG_TEST
	return kzalloc(PAGE_SIZE, GFP_KERNEL);
#else
	return of_iomap(np, index);
#endif
}

static void __init mt_scpsys_init(struct device_node *node)
{
	struct clk_onecell_data *clk_data;
	void __iomem *infracfg_reg;
	void __iomem *spm_reg;
	void __iomem *infra_reg;
	int r;

	infracfg_reg = get_reg(node, 0);
	spm_reg = get_reg(node, 1);
	infra_reg = get_reg(node, 2);

	if (!infracfg_reg || !spm_reg) {
		pr_err("clk-pg-mt6755: missing reg\n");
		return;
	}


	clk_data = alloc_clk_data(SCP_NR_SYSS);

	init_clk_scpsys(infracfg_reg, spm_reg, infra_reg, clk_data);

	r = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
	if (r)
		pr_err("[CCF] %s:could not register clock provide\n", __func__);

#if !MT_CCF_BRINGUP
	/* subsys init: per modem owner request, disable modem power first */
	disable_subsys(SYS_MD1);
	disable_subsys(SYS_C2K);
#else				/*power on all subsys for bring up */
#ifndef CONFIG_FPGA_EARLY_PORTING
	spm_mtcmos_ctrl_mfg_async(STA_POWER_ON);
	spm_mtcmos_ctrl_mfg(STA_POWER_ON);
	spm_mtcmos_ctrl_mfg_core0(STA_POWER_ON);
	spm_mtcmos_ctrl_mfg_core1(STA_POWER_ON);

	spm_mtcmos_ctrl_dis(STA_POWER_ON);
	spm_mtcmos_ctrl_cam(STA_POWER_ON);
	spm_mtcmos_ctrl_ven(STA_POWER_ON);
	spm_mtcmos_ctrl_vde(STA_POWER_ON);
	spm_mtcmos_ctrl_isp(STA_POWER_ON);

	spm_mtcmos_ctrl_mdsys_intf_infra(STA_POWER_ON);
	spm_mtcmos_ctrl_md1(STA_POWER_ON);
	spm_mtcmos_ctrl_c2k(STA_POWER_ON);

	spm_mtcmos_ctrl_audio(STA_POWER_ON);
#endif
#endif				/* !MT_CCF_BRINGUP */
}

CLK_OF_DECLARE(mtk_pg_regs, "mediatek,mt6757-scpsys", mt_scpsys_init);

#if 1 /*only use for suspend test*/
void mtcmos_force_off(void)
{
	spm_mtcmos_ctrl_mfg_core0(STA_POWER_DOWN);
	spm_mtcmos_ctrl_mfg_core1(STA_POWER_DOWN);
	spm_mtcmos_ctrl_mfg(STA_POWER_DOWN);
	spm_mtcmos_ctrl_mfg_async(STA_POWER_DOWN);

	spm_mtcmos_ctrl_cam(STA_POWER_DOWN);
	spm_mtcmos_ctrl_ven(STA_POWER_DOWN);
	spm_mtcmos_ctrl_vde(STA_POWER_DOWN);
	spm_mtcmos_ctrl_isp(STA_POWER_DOWN);
	spm_mtcmos_ctrl_dis(STA_POWER_DOWN);

	spm_mtcmos_ctrl_md1(STA_POWER_DOWN);
	spm_mtcmos_ctrl_c2k(STA_POWER_DOWN);
	spm_mtcmos_ctrl_mdsys_intf_infra(STA_POWER_DOWN);

	spm_mtcmos_ctrl_audio(STA_POWER_DOWN);

}
#endif

#if CLK_DEBUG

#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

static char last_cmd[128] = "null";

static int test_pg_dump_regs(struct seq_file *s, void *v)
{
	int i;

	for (i = 0; i < NR_SYSS; i++) {
		if (!syss[i].ctl_addr)
			continue;

		seq_printf(s, "%10s: [0x%p]: 0x%08x\n", syss[i].name,
			   syss[i].ctl_addr, clk_readl(syss[i].ctl_addr));
	}

	return 0;
}

static void dump_pg_state(const char *clkname, struct seq_file *s)
{
	struct clk *c = __clk_lookup(clkname);
	struct clk *p = IS_ERR_OR_NULL(c) ? NULL : __clk_get_parent(c);

	if (IS_ERR_OR_NULL(c)) {
		seq_printf(s, "[%17s: NULL]\n", clkname);
		return;
	}

	seq_printf(s, "[%17s: %3s, %3d, %3d, %10ld, %17s]\n",
		   __clk_get_name(c),
		   __clk_is_enabled(c) ? "ON" : "off",
		   __clk_get_prepare_count(c),
		   __clk_get_enable_count(c), __clk_get_rate(c), p ? __clk_get_name(p) : "");


	clk_put(c);
}

static int test_pg_dump_state_all(struct seq_file *s, void *v)
{
	static const char *const clks[] = {
		pg_md1,
		pg_conn,
		pg_dis,
		pg_mfg,
		pg_isp,
		pg_vde,
		pg_ven,
		pg_mfg_async,
		pg_audio,
		pg_cam,
		pg_c2k,
		pg_mdsys_intf_infra,
		pg_mfg_core1,
		pg_mfg_core0,
	};

	int i;

/*	pr_debug("\n");*/
	for (i = 0; i < ARRAY_SIZE(clks); i++)
		dump_pg_state(clks[i], s);

	return 0;
}

static struct {
	const char *name;
	struct clk *clk;
} g_clks[] = {
	{
	.name = pg_md1}, {
	.name = pg_vde}, {
	.name = pg_ven}, {
.name = pg_mfg},};

static int test_pg_1(struct seq_file *s, void *v)
{
	int i;

/*	pr_debug("\n");*/

	for (i = 0; i < ARRAY_SIZE(g_clks); i++) {
		g_clks[i].clk = __clk_lookup(g_clks[i].name);
		if (IS_ERR_OR_NULL(g_clks[i].clk)) {
			seq_printf(s, "clk_get(%s): NULL\n", g_clks[i].name);
			continue;
		}

		clk_prepare_enable(g_clks[i].clk);
		seq_printf(s, "clk_prepare_enable(%s)\n", __clk_get_name(g_clks[i].clk));
	}

	return 0;
}

static int test_pg_2(struct seq_file *s, void *v)
{
	int i;

/*	pr_debug("\n");*/

	for (i = 0; i < ARRAY_SIZE(g_clks); i++) {
		if (IS_ERR_OR_NULL(g_clks[i].clk)) {
			seq_printf(s, "(%s).clk: NULL\n", g_clks[i].name);
			continue;
		}

		seq_printf(s, "clk_disable_unprepare(%s)\n", __clk_get_name(g_clks[i].clk));
		clk_disable_unprepare(g_clks[i].clk);
		clk_put(g_clks[i].clk);
	}

	return 0;
}

static int test_pg_show(struct seq_file *s, void *v)
{
	static const struct {
		int (*fn)(struct seq_file *, void *);
		const char *cmd;
	} cmds[] = {
		{
		.cmd = "dump_regs", .fn = test_pg_dump_regs}, {
		.cmd = "dump_state", .fn = test_pg_dump_state_all}, {
		.cmd = "1", .fn = test_pg_1}, {
	.cmd = "2", .fn = test_pg_2},};

	int i;

/*	pr_debug("last_cmd: %s\n", last_cmd);*/

	for (i = 0; i < ARRAY_SIZE(cmds); i++) {
		if (strcmp(cmds[i].cmd, last_cmd) == 0)
			return cmds[i].fn(s, v);
	}

	return 0;
}

static int test_pg_open(struct inode *inode, struct file *file)
{
	return single_open(file, test_pg_show, NULL);
}

static ssize_t test_pg_write(struct file *file,
			     const char __user *buffer, size_t count, loff_t *data)
{
	char desc[sizeof(last_cmd)];
	int len = 0;

/*	pr_debug("count: %zu\n", count);*/
	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return 0;

	desc[len] = '\0';
	strcpy(last_cmd, desc);
	if (last_cmd[len - 1] == '\n')
		last_cmd[len - 1] = 0;

	return count;
}

static const struct file_operations test_pg_fops = {
	.owner = THIS_MODULE,
	.open = test_pg_open,
	.read = seq_read,
	.write = test_pg_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init debug_init(void)
{
	static int init;
	struct proc_dir_entry *entry;

/*	pr_debug("init: %d\n", init);*/

	if (init)
		return 0;

	++init;

	entry = proc_create("test_pg", 0, 0, &test_pg_fops);
	if (!entry)
		return -ENOMEM;

	++init;
	return 0;
}

static void __exit debug_exit(void)
{
	remove_proc_entry("test_pg", NULL);
}

module_init(debug_init);
module_exit(debug_exit);

#endif				/* CLK_DEBUG */
