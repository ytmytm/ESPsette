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
enum { FL_DIRECT, FL_TAPE, FL_BOOTSTRAP, FL_PAYLOAD };
static int fl_mode, fl_tape_motor, fl_write_high = 1, fl_tape_high = 1;
static unsigned fl_signature, fl_half;
static uint32_t fl_periods[100000];
static size_t fl_period_count, fl_period_index, fl_boot_size;
static uint8_t fl_boot_bytes[1024];
static size_t fl_payload_size;
static fl_config fl_payload_config;
static uint64_t fl_tape_next, fl_tape_remaining;
static unsigned fl_boot_requests;

static void fl_alarm_callback(CLOCK offset, void *unused);

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

static void fl_tape_schedule(uint64_t now)
{
    if (!fl_tape_motor || fl_period_index == fl_period_count) return;
    uint64_t delay = fl_tape_remaining;
    if (!delay) delay = fl_periods[fl_period_index] / (fl_payload_config.c16 ? 2 : 1);
    fl_tape_remaining = 0;
    fl_tape_next = now + delay;
    alarm_set(fl_alarm, (CLOCK)fl_tape_next);
}

static void fl_select_bootstrap(void)
{
    fl_config config = fl_payload_config;
    config.hold = fl_us("ESPSETTE_FL_BOOT_HOLD_US", 80);
    config.byte_pause = fl_us("ESPSETTE_FL_BOOT_PAUSE_US", 80);
    config.jitter = 0;
    config.block_bytes = 256;
    alarm_unset(fl_alarm);
    fl_mode = FL_BOOTSTRAP;
    fl_init_bootstrap(&fl, config, fl_boot_bytes, fl_boot_size);
    tapeport_set_read_in(config.c16 ? 1 : 0, 0);
    fprintf(stderr, "ESPSETTE_BOOT signature=ca65 standard_periods=%zu stage_bytes=%zu\n",
            fl_period_index, fl_boot_size);
}

static void fl_alarm_callback(CLOCK offset, void *unused)
{
    int old_read = fl.read_high;
    uint64_t now = fl.next;
    (void)offset;
    (void)unused;
    alarm_unset(fl_alarm);
    if (fl_mode == FL_TAPE) {
        if (fl_payload_config.c16) {
            fl_tape_high = !fl_tape_high;
            tapeport_set_read_in(fl_tape_high, 0);
            if (++fl_half == 2) { fl_half = 0; ++fl_period_index; }
        } else {
            tapeport_set_read_in(1, 0);
            ++fl_period_index;
        }
        fl_tape_schedule(maincpu_clk - offset);
        return;
    }
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
    if (fl_mode != FL_BOOTSTRAP && fl.phase == 3 && fl.symbol == 3 && fl.sent + 1 == fl.size) fl_end = now;
    if (fl_mode == FL_BOOTSTRAP && fl.sent == fl.size) {
        fl_boot_requests = fl.requests;
        fprintf(stderr, "ESPSETTE_BOOT stage_received=%zu requests=%u\n", fl.sent, fl.requests);
        fl_mode = FL_PAYLOAD;
        fl_init(&fl, fl_payload_config, fl_bytes, fl_payload_size);
        fl_start = fl_end = 0;
    } else if (fl_mode == FL_PAYLOAD && fl.sent == fl.size) {
        tapeport_set_tape_sense(0, 0); /* release PLAY before KERNAL IRQ resumes */
        tapeport_set_write_in(fl_payload_config.c16 ? 1 : 0, 0);
    }
    fl_schedule();
}

static void fl_store_motor(int port, int on)
{
    uint64_t request_clock = maincpu_clk;
    (void)port;
    if (!fl_enabled) return;
    if (fl_mode == FL_TAPE) {
        if (on == fl_tape_motor) return;
        fl_tape_motor = !!on;
        if (on) {
            fl_signature = ((fl_signature << 1) | fl_write_high) & 65535;
            if (fl_signature == 0xca65) { fl_select_bootstrap(); return; }
            fl_tape_schedule(maincpu_clk);
        } else {
            fl_tape_remaining = fl_tape_next > maincpu_clk ? fl_tape_next - maincpu_clk : 1;
            alarm_unset(fl_alarm);
        }
        return;
    }
    if (!fl_start && on != fl.motor) fl_start = maincpu_clk;
    if (!fl.requests && getenv("ESPSETTE_FL_PHASE_TICKS")) {
        request_clock += strtoul(getenv("ESPSETTE_FL_PHASE_TICKS"), NULL, 10);
    }
    fl_motor(&fl, request_clock, on);
    fl_schedule();
}

static void fl_store_write(int port, int high)
{
    (void)port;
    fl_write_high = !!high;
}

static size_t fl_read_file(const char *path, void *bytes, size_t capacity)
{
    FILE *input = path ? fopen(path, "rb") : NULL;
    size_t size;
    if (!input) return 0;
    size = fread(bytes, 1, capacity, input);
    if (ferror(input) || !feof(input)) size = 0;
    fclose(input);
    return size;
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
    fl_mode = FL_DIRECT;
    fl_boot_requests = 0;
    fl_payload_config = config;
    fl_payload_size = count;
    if (getenv("ESPSETTE_FL_BOOT_PERIODS")) {
        size_t bytes = fl_read_file(getenv("ESPSETTE_FL_BOOT_PERIODS"), fl_periods, sizeof(fl_periods));
        fl_boot_size = fl_read_file(getenv("ESPSETTE_FL_BOOT_RECEIVER"), fl_boot_bytes, sizeof(fl_boot_bytes));
        if (!bytes || bytes % 4 || !fl_boot_size || fl_boot_size % 256) return -1;
        fl_period_count = bytes / 4;
        for (size_t i = 0; i < fl_period_count; ++i) {
            const uint8_t *p = (const uint8_t*)fl_periods + 4 * i;
            fl_periods[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                            ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        }
        fl_mode = FL_TAPE;
        fl_period_index = 0;
        fl_tape_next = fl_tape_remaining = fl_signature = fl_half = 0;
        fl_tape_motor = 0;
        /* One encoder period is LOW then HIGH. Starting HIGH would pair
         * a pulse's LOW half with the following pulse's HIGH half on TED. */
        fl_tape_high = 0;
        tapeport_set_tape_sense(1, 0);
    }
    tapeport_set_read_in(config.c16 && fl_mode != FL_TAPE ? 1 : 0, 0);
    return 0;
}

static void fl_shutdown(void)
{
    if (fl_mode != FL_DIRECT) fprintf(stderr, "ESPSETTE_BOOT mode=%d boot_requests=%u periods=%zu/%zu signature_state=%04x motor=%d\n",
                                      fl_mode, fl_boot_requests, fl_period_index, fl_period_count,
                                      fl_signature, fl_tape_motor);
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
    fl_enable, NULL, fl_shutdown, fl_store_motor, fl_store_write, NULL, NULL, NULL, NULL
};
