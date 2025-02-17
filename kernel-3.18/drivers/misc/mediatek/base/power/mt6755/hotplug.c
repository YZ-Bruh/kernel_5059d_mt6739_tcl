#include <linux/errno.h>
#include <asm/cacheflush.h>
#include <mt-plat/sync_write.h>
#include <mach/mt_spm_mtcmos.h>

#include "mt-smp.h"
#include "smp.h"
#include "hotplug.h"

atomic_t hotplug_cpu_count = ATOMIC_INIT(1);

static inline void cpu_enter_lowpower(unsigned int cpu)
{
	if (((cpu == 4) && (cpu_online(5) == 0) && (cpu_online(6) == 0)
	     && (cpu_online(7) == 0)) || ((cpu == 5) && (cpu_online(4) == 0)
					  && (cpu_online(6) == 0)
					  && (cpu_online(7) == 0))
	    || ((cpu == 6) && (cpu_online(4) == 0) && (cpu_online(5) == 0)
		&& (cpu_online(7) == 0)) || ((cpu == 7) && (cpu_online(4) == 0)
					     && (cpu_online(5) == 0)
					     && (cpu_online(6) == 0))) {
		__disable_dcache__inner_flush_dcache_L1__inner_flush_dcache_L2
		    ();

		/* Switch the processor from SMP mode to AMP mode by clearing
		   the ACTLR SMP bit */
		__switch_to_amp();

		/* Execute an ISB instruction to ensure that all of the CP15
		   register changes from the previous steps have been
		   committed */
		isb();

		/* Execute a DSB instruction to ensure that all cache, TLB
		   and branch predictor maintenance operations issued by any
		   processor in the multiprocessor device before the SMP bit
		   was cleared have completed */
		mb();

		/* Disable snoop requests and DVM message requests */
		REG_WRITE((void *)CCI400_SI3_SNOOP_CONTROL,
			readl((void *)CCI400_SI3_SNOOP_CONTROL) &
				~(SNOOP_REQ | DVM_MSG_REQ));
		while (readl((void *)CCI400_STATUS) & CHANGE_PENDING)
			;

		/* Disable CA15L snoop function */
		REG_WRITE((void *)MP1_AXI_CONFIG,
			readl((void *)MP1_AXI_CONFIG) | ACINACTM);
	} else {
		__disable_dcache__inner_flush_dcache_L1__inner_clean_dcache_L2
		    ();

		/* Execute a CLREX instruction */
		__asm__ __volatile__("clrex");

		/* Switch the processor from SMP mode to AMP mode by
		   clearing the ACTLR SMP bit */
		__switch_to_amp();
	}
}

static inline void cpu_leave_lowpower(unsigned int cpu)
{
	if (((cpu == 4) && (cpu_online(5) == 0) && (cpu_online(6) == 0)
	     && (cpu_online(7) == 0)) || ((cpu == 5) && (cpu_online(4) == 0)
					  && (cpu_online(6) == 0)
					  && (cpu_online(7) == 0))
	    || ((cpu == 6) && (cpu_online(4) == 0) && (cpu_online(5) == 0)
		&& (cpu_online(7) == 0)) || ((cpu == 7) && (cpu_online(4) == 0)
					     && (cpu_online(5) == 0)
					     && (cpu_online(6) == 0))) {
		/* Enable CA15L snoop function */
		REG_WRITE((void *)MP1_AXI_CONFIG,
			readl((void *)MP1_AXI_CONFIG) & ~ACINACTM);

		/* Enable snoop requests and DVM message requests */
		REG_WRITE((void *)CCI400_SI3_SNOOP_CONTROL,
			  readl((void *)CCI400_SI3_SNOOP_CONTROL) |
				(SNOOP_REQ | DVM_MSG_REQ));
		while (readl((void *)CCI400_STATUS) & CHANGE_PENDING)
			;
	}

	/* Set the ACTLR.SMP bit to 1 for SMP mode */
	__switch_to_smp();

	/* Enable dcache */
	__enable_dcache();
}

static inline void platform_do_lowpower(unsigned int cpu, int *spurious)
{
	/* Just enter wfi for now. TODO: Properly shut off the cpu. */
	for (;;) {

		/* Execute an ISB instruction to ensure that all of the CP15
		   register changes from the previous steps have been
		   committed */
		isb();

		/* Execute a DSB instruction to ensure that all cache, TLB and
		   branch predictor maintenance operations issued by any
		   processor in the multiprocessor device before the SMP bit
		   was cleared have completed */
		mb();

		/*
		 * here's the WFI
		 */
		__asm__ __volatile__("wfi");

		if (pen_release == cpu) {
			/*
			 * OK, proper wakeup, we're done
			 */
			break;
		}

		/*
		 * Getting here, means that we have come out of WFI without
		 * having been woken up - this shouldn't happen
		 *
		 * Just note it happening - when we're woken, we can report
		 * its occurrence.
		 */
		(*spurious)++;
	}
}

int mt_cpu_kill(unsigned int cpu)
{
	/*HOTPLUG_INFO("mt_cpu_kill, cpu: %d\n", cpu);*/

#ifdef CONFIG_HOTPLUG_WITH_POWER_CTRL
	switch (cpu) {
	case 0:
		spm_mtcmos_ctrl_cpu0(STA_POWER_DOWN, 1);
		break;
	case 1:
		spm_mtcmos_ctrl_cpu1(STA_POWER_DOWN, 1);
		break;
	case 2:
		spm_mtcmos_ctrl_cpu2(STA_POWER_DOWN, 1);
		break;
	case 3:
		spm_mtcmos_ctrl_cpu3(STA_POWER_DOWN, 1);
		break;
	case 4:
		spm_mtcmos_ctrl_cpu4(STA_POWER_DOWN, 1);
		break;
	case 5:
		spm_mtcmos_ctrl_cpu5(STA_POWER_DOWN, 1);
		break;
	case 6:
		spm_mtcmos_ctrl_cpu6(STA_POWER_DOWN, 1);
		break;
	case 7:
		spm_mtcmos_ctrl_cpu7(STA_POWER_DOWN, 1);
		break;
	default:
		break;
	}
#endif
	atomic_dec(&hotplug_cpu_count);

	return 1;
}

void mt_cpu_die(unsigned int cpu)
{
	int spurious = 0;

	/*HOTPLUG_INFO("mt_cpu_die, cpu: %d\n", cpu);*/
	/*
	 * we're ready for shutdown now, so do it
	 */
	cpu_enter_lowpower(cpu);
	platform_do_lowpower(cpu, &spurious);

	/*
	 * bring this CPU back into the world of cache
	 * coherency, and then restore interrupts
	 */
	cpu_leave_lowpower(cpu);

	if (spurious)
		HOTPLUG_INFO(
			"spurious wakeup call, cpu: %d, spurious: %d\n",
			 cpu, spurious);
}

int mt_cpu_disable(unsigned int cpu)
{
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	/*HOTPLUG_INFO("mt_cpu_disable, cpu: %d\n", cpu);*/

	return 0;
}
