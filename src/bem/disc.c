/*B-em v2.2 by Tom Walker
  * Pico version (C) 2021 Graham Sanderson
  *
  * Disc support*/

#include "b-em.h"
#include "gui-allegro.h"
#include "fdi.h"
#include "disc.h"
#include "sdf.h"

#include "ddnoise.h"

DRIVE drives[2];

int8_t curdrive = 0;

ALLEGRO_PATH *discfns[2] = { NULL, NULL };
#ifndef NO_USE_DISC_WRITE
bool defaultwriteprot = false;
int writeprot[NUM_DRIVES], fwriteprot[NUM_DRIVES];
#endif

#ifndef USE_HW_EVENT
int fdc_time;
int disc_time;
#endif

int motorspin;
bool motoron;

void (*fdc_callback)();
void (*fdc_data)(uint8_t dat);
void (*fdc_spindown)();
void (*fdc_finishread)();
void (*fdc_notfound)();
void (*fdc_datacrcerror)();
void (*fdc_headercrcerror)();
void (*fdc_writeprotect)();
int  (*fdc_getdata)(int last);

#ifndef USE_SECTOR_READ
void disc_load(int drive, ALLEGRO_PATH *fn)
{
    const char *ext;
    const char *cpath;

    if (!fn)
        return;
    gui_allegro_set_eject_text(drive, fn);
    cpath = al_path_cstr(fn, ALLEGRO_NATIVE_PATH_SEP);
    if ((ext = al_get_path_extension(fn))) {
        if (*ext == '.')
            ext++;
#ifndef NO_USE_FDI
        if (strcasecmp(ext, "fdi") == 0) {
            log_debug("Loading %i: %s as FDI", drive, cpath);
            fdi_load(drive, cpath);
            return;
        }
#endif
    }
    sdf_load(drive, cpath, ext);
}
#elif !defined(NO_USE_CMD_LINE)
#include "discs/discs.h"
#include "menu.h"
void disc_load(int drive, ALLEGRO_PATH *fn)
{
    const char *ext;
    const char *cpath;

    if (!fn)
        return;
    cpath = al_path_cstr(fn, ALLEGRO_NATIVE_PATH_SEP);
    if ((ext = al_get_path_extension(fn))) {
        if (*ext == '.')
            ext++;
#ifndef NO_USE_FDI
        if (strcasecmp(ext, "fdi") == 0) {
            log_debug("Unsupported: FDI: %i: %s", drive, cpath);
            return;
        }
#endif
    }

    // todo a bit of a hack to inline here - just do it for now
    FILE *fp;
    const struct sdf_geometry *geo;

    if ((fp = fopen(cpath, "rb+")) == NULL) {
        if ((fp = fopen(cpath, "rb")) == NULL) {
            log_error("Unable to open file '%s' for reading - %s", cpath, strerror(errno));
            return;
        }
    }
    if ((geo = sdf_find_geo(cpath, ext, fp))) {
        fseek(fp, 0, SEEK_END);
        uint size = (uint)ftell(fp);
        uint8_t *data  = malloc(size);
        fseek(fp, 0, SEEK_SET);
        if (1 != fread(data, size, 1, fp)) {
            log_error("sdf: drive %d: unable to determine geometry for %s", drive, cpath);
            return;
        }
        cmd_line_disc.data = data;
        cmd_line_disc.geometry = geo;
        cmd_line_disc.name = "<cmd line>";
        cmd_line_disc.data_size = size; // mark as valid
        select_cmd_line_disc();
    } else {
        log_error("sdf: drive %d: unable to determine geometry for %s", drive, cpath);
    }
    fclose(fp);
}
#endif

void disc_close(int drive)
{
        if (drives[drive].close)
            drives[drive].close(drive);
        // Force the drive to spin down (i.e. become not-ready) when the disk is unloaded
        // This prevents the file system (e.g DFS) caching the old disk catalogue
        if (fdc_spindown)
            fdc_spindown();

}


#ifndef USE_HW_EVENT
static int disc_notfound=0;
void set_disc_notfound(int cycles16) { disc_notfound = cycles16; }
#else

bool invoke_disc_notfound(struct hw_event *event) {
    fdc_notfound();
    return false;
}

static struct hw_event disc_notfound_event = {
        .invoke = invoke_disc_notfound
};

void set_disc_notfound(int cycles16) {
    set_simple_hw_event_counter(&disc_notfound_event, cycles16 * 16);
}

bool __time_critical_func(invoke_motorspin)(struct hw_event *event) {
    fdc_spindown();
    return false;
}

static struct hw_event motorspin_event = {
        .invoke = invoke_motorspin
};

void set_motorspin(int other_cycles) {
    set_simple_hw_event_counter(&motorspin_event, other_cycles * 128);
}

bool __time_critical_func(invoke_fdc)(struct hw_event *event) {
    // todo note this doesn't quite preserve the existing behavior which would be that if motoron
    //  was off the count would cease, and potentially resume again later... i don't think I care
    //  enough to see if every case that turns fdc_time on again is gated by motoron anyway, which
    //  would make the point moot.
    if (motoron) fdc_callback();
    return false;
}

static struct hw_event fdc_event = {
        .invoke = invoke_fdc
};

void __time_critical_func(set_fdc_time)(int fdc_time) {
    set_simple_hw_event_counter(&fdc_event, fdc_time);
}

#endif

void disc_init()
{
        drives[0].poll = drives[1].poll = 0;
        drives[0].seek = drives[1].seek = 0;
        drives[0].readsector = drives[1].readsector = 0;
        curdrive = 0;
}

#ifndef USE_HW_EVENT
void __time_critical_func(disc_poll)()
{
        if (drives[curdrive].poll) drives[curdrive].poll();
        if (disc_notfound)
        {
                disc_notfound--;
                if (!disc_notfound)
                   fdc_notfound();
        }
}
void disc_cycle_sync(int drive) {}
#else
void __time_critical_func(disc_cycle_sync)(int drive) {
    if (drives[drive].cycle_sync) {
        drives[drive].cycle_sync(drive);
    }
}
#endif

int oldtrack[2] = {0, 0};
void __time_critical_func(disc_seek)(int drive, int track)
{
        if (drives[drive].seek)
            drives[drive].seek(drive, track);
        ddnoise_seek(track - oldtrack[drive]);
        oldtrack[drive] = track;
}

void __time_critical_func(disc_readsector)(int drive, int sector, int track, int side, int density)
{
        if (drives[drive].readsector) {
            autoboot = 0;
            drives[drive].readsector(drive, sector, track, side, density);
        }
        else
           set_disc_notfound(10000);
}

void disc_writesector(int drive, int sector, int track, int side, int density)
{
        if (drives[drive].writesector)
           drives[drive].writesector(drive, sector, track, side, density);
        else
           set_disc_notfound(10000);
}

void disc_readaddress(int drive, int track, int side, int density)
{
        if (drives[drive].readaddress)
           drives[drive].readaddress(drive, track, side, density);
        else
           set_disc_notfound(10000);
}

void disc_format(int drive, int track, int side, int density)
{
        if (drives[drive].format)
           drives[drive].format(drive, track, side, density);
        else
           set_disc_notfound(10000);
}

void disc_abort(int drive)
{
        if (drives[drive].abort)
           drives[drive].abort(drive);
        else
           set_disc_notfound(10000);
}

int disc_verify(int drive, int track, int density)
{
        if (drives[drive].verify)
            return drives[drive].verify(drive, track, density);
        else
            return 1;
}
