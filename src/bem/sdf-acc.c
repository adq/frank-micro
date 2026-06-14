/*
 * B-EM SDF - Simple Disk Formats - Access
 *
 * This B-Em module is part of the handling of simple disc formats,
 * i.e. those where the sectors that comprise the disk image are
 * stored in the file in a logical order and without ID headers.
 *
 * This module contains the functions to open and access the disc
 * images who geometry is described by the companion module sdf-geo.c
 *
 * Pico version (C) 2021 Graham Sanderson
 */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "b-em.h"
#include "disc.h"
#include "sdf.h"
#if USE_SECTOR_READ
#include "sector_read.h"
#endif

#define MMB_CAT_SIZE 0x2000

#ifdef USE_HW_EVENT
cycle_timestamp_t hw_event_motor_base;
#endif

#ifndef USE_SECTOR_READ
static FILE *sdf_fp[NUM_DRIVES];
#ifndef NO_USE_MMB
static FILE *mmb_fp;
#endif
#else
static struct sector_read *sr[NUM_DRIVES];
static struct sector_buffer *sr_sector[NUM_DRIVES];
static int8_t sr_counter[NUM_DRIVES]; // so we can detect disc change
static int8_t sr_counter_last[NUM_DRIVES];
#endif
static const struct sdf_geometry *geometry[NUM_DRIVES];
static uint8_t current_track[NUM_DRIVES];
#ifndef NO_USE_MMB
static off_t mmb_offset[NUM_DRIVES][2];
static char *mmb_cat;
char *mmb_fn;
#endif

typedef enum {
    ST_IDLE,
    ST_NOTFOUND,
    ST_READSECTOR,
    ST_WRITESECTOR,
    ST_READ_ADDR0,
    ST_READ_ADDR1,
    ST_READ_ADDR2,
    ST_READ_ADDR3,
    ST_READ_ADDR4,
    ST_READ_ADDR5,
    ST_READ_ADDR6,
    ST_FORMAT
} state_t;

state_t state = ST_IDLE;

static uint16_t count = 0;

static int     sdf_time;
static uint8_t sdf_drive;
static uint8_t sdf_side;
static uint8_t sdf_track;
static uint8_t sdf_sector;

#ifdef USE_HW_EVENT
static void sdf_poll();
static bool invoke_sdf(struct hw_event *event);

//static void sdf_cycle_sync(int);

static struct hw_event sdf_event = {
        .invoke = invoke_sdf
};

static bool __time_critical_func(invoke_sdf)(struct hw_event *event) {
    sdf_event.user_time = get_hardware_timestamp();
    sdf_time = 16;
    sdf_poll();
    return false;
}


void __time_critical_func(sdf_cycle_sync)(int drive) {
    // nothing to do i think
}
#endif

static void __time_critical_func(set_state)(int new_state) {
#ifdef USE_HW_EVENT
    if (new_state != ST_IDLE) {
        // in general this isn't necessary, but for comparison with non HW_EVENT versions we'd like to
        // have the events happen at correct multiples of 16 * 17 cycles, so if we are in an event, then we
        // add that, otherwise we weren't paying attention, and we will just use the last user_event
        // time (which will be good for up to an hour)
#if 1
        const int delay = 16 * (17 - sdf_time); // counts to 17 not 16 it seems
        int32_t elapsed = get_hardware_timestamp() - sdf_event.user_time;
        if (elapsed >= 0 && elapsed < delay) {
            // user_time is around now, so next event is based on that
            sdf_event.target = sdf_event.user_time + delay;
        } else {
            // todo baseline should be something other than user_time that gets updated before time wraps, but this is good enough for now
            uint32_t periods = ((uint32_t)(get_hardware_timestamp() - hw_event_motor_base)) / (16 * 17);
            sdf_event.target = hw_event_motor_base + periods * 16 * 17 + delay;
        }
#else
#error this branch was just to see if the above was breaking NUCC.
        sdf_event.target = get_hardware_timestamp() + 16 * 17;
#endif
        upsert_hw_event(&sdf_event);
    } else {
        remove_hw_event(&sdf_event);
    }
#endif
    state = new_state;
}
static void sdf_close(int drive)
{
    if (drive < NUM_DRIVES) {
        geometry[drive] = NULL;
#ifndef USE_SECTOR_READ
        if (sdf_fp[drive]) {
#ifndef NO_USE_MMB
            if (sdf_fp[drive] != mmb_fp)
                fclose(sdf_fp[drive]);
#else
            fclose(sdf_fp[drive]);
#endif
            sdf_fp[drive] = NULL;
        }
#else
        if (sr[drive]) {
            if (sr_sector[drive]) {
                sector_read_release_buffer(sr[drive], sr_sector[drive]);
                sr_sector[drive] = NULL;
            }
            sector_read_close(sr[drive]);
            sr[drive] = NULL;
        }
#endif
    }
}

