
#ifndef __USB20_H__
#define __USB20_H__

#define ID_PIN_USE_EX_EINT 1

#ifdef CONFIG_USB_MTK_OTG
#ifdef ID_PIN_USE_EX_EINT
#define IDDIG_EINT_PIN (GPIO_OTG_IDDIG_EINT_PIN & ~(0x80000000))
/* #define IDDIG_EINT_PIN 0 */
#else
#ifdef CONFIG_OF
extern struct musb *mtk_musb;
#define U2PHYDTM1  (((unsigned long)mtk_musb->xceiv->io_priv)+0x800 + 0x6c)
#else
#define U2PHYDTM1  (USB_SIF_BASE+0x800 + 0x6c)
#endif
#define ID_PULL_UP 0x0101
#define ID_PHY_RESET 0x3d11
#endif
extern unsigned int mt_gpio_to_irq(unsigned int gpio);
#endif

#if defined(CONFIG_MTK_FAN5405_SUPPORT) \
|| defined(CONFIG_MTK_BQ24158_SUPPORT) \
|| defined(CONFIG_MTK_NCP1851_SUPPORT) \
|| defined(CONFIG_MTK_BQ24196_SUPPORT) \
|| defined(CONFIG_MTK_BQ24296_SUPPORT) \
|| defined(CONFIG_MTK_NCP1854_SUPPORT)
#define SWITCH_CHARGER 1
#endif

#if defined(CONFIG_MTK_FPGA)
#define FPGA_PLATFORM 1
#endif

struct mt_usb_glue {
	struct device *dev;
	struct platform_device *musb;
};

/* Battery relative function */
typedef enum {
	CHARGER_UNKNOWN = 0,
	STANDARD_HOST,		/* USB : 450mA */
	CHARGING_HOST,
	NONSTANDARD_CHARGER,	/* AC : 450mA~1A */
	STANDARD_CHARGER,	/* AC : ~1A */
} CHARGER_TYPE;

extern int first_connect;
extern int check_delay_done;
extern void wake_up_bat(void);
extern CHARGER_TYPE mt_charger_type_detection(void);
extern kal_bool upmu_is_chr_det(void);
extern unsigned int upmu_get_rgs_chrdet(void);
extern void BATTERY_SetUSBState(int usb_state);
extern void upmu_interrupt_chrdet_int_en(unsigned int val);

/* specific USB fuctnion */
typedef enum {
	CABLE_MODE_CHRG_ONLY = 0,
	CABLE_MODE_NORMAL,
	CABLE_MODE_HOST_ONLY,
	CABLE_MODE_MAX
} CABLE_MODE;

#ifdef CONFIG_MTK_UART_USB_SWITCH
typedef enum {
	PORT_MODE_USB = 0,
	PORT_MODE_UART,
	PORT_MODE_MAX
} PORT_MODE;

extern bool usb_phy_check_in_uart_mode(void);
extern void usb_phy_switch_to_usb(void);
extern void usb_phy_switch_to_uart(void);
#endif
/*add struct and enum for linux kernel 3.10 dts*/

#if 0

#endif

/* switch charger API*/
#ifdef CONFIG_MTK_FAN5405_SUPPORT
extern void fan5405_set_opa_mode(unsigned int val);
extern void fan5405_set_otg_pl(unsigned int val);
extern void fan5405_set_otg_en(unsigned int val);
extern unsigned int fan5405_reg_config_interface(unsigned char RegNum, unsigned char val);
#elif defined(CONFIG_MTK_BQ24261_SUPPORT)
extern void bq24261_set_en_boost(unsigned int val);
#elif defined(CONFIG_MTK_BQ24296_SUPPORT)
extern void bq24296_set_otg_config(unsigned int val);
extern void bq24296_set_boostv(unsigned int val);
extern void bq24296_set_boost_lim(unsigned int val);
extern void bq24296_set_en_hiz(unsigned int val);
#elif defined(CONFIG_MTK_BQ24196_SUPPORT)
extern void bq24196_set_otg_config(unsigned int val);
extern void bq24196_set_boost_lim(unsigned int val);
#elif defined CONFIG_MTK_NCP1854_SUPPORT
extern void ncp1854_set_chg_en(unsigned int val);
extern void ncp1854_set_otg_en(unsigned int val);
#endif

#ifdef FPGA_PLATFORM
extern void USB_PHY_Write_Register8(UINT8 var, UINT8 addr);
extern UINT8 USB_PHY_Read_Register8(UINT8 addr);
#endif

extern struct clk *musb_clk;
extern void __iomem *ap_uart0_base;
#ifdef CONFIG_MTK_UART_USB_SWITCH
extern bool in_uart_mode;
#endif
extern int usb20_phy_init_debugfs(void);
extern CHARGER_TYPE mt_get_charger_type(void);
#include <upmu_common.h>
bool hwPowerOn(MT65XX_POWER powerId, int powerVolt, char *mode_name);

#endif
