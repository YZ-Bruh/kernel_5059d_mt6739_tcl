
#include <linux/kernel.h>
#include <linux/init.h>
#if defined(__KERNEL__)  /* || !defined(__CTP__) */
	#include <linux/export.h>
	#include <linux/module.h>
#endif /* #if !defined(__CTP__) */

/* #include <asm/system.h> */
#include "mt_spower_data.h"
#include "mt_static_power.h"

#define SP_TAG     "[Power/spower] "
#define SPOWER_LOG_NONE                                0
#define SPOWER_LOG_WITH_PRINTK                         1

/* #define SPOWER_LOG_PRINT SPOWER_LOG_WITH_PRINTK */
#define SPOWER_LOG_PRINT SPOWER_LOG_NONE

#if (SPOWER_LOG_PRINT == SPOWER_LOG_NONE)
#define SPOWER_INFO(fmt, args...)
#elif (SPOWER_LOG_PRINT == SPOWER_LOG_WITH_PRINTK)
/* printk("[Power/spower] "fmt, ##args) */
#define SPOWER_INFO(fmt, args...)	 pr_err(SP_TAG fmt, ##args)
#endif

#define V_OF_FUSE			1100
#define T_OF_FUSE			30
#define DEVINFO_IDX0		(51)
#define DEVINFO_IDX1		(53)
#define DEVINFO_IDX2		(55)
#define DEVINFO_IDX3		(57)
#define DEVINFO_IDX4		(68)
#define DEF_CPUL_LEAKAGE		(150 * V_OF_FUSE/1000)
#define DEF_CPULL_LEAKAGE		(100 * V_OF_FUSE/1000)
#define DEF_GPU_LEAKAGE			(50 * V_OF_FUSE/1000)
#define DEF_MODEM_LEAKAGE		(50 * V_OF_FUSE/1000)
#define DEF_MODEM_SRAM_LEAKAGE	(25 * V_OF_FUSE/1000)
#define DEF_MD_LEAKAGE			(50 * V_OF_FUSE/1000)
#define DEF_VCORE_LEAKAGE		(100 * V_OF_FUSE/1000)
#define DEF_VCORE_SRAM_LEAKAGE	(50 * V_OF_FUSE/1000)

#define TABLE_MAX_VOL		1250
#define TABLE_MIN_VOL		600
#define TABLE_MAX_TEMP		125
#define TABLE_MIN_TEMP		-20


static sptbl_t sptab[MT_SPOWER_MAX]; /* CPUL, CPULL, GPU, MODEM, MODEM_SRAM, MD, VCORE, VCORE_SRAM  */

int devinfo_table[] = {
		3539,   492,    1038,   106,    231,    17,     46,     2179,
		4,      481,    1014,   103,    225,    17,     45,     2129,
		3,      516,    1087,   111,    242,    19,     49,     2282,
		4,      504,    1063,   108,    236,    18,     47,     2230,
		4,      448,    946,    96,     210,    15,     41,     1986,
		2,      438,    924,    93,     205,    14,     40,     1941,
		2,      470,    991,    101,    220,    16,     43,     2080,
		3,      459,    968,    98,     215,    16,     42,     2033,
		3,      594,    1250,   129,    279,    23,     57,     2621,
		6,      580,    1221,   126,    273,    22,     56,     2561,
		6,      622,    1309,   136,    293,    24,     60,     2745,
		7,      608,    1279,   132,    286,    23,     59,     2683,
		6,      541,    1139,   117,    254,    20,     51,     2390,
		5,      528,    1113,   114,    248,    19,     50,     2335,
		4,      566,    1193,   123,    266,    21,     54,     2503,
		5,      553,    1166,   120,    260,    21,     53,     2446,
		5,      338,    715,    70,     157,    9,      29,     1505,
		3153,   330,    699,    69,     153,    9,      28,     1470,
		3081,   354,    750,    74,     165,    10,     31,     1576,
		3302,   346,    732,    72,     161,    10,     30,     1540,
		3227,   307,    652,    63,     142,    8,      26,     1371,
		2875,   300,    637,    62,     139,    7,      25,     1340,
		2809,   322,    683,    67,     149,    8,      27,     1436,
		3011,   315,    667,    65,     146,    8,      26,     1404,
		2942,   408,    862,    86,     191,    13,     37,     1811,
		1,      398,    842,    84,     186,    12,     36,     1769,
		1,      428,    903,    91,     200,    14,     39,     1896,
		2,      418,    882,    89,     195,    13,     38,     1853,
		2,      371,    785,    78,     173,    11,     33,     1651,
		3458,   363,    767,    76,     169,    10,     32,     1613,
		3379,   389,    823,    82,     182,    12,     35,     1729,
		1,      380,    804,    80,     177,    11,     34,     1689,
};


