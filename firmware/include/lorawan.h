#ifndef LORAWAN_H
#define LORAWAN_H
/* Join management with bounded, exponential backoff (design doc §10.2) and
 * unconfirmed uplinks only (§10.3). */
#include <stdbool.h>
#include <stdint.h>
#include "airtime.h"

/* JoinRequest PHY payload is 23 bytes = 10 "application" bytes + 13 overhead
 * in airtime_ms() terms. Joins are uplinks and count against fair use. */
#define JOIN_REQUEST_APP_EQUIV 10u

typedef enum {
    LW_JOINED,
    LW_JOIN_WAITING,     /* between attempts, backing off */
    LW_JOIN_DORMANT,     /* gave up for now; retrying a few times a day */
} lw_state_t;

void       lorawan_init(uint32_t now_s);
lw_state_t lorawan_state(void);
bool       lorawan_is_joined(void);

/* Attempts a join if one is due and the airtime budget allows it. Charges the
 * join's airtime to `budget`. Returns the resulting state. */
lw_state_t lorawan_service(uint32_t now_s, airtime_budget_t *budget);

/* Seconds until the next join attempt is due (0 when joined). */
uint32_t   lorawan_seconds_until_join(uint32_t now_s);

bool       lorawan_send_unconfirmed(const uint8_t *buf, uint8_t len);
uint8_t    lorawan_current_sf(void);

/* Network lost the session (e.g. repeated send failures) -> rejoin. */
void       lorawan_session_lost(uint32_t now_s);

/* Test visibility */
uint32_t   lorawan_join_attempts(void);
uint32_t   lorawan_backoff_s(void);

#endif
