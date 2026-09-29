/* LoRaWAN port for STM32WL on STM32CubeWL's LoRaMAC-node LmHandler.
 *
 * LmHandler is event driven; this file turns it into the blocking calls in
 * lorawan_port.h by running LmHandlerProcess() and sleeping in STOP2 between
 * RX windows until the matching callback fires. */
#include <string.h>
#include "airtime.h"
#include "board.h"
#include "config.h"
#include "hal.h"
#include "lorawan_port.h"
#include "LmHandler.h"
#include "lora_info.h"
#include "stm32_timer.h"
#include "sys_app.h"        /* CubeWL template: GetBatteryLevel, GetUniqueId... */

#ifndef LORAWAN_REGION
#define LORAWAN_REGION LORAMAC_REGION_EU868
#endif

#define JOIN_TIMEOUT_MS  12000u     /* JoinAccept RX1 5 s, RX2 6 s + airtime */
#define SEND_TIMEOUT_MS  8000u      /* RX1 1 s, RX2 2 s + SF12 airtime */

static volatile enum { EV_NONE, EV_OK, EV_FAIL } join_ev, tx_ev;
static volatile uint16_t loaded_mv;
static volatile bool time_synced;
static uint32_t unix_offset;
static UTIL_TIMER_Object_t vbat_timer, timeout_timer;
static volatile bool timed_out;

/* ------------------------------------------------------------ callbacks */
static void on_join_request(LmHandlerJoinParams_t *p)
{
    join_ev = (p->Status == LORAMAC_HANDLER_SUCCESS) ? EV_OK : EV_FAIL;
}

static void on_tx_data(LmHandlerTxParams_t *p)
{
    /* Fires from McpsConfirm, after both RX windows closed. */
    tx_ev = (p != NULL && p->Status == LORAMAC_EVENT_INFO_STATUS_OK) ? EV_OK : EV_FAIL;
}

static void on_rx_data(LmHandlerAppData_t *app, LmHandlerRxParams_t *p)
{
    (void)p;
    /* Downlinks are reserved for interval/flag changes (design doc §10.3).
     * Port 10: [0x01][limit_s_hi][limit_s_lo] could adjust MIN_INTERVAL_S;
     * left as an extension point. */
    (void)app;
}

static void on_sys_time_update(void)
{
    const SysTime_t t = SysTimeGet();          /* LmHandler sets Unix time */
    unix_offset = t.Seconds - hal_rtc_seconds();
    time_synced = true;
}

static void on_mac_process(void) { /* LmHandlerProcess runs in our wait loop */ }

static LmHandlerCallbacks_t callbacks = {
    .GetBatteryLevel   = GetBatteryLevel,
    .GetTemperature    = GetTemperatureLevel,
    .GetUniqueId       = GetUniqueId,
    .GetDevAddr        = GetDevAddr,
    .OnMacProcess      = on_mac_process,
    .OnJoinRequest     = on_join_request,
    .OnTxData          = on_tx_data,
    .OnRxData          = on_rx_data,
    .OnSysTimeUpdate   = on_sys_time_update,
};

static LmHandlerParams_t params = {
    .ActiveRegion           = LORAWAN_REGION,
    .DefaultClass           = CLASS_A,
    .AdrEnable              = true,                              /* §3.3 */
    .IsTxConfirmed          = LORAMAC_HANDLER_UNCONFIRMED_MSG,   /* §10.3 */
    .TxDatarate             = DR_0,
    .PingPeriodicity        = 4,
    .RxBCTimeout            = 0,
};

/* ------------------------------------------------------------ helpers */
static void on_timeout(void *ctx) { (void)ctx; timed_out = true; }
static void on_vbat(void *ctx)    { (void)ctx; loaded_mv = hal_battery_mv(); }

static bool wait_for(volatile int *ev, uint32_t timeout_ms)
{
    timed_out = false;
    UTIL_TIMER_Create(&timeout_timer, timeout_ms, UTIL_TIMER_ONESHOT, on_timeout, NULL);
    UTIL_TIMER_Start(&timeout_timer);
    while (*ev == EV_NONE && !timed_out) {
        LmHandlerProcess();
        if (*ev != EV_NONE || timed_out) break;
        HAL_SuspendTick();
        HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
        extern void SystemClock_Config(void);
        SystemClock_Config();
        HAL_ResumeTick();
    }
    UTIL_TIMER_Stop(&timeout_timer);
    return *ev == EV_OK;
}

static uint8_t dr_to_sf(int8_t dr)
{
    /* EU868: DR0 = SF12 ... DR5 = SF7. (US915 differs: DR0 = SF10.) */
    if (dr < 0) dr = 0;
    if (dr > 5) dr = 5;
    return (uint8_t)(12 - dr);
}

/* ------------------------------------------------------------ port API */
void lw_port_init(void)
{
    LoraInfo_Init();
    LmHandlerInit(&callbacks, APP_VERSION);
    LmHandlerConfigure(&params);
    loaded_mv = 0;
    time_synced = false;
}

bool lw_port_join(void)
{
    join_ev = EV_NONE;
    LmHandlerJoin(ACTIVATION_TYPE_OTAA, true);
    const bool ok = wait_for((volatile int *)&join_ev, JOIN_TIMEOUT_MS);
    if (ok) LmHandlerDeviceTimeReq();    /* piggybacks on the next uplink */
    return ok;
}

bool lw_port_send(uint8_t fport, const uint8_t *buf, uint8_t len)
{
    static uint8_t tx_buf[16];
    if (len > sizeof tx_buf) return false;
    memcpy(tx_buf, buf, len);
    LmHandlerAppData_t app = { .Port = fport, .BufferSize = len, .Buffer = tx_buf };

    /* Sample the pack mid-transmit (§5.4): halfway through the airtime. */
    const uint32_t toa = airtime_ms(lw_port_current_sf(), len);
    UTIL_TIMER_Create(&vbat_timer, toa / 2u + 1u, UTIL_TIMER_ONESHOT, on_vbat, NULL);

    tx_ev = EV_NONE;
    if (LmHandlerSend(&app, LORAMAC_HANDLER_UNCONFIRMED_MSG, false) != LORAMAC_HANDLER_SUCCESS)
        return false;   /* e.g. stack's own duty-cycle restriction: nothing was sent */
    UTIL_TIMER_Start(&vbat_timer);
    return wait_for((volatile int *)&tx_ev, SEND_TIMEOUT_MS);
}

uint8_t lw_port_current_sf(void)
{
    int8_t dr = DR_0;
    LmHandlerGetTxDatarate(&dr);
    return dr_to_sf(dr);
}

uint8_t lw_port_join_sf(void)
{
    /* LoRaMAC-node starts joins at the configured TxDatarate (DR0 = SF12)
     * and may alternate; budgeting at SF12 is the safe assumption. */
    return 12;
}

uint16_t lw_port_last_tx_battery_mv(void) { return loaded_mv; }

bool lw_port_time(uint32_t *offset)
{
    if (!time_synced) return false;
    *offset = unix_offset;
    return true;
}

void lw_port_sleep(void)
{
    /* The SX126x core in the WL goes to sleep via the MAC after RX2 closes;
     * nothing to do unless a transfer is pending. */
}