int interpolate(int x1, int x2, int x3, int y1, int y2)
{
	/* BUG_ON(x1 == x2); */
	if (x1 == x2)
		return (y1+y2)/2;

	return (x3-x1) * (y2-y1) / (x2 - x1) + y1;
}

int interpolate_2d(sptbl_t *tab, int v1, int v2, int t1, int t2, int voltage, int degree)
{
	int c1, c2, p1, p2, p;

	if ((v1 == v2) && (t1 == t2)) {
		p = mA(tab, v1, t1);
	} else if (v1 == v2) {
		c1 = mA(tab, v1, t1);
		c2 = mA(tab, v1, t2);
		p = interpolate(deg(tab, t1), deg(tab, t2), degree, c1, c2);
	} else if (t1 == t2) {
		c1 = mA(tab, v1, t1);
		c2 = mA(tab, v2, t1);
		p = interpolate(mV(tab, v1), mV(tab, v2), voltage, c1, c2);
	} else {
		c1 = mA(tab, v1, t1);
		c2 = mA(tab, v1, t2);
		p1 = interpolate(deg(tab, t1), deg(tab, t2), degree, c1, c2);

		c1 = mA(tab, v2, t1);
		c2 = mA(tab, v2, t2);
		p2 = interpolate(deg(tab, t1), deg(tab, t2), degree, c1, c2);

		p = interpolate(mV(tab, v1), mV(tab, v2), voltage, p1, p2);
	}

	return p;
}
/* c1, c2, c3(EFUSE) => make sptab 239, 53, 100 */
void interpolate_table(sptbl_t *spt, int c1, int c2, int c3, sptbl_t *tab1, sptbl_t *tab2)
{
	int v, t;

	/* avoid divid error, if we have bad raw data table */
	if (unlikely(c1 == c2)) {
		*spt = *tab1;
		SPOWER_INFO("sptab equal to tab1:%d/%d\n",  c1, c3);
	} else {
		SPOWER_INFO("make sptab %d, %d, %d\n", c1, c2, c3);
		for (t = 0; t < tsize(spt); t++) {
			for (v = 0; v < vsize(spt); v++) {
				int *p = &mA(spt, v, t);

				p[0] = interpolate(c1, c2, c3,
					   mA(tab1, v, t),
					   mA(tab2, v, t));

				SPOWER_INFO("%d ", p[0]);
			}
			SPOWER_INFO("\n");
		}
		SPOWER_INFO("make sptab done!\n");
	}
}


int sptab_lookup(sptbl_t *tab, int voltage, int degree)
{
	int x1, x2, y1, y2, i;
	int mamper;

	/** lookup voltage **/
	for (i = 0; i < vsize(tab); i++) {
		if (voltage <= mV(tab, i))
			break;
	}

	if (unlikely(voltage == mV(tab, i))) {
		x1 = x2 = i;
	} else if (unlikely(i == vsize(tab))) {
		x1 = vsize(tab)-2;
		x2 = vsize(tab)-1;
	} else if (i == 0) {
		x1 = 0;
		x2 = 1;
	} else {
		x1 = i-1;
		x2 = i;
	}


	/** lookup degree **/
	for (i = 0; i < tsize(tab); i++) {
		if (degree <= deg(tab, i))
			break;
	}

	if (unlikely(degree == deg(tab, i))) {
		y1 = y2 = i;
	} else if (unlikely(i == tsize(tab))) {
		y1 = tsize(tab)-2;
		y2 = tsize(tab)-1;
	} else if (i == 0) {
		y1 = 0;
		y2 = 1;
	} else {
		y1 = i-1;
		y2 = i;
	}

	mamper = interpolate_2d(tab, x1, x2, y1, y2, voltage, degree);

	return mamper;
}

