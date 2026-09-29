/* STM32WL implementation of hal.h.
 *
 * Built inside a CubeMX/STM32CubeWL "LoRaWAN_End_Node" project, which
 * provides SystemClock_Config(), the RTC in binary mode (rtc_if.c), the
 * UTIL_TIMER server (stm32_timer.c) and the sub-GHz radio. See README.md in
 * this directory for the integration steps. */
#include <string.h>
#include "board.h"
#include "config.h"
#include "hal.h"
#include "rtc_if.h"          /* TIMER_IF_GetTime: RTC seconds with MSB-tick extension */
#include "stm32_timer.h"
#include "stm32wlxx_hal.h"
#include "LmHandler.h"

extern void SystemClock_Config(void);
extern RTC_HandleTypeDef hrtc;

static I2C_HandleTypeDef  hi2c;
static SPI_HandleTypeDef  hspi;
static ADC_HandleTypeDef  hadc;
static IWDG_HandleTypeDef hiwdg;
static bool reset_by_iwdg;

/* ------------------------------------------------------------ clock/RTC */
uint32_t hal_rtc_seconds(void)
{
    uint16_t ms;
    return TIMER_IF_GetTime(&ms);   /* MCU time: unaffected by SysTimeSet() */
}

void hal_delay_ms(uint32_t ms) { HAL_Delay(ms); }

/* ------------------------------------------------------------ sleep */
static volatile bool wake_flag;
static UTIL_TIMER_Object_t wake_timer;
static void on_wake(void *ctx) { (void)ctx; wake_flag = true; }

void hal_enter_stop(uint32_t seconds)
{
    /* The RTC alarm is shared with LoRaMAC's timers, so sleep through the
     * UTIL_TIMER server rather than programming the wake-up timer directly. */
    wake_flag = false;
    UTIL_TIMER_Create(&wake_timer, seconds * 1000u, UTIL_TIMER_ONESHOT, on_wake, NULL);
    UTIL_TIMER_Start(&wake_timer);

    while (!wake_flag) {
        LmHandlerProcess();                         /* service any MAC event first */
        if (wake_flag) break;
        HAL_SuspendTick();
        HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
        /* STOP drops the PLL/HSE: restore the run clock tree. */
        SystemClock_Config();
        HAL_ResumeTick();
    }
    UTIL_TIMER_Stop(&wake_timer);
}

void hal_gpio_config_for_sleep(void)
{
    /* Everything that is not driving something to a defined level becomes
     * analog: no input buffer, no leakage, no oscillating floating input. */
    GPIO_InitTypeDef g = { .Mode = GPIO_MODE_ANALOG, .Pull = GPIO_NOPULL };

    HAL_I2C_DeInit(&hi2c);
    HAL_SPI_DeInit(&hspi);
    g.Pin = BOARD_I2C_SCL_PIN;  HAL_GPIO_Init(BOARD_I2C_SCL_PORT, &g);   /* external pull-ups idle high: no current */
    g.Pin = BOARD_I2C_SDA_PIN;  HAL_GPIO_Init(BOARD_I2C_SDA_PORT, &g);
    g.Pin = BOARD_SPI_SCK_PIN;  HAL_GPIO_Init(BOARD_SPI_SCK_PORT, &g);
    g.Pin = BOARD_SPI_MOSI_PIN; HAL_GPIO_Init(BOARD_SPI_MOSI_PORT, &g);
    g.Pin = BOARD_EINK_DC_PIN;  HAL_GPIO_Init(BOARD_EINK_DC_PORT, &g);
    g.Pin = BOARD_EINK_BUSY_PIN; HAL_GPIO_Init(BOARD_EINK_BUSY_PORT, &g);
    g.Pin = BOARD_VBAT_ADC_PIN; HAL_GPIO_Init(BOARD_VBAT_ADC_PORT, &g);

    /* Driven lines stay driven: CS high (deselected), RST high (the panel is in
     * deep sleep, not held in reset), divider switch off. */
    HAL_GPIO_WritePin(BOARD_EINK_CS_PORT, BOARD_EINK_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BOARD_EINK_RST_PORT, BOARD_EINK_RST_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BOARD_VBAT_EN_PORT, BOARD_VBAT_EN_PIN, GPIO_PIN_RESET);
}

