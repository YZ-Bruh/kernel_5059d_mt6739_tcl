
#ifndef __MT_RTC_HW_H__
#define __MT_RTC_HW_H__

/* RTC registers */
#define MT_PMIC_REG_BASE (0x0000)
#define RTC_BBPU            (RTC_BASE + 0x0000)
#define RTC_BBPU_PWREN            (1U << 0)    /* BBPU = 1 when alarm occurs */
#define RTC_BBPU_BBPU            (1U << 2)    /* 1: power on, 0: power down */
#define RTC_BBPU_AUTO            (1U << 3)    /* BBPU = 0 when xreset_rstb goes low */
#define RTC_BBPU_CLRPKY        (1U << 4)
#define RTC_BBPU_RELOAD        (1U << 5)
#define RTC_BBPU_CBUSY            (1U << 6)
#define RTC_BBPU_KEY            (0x43 << 8)

#define RTC_IRQ_STA            (RTC_BASE + 0x0002)
#define RTC_IRQ_STA_AL            (1U << 0)
#define RTC_IRQ_STA_TC            (1U << 1)
#define RTC_IRQ_STA_LP            (1U << 3)

#define RTC_IRQ_EN            (RTC_BASE + 0x0004)
#define RTC_IRQ_EN_AL            (1U << 0)
#define RTC_IRQ_EN_TC            (1U << 1)
#define RTC_IRQ_EN_ONESHOT        (1U << 2)
#define RTC_IRQ_EN_LP            (1U << 3)
#define RTC_IRQ_EN_ONESHOT_AL        (RTC_IRQ_EN_ONESHOT | RTC_IRQ_EN_AL)

#define RTC_CII_EN            (RTC_BASE + 0x0006)
#define RTC_CII_SEC             (1U << 0)
#define RTC_CII_MIN             (1U << 1)
#define RTC_CII_HOU             (1U << 2)
#define RTC_CII_DOM             (1U << 3)
#define RTC_CII_DOW             (1U << 4)
#define RTC_CII_MTH             (1U << 5)
#define RTC_CII_YEA             (1U << 6)
#define RTC_CII_1_2_SEC            (1U << 7)
#define RTC_CII_1_4_SEC            (1U << 8)
#define RTC_CII_1_8_SEC            (1U << 9)

#define RTC_AL_MASK            (RTC_BASE + 0x0008)
#define RTC_AL_MASK_SEC        (1U << 0)
#define RTC_AL_MASK_MIN        (1U << 1)
#define RTC_AL_MASK_HOU        (1U << 2)
#define RTC_AL_MASK_DOM        (1U << 3)
#define RTC_AL_MASK_DOW        (1U << 4)
#define RTC_AL_MASK_MTH        (1U << 5)
#define RTC_AL_MASK_YEA        (1U << 6)

#define RTC_TC_SEC            (RTC_BASE + 0x000a)
#define RTC_TC_SEC_MASK 0x003f

#define RTC_TC_MIN            (RTC_BASE + 0x000c)
#define RTC_TC_MIN_MASK 0x003f

#define RTC_TC_HOU            (RTC_BASE + 0x000e)
#define RTC_TC_HOU_MASK 0x001f

#define RTC_TC_DOM            (RTC_BASE + 0x0010)
#define RTC_TC_DOM_MASK 0x001f

#define RTC_TC_DOW            (RTC_BASE + 0x0012)
#define RTC_TC_DOW_MASK 0x0007

#define RTC_TC_MTH            (RTC_BASE + 0x0014)
#define RTC_TC_MTH_MASK 0x000f

#define RTC_TC_YEA            (RTC_BASE + 0x0016)
#define RTC_TC_YEA_MASK 0x007f

#define RTC_AL_SEC            (RTC_BASE + 0x0018)
#define RTC_K_EOSC32_VTCXO_ON_SEL    (1U << 15)
#define RTC_LPD_OPT_SHIFT        13
#define RTC_LPD_OPT            (1U << RTC_LPD_OPT_SHIFT)
#define RTC_BBPU_2SEC_STAT_CLEAR    (1U << 11)
#define RTC_BBPU_2SEC_MODE        (3U << 9)
#define RTC_BBPU_2SEC_EN        (1U << 8)
#define RTC_BBPU_2SEC_CK_SEL        (1U << 7)
#define RTC_BBPU_AUTO_PDN_SEL		(1U << 6)
#define RTC_AL_SEC_MASK 0x003f

#define RTC_AL_MIN            (RTC_BASE + 0x001a)
#define RTC_AL_MIN_MASK 0x003f

#define RTC_AL_HOU            (RTC_BASE + 0x001c)
#define RTC_AL_HOU_MASK         0x001f

