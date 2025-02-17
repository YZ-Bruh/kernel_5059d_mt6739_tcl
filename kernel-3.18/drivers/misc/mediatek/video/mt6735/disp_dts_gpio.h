
#ifndef __DISP_DTS_GPIO_H__
#define __DISP_DTS_GPIO_H__


#include <linux/platform_device.h> /* struct platform_device */

/* DTS state */
typedef enum tagDTS_GPIO_STATE {
	DTS_GPIO_STATE_TE_MODE_GPIO = 0,    /* mode_te_gpio */
	DTS_GPIO_STATE_TE_MODE_TE,          /* mode_te_te */
	DTS_GPIO_STATE_PWM_TEST_PINMUX_55,  /* pwm_test_pin_mux_gpio55 */
	DTS_GPIO_STATE_PWM_TEST_PINMUX_69,  /* pwm_test_pin_mux_gpio69 */
	DTS_GPIO_STATE_PWM_TEST_PINMUX_129, /* pwm_test_pin_mux_gpio129 */
	DTS_GPIO_STATE_LCD_BIAS_ENN,
	DTS_GPIO_STATE_LCD_BIAS_ENP,

	DTS_GPIO_STATE_MAX,                 /* for array size */
} DTS_GPIO_STATE;

long    disp_dts_gpio_init(struct platform_device *pdev);

long    disp_dts_gpio_select_state(DTS_GPIO_STATE s);

long lcm_turn_on_gate_by_name(bool bOn, char *pinName);

/* repo of initialization */
#ifdef CONFIG_MTK_LEGACY
#define disp_dts_gpio_init_repo(x)  (0)
#else
#define disp_dts_gpio_init_repo(x)  (disp_dts_gpio_init(x))
#endif

#endif/*__DISP_DTS_GPIO_H__ */
