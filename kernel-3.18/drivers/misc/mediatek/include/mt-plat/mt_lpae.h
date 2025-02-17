
#ifndef __MT_LPAE_H__
#define __MT_LPAE_H__
#ifdef CONFIG_MTK_LM_MODE

#include <mt-plat/mt_io.h>

#define INTERAL_MAPPING_OFFSET (0x40000000)
#define INTERAL_MAPPING_LIMIT (INTERAL_MAPPING_OFFSET + 0x80000000)

#define MT_OVERFLOW_ADDR_START 0x100000000ULL


extern unsigned int enable_4G(void);

/* For HW modules which support 33-bit address setting */
#define CROSS_OVERFLOW_ADDR_TRANSFER(phy_addr, size, ret) \
	do { \
		ret = 0; \
		if (enable_4G()) {\
			if (((phys_addr_t)phy_addr < MT_OVERFLOW_ADDR_START)\
					&& (((phys_addr_t)phy_addr + size) >= MT_OVERFLOW_ADDR_START)) \
				ret = MT_OVERFLOW_ADDR_START - phy_addr; \
		} \
	}  while (0) \

/* For SPM and MD32 only in ROME */
#define MAPPING_DRAM_ACCESS_ADDR(phy_addr) \
	do { \
		if (enable_4G()) {\
			if (phy_addr >= INTERAL_MAPPING_OFFSET && phy_addr < INTERAL_MAPPING_LIMIT) \
				phy_addr += INTERAL_MAPPING_OFFSET; \
		} \
	} while (0)\

#else /* !CONFIG_ARM_LPAE */

#define CROSS_OVERFLOW_ADDR_TRANSFER(phy_addr, size, ret)
#define MAPPING_DRAM_ACCESS_ADDR(phy_addr)
#define MT_OVERFLOW_ADDR_START 0

static inline unsigned int enable_4G(void)
{
	return 0;
}

#endif
#endif  /*!__MT_LPAE_H__ */