#define RTC_FG_SOC            RTC_AL_HOU
#define RTC_FG_SOC_MASK            0x7f
#define RTC_FG_SOC_SHIFT        8


#define RTC_AL_DOM            (RTC_BASE + 0x001e)
#define RTC_NEW_SPARE1    0xff00
#define RTC_AL_DOM_MASK 0x001f

#define RTC_AL_DOW            (RTC_BASE + 0x0020)
#define RTC_NEW_SPARE2    0xff00
#define RTC_AL_DOW_MASK 0x0007

#define RTC_AL_MTH            (RTC_BASE + 0x0022)
#define RTC_NEW_SPARE3    0xff00
#define RTC_AL_MTH_MASK 0x000f

#define RTC_AL_YEA            (RTC_BASE + 0x0024)
#define RTC_K_EOSC_RSV_7        (1U << 15)
#define RTC_K_EOSC_RSV_6        (1U << 14)
#define RTC_K_EOSC_RSV_5        (1U << 13)
#define RTC_K_EOSC_RSV_4        (1U << 12)
#define RTC_K_EOSC_RSV_3        (1U << 11)
#define RTC_K_EOSC_RSV_2        (1U << 10)
#define RTC_K_EOSC_RSV_1        (1U << 9)
#define RTC_K_EOSC_RSV_0        (1U << 8)
#define RTC_AL_YEA_MASK 0x007f

#define RTC_OSC32CON            (RTC_BASE + 0x0026)
#define RTC_OSC32CON_UNLOCK1        0x1a57
#define RTC_OSC32CON_UNLOCK2        0x2b68
#define RTC_REG_XOSC32_ENB        (1U << 15)
#define RTC_GP_OSC32_CON        (3U << 13)
#define RTC_EOSC32_CHOP_EN         (1U << 12)
#define RTC_EOSC32_VCT_EN_SHIFT    11
#define RTC_EOSC32_VCT_EN        (1U << RTC_EOSC32_VCT_EN_SHIFT)
#define RTC_EOSC32_OPT          (1U << 10)
#define RTC_EMBCK_SEL_OPTION        (1U << 9)
#define RTC_EMBCK_SRC_SEL        (1U << 8)
#define RTC_EMBCK_SEL_MODE        (3U << 6)
/* 0: emb_hw    1: emb_k_eosc_32        2:dcxo_ck    3: eosc32_ck*/
#define RTC_EMBCK_SEL_HW         (0 << 6)
#define RTC_EMBCK_SEL_K_EOSC         (1U << 6)
#define RTC_EMBCK_SEL_DCXO         (2U << 6)
#define RTC_EMBCK_SEL_EOSC         (3U << 6)
/* 0 (32k crystal exist)    1 (32k crystal doesn't exist)*/
#define RTC_XOSC32_ENB           (1U << 5)
/*Default 4'b0111, 2nd step suggest to set to 4'b0000 EOSC_CALI = charging cap calibration*/
#define RTC_XOSCCALI_MASK        0x001f

#define OSC32CON_ANALOG_SETTING    (RTC_GP_OSC32_CON | RTC_EOSC32_VCT_EN | RTC_EOSC32_CHOP_EN \
	& (~RTC_REG_XOSC32_ENB) & (~RTC_EMBCK_SEL_MODE) | RTC_EMBCK_SEL_OPTION | RTC_EMBCK_SRC_SEL)

#define RTC_XOSCCALI_START        0x0000
#define RTC_XOSCCALI_END        0x001f

#define RTC_POWERKEY1            (RTC_BASE + 0x0028)
#define RTC_POWERKEY2            (RTC_BASE + 0x002a)
#define RTC_POWERKEY1_KEY        0xa357
#define RTC_POWERKEY2_KEY        0x67d2
#define RTC_PDN1            (RTC_BASE + 0x002c)
#define RTC_PDN1_ANDROID_MASK        0x000f
#define RTC_PDN1_RECOVERY_MASK        0x0030
#define RTC_PDN1_FAC_RESET        (1U << 4)
#define RTC_PDN1_BYPASS_PWR        (1U << 6)
#define RTC_PDN1_PWRON_TIME        (1U << 7)
#define RTC_PDN1_GPIO_WIFI        (1U << 8)
#define RTC_PDN1_GPIO_GPS        (1U << 9)
#define RTC_PDN1_GPIO_BT        (1U << 10)
#define RTC_PDN1_GPIO_FM        (1U << 11)
#define RTC_PDN1_GPIO_PMIC        (1U << 12)
#define RTC_PDN1_FAST_BOOT        (1U << 13)
#define RTC_PDN1_KPOC            (1U << 14)
#define RTC_PDN1_DEBUG            (1U << 15)

