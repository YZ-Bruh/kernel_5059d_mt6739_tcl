
#ifndef MT_SPOWER_CPU_H
#define MT_SPOWER_CPU_H



#define VSIZE 10
#define TSIZE 20
#define MAX_TABLE_SIZE 3

#define CPU_TABLE_0                                                   \
	/* "(WAT 8.62%) Leakage Power"	 */                             \
					{800,	850,	900,	950,	1000,	1050,	1100,	1150,	1200,	1250, \
				25, 14, 17, 21, 26, 33, 41, 51, 65, 81, 102,\
				30, 16, 20, 24, 30, 38, 47, 59, 73, 92, 115,\
				35, 18, 22, 28, 35, 43, 53, 67, 83, 104, 130,\
				40, 21, 26, 32, 40, 49, 61, 76, 94, 118, 146,\
				45, 24, 29, 36, 45, 56, 69, 86, 107, 132, 165,\
				50, 27, 34, 42, 51, 64, 79, 97, 120, 149, 185,\
				55, 31, 39, 47, 58, 72, 89, 110, 136, 168, 208,\
				60, 36, 44, 54, 66, 82, 101, 124, 153, 189, 233,\
				65, 41, 50, 61, 75, 93, 114, 140, 172, 212, 262,\
				70, 47, 57, 70, 85, 105, 128, 157, 193, 238, 293,\
				75, 53, 65, 79, 97, 118, 144, 177, 217, 266, 327,\
				80, 60, 74, 90, 109, 133, 163, 199, 243, 297, 364,\
				85, 69, 83, 101, 123, 150, 183, 223, 272, 332, 406,\
				90, 78, 94, 114, 139, 168, 205, 249, 303, 370, 452,\
				95, 88, 107, 129, 156, 189, 229, 278, 339, 413, 502,\
				100, 99, 120, 145, 175, 212, 257, 311, 378, 459, 558,\
				105, 112, 135, 163, 197, 237, 287, 347, 420, 510, 619,\
				110, 126, 152, 183, 220, 266, 320, 386, 467, 565, 684,\
				115, 142, 171, 205, 247, 297, 357, 430, 517, 624, 755,\
				120, 160, 192, 230, 276, 330, 397, 476, 572, 690, 833},

#define CPU_TABLE_1                                                   \
	/* "(WAT 1.72%) Leakage Power"  */                              \
					{800,	850,	900,	950,	1000,	1050,	1100,	1150,	1200,	1250, \
				25, 8, 10, 13, 16, 20, 25, 31, 39, 49, 62,\
				30, 10, 12, 15, 18, 23, 29, 36, 45, 56, 71,\
				35, 11, 14, 17, 21, 26, 33, 41, 51, 64, 80,\
				40, 13, 16, 20, 24, 30, 37, 47, 58, 72, 90,\
				45, 15, 18, 23, 28, 35, 43, 53, 66, 82, 102,\
				50, 17, 21, 26, 32, 39, 49, 60, 74, 92, 115,\
				55, 20, 24, 30, 36, 45, 55, 68, 84, 104, 129,\
				60, 22, 27, 34, 41, 51, 62, 77, 95, 117, 145,\
				65, 26, 31, 38, 47, 57, 70, 86, 106, 131, 162,\
				70, 29, 36, 43, 53, 65, 79, 97, 119, 147, 181,\
				75, 33, 40, 49, 60, 73, 89, 109, 134, 164, 201,\
				80, 38, 46, 56, 68, 82, 100, 122, 149, 182, 224,\
				85, 43, 52, 63, 76, 92, 112, 136, 166, 203, 248,\
				90, 48, 58, 70, 85, 103, 125, 152, 185, 225, 274,\
				95, 54, 66, 79, 95, 115, 139, 169, 205, 249, 303,\
				100, 61, 74, 89, 107, 128, 155, 187, 226, 275, 333,\
				105, 69, 83, 99, 119, 143, 172, 207, 249, 302, 365,\
				110, 77, 93, 111, 133, 159, 190, 229, 275, 331, 400,\
				115, 86, 103, 123, 147, 176, 210, 252, 302, 363, 438,\
				120, 97, 115, 137, 163, 194, 232, 277, 331, 398, 477},