static void i2c_init(void);
static void spi_init(void);

void hal_gpio_config_for_run(void)
{
    i2c_init();
    spi_init();
}

/* ------------------------------------------------------------ backup regs */
uint32_t hal_backup_read(uint8_t idx)
{
    return idx < HAL_BKP_COUNT ? HAL_RTCEx_BKUPRead(&hrtc, BOARD_BKP_FIRST + idx) : 0u;
}

void hal_backup_write(uint8_t idx, uint32_t value)
{
    if (idx < HAL_BKP_COUNT) HAL_RTCEx_BKUPWrite(&hrtc, BOARD_BKP_FIRST + idx, value);
}

/* ------------------------------------------------------------ I2C */
static void i2c_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C2_CLK_ENABLE();
    GPIO_InitTypeDef g = { .Mode = GPIO_MODE_AF_OD, .Pull = GPIO_NOPULL,
                           .Speed = GPIO_SPEED_FREQ_LOW, .Alternate = BOARD_I2C_AF };
    g.Pin = BOARD_I2C_SCL_PIN; HAL_GPIO_Init(BOARD_I2C_SCL_PORT, &g);
    g.Pin = BOARD_I2C_SDA_PIN; HAL_GPIO_Init(BOARD_I2C_SDA_PORT, &g);

    hi2c.Instance = BOARD_I2C;
    hi2c.Init.Timing = BOARD_I2C_TIMING;
    hi2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c);
}

bool hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    return HAL_I2C_Master_Transmit(&hi2c, (uint16_t)(addr << 1), (uint8_t *)data,
                                   (uint16_t)len, 20) == HAL_OK;
}

bool hal_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *out, size_t len)
{
    return HAL_I2C_Mem_Read(&hi2c, (uint16_t)(addr << 1), reg, I2C_MEMADD_SIZE_8BIT,
                            out, (uint16_t)len, 20) == HAL_OK;
}

/* ------------------------------------------------------------ SPI / e-ink */
static void spi_init(void)
{
    __HAL_RCC_SPI1_CLK_ENABLE();
    GPIO_InitTypeDef g = { .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_NOPULL,
                           .Speed = GPIO_SPEED_FREQ_MEDIUM, .Alternate = BOARD_SPI_AF };
    g.Pin = BOARD_SPI_SCK_PIN;  HAL_GPIO_Init(BOARD_SPI_SCK_PORT, &g);
    g.Pin = BOARD_SPI_MOSI_PIN; HAL_GPIO_Init(BOARD_SPI_MOSI_PORT, &g);

    GPIO_InitTypeDef o = { .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW };
    o.Pin = BOARD_EINK_CS_PIN;  HAL_GPIO_Init(BOARD_EINK_CS_PORT, &o);
    o.Pin = BOARD_EINK_DC_PIN;  HAL_GPIO_Init(BOARD_EINK_DC_PORT, &o);
    o.Pin = BOARD_EINK_RST_PIN; HAL_GPIO_Init(BOARD_EINK_RST_PORT, &o);
    o.Pin = BOARD_EINK_PWR_PIN; HAL_GPIO_Init(BOARD_EINK_PWR_PORT, &o);
    o.Pin = BOARD_VBAT_EN_PIN;  HAL_GPIO_Init(BOARD_VBAT_EN_PORT, &o);
    GPIO_InitTypeDef i = { .Mode = GPIO_MODE_INPUT, .Pull = GPIO_NOPULL, .Pin = BOARD_EINK_BUSY_PIN };
    HAL_GPIO_Init(BOARD_EINK_BUSY_PORT, &i);
    HAL_GPIO_WritePin(BOARD_EINK_CS_PORT, BOARD_EINK_CS_PIN, GPIO_PIN_SET);

    hspi.Instance = BOARD_SPI;
    hspi.Init.Mode = SPI_MODE_MASTER;
    hspi.Init.Direction = SPI_DIRECTION_1LINE;
    hspi.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi.Init.NSS = SPI_NSS_SOFT;
    hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;   /* 6 MHz: panel max ~20 MHz */
    hspi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    HAL_SPI_Init(&hspi);
}