#define RTC_PDN2                (RTC_BASE + 0x002e)
#define RTC_PDN2_PWRON_ALARM             (1U << 4)
#define RTC_PDN2_UART_MASK            0x0060
#define RTC_PDN2_UART_SHIFT            5
#define RTC_AUTOBOOT_MASK            0x1
#define RTC_AUTOBOOT_SHIFT            7
#define RTC_PDN2_PWRON_LOGO            (1U << 15)

#define RTC_SPAR0                (RTC_BASE + 0x0030)
#define RTC_SPAR0_32K_LESS             (1U << 6)
#define RTC_SPAR0_LP_DET            (1U << 7)

#define RTC_SPAR1            (RTC_BASE + 0x0032)

#define RTC_PROT            (RTC_BASE + 0x0036)
#define RTC_PROT_UNLOCK1         0x586a
#define RTC_PROT_UNLOCK2         0x9136

#define RTC_DIFF            (RTC_BASE + 0x0038)
#define RTC_CALI_RD_SEL_SHIFT        15
#define RTC_CALI_RD_SEL         (1U << RTC_CALI_RD_SEL_SHIFT)
#define RTC_K_EOSC32_RSV        (1U << 14)
#define RTC_DIFF_MASK            0xFFF
#define RTC_DIFF_SHIFT            0

#define RTC_CALI            (RTC_BASE + 0x003a)
#define RTC_K_EOSC32_OVERFLOW        (1U << 15)
#define RTC_CALI_WR_SEL_SHIFT        14
#define RTC_CALI_WR_SEL_MASK        1
#define RTC_CALI_XOSC            0
#define RTC_CALI_K_EOSC_32        1
#define RTC_CALI_MASK            0x3FFF
#define RTC_CALI_SHIFT            0

#define RTC_WRTGR            (RTC_BASE + 0x003c)

#define RTC_CON                (RTC_BASE + 0x003e)
#define RTC_VBAT_LPSTA_RAW        (1U << 0)
#define RTC_EOSC32_LPEN            (1U << 1)
#define RTC_XOSC32_LPEN            (1U << 2)
#define RTC_CON_LPEN            (RTC_XOSC32_LPEN | RTC_EOSC32_LPEN)
#define RTC_CON_LPRST            (1U << 3)
#define RTC_CON_CDBO            (1U << 4)
#define RTC_CON_F32KOB            (1U << 5)    /* 0: RTC_GPIO exports 32K */
#define RTC_CON_GPO            (1U << 6)
#define RTC_CON_GOE            (1U << 7)    /* 1: GPO mode, 0: GPI mode */
#define RTC_CON_GSR            (1U << 8)
#define RTC_CON_GSMT            (1U << 9)
#define RTC_CON_GPEN            (1U << 10)
#define RTC_CON_GPU            (1U << 11)
#define RTC_CON_GE4            (1U << 12)
#define RTC_CON_GE8            (1U << 13)
#define RTC_CON_GPI            (1U << 14)
#define RTC_CON_LPSTA_RAW        (1U << 15)    /* 32K was stopped */

/* power on alarm time setting */

#define RTC_PWRON_YEA        RTC_PDN2
#define RTC_PWRON_YEA_MASK     0x7f00
#define RTC_PWRON_YEA_SHIFT     8

#define RTC_PWRON_MTH        RTC_PDN2
#define RTC_PWRON_MTH_MASK     0x000f
#define RTC_PWRON_MTH_SHIFT     0

#define RTC_PWRON_SEC        RTC_SPAR0
#define RTC_PWRON_SEC_MASK     0x003f
#define RTC_PWRON_SEC_SHIFT     0

#define RTC_PWRON_MIN        RTC_SPAR1
#define RTC_PWRON_MIN_MASK     0x003f
#define RTC_PWRON_MIN_SHIFT     0

#define RTC_PWRON_HOU        RTC_SPAR1
#define RTC_PWRON_HOU_MASK     0x07c0
#define RTC_PWRON_HOU_SHIFT     6

#define RTC_PWRON_DOM        RTC_SPAR1
#define RTC_PWRON_DOM_MASK     0xf800
#define RTC_PWRON_DOM_SHIFT     11

extern u16 rtc_spare_reg[][3];