#define CPU_TABLE_2							\
	/*"(WAT -11.35%) Leakage Power" */                               \
					{800,	850,	900,	950,	1000,	1050,	1100,	1150,	1200,	1250, \
				25, 4, 4, 5, 7, 8, 10, 13, 16, 21, 26,\
				30, 4, 5, 6, 8, 9, 12, 15, 18, 23, 29,\
				35, 5, 6, 7, 9, 11, 13, 17, 21, 26, 33,\
				40, 5, 6, 8, 10, 12, 15, 19, 23, 29, 36,\
				45, 6, 7, 9, 11, 14, 17, 21, 26, 33, 41,\
				50, 7, 9, 10, 13, 16, 19, 24, 29, 37, 46,\
				55, 8, 10, 12, 14, 18, 22, 27, 33, 41, 51,\
				60, 9, 11, 14, 17, 20, 25, 30, 37, 46, 57,\
				65, 11, 13, 16, 19, 23, 28, 34, 42, 52, 63,\
				70, 12, 15, 18, 21, 26, 32, 39, 47, 58, 71,\
				75, 14, 17, 20, 24, 30, 36, 44, 53, 65, 79,\
				80, 16, 19, 23, 28, 34, 41, 49, 60, 72, 89,\
				85, 19, 22, 27, 32, 38, 46, 55, 67, 81, 99,\
				90, 22, 26, 31, 36, 44, 52, 63, 76, 92, 111,\
				95, 25, 30, 35, 42, 50, 59, 71, 86, 103, 125,\
				100, 29, 34, 40, 48, 57, 67, 81, 97, 116, 141,\
				105, 33, 39, 46, 55, 64, 77, 91, 109, 131, 158,\
				110, 38, 45, 53, 62, 73, 87, 103, 123, 147, 176,\
				115, 44, 52, 61, 71, 84, 99, 117, 139, 165, 198,\
				120, 51, 60, 70, 82, 96, 113, 133, 157, 186, 222},

#define LTE_TABLE_0							\
	/* "(WAT 11.57%) Leakage Power"	 */                             \
					{800,	850,	900,	950,	1000,	1050,	1100,	1150,	1200,	1250, \
				25, 8, 9, 12, 14, 18, 22, 27, 33, 41, 52,\
				30, 9, 11, 14, 17, 20, 25, 31, 38, 48, 59,\
				35, 11, 13, 16, 19, 24, 29, 36, 44, 55, 68,\
				40, 13, 15, 18, 23, 28, 34, 42, 51, 63, 78,\
				45, 15, 18, 22, 26, 32, 39, 48, 59, 73, 90,\
				50, 17, 21, 26, 31, 38, 46, 56, 68, 84, 103,\
				55, 21, 25, 30, 36, 44, 53, 65, 79, 97, 119,\
				60, 24, 29, 35, 42, 51, 62, 75, 91, 112, 137,\
				65, 29, 34, 41, 50, 60, 72, 87, 106, 129, 157,\
				70, 34, 40, 48, 58, 70, 84, 101, 122, 148, 180,\
				75, 40, 48, 57, 68, 81, 98, 117, 142, 171, 207,\
				80, 47, 56, 67, 80, 95, 114, 136, 164, 197, 238,\
				85, 56, 66, 78, 93, 111, 133, 158, 189, 227, 274,\
				90, 66, 78, 92, 109, 130, 154, 184, 219, 262, 315,\
				95, 78, 92, 108, 128, 152, 180, 213, 254, 303, 362,\
				100, 92, 108, 127, 150, 177, 209, 247, 294, 351, 417,\
				105, 110, 128, 150, 176, 206, 244, 288, 341, 405, 483,\
				110, 129, 151, 176, 206, 241, 284, 335, 395, 469, 557,\
				115, 153, 178, 207, 242, 283, 331, 389, 458, 541, 641,\
				120, 182, 211, 245, 284, 331, 386, 452, 531, 625, 738},


