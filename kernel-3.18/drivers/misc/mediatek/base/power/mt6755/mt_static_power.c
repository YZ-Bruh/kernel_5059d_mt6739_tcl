
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



static sptbl_t sptab[MT_SPOWER_MAX]; /* CPU, VCORE, GPU, VMD1, MODEM, VMODEM_SRAM  */

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
		return p;
	} else if (v1 == v2) {
		c1 = mA(tab, v1, t1);
		c2 = mA(tab, v1, t2);
		p = interpolate(deg(tab, t1), deg(tab, t2), degree, c1, c2);
		return p;
	} else if (t1 == t2) {
		c1 = mA(tab, v1, t1);
		c2 = mA(tab, v2, t1);
		p = interpolate(mV(tab, v1), mV(tab, v2), voltage, c1, c2);
		return p;
	} else {
		c1 = mA(tab, v1, t1);
		c2 = mA(tab, v1, t2);
		p1 = interpolate(deg(tab, t1), deg(tab, t2), degree, c1, c2);

		c1 = mA(tab, v2, t1);
		c2 = mA(tab, v2, t2);
		p2 = interpolate(deg(tab, t1), deg(tab, t2), degree, c1, c2);

		p = interpolate(mV(tab, v1), mV(tab, v2), voltage, p1, p2);
		return p;
	}
}
/* c1, c2, c3(EFUSE) => make sptab 239, 53, 100 */
void interpolate_table(sptbl_t *spt, int c1, int c2, int c3, sptbl_t *tab1, sptbl_t *tab2)
{
	int v, t;

	/* avoid divid error, if we have bad raw data table */
	if (unlikely(c1 == c2)) {
		*spt = *tab1;
		SPOWER_INFO("sptab equal to tab1:%d/%d\n",  c1, c3);
		return;
	}

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

	tab[0].vsize = 0;
	tab[0].tsize = 0;
	tab[0].data = NULL;
	tab[0].vrow = NULL;
	tab[0].trow = NULL;

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

		SPOWER_INFO("sptab interpolate tab:%d/%d\n",  wat, c);
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
		case  MT_SPOWER_CPU:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_CPU\n");
			break;
		case  MT_SPOWER_VCORE:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_VCORE\n");
			break;
		case  MT_SPOWER_GPU:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_GPU\n");
			break;
		case  MT_SPOWER_VMD1:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_VMD1\n");
			break;
		case  MT_SPOWER_MODEM:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_MODEM\n");
			break;
		case  MT_SPOWER_VMODEM_SRAM:
			SPOWER_INFO("[SPOWER] - This is MT_SPOWER_VMODEM_SRAM\n");
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


		v = 1150;
		t = 105;
		p  = sptab_lookup(spt, v, t);
		SPOWER_INFO("v/t/p: %d/%d/%d\n", v, t, p);
		switch (i) {
		case  MT_SPOWER_CPU:
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_CPU  Done\n");
			break;
		case  MT_SPOWER_VCORE:
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_VCORE  Done\n");
			break;
		case  MT_SPOWER_GPU:
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_GPU  Done\n");
			break;
		case  MT_SPOWER_VMD1:
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_VMD1  Done\n");
			break;
		case  MT_SPOWER_MODEM:
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_MODEM  Done\n");
			break;
		case  MT_SPOWER_VMODEM_SRAM:
			SPOWER_INFO("[SPOWER] -  MT_SPOWER_VMODEM_SRAM  Done\n");
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
	#define DEVINFO_IDX0 (18)
	#define DEVINFO_IDX1 (19)

	int devinfo = 0, devinfo_1 = 0;
	int cpu, vcore, gpu, vmd1, modem, vmodem_sram;

	if (1 == mtSpowerInited)
		return 0;

	/* avoid side effect from multiple invocation */
	if (tab_validate(&sptab[MT_SPOWER_CPU]))
		return 0;

	devinfo = (int)get_devinfo_with_index(DEVINFO_IDX0); /* ptp_read(0xF0206274);  M_SRM_RP5 */
	cpu	= (devinfo >> 24) & 0x0ff;
	vcore	= (devinfo >> 16) & 0x0ff;
	gpu	= (devinfo >> 8) & 0x0ff;
	vmd1	= (devinfo) & 0x0ff;
	devinfo_1 = (int)get_devinfo_with_index(DEVINFO_IDX1); /* ptp_read(0xF0206278);  M_SRM_RP6 */
	modem	= (devinfo >> 24) & 0x0ff;
	vmodem_sram	= (devinfo >> 16) & 0x0ff;
	SPOWER_INFO("[SPOWER] - cpu/vcore/gpu/vmd1/modem/vmodem_sram => 0x%x/0x%x/0x%x/0x%x/0x%x/0x%x\n",
		cpu, vcore, gpu, vmd1, modem, vmodem_sram);

	if ((devinfo != 0) && (devinfo_1 != 0)) {
		cpu	= (int)devinfo_table[cpu];
		vcore	= (int)devinfo_table[vcore];
		gpu	= (int)devinfo_table[gpu];
		vmd1	= (int)devinfo_table[vmd1];
		modem	= (int)devinfo_table[modem];
		vmodem_sram	= (int)devinfo_table[vmodem_sram];
		SPOWER_INFO("[SPOWER] - cpu/vcore/gpu/vmd1/modem/vmodem_sram => 0x%x/0x%x/0x%x/0x%x/0x%x/0x%x\n",
		cpu, vcore, gpu, vmd1, modem, vmodem_sram);

		cpu	= (int)(cpu*1150/1000);
		vcore	= (int)(vcore*1150/1000);
		gpu	= (int)(gpu*1150/1000);
		vmd1	= (int)(vmd1*1150/1000);
		modem	= (int)(modem*1150/1000);
		vmodem_sram	= (int)(vmodem_sram*1150/1000);
	} else {
		cpu = 100;
		vcore = 100;
		gpu = 100;
		vmd1 = 162;
		modem = 300;
		vmodem_sram = 300;
	}

	SPOWER_INFO("[SPOWER] - cpu/vcore/gpu/vmd1/modem/vmodem_sram => %d/%d/%d/%d/%d/%d\n",
		cpu, vcore, gpu, vmd1, modem, vmodem_sram);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_CPU\n");
	mt_spower_make_table(&sptab[MT_SPOWER_CPU], &cpu_spower_raw, cpu, 1150, 30);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_VCORE\n");
	mt_spower_make_table(&sptab[MT_SPOWER_VCORE], &vcore_spower_raw, vcore, 1150, 30);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_GPU\n");
	mt_spower_make_table(&sptab[MT_SPOWER_GPU], &gpu_spower_raw, gpu, 1150, 30);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_VMD1\n");
	mt_spower_make_table(&sptab[MT_SPOWER_VMD1], &vmd1_spower_raw, vmd1, 1150, 30);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_MODEM\n");
	mt_spower_make_table(&sptab[MT_SPOWER_MODEM], &modem_spower_raw, modem, 1150, 30);
	SPOWER_INFO("[SPOWER] - MT_SPOWER_VMODEM_SRAM\n");
	mt_spower_make_table(&sptab[MT_SPOWER_VMODEM_SRAM], &vmodem_sram_spower_raw, vmodem_sram, 1150, 30);
	/* mt_spower_make_table(&sptab[MT_SPOWER_VCORE], &vcore_spower_raw, vcore, 1150, 30); */

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

	return sptab_lookup(&sptab[dev], vol, deg);
}
EXPORT_SYMBOL(mt_spower_get_leakage);