#define MT_VRTC_PWM_CON0                     ((MT_PMIC_REG_BASE+0x0FAE))
#define MT_PMIC_VRTC_PWM_MODE_ADDR                        MT_VRTC_PWM_CON0
#define MT_PMIC_VRTC_PWM_MODE_MASK                        0x1
#define MT_PMIC_VRTC_PWM_MODE_SHIFT                       0
#define MT_PMIC_VRTC_PWM_RSV_ADDR                         MT_VRTC_PWM_CON0
#define MT_PMIC_VRTC_PWM_RSV_MASK                         0x7
#define MT_PMIC_VRTC_PWM_RSV_SHIFT                        1
#define MT_PMIC_VRTC_PWM_L_DUTY_ADDR                      MT_VRTC_PWM_CON0
#define MT_PMIC_VRTC_PWM_L_DUTY_MASK                      0xF
#define MT_PMIC_VRTC_PWM_L_DUTY_SHIFT                     4
#define MT_PMIC_VRTC_PWM_H_DUTY_ADDR                      MT_VRTC_PWM_CON0
#define MT_PMIC_VRTC_PWM_H_DUTY_MASK                      0xF
#define MT_PMIC_VRTC_PWM_H_DUTY_SHIFT                     8
#define MT_PMIC_VRTC_CAP_SEL_ADDR                         MT_VRTC_PWM_CON0
#define MT_PMIC_VRTC_CAP_SEL_MASK                         0x1
#define MT_PMIC_VRTC_CAP_SEL_SHIFT                        12
#define VTRC_CAP_SEL                                     (1 << MT_PMIC_VRTC_CAP_SEL_SHIFT)
#define VRTC_PWM_H_DUTY_12_8_MS                             (0 << MT_PMIC_VRTC_PWM_H_DUTY_SHIFT)
#define VRTC_PWM_H_DUTY_25_6_MS                             (1 << MT_PMIC_VRTC_PWM_H_DUTY_SHIFT)
#define VRTC_PWM_H_DUTY_51_2_MS                             (3 << MT_PMIC_VRTC_PWM_H_DUTY_SHIFT)
#define VRTC_PWM_H_DUTY_102_4_MS                         (7 << MT_PMIC_VRTC_PWM_H_DUTY_SHIFT)
#define VRTC_PWM_H_DUTY_204_8_MS                         (15 << MT_PMIC_VRTC_PWM_H_DUTY_SHIFT)
#define VRTC_PWM_H_DUTY_0_64_MS                             (0 << MT_PMIC_VRTC_PWM_H_DUTY_SHIFT)
#define VRTC_PWM_H_DUTY_1_28_MS                             (1 << MT_PMIC_VRTC_PWM_H_DUTY_SHIFT)
#define VRTC_PWM_H_DUTY_2_56_MS                             (3 << MT_PMIC_VRTC_PWM_H_DUTY_SHIFT)
#define VRTC_PWM_H_DUTY_5_12_MS                             (7 << MT_PMIC_VRTC_PWM_H_DUTY_SHIFT)
#define VRTC_PWM_H_DUTY_10_24_MS                         (15 << MT_PMIC_VRTC_PWM_H_DUTY_SHIFT)
#define VRTC_PWM_L_DUTY_128_0_MS                         (0 << MT_PMIC_VRTC_PWM_L_DUTY_SHIFT)
#define VRTC_PWM_L_DUTY_256_0_MS                         (1 << MT_PMIC_VRTC_PWM_L_DUTY_SHIFT)
#define VRTC_PWM_L_DUTY_512_0_MS                         (3 << MT_PMIC_VRTC_PWM_L_DUTY_SHIFT)
#define VRTC_PWM_L_DUTY_1024_0_MS                         (7 << MT_PMIC_VRTC_PWM_L_DUTY_SHIFT)
#define VRTC_PWM_L_DUTY_2148_0_MS                         (15 << MT_PMIC_VRTC_PWM_L_DUTY_SHIFT)
#define VRTC_PWM_L_DUTY_6_4_MS                             (0 << MT_PMIC_VRTC_PWM_L_DUTY_SHIFT)
#define VRTC_PWM_L_DUTY_12_8_MS                             (1 << MT_PMIC_VRTC_PWM_L_DUTY_SHIFT)
#define VRTC_PWM_L_DUTY_25_6_MS                             (3 << MT_PMIC_VRTC_PWM_L_DUTY_SHIFT)
#define VRTC_PWM_L_DUTY_51_2_MS                             (7 << MT_PMIC_VRTC_PWM_L_DUTY_SHIFT)
#define VRTC_PWM_L_DUTY_102_4_MS                         (15 << MT_PMIC_VRTC_PWM_L_DUTY_SHIFT)
#define VRTC_PWM_MODE                                     (1 << MT_PMIC_VRTC_PWM_MODE_SHIFT)


#endif /* __MT_RTC_HW_H__ */