#define LTE_TABLE_1							\
	/* "(WAT 0.78%) Leakage Power" */                               \
					{800,	850,	900,	950,	1000,	1050,	1100,	1150,	1200,	1250, \
				25, 5, 6, 7, 9, 12, 15, 18, 23, 29, 37,\
				30, 6, 7, 9, 11, 13, 16, 21, 26, 33, 42,\
				35, 7, 8, 10, 12, 15, 19, 23, 29, 37, 47,\
				40, 8, 9, 11, 14, 17, 22, 27, 33, 42, 53,\
				45, 9, 11, 13, 16, 20, 25, 31, 38, 47, 59,\
				50, 10, 13, 15, 19, 23, 28, 35, 43, 54, 67,\
				55, 12, 15, 18, 22, 27, 33, 40, 50, 61, 76,\
				60, 15, 18, 21, 26, 31, 38, 46, 57, 70, 86,\
				65, 17, 21, 25, 30, 36, 44, 53, 65, 80, 98,\
				70, 20, 24, 29, 35, 42, 51, 62, 75, 91, 112,\
				75, 24, 29, 34, 41, 49, 59, 71, 87, 105, 128,\
				80, 29, 34, 41, 49, 58, 69, 83, 100, 121, 147,\
				85, 35, 41, 48, 57, 68, 81, 97, 116, 140, 169,\
				90, 42, 49, 57, 68, 80, 95, 113, 135, 162, 196,\
				95, 50, 58, 68, 80, 95, 112, 133, 158, 188, 226,\
				100, 61, 70, 82, 96, 113, 132, 156, 185, 220, 263,\
				105, 73, 84, 98, 114, 133, 157, 184, 217, 258, 306,\
				110, 88, 102, 117, 137, 159, 185, 217, 255, 301, 356,\
				115, 108, 124, 142, 164, 190, 221, 258, 301, 353, 418,\
				120, 131, 149, 171, 197, 228, 263, 306, 356, 416, 488},


#define LTE_TABLE_2							\
	/* "(WAT -8.16%) Leakage Power"	 */                             \
					{800,	850,	900,	950,	1000,	1050,	1100,	1150,	1200,	1250, \
				25, 3, 4, 5, 6, 8, 10, 13, 17, 22, 30,\
				30, 3, 4, 5, 7, 8, 11, 14, 18, 24, 32,\
				35, 4, 5, 6, 7, 9, 12, 15, 20, 26, 34,\
				40, 4, 5, 7, 8, 10, 13, 17, 22, 28, 37,\
				45, 5, 6, 7, 9, 12, 15, 19, 24, 31, 40,\
				50, 6, 7, 8, 11, 13, 17, 21, 26, 34, 43,\
				55, 6, 8, 10, 12, 15, 19, 23, 29, 37, 48,\
				60, 8, 9, 11, 14, 17, 21, 26, 33, 41, 53,\
				65, 9, 11, 13, 16, 19, 24, 29, 37, 46, 58,\
				70, 10, 13, 15, 18, 22, 27, 34, 41, 52, 65,\
				75, 12, 15, 18, 21, 26, 32, 38, 47, 58, 73,\
				80, 15, 17, 21, 25, 30, 36, 44, 54, 66, 82,\
				85, 18, 21, 25, 29, 35, 42, 51, 62, 76, 94,\
				90, 21, 25, 29, 35, 41, 49, 59, 72, 88, 107,\
				95, 26, 30, 36, 42, 49, 58, 70, 84, 101, 123,\
				100, 32, 37, 43, 50, 58, 69, 82, 98, 118, 143,\
				105, 39, 45, 52, 60, 70, 83, 97, 115, 138, 166,\
				110, 48, 55, 63, 73, 84, 99, 116, 136, 162, 193,\
				115, 60, 68, 78, 89, 102, 118, 138, 162, 192, 227,\
				120, 75, 84, 96, 109, 124, 142, 165, 193, 228, 268},

