
#ifndef __HW_BREAKPOINT_32_H
#define __HW_BREAKPOINT_32_H
#include <asm/io.h>
#include <mt-plat/mt_io.h>
#include <mt-plat/sync_write.h>
#include <mt-plat/hw_watchpoint.h>

#define MAX_NR_WATCH_POINT 4
struct wp_trace_context_t {
	void __iomem **debug_regs;
	struct wp_event wp_events[MAX_NR_WATCH_POINT];
	unsigned int wp_nr;
	unsigned int bp_nr;
	unsigned long dbgdidr;
	unsigned int nr_dbg;
};



struct dbgreg_set {
	unsigned long regs[28];
};
#define SDSR            regs[22]
#define DBGVCR          regs[21]
#define DBGWCR3         regs[20]
#define DBGWCR2         regs[19]
#define DBGWCR1         regs[18]
#define DBGWCR0         regs[17]
#define DBGWVR3         regs[16]
#define DBGWVR2         regs[15]
#define DBGWVR1         regs[14]
#define DBGWVR0         regs[13]
#define DBGBCR5         regs[12]
#define DBGBCR4         regs[11]
#define DBGBCR3         regs[10]
#define DBGBCR2         regs[9]
#define DBGBCR1         regs[8]
#define DBGBCR0         regs[7]
#define DBGBVR5         regs[6]
#define DBGBVR4         regs[5]
#define DBGBVR3		regs[4]
#define DBGBVR2		regs[3]
#define DBGBVR1		regs[2]
#define DBGBVR0		regs[1]
#define DBGDSCRext	regs[0]

#define DBGWVR 0x800
#define DBGWCR 0x808
#define DBGBVR 0x400
#define DBGBCR 0x408

#define DBGLAR 0xFB0
#define DBGLSR 0xFB4
#define DBGOSLAR 0x300

#define UNLOCK_KEY 0xC5ACCE55
#define HDBGEN (1 << 14)
#define MDBGEN (1 << 15)
#define DBGWCR_VAL 0x000001E7
/**/
#define WP_EN (1 << 0)
#define LSC_LDR (1 << 3)
#define LSC_STR (2 << 3)
#define LSC_ALL (3 << 3)


#define dbg_mem_read(addr)          readl(IOMEM(addr))
#define dbg_mem_write(addr, val)    mt_reg_sync_writel(val, addr)
#define dbg_reg_copy(offset, base_to, base_from)   \
	dbg_mem_write((base_to) + (offset), dbg_mem_read((base_from) + (offset)))

static inline void cs_cpu_write(void __iomem *addr_base, u32 offset, u32 wdata)
{
	/* TINFO="Write addr %h, with data %h", addr_base+offset, wdata */
	mt_reg_sync_writel(wdata, addr_base + offset);
}

static inline u32 cs_cpu_read(const void __iomem *addr_base, u32 offset)
{
	u32 actual;

	/* TINFO="Read addr %h, with data %h", addr_base+offset, actual */
	actual = readl(addr_base + offset);
	return actual;
}
void smp_read_dbgsdsr_callback(void *info);
void smp_write_dbgsdsr_callback(void *info);
void smp_read_dbgdscr_callback(void *info);
void smp_write_dbgdscr_callback(void *info);
void smp_read_dbgoslsr_callback(void *info);
void smp_write_dbgoslsr_callback(void *info);
void smp_read_dbgvcr_callback(void *info);
void smp_write_dbgvcr_callback(void *info);
int register_wp_context(struct wp_trace_context_t **wp_tracer_context);
void __iomem *get_wp_base(void);
void smp_read_dbgdscr_callback(void *info);
void smp_write_dbgdscr_callback(void *info);
void smp_read_dbgoslsr_callback(void *info);
void smp_write_dbgoslsr_callback(void *info);

extern unsigned read_clusterid(void);

#endif  /* !__HW_BREAKPOINT_32_H */