void hal_eink_power(bool on)
{
    HAL_GPIO_WritePin(BOARD_EINK_PWR_PORT, BOARD_EINK_PWR_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    if (on) HAL_Delay(2);
}

void hal_eink_reset(void)
{
    HAL_GPIO_WritePin(BOARD_EINK_RST_PORT, BOARD_EINK_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(BOARD_EINK_RST_PORT, BOARD_EINK_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(10);
}

static void spi_tx(bool data, const uint8_t *buf, size_t len)
{
    HAL_GPIO_WritePin(BOARD_EINK_DC_PORT, BOARD_EINK_DC_PIN, data ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BOARD_EINK_CS_PORT, BOARD_EINK_CS_PIN, GPIO_PIN_RESET);
    while (len) {
        const uint16_t chunk = len > 0xFFFFu ? 0xFFFFu : (uint16_t)len;
        HAL_SPI_Transmit(&hspi, (uint8_t *)buf, chunk, 100);
        buf += chunk; len -= chunk;
    }
    HAL_GPIO_WritePin(BOARD_EINK_CS_PORT, BOARD_EINK_CS_PIN, GPIO_PIN_SET);
}

void hal_eink_command(uint8_t cmd)                { spi_tx(false, &cmd, 1); }
void hal_eink_data(const uint8_t *d, size_t len)  { spi_tx(true, d, len); }
bool hal_eink_busy(void)
{
    return HAL_GPIO_ReadPin(BOARD_EINK_BUSY_PORT, BOARD_EINK_BUSY_PIN) == GPIO_PIN_SET;
}

/* ------------------------------------------------------------ battery ADC */
static uint32_t adc_read(uint32_t channel)
{
    ADC_ChannelConfTypeDef c = { .Channel = channel, .Rank = ADC_REGULAR_RANK_1,
                                 .SamplingTime = ADC_SAMPLINGTIME_COMMON_1 };
    HAL_ADC_ConfigChannel(&hadc, &c);
    HAL_ADC_Start(&hadc);
    HAL_ADC_PollForConversion(&hadc, 10);
    const uint32_t v = HAL_ADC_GetValue(&hadc);
    HAL_ADC_Stop(&hadc);
    return v;
}

uint16_t hal_battery_mv(void)
{
    __HAL_RCC_ADC_CLK_ENABLE();
    hadc.Instance = ADC;
    hadc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc.Init.Resolution = ADC_RESOLUTION_12B;
    hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc.Init.LowPowerAutoWait = DISABLE;
    hadc.Init.LowPowerAutoPowerOff = DISABLE;
    hadc.Init.ContinuousConvMode = DISABLE;
    hadc.Init.NbrOfConversion = 1;
    hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_160CYCLES_5;  /* high-Z divider */
    hadc.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_LOW;
    if (HAL_ADC_Init(&hadc) != HAL_OK) return 0;
    HAL_ADCEx_Calibration_Start(&hadc);

    GPIO_InitTypeDef g = { .Mode = GPIO_MODE_ANALOG, .Pull = GPIO_NOPULL, .Pin = BOARD_VBAT_ADC_PIN };
    HAL_GPIO_Init(BOARD_VBAT_ADC_PORT, &g);
    HAL_GPIO_WritePin(BOARD_VBAT_EN_PORT, BOARD_VBAT_EN_PIN, GPIO_PIN_SET);
    HAL_Delay(BOARD_VBAT_SETTLE_MS);

    const uint32_t vref_raw = adc_read(ADC_CHANNEL_VREFINT);
    const uint32_t vdda_mv  = __HAL_ADC_CALC_VREFANALOG_VOLTAGE(vref_raw, ADC_RESOLUTION_12B);
    const uint32_t raw      = adc_read(BOARD_VBAT_ADC_CHANNEL);

    HAL_GPIO_WritePin(BOARD_VBAT_EN_PORT, BOARD_VBAT_EN_PIN, GPIO_PIN_RESET);
    HAL_ADC_DeInit(&hadc);
    __HAL_RCC_ADC_CLK_DISABLE();

    return (uint16_t)(__HAL_ADC_CALC_DATA_TO_VOLTAGE(vdda_mv, raw, ADC_RESOLUTION_12B)
                      * BOARD_VBAT_DIVIDER_NUM / BOARD_VBAT_DIVIDER_DEN);
}

/* ------------------------------------------------------------ watchdog */
static void ensure_iwdg_frozen_in_stop(void)
{
    /* The IWDG cannot be refreshed while in STOP2 and tops out near 32 s,
     * so freeze it during STOP/STANDBY. It still guards every awake path.
     * Programming option bytes triggers a reset; this runs once per device. */
    FLASH_OBProgramInitTypeDef ob = {0};
    HAL_FLASHEx_OBGetConfig(&ob);
    const uint32_t want = OB_IWDG_STOP_FREEZE | OB_IWDG_STDBY_FREEZE | OB_BOR_LEVEL_1;
    if ((ob.UserConfig & (FLASH_OPTR_IWDG_STOP | FLASH_OPTR_IWDG_STDBY | FLASH_OPTR_BOR_LEV)) ==
        (want & (FLASH_OPTR_IWDG_STOP | FLASH_OPTR_IWDG_STDBY | FLASH_OPTR_BOR_LEV)))
        return;
    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();
    ob.OptionType = OPTIONBYTE_USER;
    /* BOR level 1 (~2.0 V): above the MCU minimum, so a sagging pack resets
     * cleanly instead of corrupting flash (§5.3). */
    ob.UserType = OB_USER_IWDG_STOP | OB_USER_IWDG_STDBY | OB_USER_BOR_LEV;
    ob.UserConfig = want;
    HAL_FLASHEx_OBProgram(&ob);
    HAL_FLASH_OB_Launch();                 /* resets the MCU */
}

void hal_watchdog_init(uint32_t timeout_ms)
{
    ensure_iwdg_frozen_in_stop();
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;           /* LSI 32 kHz / 256 = 125 Hz */
    uint32_t reload = timeout_ms / 8u;
    hiwdg.Init.Reload = reload > 0x0FFFu ? 0x0FFFu : reload;
    hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
    HAL_IWDG_Init(&hiwdg);
}

void hal_watchdog_refresh(void) { HAL_IWDG_Refresh(&hiwdg); }
bool hal_reset_was_watchdog(void) { return reset_by_iwdg; }

/* ------------------------------------------------------------ system */
void hal_system_init(void)
{
    HAL_Init();
    reset_by_iwdg = __HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    SystemClock_Config();

    /* Unused pins -> analog before anything else is configured. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef g = { .Mode = GPIO_MODE_ANALOG, .Pull = GPIO_NOPULL, .Pin = GPIO_PIN_All };
    g.Pin = GPIO_PIN_All & ~(GPIO_PIN_13 | GPIO_PIN_14);   /* keep SWD on PA13/PA14 */
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_All;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_Init(GPIOC, &g);

    /* RTC, UTIL_TIMER and the radio come from the CubeWL template. */
    extern void MX_RTC_Init(void);
    MX_RTC_Init();
    UTIL_TIMER_Init();

    i2c_init();
    spi_init();
}
