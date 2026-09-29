#ifndef LORAWAN_PORT_H
#define LORAWAN_PORT_H
/* The thin seam between the application and a LoRaWAN stack. The STM32WL
 * implementation wraps LoRaMAC-node's LmHandler; the host implementation is a
 * scriptable simulator. All calls are blocking from the caller's view: the
 * port sleeps the MCU between RX windows internally. */
#include <stdbool.h>
#include <stdint.h>

void    lw_port_init(void);

/* One OTAA join attempt (JoinRequest + JoinAccept windows). */
bool    lw_port_join(void);

/* Unconfirmed uplink. Returns true once TX and both RX windows completed. */
bool    lw_port_send(uint8_t fport, const uint8_t *buf, uint8_t len);

/* Spreading factor the stack will use for the next uplink (ADR-controlled). */
uint8_t lw_port_current_sf(void);

/* SF the stack uses for join requests (LoRaMAC-node starts joins at DR0). */
uint8_t lw_port_join_sf(void);

/* Battery voltage sampled by the port during the last transmit, 0 if none. */
uint16_t lw_port_last_tx_battery_mv(void);

/* Network time from DeviceTimeAns: unix = rtc_seconds + *offset. */
bool    lw_port_time(uint32_t *offset);

void    lw_port_sleep(void);

#endif