int mt_spower_make_table(sptbl_t *spt, spower_raw_t *spower_raw, int wat, int voltage, int degree)
{
	int i;
	int c1, c2, c = -1;
	sptbl_t tab[MAX_TABLE_SIZE], *tab1, *tab2, *tspt;

	/** FIXME, test only; please read efuse to assign. **/
	/* wat = 80; */
	/* voltage = 1150; */
	/* degree = 30; */

	BUG_ON(spower_raw->table_size < MAX_TABLE_SIZE);

	/** structurize the raw data **/
	spower_tab_construct(&tab, spower_raw);

	/** lookup tables which the chip type locates to **/
	for (i = 0; i < spower_raw->table_size; i++) {
		c = sptab_lookup(&tab[i], voltage, degree);
		/** table order: ff, tt, ss **/
		if (wat >= c)
			break;
	}

	/** FIXME,
	 * There are only 2 tables are used to interpolate to form SPTAB.
	 * Thus, sptab takes use of the container which raw data is not used anymore.
	 **/
	if (wat == c) {
		/** just match **/
		tab1 = tab2 = &tab[i];
		/** pointer duplicate  **/
		tspt = tab1;
		SPOWER_INFO("sptab equal to tab:%d/%d\n",  wat, c);
	} else if (i == spower_raw->table_size) {
		/** above all **/
		#if defined(EXTER_POLATION)
			tab1 = &tab[spower_raw->table_size-2];
			tab2 = &tab[spower_raw->table_size-1];

			/** occupy the free container**/
			tspt = &tab[spower_raw->table_size-3];
		#else /* #if defined(EXTER_POLATION) */
			tspt = tab1 = tab2 = &tab[spower_raw->table_size-1];
		#endif /* #if defined(EXTER_POLATION) */

		SPOWER_INFO("sptab max tab:%d/%d\n",  wat, c);
	} else if (i == 0) {
#if defined(EXTER_POLATION)
		/** below all **/
		tab1 = &tab[0];
		tab2 = &tab[1];

		/** occupy the free container**/
		tspt = &tab[2];
#else /* #if defined(EXTER_POLATION) */
		tspt = tab1 = tab2 = &tab[0];
#endif /* #if defined(EXTER_POLATION) */

		SPOWER_INFO("sptab min tab:%d/%d\n",  wat, c);
	} else {
		/** anyone **/
		tab1 = &tab[i-1];
		tab2 = &tab[i];

		/** occupy the free container**/
		tspt = &tab[(i+1)%spower_raw->table_size];

		SPOWER_INFO("sptab interpolate tab:%d/%d, i:%d\n",  wat, c, i);
	}

	/** sptab needs to interpolate 2 tables. **/
	if (tab1 != tab2) {
		c1 = sptab_lookup(tab1, voltage, degree);
		c2 = sptab_lookup(tab2, voltage, degree);

		interpolate_table(tspt, c1, c2, wat, tab1, tab2);
	}

	/** update to global data **/
	*spt = *tspt;

	return 0;
}

/* #define MT_SPOWER_UT 0 */

