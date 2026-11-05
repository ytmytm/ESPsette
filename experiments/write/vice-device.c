/* Test-only physical WRITE/MOTOR trace, included by the isolated VICE patch.
 * No ROM traps, pulse encoder or decoder: SAVE runs the real KERNAL code. */
static FILE *wc_trace;
static alarm_t *wc_read_alarm;
static int wc_motor_on, wc_read_high;
static unsigned wc_read_index;

static void wc_read_callback(CLOCK offset, void *unused)
{
    static const unsigned periods[] = {384, 536, 680};
    (void)unused;
    if (!wc_motor_on || !wc_trace) return;
    if (machine_class == VICE_MACHINE_PLUS4) {
        wc_read_high = !wc_read_high;
        tapeport_set_read_in(wc_read_high, 0);
    } else tapeport_set_read_in(1, 0); /* C64 physical FLAG falling event */
    alarm_set(wc_read_alarm, maincpu_clk - offset + periods[wc_read_index++ % 3]);
}

static void wc_event(char signal, int high)
{
    if (wc_trace) fprintf(wc_trace, "%llu,%c,%d\n",
                          (unsigned long long)maincpu_clk, signal, !!high);
}

static int wc_enable(int port, int enabled)
{
    const char *path = getenv("ESPSETTE_WRITE_TRACE");
    (void)port;
    if (wc_read_alarm) alarm_unset(wc_read_alarm);
    wc_motor_on = 0;
    if (wc_trace) { fclose(wc_trace); wc_trace = NULL; }
    if (!enabled) return 0;
    if (!path || !(wc_trace = fopen(path, "w"))) return -1;
    fprintf(wc_trace, "hz,%ld\n", machine_get_cycles_per_second());
    wc_motor_on = wc_read_index = wc_read_high = 0;
    if (getenv("ESPSETTE_WRITE_READ") && !wc_read_alarm)
        wc_read_alarm = alarm_new(maincpu_alarm_context, "ESPsetteReadDuringSave", wc_read_callback, NULL);
    tapeport_set_tape_sense(1, 0); /* PLAY pressed, physical SENSE low */
    return 0;
}

static void wc_motor(int port, int on)
{
    (void)port;
    wc_event('M', on);
    wc_motor_on = !!on;
    if (wc_read_alarm && getenv("ESPSETTE_WRITE_READ")) {
        if (on) alarm_set(wc_read_alarm, maincpu_clk + 384);
        else alarm_unset(wc_read_alarm);
    }
}

static void wc_write(int port, int value)
{
    (void)port;
    /* C64 callback is the CPU pin level; TED's callback complements it.
     * Undo VICE's convention here: the ESP wiring itself has no inverter. */
    wc_event('W', machine_class == VICE_MACHINE_PLUS4 ? !value : !!value);
}

static void wc_shutdown(void)
{
    /* Some machines destroy maincpu's alarm context before tapeport shutdown. */
    wc_motor_on = 0;
    wc_read_alarm = NULL;
    if (wc_trace) { fclose(wc_trace); wc_trace = NULL; }
}

static tapeport_device_t wc_device = {
    "ESPsette WRITE capture", TAPEPORT_DEVICE_TYPE_STORAGE,
    VICE_MACHINE_C64 | VICE_MACHINE_C64SC | VICE_MACHINE_PLUS4,
    TAPEPORT_PORT_1_MASK,
    wc_enable, NULL, wc_shutdown, wc_motor, wc_write, NULL, NULL, NULL, NULL
};