#define VCORE_TABLE_0							\
	/* "(WAT 11.57%) Leakage Power"	 */                             \
					{800,	850,	900,	950,	1000,	1050,	1100,	1150,	1200,	1250, \
				25, 8, 10, 12, 15, 18, 22, 28, 34, 42, 53,\
				30, 9, 11, 14, 17, 21, 26, 32, 39, 48, 60,\
				35, 11, 13, 16, 20, 24, 30, 36, 45, 55, 69,\
				40, 13, 16, 19, 23, 28, 34, 42, 51, 63, 78,\
				45, 15, 18, 22, 27, 32, 39, 48, 59, 72, 89,\
				50, 18, 21, 26, 31, 37, 45, 55, 68, 83, 102,\
				55, 21, 25, 30, 36, 43, 52, 64, 78, 95, 116,\
				60, 24, 29, 35, 42, 50, 60, 73, 89, 109, 133,\
				65, 28, 34, 40, 48, 58, 70, 84, 102, 124, 152,\
				70, 33, 39, 47, 56, 67, 81, 97, 118, 142, 173,\
				75, 38, 46, 54, 65, 78, 93, 112, 135, 163, 198,\
				80, 45, 53, 63, 75, 90, 107, 129, 154, 186, 225,\
				85, 52, 62, 73, 87, 104, 123, 148, 177, 213, 257,\
				90, 61, 72, 85, 101, 120, 142, 169, 203, 243, 293,\
				95, 72, 84, 99, 117, 138, 164, 195, 232, 278, 335,\
				100, 84, 98, 115, 135, 160, 189, 224, 266, 318, 382,\
				105, 98, 114, 134, 156, 184, 218, 257, 305, 363, 434,\
				110, 114, 133, 155, 182, 213, 250, 295, 350, 415, 494,\
				115, 133, 155, 180, 211, 246, 288, 339, 401, 474, 564,\
				120, 155, 180, 210, 244, 284, 332, 389, 459, 542, 642},

#define VCORE_TABLE_1							\
	/* "(WAT 0.78%) Leakage Power"	 */                             \
					{800,	850,	900,	950,	1000,	1050,	1100,	1150,	1200,	1250, \
				25, 5, 6, 8, 10, 12, 15, 19, 23, 30, 38,\
				30, 6, 7, 9, 11, 14, 17, 21, 27, 33, 42,\
				35, 7, 9, 10, 13, 16, 19, 24, 30, 38, 48,\
				40, 8, 10, 12, 15, 18, 22, 28, 34, 43, 54,\
				45, 10, 12, 14, 17, 21, 25, 31, 39, 48, 60,\
				50, 11, 13, 16, 20, 24, 29, 36, 44, 55, 68,\
				55, 13, 16, 19, 23, 27, 34, 41, 50, 62, 77,\
				60, 15, 18, 22, 26, 32, 38, 47, 58, 71, 87,\
				65, 18, 21, 25, 30, 37, 44, 54, 66, 80, 99,\
				70, 21, 25, 30, 35, 42, 51, 62, 75, 91, 112,\
				75, 25, 29, 34, 41, 49, 59, 71, 86, 104, 127,\
				80, 29, 34, 40, 48, 57, 68, 81, 98, 119, 145,\
				85, 34, 40, 47, 55, 66, 78, 94, 112, 136, 165,\
				90, 40, 47, 55, 65, 76, 90, 108, 129, 156, 188,\
				95, 47, 55, 64, 75, 88, 104, 124, 148, 178, 214,\
				100, 56, 65, 75, 88, 103, 121, 143, 170, 204, 245,\
				105, 65, 76, 88, 102, 119, 140, 165, 196, 234, 280,\
				110, 77, 89, 103, 120, 139, 163, 192, 227, 270, 321,\
				115, 92, 105, 121, 141, 163, 191, 223, 263, 310, 368,\
				120, 109, 125, 143, 165, 191, 222, 259, 303, 357, 421},

