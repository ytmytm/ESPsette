#ifndef ESPSETTE_FASTLOADER_SENDER_H
#define ESPSETTE_FASTLOADER_SENDER_H

#include <stddef.h>
#include <stdint.h>

#define FL_HEADER_BYTES 6u
#define FL_BLOCK_BYTES 256u

/* Portable transmitter model. Time units belong to the caller (VICE CPU ticks
 * in the integration test). No emulated CPU registers or receiver PC accesses. */
typedef struct {
    uint64_t setup, hold, gap, motor_on, motor_off, byte_pause, jitter;
    int c16;
    unsigned block_bytes;
} fl_config;

typedef struct {
    fl_config config;
    const uint8_t *data;
    size_t size, sent, block_end;
    uint64_t next, request_time;
    unsigned symbol, phase, random, requests;
    int motor, pending, read_high, write_high, sense_high;
} fl_sender;

void fl_init(fl_sender *s, fl_config config, const uint8_t *data, size_t size);
void fl_motor(fl_sender *s, uint64_t now, int on);
/* Call at s->next, then apply the three physical output levels. UINT64_MAX
 * means waiting for MOTOR. The descriptor is a separate six-byte block,
 * followed by payload blocks of config.block_bytes (default 256).
 * At most one queued block request is possible. */
void fl_step(fl_sender *s);

#endif
