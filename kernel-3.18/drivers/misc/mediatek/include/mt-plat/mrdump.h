
#if !defined(__MRDUMP_H__)
#define __MRDUMP_H__

#include <stdarg.h>
#include <linux/elf.h>
#include <linux/elfcore.h>
#include <asm/ptrace.h>
#include <mt-plat/aee.h>

#ifdef __aarch64__
#define reg_pc	pc
#define reg_lr	regs[30]
#define reg_sp	sp
#define reg_fp	regs[29]
#else
#define reg_pc	ARM_pc
#define reg_lr	ARM_lr
#define reg_sp	ARM_sp
#define reg_ip	ARM_ip
#define reg_fp	ARM_fp
#endif

#define MRDUMP_CPU_MAX 16

#define MRDUMP_ENABLE_COOKIE 0x590d2ba3

#define MRDUMP_GO_DUMP "MRDUMP07"

#define KSYM_32        1
#define KSYM_64        2

typedef uint32_t arm32_gregset_t[18];
typedef uint64_t aarch64_gregset_t[34];

struct mrdump_crash_record {
	int reboot_mode;

	char msg[128];
	char backtrace[512];

	uint32_t fault_cpu;

	union {
		arm32_gregset_t arm32_regs;
		aarch64_gregset_t aarch64_regs;
	} cpu_regs[MRDUMP_CPU_MAX];
};

struct mrdump_ksyms_param {
	char     tag[4];
	uint32_t flag;
	uint32_t crc;
	uint64_t start_addr;
	uint32_t size;
	uint32_t addresses_off;
	uint32_t num_syms_off;
	uint32_t names_off;
	uint32_t markers_off;
	uint32_t token_table_off;
	uint32_t token_index_off;
} __packed;

struct mrdump_machdesc {
	uint32_t nr_cpus;

	uint64_t page_offset;
	uint64_t high_memory;

	uint64_t kimage_vaddr;
	uint64_t kimage_init_begin;
	uint64_t kimage_init_end;
	uint64_t kimage_stext;
	uint64_t kimage_etext;
	uint64_t kimage_srodata;
	uint64_t kimage_erodata;
	uint64_t kimage_sdata;
	uint64_t kimage_edata;

	uint64_t vmalloc_start;
	uint64_t vmalloc_end;

	uint64_t modules_start;
	uint64_t modules_end;

	uint64_t phys_offset;
	uint64_t master_page_table;

	uint64_t memmap;

	uint64_t dfdmem_pa;

	struct mrdump_ksyms_param kallsyms;
};

struct mrdump_control_block {
	char sig[8];

	struct mrdump_machdesc machdesc;
	uint32_t machdesc_crc;

	uint32_t enabled;
	uint32_t output_fs_lbaooo;

	struct mrdump_crash_record crash_record;
};

/* NOTE!! any change to this struct should be compatible in aed */
struct mrdump_mini_reg_desc {
	unsigned long reg;	/* register value */
	unsigned long kstart;	/* start kernel addr of memory dump */
	unsigned long kend;	/* end kernel addr of memory dump */
	unsigned long pos;	/* next pos to dump */
	int valid;		/* 1: valid regiser, 0: invalid regiser */
	int pad;		/* reserved */
	loff_t offset;		/* offset in buffer */
};

/* it should always be smaller than MRDUMP_MINI_HEADER_SIZE */
struct mrdump_mini_header {
	struct mrdump_mini_reg_desc reg_desc[ELF_NGREG];
};

#define MRDUMP_MINI_NR_SECTION 60
#define MRDUMP_MINI_SECTION_SIZE (32 * 1024)
#define NT_IPANIC_MISC 4095
#define MRDUMP_MINI_NR_MISC 20

struct mrdump_mini_elf_misc {
	unsigned long vaddr;
	unsigned long paddr;
	unsigned long start;
	unsigned long size;
};

#define NOTE_NAME_SHORT 12
#define NOTE_NAME_LONG  20

struct mrdump_mini_elf_psinfo {
		struct elf_note note;
		char name[NOTE_NAME_SHORT];
		struct elf_prpsinfo data;
};

struct mrdump_mini_elf_prstatus {
		struct elf_note note;
		char name[NOTE_NAME_SHORT];
		struct elf_prstatus data;
};

struct mrdump_mini_elf_note {
		struct elf_note note;
		char name[NOTE_NAME_LONG];
		struct mrdump_mini_elf_misc data;
};

struct mrdump_mini_elf_header {
	struct elfhdr ehdr;
	struct elf_phdr phdrs[MRDUMP_MINI_NR_SECTION];
	struct mrdump_mini_elf_psinfo psinfo;
	struct mrdump_mini_elf_prstatus prstatus[NR_CPUS + 1];
	struct mrdump_mini_elf_note misc[MRDUMP_MINI_NR_MISC];
};

typedef struct mrdump_rsvmem_block {
	phys_addr_t start_addr;
	phys_addr_t size;
} mrdump_rsvmem_block_t;


#define MRDUMP_MINI_HEADER_SIZE ALIGN(sizeof(struct mrdump_mini_elf_header), PAGE_SIZE)
#define MRDUMP_MINI_DATA_SIZE (MRDUMP_MINI_NR_SECTION * MRDUMP_MINI_SECTION_SIZE)
#define MRDUMP_MINI_BUF_SIZE (MRDUMP_MINI_HEADER_SIZE + MRDUMP_MINI_DATA_SIZE)

#ifdef CONFIG_MTK_RAM_CONSOLE_DRAM_ADDR
#define MRDUMP_MINI_BUF_PADDR (CONFIG_MTK_RAM_CONSOLE_DRAM_ADDR + 0xf0000)
#else
#define MRDUMP_MINI_BUF_PADDR 0
#endif

int mrdump_init(void);
void __mrdump_create_oops_dump(AEE_REBOOT_MODE reboot_mode, struct pt_regs *regs, const char *msg,
			       ...);

#if defined(CONFIG_MTK_AEE_MRDUMP)
void mrdump_reserve_memory(void);

void aee_kdump_reboot(AEE_REBOOT_MODE, const char *msg, ...);

#else
static inline void mrdump_reserve_memory(void)
{
}

static inline void aee_kdump_reboot(AEE_REBOOT_MODE reboot_mode, const char *msg, ...)
{
}

#endif

void __init __weak mrdump_rsvmem(void);

typedef int (*mrdump_write)(void *buf, int off, int len, int encrypt);
#if defined(CONFIG_MTK_AEE_IPANIC)
int mrdump_mini_create_oops_dump(AEE_REBOOT_MODE reboot_mode, mrdump_write write,
				 loff_t sd_offset, const char *msg, va_list ap);
void mrdump_mini_reserve_memory(void);
#else
static inline int mrdump_mini_create_oops_dump(AEE_REBOOT_MODE reboot_mode, mrdump_write write,
					       loff_t sd_offset, const char *msg, va_list ap)
{
	return 0;
}

static inline void mrdump_mini_reserve_memory(void)
{
}
#endif

#endif