#if defined(MT_SPOWER_UT)
void mt_spower_ut(void)
{
	int v, t, p, i;

	for (i = 0; i < MT_SPOWER_MAX; i++) {
		sptbl_t *spt = &sptab[i];

		switch (i) {
		case  MT_SPOWER_CPUL:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_CPUL\n");
			break;
		case  MT_SPOWER_CPULL:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_CPULL\n");
			break;
		case  MT_SPOWER_GPU:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_GPU\n");
			break;
		case  MT_SPOWER_MODEM:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_MODEM\n");
			break;
		case  MT_SPOWER_MODEM_SRAM:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_MODEM_SRAM\n");
			break;
		case  MT_SPOWER_MD:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_MD\n");
			break;
		case  MT_SPOWER_VCORE:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_VCORE\n");
			break;
		case  MT_SPOWER_VCORE_SRAM:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_VCORE_SRAM\n");
			break;
		default:
			break;
		}

		v = 750;
		t = 22;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 750;
		t = 25;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 750;
		t = 28;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 750;
		t = 82;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 750;
		t = 120;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 820;
		t = 22;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 820;
		t = 25;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 820;
		t = 28;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 820;
		t = 82;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 820;
		t = 120;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 1200;
		t = 22;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 1200;
		t = 25;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 1200;
		t = 28;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 1200;
		t = 82;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 1200;
		t = 120;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 950;
		t = 80;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 1000;
		t = 85;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		/* new test case */
		v = 1150;
		t = 105;
		p  = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 700;
		t = 20;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 650;
		t = 18;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 600;
		t = 15;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 550;
		t = 22;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 550;
		t = 10;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 400;
		t = 10;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 320;
		t = 5;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 220;
		t = 0;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 80;
		t = -5;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 0;
		t = -10;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 1200;
		t = -10;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 1200;
		t = -25;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 1200;
		t = -28;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 120;
		t = -39;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 120;
		t = -120;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 950;
		t = -80;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 1000;
		t = 5;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		v = 1150;
		t = 10;
		p = mt_spower_get_leakage(i, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);

		switch (i) {
		case  MT_SPOWER_CPUL:
			SPOWER_INFO("[SPOWER] -  CPUL efuse:%d\n", mt_spower_get_efuse_lkg(MT_SPOWER_CPUL));
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_CPUL Done\n");
			break;
		case  MT_SPOWER_CPULL:
			SPOWER_INFO("[SPOWER] -  CPULL efuse:%d\n", mt_spower_get_efuse_lkg(MT_SPOWER_CPULL));
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_CPULL Done\n");
			break;
		case  MT_SPOWER_GPU:
			SPOWER_INFO("[SPOWER] -  GPU efuse:%d\n", mt_spower_get_efuse_lkg(MT_SPOWER_GPU));
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_GPU Done\n");
			break;
		case  MT_SPOWER_MODEM:
			SPOWER_INFO("[SPOWER] -  MODEM efuse:%d\n", mt_spower_get_efuse_lkg(MT_SPOWER_MODEM));
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_MODEM Done\n");
			break;
		case  MT_SPOWER_MODEM_SRAM:
			SPOWER_INFO("[SPOWER] -  MODEM_SRAM efuse:%d\n", mt_spower_get_efuse_lkg(MT_SPOWER_MODEM_SRAM));
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_MODEM_SRAM Done\n");
			break;
		case  MT_SPOWER_MD:
			SPOWER_INFO("[SPOWER] -  MD efuse:%d\n", mt_spower_get_efuse_lkg(MT_SPOWER_MD));
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_MD Done\n");
			break;
		case  MT_SPOWER_VCORE:
			SPOWER_INFO("[SPOWER] -  VCORE efuse:%d\n", mt_spower_get_efuse_lkg(MT_SPOWER_VCORE));
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_VCORE Done\n");
			break;
		case  MT_SPOWER_VCORE_SRAM:
			SPOWER_INFO("[SPOWER] -  VCORE_SRAM efuse:%d\n", mt_spower_get_efuse_lkg(MT_SPOWER_VCORE_SRAM));
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_VCORE_SRAM Done\n");
			break;
		default:
			break;
		}
	}
}
#endif /* #if defined(MT_SPOWER_UT) */
static unsigned int mtSpowerInited;
int mt_spower_init(void)
{
	/* int devinfo_1 = 0, devinfo_2 = 0, devinfo_3 = 0, devinfo_4 = 0;
	int devinfo_5 = 0, devinfo_6 = 0, devinfo_7 = 0, devinfo_8 = 0; */
	int cpul, cpull, gpu, modem, modem_sram, md, vcore, vcore_sram;

	if (1 == mtSpowerInited)
		return 0;

	/* avoid side effect from multiple invocation */
	if (tab_validate(&sptab[MT_SPOWER_CPUL]))
		return 0;

#if 0 /* non-eFuse define stage */
	devinfo_1 = (int)get_devinfo_with_index(DEVINFO_IDX2); /* P_OD2 */
	cpul = (devinfo >> 8) & 0xff;
	devinfo_2 = (int)get_devinfo_with_index(DEVINFO_IDX4); /* P_OD4 */
	cpull = (devinfo >> 8) & 0xff;
	devinfo_3 = (int)get_devinfo_with_index(DEVINFO_IDX8); /* P_OD8 */
	gpu = (devinfo >> 8) & 0xff;
	devinfo_4 = (int)get_devinfo_with_index(DEVINFO_IDX10); /* P_OD10 */
	modem = (devinfo >> 8) & 0xff;
	devinfo_5 = (int)get_devinfo_with_index(DEVINFO_IDX10); /* P_ODxx */
	modem_sram = (devinfo >> 8) & 0xff;
	devinfo_6 = (int)get_devinfo_with_index(DEVINFO_IDX14); /* P_OD14 */
	md = (devinfo >> 8) & 0xff;
	devinfo_7 = (int)get_devinfo_with_index(DEVINFO_IDX12); /* P_OD12 */
	vcore = (devinfo >> 8) & 0xff;
	devinfo_8 = (int)get_devinfo_with_index(DEVINFO_IDX12); /* P_ODxx */
	vcore_sram = (devinfo >> 8) & 0xff;

	SPOWER_INFO("[SPOWER] -eFuse:c7l/c7ll/gpu/mod/mod_m/md/vcor/vcor_m = 0x%x/0x%x/0x%x/0x%x/0x%x/0x%x/0x%x/0x%x\n",
	cpul, cpull, gpu, modem, modem_sram, md, vcore, vcore_sram);

	if (cpul != 0) {
		cpul = (int)devinfo_table[cpul];
		cpul = (int)(cpul*V_OF_FUSE/1000);
	} else {
		cpul = DEF_CPUL_LEAKAGE;
	}

	if (cpull != 0) {
		cpull = (int)devinfo_table[cpull];
		cpull = (int)(cpull*V_OF_FUSE/1000);
	} else {
		cpull = DEF_CPULL_LEAKAGE;
	}

	if (gpu != 0) {
		gpu	= (int)devinfo_table[gpu];
		gpu	= (int)(gpu*V_OF_FUSE/1000);
	} else {
		gpu = DEF_GPU_LEAKAGE;
	}

	if (modem != 0) {
		modem = (int)devinfo_table[modem];
		modem = (int)(modem*V_OF_FUSE/1000);
	} else {
		modem = DEF_MODEM_LEAKAGE;
	}

	if (modem_sram != 0) {
		modem_sram = (int)devinfo_table[modem_sram];
		modem_sram = (int)(modem_sram*V_OF_FUSE/1000);
	} else {
		modem_sram = DEF_MODEM_SRAM_LEAKAGE;
	}

	if (md != 0) {
		md = (int)devinfo_table[md];
		md = (int)(md*V_OF_FUSE/1000);
	} else {
		md = DEF_MD_LEAKAGE;
	}

	if (vcore != 0) {
		vcore = (int)devinfo_table[vcore];
		vcore = (int)(vcore*V_OF_FUSE/1000);
	} else {
		vcore = DEF_VCORE_LEAKAGE;
	}

	if (vcore_sram != 0) {
		vcore_sram = (int)devinfo_table[vcore_sram];
		vcore_sram = (int)(vcore_sram*V_OF_FUSE/1000);
	} else {
		vcore_sram = DEF_VCORE_SRAM_LEAKAGE;
	}
#else
	cpul = DEF_CPUL_LEAKAGE;
	cpull = DEF_CPULL_LEAKAGE;
	gpu = DEF_GPU_LEAKAGE;
	modem = DEF_MODEM_LEAKAGE;
	modem_sram = DEF_MODEM_SRAM_LEAKAGE;
	md = DEF_MD_LEAKAGE;
	vcore = DEF_VCORE_LEAKAGE;
	vcore_sram = DEF_VCORE_SRAM_LEAKAGE;
#endif

	SPOWER_INFO("[SPOWER] -LeakPW:c7l/c7ll/gpu/mod/mod_m/md/vcor/vcor_m = %d/%d/%d/%d/%d/%d/%d/%d\n",
	cpul, cpull, gpu, modem, modem_sram, md, vcore, vcore_sram);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_CPUL\n");
	mt_spower_make_table(&sptab[MT_SPOWER_CPUL], &cpul_spower_raw, cpul, V_OF_FUSE, T_OF_FUSE);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_CPULL\n");
	mt_spower_make_table(&sptab[MT_SPOWER_CPULL], &cpull_spower_raw, cpull, V_OF_FUSE, T_OF_FUSE);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_GPU\n");
	mt_spower_make_table(&sptab[MT_SPOWER_GPU], &gpu_spower_raw, gpu, V_OF_FUSE, T_OF_FUSE);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_MODEM\n");
	mt_spower_make_table(&sptab[MT_SPOWER_MODEM], &modem_spower_raw, modem, V_OF_FUSE, T_OF_FUSE);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_MODEM_SRAM\n");
	mt_spower_make_table(&sptab[MT_SPOWER_MODEM_SRAM], &modem_sram_spower_raw,
	modem_sram, V_OF_FUSE, T_OF_FUSE);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_MD\n");
	mt_spower_make_table(&sptab[MT_SPOWER_MD], &md_spower_raw, md, V_OF_FUSE, T_OF_FUSE);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_VCORE\n");
	mt_spower_make_table(&sptab[MT_SPOWER_VCORE], &vcore_spower_raw, vcore, V_OF_FUSE, T_OF_FUSE);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_VCORE_SRAM\n");
	mt_spower_make_table(&sptab[MT_SPOWER_VCORE_SRAM], &vcore_sram_spower_raw,
	vcore_sram, V_OF_FUSE, T_OF_FUSE);

	SPOWER_INFO("[SPOWER] - Start SPOWER UT!\n");
	#if defined(MT_SPOWER_UT)
	mt_spower_ut();
	#endif
	SPOWER_INFO("[SPOWER] - End SPOWER UT!\n");

	mtSpowerInited = 1;
	return 0;
}

