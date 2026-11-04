#include "sender.h"
#include <string.h>

static uint64_t jitter(fl_sender *s)
{
    s->random = s->random * 1664525u + 1013904223u;
    return s->config.jitter ? s->random % (s->config.jitter + 1) : 0;
}

static void begin_byte(fl_sender *s, uint64_t now)
{
    s->symbol = 0;
    s->phase = 1;
    s->next = now + s->config.byte_pause + jitter(s);
}

static void begin_block(fl_sender *s, uint64_t now)
{
    if (!s->pending || s->sent == s->size) return;
    s->pending = 0;
    ++s->requests;
    s->block_end = s->sent + (!s->sent && s->header_bytes ? s->header_bytes : s->config.block_bytes);
    if (s->block_end > s->size) s->block_end = s->size;
    if (now < s->request_time) now = s->request_time;
    begin_byte(s, now);
}

void fl_init(fl_sender *s, fl_config config, const uint8_t *data, size_t size)
{
    memset(s, 0, sizeof(*s));
    s->config = config;
    if (!s->config.block_bytes) s->config.block_bytes = FL_BLOCK_BYTES;
    s->data = data;
    s->size = size;
    s->header_bytes = FL_HEADER_BYTES;
    s->motor = s->read_high = s->write_high = s->sense_high = 1;
    s->random = 1;
    s->next = UINT64_MAX;
}

void fl_init_bootstrap(fl_sender *s, fl_config config, const uint8_t *data, size_t size)
{
    fl_init(s, config, data, size);
    s->header_bytes = 0;
    s->bootstrap_pairs = 1;
}

void fl_motor(fl_sender *s, uint64_t now, int on)
{
    on = !!on;
    if (on == s->motor) return;
    s->motor = on;
    s->pending = 1;
    s->request_time = now + (on ? s->config.motor_on : s->config.motor_off);
    if (!s->phase) begin_block(s, now);
}

void fl_step(fl_sender *s)
{
    static const unsigned write_bit[4] = {4, 6, 0, 2};
    uint64_t now = s->next;
    switch (s->phase) {
    case 1: /* Upper nibble first; low pair before high pair in each nibble. */
        s->write_high = (s->data[s->sent] >> (s->bootstrap_pairs ? 7 - 2 * s->symbol : write_bit[s->symbol])) & 1u;
        s->sense_high = (s->data[s->sent] >> (s->bootstrap_pairs ? 6 - 2 * s->symbol : write_bit[s->symbol] + 1)) & 1u;
        s->phase = 2;
        s->next = now + s->config.setup;
        break;
    case 2:
        s->read_high = s->config.c16 ? (s->symbol & 1u) : 0;
        s->phase = 3;
        s->next = now + s->config.hold + jitter(s);
        break;
    case 3:
        if (!s->config.c16) s->read_high = 1;
        if (++s->symbol < 4) {
            s->phase = 1;
            s->next = now + s->config.gap;
        } else {
            ++s->sent;
            if (s->sent < s->block_end) {
                begin_byte(s, now);
            } else {
                s->phase = 0;
                s->next = UINT64_MAX;
                begin_block(s, now);
            }
        }
        break;
    default:
        break;
    }
}