static void __time_critical_func(sdf_seek)(int drive, int track)
{
    if (drive < NUM_DRIVES)
        current_track[drive] = track;
}

static int sdf_verify(int drive, int track, int density)
{
    const struct sdf_geometry *geo;

    if (drive < NUM_DRIVES)
        if ((geo = geometry[drive]))
            if (track >= 0 && track < geo->tracks)
                if ((!density && geo->density == SDF_DENS_SINGLE) || (density && geo->density == SDF_DENS_DOUBLE))
                    return 1;
    return 0;
}

static bool __time_critical_func(io_seek)(const struct sdf_geometry *geo, uint8_t drive, uint8_t sector, uint8_t track, uint8_t side)
{
    uint32_t track_bytes, offset;

    if (track >= 0 && track < geo->tracks) {
        if (geo->sector_size > 256)
            sector--;
        if (sector >= 0 && sector <= geo->sectors_per_track ) {
            track_bytes = geo->sectors_per_track * geo->sector_size;
            if (side == 0) {
                offset = track * track_bytes;
                if (geo->sides == SDF_SIDES_INTERLEAVED)
                    offset *= 2;
            } else {
                switch(geo->sides)
                {
                case SDF_SIDES_SEQUENTIAL:
                    offset = (track + geo->tracks) * track_bytes;
                    break;
                case SDF_SIDES_INTERLEAVED:
                    offset = (track * 2 + 1) * track_bytes;
                    break;
                default:
                    log_debug("sdf: drive %u: attempt to read second side of single-sided disc", drive);
                    return false;
                }
            }
            offset += sector * geo->sector_size;
#ifndef NO_USE_MMB
            offset += mmb_offset[drive][side];
#endif
            log_debug("sdf: drive %u: seeking for side=%u, track=%u, sector=%u to %d bytes\n", drive, side, track, sector, (int)offset);
#ifndef USE_SECTOR_READ
            fseek(sdf_fp[drive], offset, SEEK_SET);
#else
            if (sr_sector[drive]) {
                sector_read_release_buffer(sr[drive], sr_sector[drive]);
                assert(!(offset % geo->sector_size));
            }
            sr_sector[drive] = sector_read_acquire_buffer(sr[drive], offset / geo->sector_size);
#endif
            return true;
        }
        else
            log_debug("sdf: drive %u: invalid sector: %d not between 0 and %d", drive, sector, geo->sectors_per_track);
    }
    else
        log_debug("sdf: drive %u: invalid track: %d not between 0 and %d", drive, track, geo->tracks);
    return false;
}

FILE *sdf_owseek(uint8_t drive, uint8_t sector, uint8_t track, uint8_t side, uint16_t ssize)
{
#ifndef USE_SECTOR_READ
    const struct sdf_geometry *geo;

    if (drive < NUM_DRIVES) {
        if ((geo = geometry[drive])) {
            if (ssize == geo->sector_size) {
                if (io_seek(geo, drive, sector, track, side))
                    return sdf_fp[drive];
            }
            else
                log_debug("sdf: osword seek, sector size %u does not match disk (%u)", ssize, geo->sector_size);
        }
        else
            log_debug("sdf: osword seek, no geometry for drive %u", drive);
    }
    else
        log_debug("sdf: osword seek, drive %u out of range", drive);
#endif
    return NULL;
}