late_initcall(mt_spower_init);

/** return 0, means sptab is not yet ready. **/
int mt_spower_get_leakage(int dev, int vol, int deg)
{
	BUG_ON(!(dev < MT_SPOWER_MAX));

	if (!tab_validate(&sptab[dev]))
		return 0;

	if (vol > TABLE_MAX_VOL)
		vol = TABLE_MAX_VOL;
	else if (vol < TABLE_MIN_VOL)
		vol = TABLE_MIN_VOL;

	if (deg > TABLE_MAX_TEMP)
		deg = TABLE_MAX_TEMP;
	else if (deg < TABLE_MIN_TEMP)
		deg = TABLE_MIN_TEMP;

	return sptab_lookup(&sptab[dev], vol, deg);
}
EXPORT_SYMBOL(mt_spower_get_leakage);

int mt_spower_get_efuse_lkg(int dev)
{
	/* int devinfo = 0, efuse_lkg = 0; */
	int efuse_lkg_mw = 0;

	BUG_ON(!(dev < MT_SPOWER_MAX));
#if 0 /* non-eFuse define stage */
	switch (dev) {
	case  MT_SPOWER_CPUL:
		devinfo = (int)get_devinfo_with_index(DEVINFO_IDX2); /* P_OD2 */
		efuse_lkg = (devinfo >> 8) & 0xff;
		efuse_lkg_mw = (efuse_lkg == 0) ? DEF_CPUL_LEAKAGE : (int)(devinfo_table[efuse_lkg]*V_OF_FUSE/1000);
		break;
	case  MT_SPOWER_CPULL:
		devinfo = (int)get_devinfo_with_index(DEVINFO_IDX4); /* P_OD4 */
		efuse_lkg = (devinfo >> 8) & 0xff;
		efuse_lkg_mw = (efuse_lkg == 0) ? DEF_CPULL_LEAKAGE : (int)(devinfo_table[efuse_lkg]*V_OF_FUSE/1000);
		break;
	case  MT_SPOWER_GPU:
		devinfo = (int)get_devinfo_with_index(DEVINFO_IDX8); /* P_OD8 */
		efuse_lkg = (devinfo >> 8) & 0xff;
		efuse_lkg_mw = (efuse_lkg == 0) ? DEF_GPU_LEAKAGE : (int)(devinfo_table[efuse_lkg]*V_OF_FUSE/1000);
		break;
	case  MT_SPOWER_MODEM:
		devinfo = (int)get_devinfo_with_index(DEVINFO_IDX10); /* P_OD10 */
		efuse_lkg = (devinfo >> 8) & 0xff;
		efuse_lkg_mw = (efuse_lkg == 0) ? DEF_MODEM_LEAKAGE : (int)(devinfo_table[efuse_lkg]*V_OF_FUSE/1000);
		break;
	case  MT_SPOWER_MODEM_SRAM:
		devinfo = (int)get_devinfo_with_index(DEVINFO_IDX10); /* P_ODxx */
		efuse_lkg = (devinfo >> 8) & 0xff;
		efuse_lkg_mw = (efuse_lkg == 0) ? DEF_MODEM_SRAM_LEAKAGE :
						(int)(devinfo_table[efuse_lkg]*V_OF_FUSE/1000);
		break;
	case  MT_SPOWER_MD:
		devinfo = (int)get_devinfo_with_index(DEVINFO_IDX14); /* P_OD14 */
		efuse_lkg = (devinfo >> 8) & 0xff;
		efuse_lkg_mw = (efuse_lkg == 0) ? DEF_MD_LEAKAGE : (int)(devinfo_table[efuse_lkg]*V_OF_FUSE/1000);
		break;
	case  MT_SPOWER_VCORE:
		devinfo = (int)get_devinfo_with_index(DEVINFO_IDX12); /* P_OD12 */
		efuse_lkg = (devinfo >> 8) & 0xff;
		efuse_lkg_mw = (efuse_lkg == 0) ? DEF_VCORE_LEAKAGE : (int)(devinfo_table[efuse_lkg]*V_OF_FUSE/1000);
		break;
	case  MT_SPOWER_VCORE_SRAM:
		devinfo = (int)get_devinfo_with_index(DEVINFO_IDX12); /* P_ODxx */
		efuse_lkg = (devinfo >> 8) & 0xff;
		efuse_lkg_mw = (efuse_lkg == 0) ? DEF_VCORE_SRAM_LEAKAGE :
						(int)(devinfo_table[efuse_lkg]*V_OF_FUSE/1000);
		break;
	default:
		break;
	}
#else
	switch (dev) {
	case  MT_SPOWER_CPUL:
		efuse_lkg_mw = DEF_CPUL_LEAKAGE;
		break;
	case  MT_SPOWER_CPULL:
		efuse_lkg_mw = DEF_CPULL_LEAKAGE;
		break;
	case  MT_SPOWER_GPU:
		efuse_lkg_mw = DEF_GPU_LEAKAGE;
		break;
	case  MT_SPOWER_MODEM:
		efuse_lkg_mw = DEF_MODEM_LEAKAGE;
		break;
	case  MT_SPOWER_MODEM_SRAM:
		efuse_lkg_mw = DEF_MODEM_SRAM_LEAKAGE;
		break;
	case  MT_SPOWER_MD:
		efuse_lkg_mw = DEF_MD_LEAKAGE;
		break;
	case  MT_SPOWER_VCORE:
		efuse_lkg_mw = DEF_VCORE_LEAKAGE;
		break;
	case  MT_SPOWER_VCORE_SRAM:
		efuse_lkg_mw = DEF_VCORE_SRAM_LEAKAGE;
		break;
	default:
		break;
	}
#endif

	return efuse_lkg_mw;
}
EXPORT_SYMBOL(mt_spower_get_efuse_lkg);
