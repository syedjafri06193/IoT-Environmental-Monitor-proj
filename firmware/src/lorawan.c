#include "lorawan.h"
#include "config.h"
#include "lorawan_port.h"

static struct {
    lw_state_t state;
    uint32_t   attempts;
    uint32_t   backoff_s;
    uint32_t   next_attempt_s;
} lw;

void lorawan_init(uint32_t now_s)
{
    lw_port_init();
    lw.state = LW_JOIN_WAITING;
    lw.attempts = 0;
    lw.backoff_s = JOIN_BACKOFF_START_S;
    lw.next_attempt_s = now_s;          /* first attempt immediately */
}

lw_state_t lorawan_state(void)     { return lw.state; }
bool lorawan_is_joined(void)       { return lw.state == LW_JOINED; }
uint32_t lorawan_join_attempts(void) { return lw.attempts; }
uint32_t lorawan_backoff_s(void)   { return lw.backoff_s; }
uint8_t lorawan_current_sf(void)   { return lw_port_current_sf(); }

static void on_join_failed(uint32_t now_s)
{
    lw.attempts++;
    if (lw.attempts >= MAX_JOIN_ATTEMPTS) {
        /* Never retry tightly from a coverage hole: transmitting is the most
         * expensive thing this device does. */
        lw.state = LW_JOIN_DORMANT;
        lw.next_attempt_s = now_s + DORMANT_RETRY_S;
        return;
    }
    lw.state = LW_JOIN_WAITING;
    lw.next_attempt_s = now_s + lw.backoff_s;
    lw.backoff_s = lw.backoff_s * 2u > JOIN_BACKOFF_MAX_S ? JOIN_BACKOFF_MAX_S : lw.backoff_s * 2u;
}

lw_state_t lorawan_service(uint32_t now_s, airtime_budget_t *budget)
{
    if (lw.state == LW_JOINED || now_s < lw.next_attempt_s) return lw.state;

    const uint8_t sf = lw_port_join_sf();
    if (!airtime_may_transmit(budget, now_s, sf, JOIN_REQUEST_APP_EQUIV)) {
        lw.next_attempt_s = now_s + airtime_seconds_until_reset(budget, now_s);
        return lw.state;
    }

    const bool ok = lw_port_join();
    airtime_record(budget, now_s, sf, JOIN_REQUEST_APP_EQUIV);

    if (ok) {
        lw.state = LW_JOINED;
        lw.attempts = 0;
        lw.backoff_s = JOIN_BACKOFF_START_S;
    } else {
        on_join_failed(now_s);
    }
    return lw.state;
}

uint32_t lorawan_seconds_until_join(uint32_t now_s)
{
    if (lw.state == LW_JOINED) return 0;
    return lw.next_attempt_s > now_s ? lw.next_attempt_s - now_s : 0;
}

bool lorawan_send_unconfirmed(const uint8_t *buf, uint8_t len)
{
    if (lw.state != LW_JOINED) return false;
    return lw_port_send(LORAWAN_APP_PORT, buf, len);
}

void lorawan_session_lost(uint32_t now_s)
{
    lw.state = LW_JOIN_WAITING;
    lw.attempts = 0;
    lw.backoff_s = JOIN_BACKOFF_START_S;
    lw.next_attempt_s = now_s;
}