static const struct sdf_geometry *check_seek(int drive, int sector, int track, int side, int density)
{
    const struct sdf_geometry *geo;

    if (drive < NUM_DRIVES) {
        if ((geo = geometry[drive])) {
            if ((!density && geo->density == SDF_DENS_SINGLE) || (density && geo->density == SDF_DENS_DOUBLE)) {
                if (track == current_track[drive]) {
                    if (io_seek(geo, drive, sector, track, side))
                        return geo;
                }
                else
                    log_debug("sdf: drive %u: invalid track: %d should be %d", drive, track, current_track[drive]);
            }
            else
                log_debug("sdf: drive %u: invalid density", drive);
        }
        else
            log_debug("sdf: drive %u: geometry not found", drive);
    }
    else
        log_debug("sdf: drive %d: drive number  out of range", drive);

    count = 500;
    set_state(ST_NOTFOUND);
    return NULL;
}

static void __time_critical_func(sdf_readsector)(int drive, int sector, int track, int side, int density)
{
    const struct sdf_geometry *geo;

    if (state == ST_IDLE && (geo = check_seek(drive, sector, track, side, density))) {
        count = geo->sector_size;
        sdf_drive = drive;
//        printf("to ST_READSECTOR %d\n", get_hardware_timestamp());
#ifdef USE_SECTOR_READ
        sr_counter_last[drive] = sr_counter[drive];
#endif
        set_state(ST_READSECTOR);
    }
}

static void sdf_writesector(int drive, int sector, int track, int side, int density)
{
    const struct sdf_geometry *geo;

    if (state == ST_IDLE && (geo = check_seek(drive, sector, track, side, density))) {
        count = geo->sector_size;
        sdf_drive = drive;
        sdf_side = side;
        sdf_track = track;
        sdf_sector = sector;
        sdf_time = -20;
        set_state(ST_WRITESECTOR);
    }
}

static void __time_critical_func(sdf_readaddress)(int drive, int track, int side, int density)
{
    const struct sdf_geometry *geo;

    if (state == ST_IDLE) {
        if (drive < NUM_DRIVES) {
            if ((geo = geometry[drive])) {
                if ((!density && geo->density == SDF_DENS_SINGLE) || (density && geo->density == SDF_DENS_DOUBLE)) {
                    if (side == 0 || geo->sides != SDF_SIDES_SINGLE) {
                        sdf_drive = drive;
                        sdf_side = side;
                        sdf_track = track;
                        set_state(ST_READ_ADDR0);
                        return;
                    }
                }
            }
        }
        count = 500;
        set_state(ST_NOTFOUND);
    }
}

static void sdf_format(int drive, int track, int side, int density)
{
    const struct sdf_geometry *geo;

    if (state == ST_IDLE && (geo = check_seek(drive, 0, track, side, density))) {
        sdf_drive = drive;
        sdf_side = side;
        sdf_track = track;
        sdf_sector = 0;
        count = 500;
        set_state(ST_FORMAT);
    }
}

