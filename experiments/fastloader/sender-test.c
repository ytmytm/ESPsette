#include "sender.h"
#include <assert.h>
#include <stdio.h>

static void transfer(int c16, size_t payload, unsigned block_bytes)
{
    uint8_t bytes[FL_HEADER_BYTES + 513];
    fl_sender s;
    fl_config config = {3, 64, 3, 5, 1000, 96, 7, 0, 0};
    uint64_t now = 0, data_time = 0;
    size_t received = 0, size = FL_HEADER_BYTES + payload;
    unsigned decoded = 0, pairs = 0, requests = 0;
    int read_high = 1;
    for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = (uint8_t)i;
    config.c16 = c16;
    config.block_bytes = block_bytes;
    fl_init(&s, config, bytes, size);
    assert(s.next == UINT64_MAX);
    fl_motor(&s, now, 1); /* Same level is not another request. */
    assert(s.next == UINT64_MAX);
    while (received < size) {
        size_t boundary = received + (received ? block_bytes : FL_HEADER_BYTES);
        if (boundary > size) boundary = size;
        assert(s.phase == 0);
        now += requests == 1 ? 10000000 : 100;
        fl_motor(&s, now, !s.motor);
        ++requests;
        assert(s.next >= now + (s.motor ? config.motor_on : config.motor_off));
        while (s.next != UINT64_MAX) {
            unsigned phase = s.phase;
            now = s.next;
            fl_step(&s);
            if (phase == 1) data_time = now;
            if (s.read_high != read_high) {
                if (c16 || !s.read_high) {
                    assert(now - data_time >= config.setup);
                    /* Independent expectation: first fill the upper nibble
                     * low-to-high, then the lower nibble low-to-high. */
                    unsigned bit = pairs < 2 ? 4 + 2 * pairs : 2 * (pairs - 2);
                    decoded |= (s.write_high | (s.sense_high << 1)) << bit;
                    if (++pairs == 4) {
                        assert(received < boundary);
                        assert(decoded == bytes[received++]);
                        decoded = pairs = 0;
                    }
                }
                read_high = s.read_high;
            }
        }
        assert(received == boundary && s.sent == boundary);
    }
    assert(requests == 1 + (payload + block_bytes - 1) / block_bytes);
    assert(s.requests == requests);
    fl_motor(&s, now + 100, !s.motor);
    assert(s.next == UINT64_MAX && s.sent == size);
}

static void queued_request(int c16)
{
    uint8_t bytes[FL_HEADER_BYTES + 257] = {0};
    fl_config config = {3, 64, 3, 0, 0, 0, 0, 0, 256};
    fl_sender s;
    config.c16 = c16;
    fl_init(&s, config, bytes, sizeof(bytes));
    fl_motor(&s, 0, 0);
    while (!(s.sent == 5 && s.symbol == 3 && s.phase == 3)) fl_step(&s);
    /* The receiver requests the payload while the descriptor's final pair
     * is still held. The request survives, and exactly 256 bytes follow. */
    fl_motor(&s, s.next - 1, 1);
    assert(s.pending);
    while (s.next != UINT64_MAX) fl_step(&s);
    assert(s.sent == FL_HEADER_BYTES + 256 && s.requests == 2);
    fl_motor(&s, 100000 + s.sent, 0);
    while (s.next != UINT64_MAX) fl_step(&s);
    assert(s.sent == sizeof(bytes) && s.requests == 3);
}

int main(void)
{
    const size_t lengths[] = {1, 255, 256, 257, 512, 513};
    for (int c16 = 0; c16 <= 1; ++c16) {
        for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
            transfer(c16, lengths[i], 256);
            transfer(c16, lengths[i], 1);
        }
        queued_request(c16);
    }
    puts("sender: both READ variants, block boundaries, stalls, latency and queued block request passed");
    return 0;
}
