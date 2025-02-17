
#ifndef __MT_SPM_PMIC_WRAP_H__
#define __MT_SPM_PMIC_WRAP_H__
/* Phase */
enum pmic_wrap_phase_id {
	PMIC_WRAP_PHASE_NORMAL,	/* as VCORE_DVFS */
	PMIC_WRAP_PHASE_SUSPEND,
	PMIC_WRAP_PHASE_DEEPIDLE,
	NR_PMIC_WRAP_PHASE,
};

/* IDX mapping */
enum {
	IDX_NM_RESERVE1,	/* 0 *//* PMIC_WRAP_PHASE_NORMAL */
	IDX_NM_RESERVE2,	/* 1 */
	IDX_NM_VCORE_HPM,	/* 2 */
	IDX_NM_VCORE_TRANS2,	/* 3 */
	IDX_NM_VCORE_TRANS1,	/* 4 */
	IDX_NM_VCORE_LPM,	/* 5 */
	NR_IDX_NM,
};
enum {
	IDX_SP_VSRAM_PWR_ON,	/* 0 *//* PMIC_WRAP_PHASE_SUSPEND */
	IDX_SP_VSRAM_SHUTDOWN,	/* 1 */
	IDX_SP_VCORE_HPM,	/* 2 */
	IDX_SP_VCORE_TRANS2,	/* 3 */
	IDX_SP_VCORE_TRANS1,	/* 4 */
	IDX_SP_VCORE_LPM,	/* 5 */
	IDX_SP_VPROC_PWR_ON = 12,	/* 12 */
	IDX_SP_VPROC_SHUTDOWN,	/* 13 */
	NR_IDX_SP,
};
enum {
	IDX_DI_VSRAM_NORMAL,	/* 0 *//* PMIC_WRAP_PHASE_DEEPIDLE */
	IDX_DI_VSRAM_SLEEP,	/* 1 */
	IDX_DI_VCORE_HPM,	/* 2 */
	IDX_DI_VCORE_TRANS2,	/* 3 */
	IDX_DI_VCORE_TRANS1,	/* 4 */
	IDX_DI_VCORE_LPM,	/* 5 */
	IDX_DI_SRCCLKEN_IN2_NORMAL,  /* 6 */
	IDX_DI_SRCCLKEN_IN2_SLEEP, /* 7 */
	IDX_DI_VPROC_NORMAL = 12,	/* 12 */
	IDX_DI_VPROC_SLEEP,	/* 13 */
	NR_IDX_DI,
};

#endif				/* __MT_SPM_PMIC_WRAP_H__ */