static void __time_critical_func(sdf_poll)()
{
#ifndef USE_SECTOR_READ
    int c;
#endif
    uint16_t sect_size;

    if (++sdf_time <= 16) {
        // todo not sure what this was about!
//#ifdef USE_HW_EVENT
//        set_state(state); // hack for HW_EVENT for now; only used by write ... reasonable hack saves a bunch of effort
//#endif
        return;
    }
    sdf_time = 0;

//    if (state != ST_IDLE) {
//        printf("state %d %d\n", state, get_hardware_timestamp());
//    }
    switch(state) {
        case ST_IDLE:
            break;

        case ST_NOTFOUND:
            if (--count == 0) {
                fdc_notfound();
                state = ST_IDLE;
            }
            break;

        case ST_READSECTOR:
#ifndef USE_SECTOR_READ
            fdc_data(getc(sdf_fp[sdf_drive]));
#else
            if (!sr_sector[sdf_drive] || sr_counter[sdf_drive] != sr_counter_last[sdf_drive]) {
                fdc_datacrcerror();
                state = ST_IDLE;
                break;
            }
            assert(count && count <= sr_sector[sdf_drive]->buffer.size);
            {
                uint offset = sr_sector[sdf_drive]->buffer.size - count;
                while (offset >
                       sector_read_ensure_check_available(sr[sdf_drive], sr_sector[sdf_drive], offset, 0)) {
                    tight_loop_contents();
                }
                fdc_data(sr_sector[sdf_drive]->buffer.bytes[offset]);
            }
#endif
            if (--count == 0) {
                fdc_finishread();
                state = ST_IDLE;
            }
            break;

        case ST_WRITESECTOR:
#ifndef USE_SECTOR_READ
#ifndef NO_USE_DISC_WRITE
            if (writeprot[sdf_drive])
#endif
            {
                log_debug("sdf: poll, write protected during write sector");
                fdc_writeprotect();
                state = ST_IDLE;
                break;
            }
            c = fdc_getdata(--count == 0);
            if (c == -1) {
                log_warn("sdf: data underrun on write");
                count++;
            } else {
                putc(c, sdf_fp[sdf_drive]);
                if (count == 0) {
                    fdc_finishread();
                    state = ST_IDLE;
                }
            }
#else
            fdc_writeprotect();
            state = ST_IDLE;
#endif
            break;

        case ST_READ_ADDR0:
            fdc_data(sdf_track);
            state = ST_READ_ADDR1;
            break;

        case ST_READ_ADDR1:
            fdc_data(sdf_side);
            state = ST_READ_ADDR2;
            break;

        case ST_READ_ADDR2:
            if (geometry[sdf_drive]->sector_size > 256)
                fdc_data(sdf_sector+1);
            else
                fdc_data(sdf_sector);
            state = ST_READ_ADDR3;
            break;

        case ST_READ_ADDR3:
            sect_size = geometry[sdf_drive]->sector_size;
            fdc_data(sect_size == 256 ? 1 : sect_size == 512 ? 2 : 3);
            state = ST_READ_ADDR4;
            break;

        case ST_READ_ADDR4:
            fdc_data(0);
            state = ST_READ_ADDR5;
            break;

        case ST_READ_ADDR5:
            fdc_data(0);
            state = ST_READ_ADDR6;
            break;

        case ST_READ_ADDR6:
            state = ST_IDLE;
            fdc_finishread();
            sdf_sector++;
            if (sdf_sector == geometry[sdf_drive]->sectors_per_track)
                sdf_sector = 0;
            break;

        case ST_FORMAT:
#ifndef USE_SECTOR_READ
#ifndef NO_USE_DISC_WRITE
            if (writeprot[sdf_drive])
#endif
            {
                log_debug("sdf: poll, write protected during write track");
                fdc_writeprotect();
                state = ST_IDLE;
                break;
            }
            if (--count == 0) {
                putc(0, sdf_fp[sdf_drive]);
                if (++sdf_sector >= geometry[sdf_drive]->sectors_per_track) {
                    state = ST_IDLE;
                    fdc_finishread();
                    break;
                }
                io_seek(geometry[sdf_drive], sdf_drive, sdf_sector, sdf_track, sdf_side);
                count = 500;
            }
#else
//            panic_unsupported();
            fdc_writeprotect();
            state = ST_IDLE;
#endif
            break;
    }
#ifdef USE_HW_EVENT
    set_state(state);
#endif
}

static void sdf_abort(int drive)
{
    set_state(ST_IDLE);
}

static void sdf_mount_init(int drive, const char *fn, const struct sdf_geometry *geo)
{
#ifdef USE_SECTOR_READ
    sr_counter[drive]++;
#endif
#if !PICO_ON_DEVICE
    log_info("Loaded drive %d with %s, format %s, %s, %d tracks, %s, %d %d byte sectors/track",
             drive, fn, geo->name, sdf_desc_sides(geo), geo->tracks,
             sdf_desc_dens(geo), geo->sectors_per_track, geo->sector_size);
#endif
    geometry[drive] = geo;
#ifndef NO_USE_MMB
    mmb_offset[drive][0] = mmb_offset[drive][1] = 0;
#endif
    drives[drive].close       = sdf_close;
    drives[drive].seek        = sdf_seek;
    drives[drive].verify      = sdf_verify;
    drives[drive].readsector  = sdf_readsector;
    drives[drive].writesector = sdf_writesector;
    drives[drive].readaddress = sdf_readaddress;
    drives[drive].poll        = sdf_poll;
    drives[drive].format      = sdf_format;
    drives[drive].abort       = sdf_abort;
#ifdef USE_HW_EVENT
    drives[drive].cycle_sync  = sdf_cycle_sync;
#endif
}