#define VCORE_TABLE_2							\
	/* "(WAT 11.57%) Leakage Power" */                              \
					{800,	850,	900,	950,	1000,	1050,	1100,	1150,	1200,	1250, \
				25, 3, 4, 5, 6, 7, 9, 12, 15, 20, 27,\
				30, 3, 4, 5, 6, 8, 10, 13, 17, 22, 29,\
				35, 4, 5, 6, 7, 9, 11, 14, 18, 24, 31,\
				40, 4, 5, 6, 8, 10, 13, 16, 20, 26, 34,\
				45, 5, 6, 7, 9, 11, 14, 18, 22, 29, 37,\
				50, 6, 7, 8, 10, 13, 16, 20, 25, 32, 41,\
				55, 7, 8, 10, 12, 14, 18, 22, 28, 35, 45,\
				60, 8, 9, 11, 13, 16, 20, 25, 31, 39, 49,\
				65, 9, 11, 13, 15, 19, 23, 28, 35, 43, 54,\
				70, 11, 12, 15, 18, 21, 26, 32, 39, 49, 61,\
				75, 12, 15, 17, 20, 24, 30, 36, 44, 55, 68,\
				80, 15, 17, 20, 24, 28, 34, 41, 50, 62, 77,\
				85, 18, 20, 24, 28, 33, 40, 47, 58, 71, 87,\
				90, 21, 24, 28, 33, 38, 46, 55, 66, 81, 99,\
				95, 25, 29, 33, 38, 45, 54, 64, 77, 93, 113,\
				100, 30, 34, 39, 46, 53, 63, 74, 89, 107, 129,\
				105, 36, 41, 47, 55, 63, 74, 87, 103, 123, 148,\
				110, 44, 50, 57, 65, 75, 87, 102, 120, 143, 171,\
				115, 54, 60, 68, 78, 89, 103, 120, 140, 165, 198,\
				120, 66, 73, 82, 93, 106, 122, 142, 164, 194, 229},

typedef struct spower_raw_s {
	int vsize;
	int tsize;
	int table_size;
	int *table[];
} spower_raw_t;


/** table order: ff, tt, ss **/

int cpu_leakage_data[][VSIZE * TSIZE + VSIZE + TSIZE] = {
	CPU_TABLE_0
	CPU_TABLE_1
	CPU_TABLE_2
};

int lte_leakage_data[][VSIZE * TSIZE + VSIZE + TSIZE] = {
	LTE_TABLE_0
	LTE_TABLE_1
	LTE_TABLE_2
};

int vcore_leakage_data[][VSIZE * TSIZE + VSIZE + TSIZE] = {
	VCORE_TABLE_0
	VCORE_TABLE_1
	VCORE_TABLE_2
};

spower_raw_t cpu_spower_raw = {
	.vsize = VSIZE,
	.tsize = TSIZE,
	.table_size = 3,
	.table = {
	    (int *)&cpu_leakage_data[0], (int *)&cpu_leakage_data[1], (int *)&cpu_leakage_data[2]},
};

spower_raw_t lte_spower_raw = {
	.vsize = VSIZE,
	.tsize = TSIZE,
	.table_size = 3,
	.table = {
	    (int *)&lte_leakage_data[0], (int *)&lte_leakage_data[1], (int *)&lte_leakage_data[2]},
};

spower_raw_t vcore_spower_raw = {
	.vsize = VSIZE,
	.tsize = TSIZE,
	.table_size = 3,
	.table = {
	    (int *)&vcore_leakage_data[0], (int *)&vcore_leakage_data[1],
	     (int *)&vcore_leakage_data[2]},
};



typedef struct voltage_row_s {
	int mV[VSIZE];
} vrow_t;

typedef struct temperature_row_s {
	int deg;
	int mA[VSIZE];
} trow_t;


typedef struct sptab_s {
	int vsize;
	int tsize;
	int *data;		/* array[VSIZE + TSIZE + (VSIZE*TSIZE)]; */
	vrow_t *vrow;		/* pointer to voltage row of data */
	trow_t *trow;		/* pointer to temperature row of data */
} sptbl_t;

#define trow(tab, ti)		((tab)->trow[ti])
#define mA(tab, vi, ti)	((tab)->trow[ti].mA[vi])
#define mV(tab, vi)		((tab)->vrow[0].mV[vi])
#define deg(tab, ti)		((tab)->trow[ti].deg)
#define vsize(tab)		((tab)->vsize)
#define tsize(tab)		((tab)->tsize)
#define tab_validate(tab)	(!!(tab) && (tab)->data != NULL)

static inline void spower_tab_construct(sptbl_t(*tab)[], spower_raw_t *raw)
{
	int i;
	sptbl_t *ptab = (sptbl_t *) tab;

	for (i = 0; i < raw->table_size; i++) {
		ptab->vsize = raw->vsize;
		ptab->tsize = raw->tsize;
		ptab->data = raw->table[i];
		ptab->vrow = (vrow_t *) ptab->data;
		ptab->trow = (trow_t *) (ptab->data + ptab->vsize);
		ptab++;
	}
}



#endif
