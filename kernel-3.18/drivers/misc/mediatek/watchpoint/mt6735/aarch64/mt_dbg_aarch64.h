
#ifndef __MT_DBG_H
#define __MT_DBG_H
unsigned int *mt_save_dbg_regs(unsigned int *p, unsigned int cpuid);
void mt_restore_dbg_regs(unsigned int *p, unsigned int cpuid);
void mt_copy_dbg_regs(int to, int from);
#ifdef DBG_REG_DUMP
void dump_dbgregs(int cpuid);
void print_dbgregs(int cpuid);

#endif
extern int get_cluster_core_count(void);
extern unsigned long saved_MDSCR_EL1;
#endif				/* !__HW_BREAKPOINT_H */
