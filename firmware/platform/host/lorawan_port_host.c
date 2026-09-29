/* Host LoRaWAN port: a scriptable stand-in for LoRaMAC-node. */
#include <string.h>
#include "airtime.h"
#include "lorawan.h"
#include "lorawan_port.h"
#include "sim.h"

#define TX_MA   45.0
#define RX_UAH  0.5    /* RX1 + RX2 windows + MCU active */

static uint16_t loaded_mv;

static double tx_uah(uint8_t sf, uint8_t app_bytes)
{
    return airtime_ms(sf, app_bytes) / 1000.0 * TX_MA * 1000.0 / 3600.0 + RX_UAH;
}

void lw_port_init(void) { loaded_mv = 0; }

bool lw_port_join(void)
{
    sim.joins_attempted++;
    sim.uah_join += tx_uah(sim.join_sf, JOIN_REQUEST_APP_EQUIV);
    if (sim.join_failures_remaining == UINT32_MAX) return false;
    if (sim.join_failures_remaining > 0) { sim.join_failures_remaining--; return false; }
    return true;
}

bool lw_port_send(uint8_t fport, const uint8_t *buf, uint8_t len)
{
    (void)fport;
    if (!sim.send_ok) return false;
    sim.uplinks++;
    sim.uah_uplink += tx_uah(sim.sf, len);
    memcpy(sim.last_payload, buf, len);
    sim.last_payload_len = len;
    /* Pack sags under the 45 mA pulse; Li-FeS2 ~0.15 ohm x3 */
    loaded_mv = sim.battery_mv > 20 ? (uint16_t)(sim.battery_mv - 20) : 0;
    return true;
}

uint8_t  lw_port_current_sf(void)          { return sim.sf; }
uint8_t  lw_port_join_sf(void)             { return sim.join_sf; }
uint16_t lw_port_last_tx_battery_mv(void)  { return loaded_mv; }
void     lw_port_sleep(void)               {}

bool lw_port_time(uint32_t *offset)
{
    if (!sim.time_valid) return false;
    *offset = sim.unix_offset;
    return true;
}
