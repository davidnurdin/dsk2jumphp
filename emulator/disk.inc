/* Disc images: "MV - CPCEMU" (standard) and "EXTENDED CPC DSK" files, up to 84 tracks, 2 sides.
 * Sectors keep their own ID (C, H, R, N), status bytes and size: system (#41-#49), data (#C1-#C9),
 * IBM or protected layouts all load; the FDC looks sectors up by ID, like the real controller. */
#define MAX_TRACKS 84
#define MAX_SECTORS 32

typedef struct { uint8_t c, h, r, n, st1, st2; int size; uint8_t *data; } Sector;
typedef struct { int count; Sector s[MAX_SECTORS]; } Track;

static Track disk[MAX_TRACKS][2];
static int disk_tracks, disk_sides;
static uint8_t *disk_image;

static int load_dsk(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    disk_image = malloc(n);
    if (fread(disk_image, 1, n, f) != (size_t)n) { fclose(f); return 0; }
    fclose(f);
    uint8_t *b = disk_image;
    int extended = !memcmp(b, "EXTENDED", 8);
    if (!extended && memcmp(b, "MV - CPC", 8)) { fprintf(stderr, "%s: not a DSK image\n", path); return 0; }
    disk_tracks = b[0x30];
    disk_sides = b[0x31] ? b[0x31] : 1;
    long off = 0x100;
    for (int i = 0; i < disk_tracks * disk_sides; i++) {
        long size = extended ? b[0x34 + i] * 256 : (b[0x32] | b[0x33] << 8);
        if (size == 0) continue; /* unformatted track (extended images) */
        if (off + 0x100 > n) break;
        uint8_t *th = b + off;
        if (memcmp(th, "Track-Info", 10)) { off += size; continue; }
        int t = th[0x10], side = th[0x11] & 1;
        if (t < MAX_TRACKS) {
            Track *tr = &disk[t][side];
            uint8_t *data = th + 0x100;
            tr->count = th[0x15] < MAX_SECTORS ? th[0x15] : MAX_SECTORS;
            for (int s = 0; s < tr->count; s++) {
                uint8_t *si = th + 0x18 + s * 8;
                Sector *sec = &tr->s[s];
                sec->c = si[0]; sec->h = si[1]; sec->r = si[2]; sec->n = si[3]; sec->st1 = si[4]; sec->st2 = si[5];
                int len = extended ? (si[6] | si[7] << 8) : 128 << (th[0x14] & 7);
                if (len == 0) len = 128 << (sec->n & 7);
                if (data + len > b + n) len = (int)(b + n - data) > 0 ? (int)(b + n - data) : 0;
                sec->size = len;
                sec->data = data;
                data += len;
            }
        }
        off += size;
    }
    return 1;
}

/* The sector with ID R on a track (side 0 unless the command asks for head 1), NULL when absent. */
static Sector *find_sector(int track, int side, uint8_t r) {
    if (track < 0 || track >= MAX_TRACKS) return NULL;
    Track *tr = &disk[track][side & 1];
    for (int s = 0; s < tr->count; s++)
        if (tr->s[s].r == r) return &tr->s[s];
    return NULL;
}