#ifndef USE_SECTOR_READ
static void sdf_mount(int drive, const char *fn, FILE *fp, const struct sdf_geometry *geo) {
    sdf_fp[drive] = fp;
#if DUMP_DISC
    const char *fn1 = strrchr(fn, '.');
    const char *fn2 = strrchr(fn, '/');
    if (fn2) fn2++;
    if (fn1 < fn2) fn1 = fn2 + strlen(fn2);
    char *afn = malloc(fn1 - fn2 + 1);
    strncpy(afn, fn2, fn1 - fn2);
    afn[fn1 - fn2] = 0;
    for(int i=0;i<fn1-fn2;i++) if (afn[i] == '-') afn[i] = '_';
    printf("static const struct sdf_geometry geo_%s = {\n", afn);
    printf("   .name = \"%s\",\n", geo->name);
    printf("   .sides = %d,\n", geo->sides);
    printf("   .density = %d,\n", geo->density);
    printf("   .tracks = %d,\n", geo->tracks);
    printf("   .sectors_per_track = %d,\n", geo->sectors_per_track);
    printf("   .sector_size = %d,\n", geo->sector_size);
    printf("};\n\n");
    fseek(fp, 0, SEEK_END);
    size_t len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
//    len = 0x9000;
    printf("static const uint8_t data_%s[%d] = {\n", afn, (uint)len);
    for(uint i=0;i<len;i+=32) {
        printf("    ");
        for(uint j=i;j<MIN(len, i+32);j++) {
            printf("0x%02x, ", fgetc(fp));
        }
        printf("\n");
    }
    printf("};\n");
    printf("static const uint32_t data_%s_size = %d;\n\n", afn, (uint)len);
    fseek(fp, 0, SEEK_SET);
    free(afn);
#endif
    sdf_mount_init(drive, fn, geo);
}

void sdf_load(int drive, const char *fn, const char *ext)
{
    FILE *fp;
    const struct sdf_geometry *geo;

#ifndef NO_USE_DISC_WRITE
    writeprot[drive] = 0;
#endif
    if ((fp = fopen(fn, "rb+")) == NULL) {
        if ((fp = fopen(fn, "rb")) == NULL) {
            log_error("Unable to open file '%s' for reading - %s", fn, strerror(errno));
            return;
        }
#ifndef NO_USE_DISC_WRITE
        writeprot[drive] = 1;
#endif
    }
    if ((geo = sdf_find_geo(fn, ext, fp)))
        sdf_mount(drive, fn, fp, geo);
    else {
        log_error("sdf: drive %d: unable to determine geometry for %s", drive, fn);
        fclose(fp);
    }
}
#else
void sdf_load_image(int drive, const struct sdf_geometry *geo, struct sector_read *_sr) {
    sr[drive] = _sr;
    sdf_mount_init(drive, "", geo);
}

void sdf_load_image_memory(int drive, const struct sdf_geometry *geo, const uint8_t *buf, uint32_t buf_size) {
    assert(!(buf_size % geo->sector_size));
//    sdf_load_image(drive, geo, memory_sector_read_open(buf, geo->sector_size, buf_size / geo->sector_size, false));
    sdf_load_image(drive, geo, xip_sector_read_open(buf, geo->sector_size, buf_size / geo->sector_size));
}
#endif

#ifndef NO_USE_DISC_WRITE
void sdf_new_disc(int drive, ALLEGRO_PATH *fn, enum sdf_disc_type dtype)
{
#ifndef USE_SECTOR_READ
    const struct sdf_geometry *geo;
    const char *cpath;
    FILE *f;

    if (dtype > SDF_FMT_MAX)
        log_error("sdf: drive %d: inavlid disc type %d for new disc", drive, dtype);
    else {
        geo = sdf_geo_tab + dtype;
        if (!geo->new_disc)
            log_error("sdf: drive %d: creation of file disc type %s (%d) not supported", drive, geo->name, dtype);
        else {
            cpath = al_path_cstr(fn, ALLEGRO_NATIVE_PATH_SEP);
            if ((f = fopen(cpath, "wb+"))) {
                writeprot[drive] = 0;
                geo->new_disc(f, geo);
                sdf_mount(drive, cpath, f, geo);
            }
            else
                log_error("sdf: drive %d: unable to open disk image %s for writing: %s", drive, cpath, strerror(errno));
        }
    }
#endif
}
#endif

