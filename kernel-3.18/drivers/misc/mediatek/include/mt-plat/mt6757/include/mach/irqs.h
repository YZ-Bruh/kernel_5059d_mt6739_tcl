
#ifndef __IRQS_H
#define __IRQS_H

#define GIC_PRIVATE_SIGNALS     (32)
#define NR_GIC_SGI              (16)
#define NR_GIC_PPI              (16)
#define GIC_PPI_OFFSET          (27)
#define MT_NR_PPI               (5)
#define MT_NR_SPI               (256)
#define NR_MT_IRQ_LINE          (GIC_PPI_OFFSET + MT_NR_PPI + MT_NR_SPI)

#ifdef NR_IRQS
#undef NR_IRQS
#endif
#define NR_IRQS			(NR_MT_IRQ_LINE+400)

#define GIC_PPI_GLOBAL_TIMER    (GIC_PPI_OFFSET + 0)
#define GIC_PPI_LEGACY_FIQ      (GIC_PPI_OFFSET + 1)
#define GIC_PPI_PRIVATE_TIMER   (GIC_PPI_OFFSET + 2)
#define GIC_PPI_WATCHDOG_TIMER  (GIC_PPI_OFFSET + 3)
#define GIC_PPI_LEGACY_IRQ      (GIC_PPI_OFFSET + 4)

#define MT_BTIF_IRQ_ID			(GIC_PRIVATE_SIGNALS + 50)
#define MT_DMA_BTIF_TX_IRQ_ID		(GIC_PRIVATE_SIGNALS + 71)
#define MT_DMA_BTIF_RX_IRQ_ID		(GIC_PRIVATE_SIGNALS + 72)

#define FIQ_START 0
#define CPU_BRINGUP_SGI 1
#define FIQ_SMP_CALL_SGI 13
#define FIQ_DBG_SGI 14

#define MT_EDGE_SENSITIVE	0
#define MT_LEVEL_SENSITIVE	1
#define MT_POLARITY_LOW		0
#define MT_POLARITY_HIGH	1

#ifndef __ASSEMBLY__
typedef void (*fiq_isr_handler)(void *arg, void *regs, void *svc_sp);

int request_fiq(int irq, fiq_isr_handler handler,
		unsigned long irq_flags, void *arg);
#endif

#endif
