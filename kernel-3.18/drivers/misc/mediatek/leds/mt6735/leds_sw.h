

#ifndef _LEDS_SW_H
#define _LEDS_SW_H


enum mt65xx_led_type {
	MT65XX_LED_TYPE_RED = 0,
	MT65XX_LED_TYPE_GREEN,
	MT65XX_LED_TYPE_BLUE,
	MT65XX_LED_TYPE_JOGBALL,
	MT65XX_LED_TYPE_KEYBOARD,
	MT65XX_LED_TYPE_BUTTON,
	MT65XX_LED_TYPE_LCD,
	MT65XX_LED_TYPE_TOTAL,
};

enum mt65xx_led_mode {
	MT65XX_LED_MODE_NONE,
	MT65XX_LED_MODE_PWM,
	MT65XX_LED_MODE_GPIO,
	MT65XX_LED_MODE_PMIC,
	MT65XX_LED_MODE_CUST_LCM,
	MT65XX_LED_MODE_CUST_BLS_PWM
};


/* backlight call back function */
typedef int (*cust_brightness_set) (int level, int div);
typedef int (*cust_set_brightness) (int level);

/* 10bit backlight level */
#define LED_INCREASE_LED_LEVEL_MTKPATCH
#ifdef LED_INCREASE_LED_LEVEL_MTKPATCH
#define MT_LED_INTERNAL_LEVEL_BIT_CNT 10
#endif


enum mt65xx_led_pmic {
	MT65XX_LED_PMIC_LCD_ISINK = 0,
	MT65XX_LED_PMIC_NLED_ISINK0,
	MT65XX_LED_PMIC_NLED_ISINK1,
	MT65XX_LED_PMIC_NLED_ISINK2,
	MT65XX_LED_PMIC_NLED_ISINK3
};

enum MT65XX_PMIC_ISINK_MODE {
	ISINK_PWM_MODE = 0,
	ISINK_BREATH_MODE = 1,
	ISINK_REGISTER_MODE = 2
};

enum MT65XX_PMIC_ISINK_STEP {
	ISINK_0 = 0,		/* 4mA */
	ISINK_1 = 1,		/* 8mA */
	ISINK_2 = 2,		/* 12mA */
	ISINK_3 = 3,		/* 16mA */
	ISINK_4 = 4,		/* 20mA */
	ISINK_5 = 5		/* 24mA */
};

enum MT65XX_PMIC_ISINK_FSEL {
	/* 32K clock */
	ISINK_1KHZ = 0,
	ISINK_200HZ = 4,
	ISINK_5HZ = 199,
	ISINK_2HZ = 499,
	ISINK_1HZ = 999,
	ISINK_05HZ = 1999,
	ISINK_02HZ = 4999,
	ISINK_01HZ = 9999,
	/* 2M clock */
	ISINK_2M_20KHZ = 2,
	ISINK_2M_1KHZ = 61,
	ISINK_2M_200HZ = 311,
	ISINK_2M_5HZ = 12499,
	ISINK_2M_2HZ = 31249,
	ISINK_2M_1HZ = 62499
};


#define MIN_FRE_OLD_PWM 32	/* the min frequence when use old mode pwm by kHz */
#define BACKLIGHT_LEVEL_PWM_64_FIFO_MODE_SUPPORT 64
#define BACKLIGHT_LEVEL_PWM_256_SUPPORT 256
#define BACKLIGHT_LEVEL_PWM_MODE_CONFIG BACKLIGHT_LEVEL_PWM_256_SUPPORT
static inline unsigned int Cust_GetBacklightLevelSupport_byPWM(void)
{
	return BACKLIGHT_LEVEL_PWM_MODE_CONFIG;
}

static inline unsigned int brightness_mapping(unsigned int level)
{
	unsigned int mapped_level;

	mapped_level = level;
	return mapped_level;
}

struct PWM_config {
	int clock_source;
	int div;
	int low_duration;
	int High_duration;
	bool pmic_pad;
};


struct cust_mt65xx_led {
	char *name;
	enum mt65xx_led_mode mode;
	long data;
	struct PWM_config config_data;
};

struct mt65xx_led_data {
	struct led_classdev cdev;
	struct cust_mt65xx_led cust;
	struct work_struct work;
	int level;
	int delay_on;
	int delay_off;
};

#define NLED_OFF 0
#define NLED_ON 1
#define NLED_BLINK 2

struct nled_setting {
	u8 nled_mode;
	u32 blink_on_time;
	u32 blink_off_time;
};

#endif				/* _LEDS_SW_H */