#ifndef NO_USE_MMB
void mmb_load(char *fn)
{
    FILE *fp;

    if (!mmb_cat) {
        if (!(mmb_cat = malloc(MMB_CAT_SIZE))) {
            log_error("sdf: out of memory allocating MMB catalogue");
            return;
        }
    }
    writeprot[0] = 0;
    if ((fp = fopen(fn, "rb+")) == NULL) {
        if ((fp = fopen(fn, "rb")) == NULL) {
            log_error("Unable to open file '%s' for reading - %s", fn, strerror(errno));
            return;
        }
        writeprot[0] = 1;
    }
    if (fread(mmb_cat, MMB_CAT_SIZE, 1, fp) != 1) {
        log_error("mmb: %s is not a valid MMB file", fn);
        fclose(fp);
        return;
    }
    if (mmb_fp) {
        fclose(mmb_fp);
        if (sdf_fp[1] == mmb_fp) {
            sdf_mount(1, fn, fp, &sdf_geo_tab[SDF_FMT_DFS_10S_SEQ_80T]);
            writeprot[1] = writeprot[0];
            mmb_offset[1][0] = MMB_CAT_SIZE;
            mmb_offset[1][1] = MMB_CAT_SIZE + 10 * 256 * 80;
        }
    }
    sdf_mount(0, fn, fp, &sdf_geo_tab[SDF_FMT_DFS_10S_SEQ_80T]);
    mmb_offset[0][0] = MMB_CAT_SIZE;
    mmb_offset[0][1] = MMB_CAT_SIZE + 10 * 256 * 80;
    mmb_fp = fp;
    mmb_fn = fn;
    if (fdc_spindown)
        fdc_spindown();
}

static void mmb_eject_one(int drive)
{
    ALLEGRO_PATH *path;

    if (sdf_fp[drive] == mmb_fp) {
        disc_close(drive);
        if ((path = discfns[drive]))
            disc_load(drive, path);
    }
}

void mmb_eject(void)
{
    if (mmb_fp) {
        mmb_eject_one(0);
        mmb_eject_one(1);
    }
    if (mmb_fn) {
        free(mmb_fn);
        mmb_fn = NULL;
    }
}

static void reset_one(int drive)
{
    if (sdf_fp[drive] == mmb_fp) {
        mmb_offset[drive][0] = MMB_CAT_SIZE;
        mmb_offset[drive][1] = MMB_CAT_SIZE + 10 * 256 * 80;
    }
}

void mmb_reset(void)
{
    if (mmb_fp) {
        reset_one(0);
        reset_one(1);
    }
}

void mmb_pick(int drive, int disc)
{
    int side;

    switch(drive) {
        case 0:
        case 1:
            side = 0;
            break;
        case 2:
        case 4:
            drive &= 1;
            disc--;
            side = 1;
            break;
        default:
            log_debug("sdf: sdf_mmb_pick: invalid logical drive %d", drive);
            return;
    }
    log_debug("sdf: picking MMB disc, drive=%d, side=%d, disc=%d", drive, side, disc);

    if (sdf_fp[drive] != mmb_fp) {
        disc_close(drive);
        sdf_mount(drive, mmb_fn, mmb_fp, &sdf_geo_tab[SDF_FMT_DFS_10S_SEQ_80T]);
    }
    mmb_offset[drive][side] = MMB_CAT_SIZE + 10 * 256 * 80 * disc;
    if (fdc_spindown)
        fdc_spindown();
}

static inline int cat_name_cmp(const char *nam_ptr, const char *cat_ptr, const char *cat_nxt)
{
    do {
        char cat_ch = *cat_ptr++;
        char nam_ch = *nam_ptr++;
        if (!nam_ch) {
            if (!cat_ch)
                break;
            else
                return -1;
        }
        if ((cat_ch ^ nam_ch) & 0x5f)
            return -1;
    } while (cat_nxt != cat_ptr);
    return (cat_nxt - mmb_cat) / 16 - 2;
}

int mmb_find(const char *name)
{
    const char *cat_ptr = mmb_cat + 16;
    const char *cat_end = mmb_cat + MMB_CAT_SIZE;
    int i;

    do {
        const char *cat_nxt = cat_ptr + 16;
        if ((i = cat_name_cmp(name, cat_ptr, cat_nxt)) >= 0) {
            log_debug("mmb: found MMB SSD '%s' at %d", name, i);
            return i;
        }
        cat_ptr = cat_nxt;
    } while (cat_ptr < cat_end);
    return -1;
}
#endif
