/* uPD765 floppy controller, polled mode (what the CPC uses): one drive, sectors found by ID.
 * Enough for AMSDOS and for the custom loaders of games (read / write data, read ID, seek, ...). */
static uint8_t fdc_cmd[9], fdc_res[7], fdc_track, fdc_seek_done;
static int fdc_phase, fdc_cmd_len, fdc_cmd_pos, fdc_res_len, fdc_res_pos, fdc_data_pos, fdc_id_index;
static Sector *fdc_sector;
enum { FDC_IDLE, FDC_COMMAND, FDC_READ, FDC_WRITE, FDC_RESULT };

static int fdc_command_length(uint8_t c) {
    switch (c & 0x1f) {
    case 0x03: return 3; case 0x04: return 2; case 0x05: case 0x06: case 0x09: case 0x0c: return 9;
    case 0x07: return 2; case 0x08: return 1; case 0x0a: return 2; case 0x0d: return 6; case 0x0f: return 3;
    default: return 1;
    }
}
static int fdc_head(void) { return (fdc_cmd[1] >> 2) & 1; }
static void fdc_result(int n) { fdc_res_len = n; fdc_res_pos = 0; fdc_phase = FDC_RESULT; }
static void fdc_rw_result(uint8_t st0, uint8_t st1, uint8_t st2) {
    fdc_res[0] = st0 | fdc_head() << 2; fdc_res[1] = st1; fdc_res[2] = st2;
    fdc_res[3] = fdc_cmd[2]; fdc_res[4] = fdc_cmd[3]; fdc_res[5] = fdc_cmd[4]; fdc_res[6] = fdc_cmd[5];
    fdc_result(7);
}
/* the sector asked for by the command (R = fdc_cmd[4]) on the track the head is on */
static int fdc_start_sector(void) {
    fdc_sector = find_sector(fdc_track, fdc_head(), fdc_cmd[4]);
    if (!fdc_sector) { fdc_rw_result(0x40, 0x04, 0x00); return 0; } /* no data: sector not found */
    fdc_data_pos = 0;
    log_event("fdc %s track %d side %d sector %02x (%d bytes) pc=%04x", fdc_phase == FDC_WRITE ? "write" : "read",
              fdc_track, fdc_head(), fdc_cmd[4], fdc_sector->size, cpu.pc);
    return 1;
}
static void fdc_execute(void) {
    uint8_t c = fdc_cmd[0] & 0x1f;
    if (log_fdc) {
        char line[64]; int k = 0;
        for (int i = 0; i < fdc_cmd_len; i++) k += snprintf(line + k, sizeof line - k, " %02x", fdc_cmd[i]);
        log_event("fdc cmd%s pc=%04x", line, cpu.pc);
    }
    switch (c) {
    case 0x03: fdc_phase = FDC_IDLE; break;                                   /* specify */
    case 0x04: fdc_res[0] = 0x20 | (fdc_track == 0 ? 0x10 : 0) | (disk_sides > 1 ? 0x08 : 0); fdc_result(1); break;
    case 0x07: fdc_track = 0; fdc_seek_done = 1; fdc_phase = FDC_IDLE; break;   /* recalibrate */
    case 0x0f: fdc_track = fdc_cmd[2]; fdc_seek_done = 1; fdc_phase = FDC_IDLE; break; /* seek */
    case 0x08:                                                                /* sense interrupt status */
        if (fdc_seek_done) { fdc_res[0] = 0x20; fdc_res[1] = fdc_track; fdc_seek_done = 0; fdc_result(2); }
        else { fdc_res[0] = 0x80; fdc_result(1); }
        break;
    case 0x0a: {                                                              /* read ID: the sectors pass in turn */
        Track *tr = &disk[fdc_track < MAX_TRACKS ? fdc_track : 0][fdc_head()];
        if (fdc_track >= MAX_TRACKS || tr->count == 0) { fdc_res[0] = 0x40; fdc_res[1] = 0x01; fdc_res[2] = 0; fdc_result(7); break; }
        Sector *s = &tr->s[fdc_id_index++ % tr->count];
        fdc_res[0] = fdc_head() << 2; fdc_res[1] = 0; fdc_res[2] = 0;
        fdc_res[3] = s->c; fdc_res[4] = s->h; fdc_res[5] = s->r; fdc_res[6] = s->n;
        fdc_result(7);
        break;
    }
    case 0x06: case 0x0c: case 0x05: case 0x09:                               /* read / write data */
        fdc_phase = (c == 0x05 || c == 0x09) ? FDC_WRITE : FDC_READ;
        fdc_start_sector();
        break;
    case 0x0d:                                                                /* format track: accepted, not done */
        fdc_res[0] = 0; fdc_res[1] = 0; fdc_res[2] = 0; fdc_result(7);
        break;
    default: fdc_res[0] = 0x80; fdc_result(1);                                 /* invalid command */
    }
}
static void fdc_next_sector(void) {
    /* the CPC never raises TC: a transfer always ends "end of cylinder" after EOT (fdc_cmd[6]) */
    if (fdc_cmd[4] == fdc_cmd[6]) { fdc_rw_result(0x40, 0x80 | fdc_sector->st1, fdc_sector->st2); return; }
    fdc_cmd[4]++;
    fdc_start_sector();
}
static uint8_t fdc_status(void) {
    switch (fdc_phase) {
    case FDC_COMMAND: return 0x90;
    case FDC_READ: return 0xf0;
    case FDC_WRITE: return 0xb0;
    case FDC_RESULT: return 0xd0;
    }
    return 0x80;
}
static uint8_t fdc_read_data(void) {
    if (fdc_phase == FDC_READ) {
        if (fdc_data_pos == 0) fdc_first_byte = 1;
        uint8_t v = fdc_data_pos < fdc_sector->size ? fdc_sector->data[fdc_data_pos] : 0xe5;
        if (++fdc_data_pos >= fdc_sector->size) fdc_next_sector();
        return v;
    }
    if (fdc_phase == FDC_RESULT) {
        uint8_t v = fdc_res[fdc_res_pos++];
        if (fdc_res_pos >= fdc_res_len) fdc_phase = FDC_IDLE;
        return v;
    }
    return 0xff;
}
static void fdc_write_data(uint8_t v) {
    switch (fdc_phase) {
    case FDC_IDLE:
        fdc_cmd[0] = v; fdc_cmd_len = fdc_command_length(v); fdc_cmd_pos = 1;
        if (fdc_cmd_len == 1) fdc_execute(); else fdc_phase = FDC_COMMAND;
        break;
    case FDC_COMMAND:
        fdc_cmd[fdc_cmd_pos++] = v;
        if (fdc_cmd_pos == fdc_cmd_len) fdc_execute();
        break;
    case FDC_WRITE: /* written into the image in memory only: the .dsk file is never changed */
        if (fdc_data_pos < fdc_sector->size) fdc_sector->data[fdc_data_pos] = v;
        if (++fdc_data_pos >= fdc_sector->size) fdc_next_sector();
        break;
    }
}
