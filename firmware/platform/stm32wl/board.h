#ifndef BOARD_H
#define BOARD_H
/* Pin map for the NUCLEO-WL55JC1 (RF switch on PC3/PC4/PC5, LEDs on PB9/
 * PB11/PB15 - all avoided). Module boards differ: the Seeed LoRa-E5, for
 * example, drives its RF switch from PA4/PA5, which this map uses for SPI.
 * EDIT THIS to match your wiring and verify each pin's
 * alternate function against your module's datasheet; nothing else in the
 * firmware references pins. */
#include "stm32wlxx_hal.h"

/* BME280 on I2C2 */
#define BOARD_I2C                 I2C2
#define BOARD_I2C_SCL_PORT        GPIOA
#define BOARD_I2C_SCL_PIN         GPIO_PIN_12
#define BOARD_I2C_SDA_PORT        GPIOA
#define BOARD_I2C_SDA_PIN         GPIO_PIN_11
#define BOARD_I2C_AF              GPIO_AF4_I2C2
/* 100 kHz from 48 MHz PCLK (CubeMX timing value) */
#define BOARD_I2C_TIMING          0x20303E5Du

/* E-ink on SPI1 (write-only, 4-wire) */
#define BOARD_SPI                 SPI1
#define BOARD_SPI_SCK_PORT        GPIOA
#define BOARD_SPI_SCK_PIN         GPIO_PIN_5
#define BOARD_SPI_MOSI_PORT       GPIOA
#define BOARD_SPI_MOSI_PIN        GPIO_PIN_7
#define BOARD_SPI_AF              GPIO_AF5_SPI1
#define BOARD_EINK_CS_PORT        GPIOA
#define BOARD_EINK_CS_PIN         GPIO_PIN_4
#define BOARD_EINK_DC_PORT        GPIOB
#define BOARD_EINK_DC_PIN         GPIO_PIN_3
#define BOARD_EINK_RST_PORT       GPIOB
#define BOARD_EINK_RST_PIN        GPIO_PIN_4
#define BOARD_EINK_BUSY_PORT      GPIOB
#define BOARD_EINK_BUSY_PIN       GPIO_PIN_5
/* High-side load switch for the panel supply (optional; tie high if absent) */
#define BOARD_EINK_PWR_PORT       GPIOA
#define BOARD_EINK_PWR_PIN        GPIO_PIN_9

/* Battery sense: pack -> 1M/1M divider -> ADC, low side switched by an
 * N-FET so the divider draws nothing in sleep (a permanent 2M divider across
 * 4.5 V would be 2.25 uA - half the sleep budget). */
#define BOARD_VBAT_ADC_CHANNEL    ADC_CHANNEL_4        /* PB2 = ADC_IN4 */
#define BOARD_VBAT_ADC_PORT       GPIOB
#define BOARD_VBAT_ADC_PIN        GPIO_PIN_2
#define BOARD_VBAT_EN_PORT        GPIOA
#define BOARD_VBAT_EN_PIN         GPIO_PIN_10
#define BOARD_VBAT_DIVIDER_NUM    2u                   /* Vpack = Vadc x 2 */
#define BOARD_VBAT_DIVIDER_DEN    1u
#define BOARD_VBAT_SETTLE_MS      2u                   /* RC settle with 100 nF */

/* Backup registers DR0..DR2 belong to CubeWL's rtc_if (SysTime + MSB ticks). */
#define BOARD_BKP_FIRST           RTC_BKP_DR8

#endif
