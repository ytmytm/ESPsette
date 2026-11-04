/* Test-only VICE tape-port adapter, included by the patched tapeport.c.
 * The portable sender runs on alarms, independently of receiver instructions.
 * Only physical tape-port signals cross the boundary. */
#include <stdio.h>
#include <stdlib.h>
#include "alarm.h"
#include "maincpu.h"
#include "sender.c"

static fl_sender fl;
static alarm_t *fl_alarm;
static uint8_t fl_bytes[65542];
static int fl_enabled;
static uint64_t fl_start, fl_end;
static FILE *fl_trace;

static uint64_t fl_us(const char *name, unsigned fallback)
{
    const char *value = getenv(name);
    unsigned us = value ? (unsigned)strtoul(value, NULL, 10) : fallback;
    return ((uint64_t)us * machine_get_cycles_per_second() + 999999) / 1000000;
}

static void fl_schedule(void)
{
    if (fl.next != UINT64_MAX) alarm_set(fl_alarm, (CLOCK)fl.next);
}

static void fl_alarm_callback(CLOCK offset, void *unused)
{
    int old_read = fl.read_high;
    uint64_t now = fl.next;
    (void)offset;
    (void)unused;
    alarm_unset(fl_alarm);
    fl_step(&fl);
    /* VICE uses active-low assertions for C64 WRITE/SENSE, but a physical
     * input level for 7501 WRITE. C64 read_in(1) means a FLAG event. */
    tapeport_set_write_in(fl.config.c16 ? fl.write_high : !fl.write_high, 0);
    tapeport_set_tape_sense(!fl.sense_high, 0);
    if (fl.read_high != old_read) {
        if (fl.config.c16) tapeport_set_read_in(fl.read_high, 0);
        else if (!fl.read_high) tapeport_set_read_in(1, 0);
    }
    if (fl_trace) fprintf(fl_trace, "%llu,%zu,%u,%d,%d,%d\n",
                         (unsigned long long)now, fl.sent, fl.symbol,
                         fl.read_high, fl.write_high, fl.sense_high);
    if (fl.phase == 3 && fl.symbol == 3 && fl.sent + 1 == fl.size) fl_end = now;
    fl_schedule();
}

static void fl_store_motor(int port, int on)
{
    uint64_t request_clock = maincpu_clk;
    (void)port;
    if (!fl_enabled) return;
    if (!fl_start && on != fl.motor) fl_start = maincpu_clk;
    if (!fl.requests && getenv("ESPSETTE_FL_PHASE_TICKS")) {
        request_clock += strtoul(getenv("ESPSETTE_FL_PHASE_TICKS"), NULL, 10);
    }
    fl_motor(&fl, request_clock, on);
    fl_schedule();
}

static int fl_enable(int port, int enabled)
{
    const char *path, *trace;
    FILE *input;
    size_t count;
    fl_config config;
    (void)port;
    if (!enabled) {
        fl_enabled = 0;
        if (fl_alarm) alarm_unset(fl_alarm);
        return 0;
    }
    path = getenv("ESPSETTE_FL_STREAM");
    if (!path || !(input = fopen(path, "rb"))) return -1;
    count = fread(fl_bytes, 1, sizeof(fl_bytes), input);
    if (ferror(input) || !feof(input) || count < 7) {
        fclose(input);
        return -1;
    }
    fclose(input);
    config.c16 = machine_class == VICE_MACHINE_PLUS4;
    config.setup = fl_us("ESPSETTE_FL_SETUP_US", 3);
    config.hold = fl_us("ESPSETTE_FL_HOLD_US", 16);
    config.gap = fl_us("ESPSETTE_FL_GAP_US", 3);
    config.motor_on = fl_us("ESPSETTE_FL_MOTOR_ON_US", 5);
    config.motor_off = fl_us("ESPSETTE_FL_MOTOR_OFF_US", 1000);
    config.byte_pause = fl_us("ESPSETTE_FL_PAUSE_US", config.c16 ? 11 : 19);
    config.jitter = fl_us("ESPSETTE_FL_JITTER_US", 0);
    config.block_bytes = getenv("ESPSETTE_FL_BLOCK_BYTES") ?
        (unsigned)strtoul(getenv("ESPSETTE_FL_BLOCK_BYTES"), NULL, 10) : FL_BLOCK_BYTES;
    if (!config.setup || !config.hold || !config.gap) return -1;
    fl_init(&fl, config, fl_bytes, count);
    if (!fl_alarm) fl_alarm = alarm_new(maincpu_alarm_context, "ESPsetteFastloader",
                                      fl_alarm_callback, NULL);
    trace = getenv("ESPSETTE_FL_TRACE");
    if (trace && !fl_trace) fl_trace = fopen(trace, "w");
    fl_enabled = 1;
    fl_start = fl_end = 0;
    tapeport_set_read_in(config.c16 ? 1 : 0, 0);
    return 0;
}

static void fl_shutdown(void)
{
    fprintf(stderr, "ESPSETTE_FL sent=%zu total=%zu ticks=%llu hz=%ld requests=%u\n",
            fl_end ? fl.size : fl.sent, fl.size,
            (unsigned long long)(fl_end > fl_start ? fl_end - fl_start : 0),
            machine_get_cycles_per_second(), fl.requests);
    if (fl_trace) { fclose(fl_trace); fl_trace = NULL; }
}

static tapeport_device_t fl_device = {
    "ESPsette fastloader experiment", TAPEPORT_DEVICE_TYPE_STORAGE,
    VICE_MACHINE_C64 | VICE_MACHINE_C64SC | VICE_MACHINE_PLUS4,
    TAPEPORT_PORT_1_MASK,
    fl_enable, NULL, fl_shutdown, fl_store_motor, NULL, NULL, NULL, NULL, NULL
};
