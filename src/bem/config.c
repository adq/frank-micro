/*B-em v2.2 by Tom Walker
 * Pico version (C) 2021 Graham Sanderson
 *
 * Configuration handling*/

#include "b-em.h"

#include "config.h"
#include "ddnoise.h"
#include "disc.h"
#include "keyboard.h"
#include "model.h"
#include "mouse.h"
#include "ide.h"
#include "midi.h"
#include "scsi.h"
#include "sdf.h"
#include "sn76489.h"
#include "sound.h"
#include "tape.h"
#include "tube.h"
#include "vdfs.h"
#include "video_render.h"

int8_t curmodel;
#ifndef NO_USE_TUBE
int8_t selecttube = -1;
#endif
int cursid = 0;
int sidmethod = 0;

ALLEGRO_CONFIG *bem_cfg;

int get_config_int(const char *sect, const char *key, int ival)
{
    const char *str;

    if (bem_cfg) {
        if ((str = al_get_config_value(bem_cfg, sect, key)))
            ival = atoi(str);
        else if (sect && (str = al_get_config_value(bem_cfg, NULL, key))) {
            ival = atoi(str);
            al_remove_config_key(bem_cfg, "", key);
        }
    }
    return ival;
}

static bool parse_bool(const char *value)
{
    return strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || atoi(value) > 0;
}

bool get_config_bool(const char *sect, const char *key, bool bval)
{
    const char *str;

    if (bem_cfg) {
        if ((str = al_get_config_value(bem_cfg, sect, key)))
            bval = parse_bool(str);
        else if (sect && (str = al_get_config_value(bem_cfg, NULL, key))) {
            bval = parse_bool(str);
            al_remove_config_key(bem_cfg, "", key);
        }
    }
    return bval;
}

const char *get_config_string(const char *sect, const char *key, const char *sval)
{
    const char *str;

    if (bem_cfg) {
        if ((str = al_get_config_value(bem_cfg, sect, key)))
            sval = str;
        else if (sect && (str = al_get_config_value(bem_cfg, NULL, key))) {
            al_set_config_value(bem_cfg, sect, key, str);
            al_remove_config_key(bem_cfg, "", key);
            sval = al_get_config_value(bem_cfg, sect, key);
        }
    }
    return sval;
}

void config_load(void)
{
    int c;
    const char *p;
#ifndef PICO_BUILD
    const char *cpath;
    char s[16];
    ALLEGRO_PATH *path;

    if ((path = find_cfg_file("b-em", ".cfg"))) {
        cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        if (bem_cfg)
            al_destroy_config(bem_cfg);
        if (!(bem_cfg = al_load_config_file(cpath)))
            log_warn("config: unable to load config file '%s', using defaults", cpath);
        al_destroy_path(path);
    } else
        log_warn("config: no config file found, using defaults");

#else
    bem_cfg = al_create_config(); // dummy config
#endif
    if (bem_cfg) {
        if ((p = get_config_string("disc", "disc0", NULL))) {
            if (discfns[0])
                al_destroy_path(discfns[0]);
            discfns[0] = al_create_path(p);
        }
        if ((p = get_config_string("disc", "disc1", NULL))) {
            if (discfns[1])
                al_destroy_path(discfns[1]);
            discfns[1] = al_create_path(p);
        }
#ifndef NO_USE_MMB
        if ((p = get_config_string("disc", "mmb", NULL))) {
            if (mmb_fn)
                free(mmb_fn);
            mmb_fn = strdup(p);
        }
#endif
#ifndef NO_USE_TAPE
        if ((p = get_config_string("tape", "tape", NULL))) {
            if (tape_fn)
                al_destroy_path(tape_fn);
            tape_fn = al_create_path(p);
        }
#endif
        al_remove_config_key(bem_cfg, "", "video_resize");
        al_remove_config_key(bem_cfg, "", "tube6502speed");
    }

#ifndef NO_USE_DISC_WRITE
    defaultwriteprot = get_config_bool("disc", "defaultwriteprotect", 1);
#endif

    curmodel         = get_config_int(NULL, "model",         3);
#ifndef NO_USE_TUBE
    selecttube       = get_config_int(NULL, "tube",         -1);
    tube_speed_num   = get_config_int(NULL, "tubespeed",     0);
#endif

    sound_internal   = get_config_bool("sound", "sndinternal",   true);
#ifndef NO_USE_SID
    sound_beebsid    = get_config_bool("sound", "sndbeebsid",    true);
#endif
#ifndef NO_USE_MUSIC5000
    sound_music5000  = get_config_bool("sound", "sndmusic5000",  false);
#endif
    sound_dac        = get_config_bool("sound", "snddac",        false);
    sound_ddnoise    = get_config_bool("sound", "sndddnoise",    true);
#ifndef NO_USE_TAPE
    sound_tape       = get_config_bool("sound", "sndtape",       false);
#endif
#ifndef PICO_BUILD
    sound_filter     = get_config_bool("sound", "soundfilter",   true);

    curwave          = get_config_int("sound", "soundwave",     0);
    sidmethod        = get_config_int("sound", "sidmethod",     0);
    cursid           = get_config_int("sound", "cursid",        2);
#endif

#ifndef NO_USE_DD_NOISE
    ddnoise_vol      = get_config_int("sound", "ddvol",         2);
    ddnoise_type     = get_config_int("sound", "ddtype",        0);
#endif

#ifndef PICO_BUILD
    vid_fullborders  = get_config_int("video", "fullborders",   1);
    c                = get_config_int("video", "displaymode",   0);
#else
    c                = get_config_int("video", "displaymode",   0); // thought I wanted 1 for interlace - but actually that makes stuff like elite flicker - very little useful uses interlace out of mode 7.
#endif
    if (c >= 4) {
        c -= 4;
        vid_pal = 1;
    }
    video_set_disptype(c);

#ifndef NO_USE_TAPE
    fasttape         = get_config_bool("tape", "fasttape",      0);
#endif
#ifndef NO_USE_SCSI
    scsi_enabled     = get_config_bool("disc", "scsienable", 0);
#endif
#ifndef NO_USE_IDE
    ide_enable       = get_config_bool("disc", "ideenable",     0);
#endif
#ifndef NO_USE_VDFS
    vdfs_enabled     = get_config_bool("disc", "vdfsenable", 0);
#endif

    keyas            = get_config_bool(NULL, "key_as",        0);
#ifndef NO_USE_MOUSE
    mouse_amx        = get_config_bool(NULL, "mouse_amx",     0);
#endif
    kbdips           = get_config_int(NULL, "kbdips", 0);

#ifndef NO_USE_MUSIC5000
    buflen_m5        = get_config_int("sound", "buflen_music5000", BUFLEN_M5);
#endif

#ifndef NO_USE_KEY_LOOKUP
    for (c = 0; c < ALLEGRO_KEY_MAX; c++) {
        sprintf(s, "key_define_%03i", c);
        keylookup[c] = get_config_int("user_keyboard", s, c);
    }
    midi_load_config();
#endif
}

#ifndef NO_USE_WRITABLE_CONFIG
void set_config_int(const char *sect, const char *key, int value)
{
    char buf[10];

    snprintf(buf, sizeof buf, "%d", value);
    al_set_config_value(bem_cfg, sect, key, buf);
}

void set_config_bool(const char *sect, const char *key, bool value)
{
    al_set_config_value(bem_cfg, sect, key, value ? "true" : "false");
}

void set_config_string(const char *sect, const char *key, const char *value)
{
    if (value && *value)
        al_set_config_value(bem_cfg, sect, key, value);
    else
        al_remove_config_key(bem_cfg, sect, key);
}

static void set_config_path(const char *sect, const char *key, ALLEGRO_PATH *path)
{
    if (path)
        al_set_config_value(bem_cfg, sect, key, al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP));
    else
        al_remove_config_key(bem_cfg, sect, key);
}

void config_save(void)
{
    ALLEGRO_PATH *path;
    const char *cpath;
    char t[20];
    int c;

    if ((path = find_cfg_dest("b-em", ".cfg"))) {
        cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        if (!bem_cfg) {
            if (!(bem_cfg = al_create_config())) {
                log_error("config: unable to save configuration");
                al_destroy_path(path);
                return;
            }
        }
        model_savecfg();

        set_config_path("disc", "disc0", discfns[0]);
        set_config_path("disc", "disc1", discfns[1]);
#ifndef NO_USE_MMB
        set_config_string("disc", "mmb", mmb_fn);
#endif
#ifndef NO_USE_DISC_WRITE
        set_config_bool("disc", "defaultwriteprotect", defaultwriteprot);
#endif

#ifndef NO_USE_TAPE
        if (tape_loaded)
            al_set_config_value(bem_cfg, "tape", "tape", al_path_cstr(tape_fn, ALLEGRO_NATIVE_PATH_SEP));
        else
            al_remove_config_key(bem_cfg, "tape", "tape");
#endif

        set_config_int(NULL, "model", curmodel);
#ifndef NO_USE_TUBE
        set_config_int(NULL, "tube", selecttube);
        set_config_int(NULL, "tubespeed", tube_speed_num);
#endif

        set_config_bool("sound", "sndinternal", sound_internal);
        set_config_bool("sound", "sndbeebsid",  sound_beebsid);
        set_config_bool("sound", "sndmusic5000",sound_music5000);
        set_config_bool("sound", "snddac",      sound_dac);
        set_config_bool("sound", "sndddnoise",  sound_ddnoise);
        set_config_bool("sound", "sndtape",     sound_tape);
        set_config_bool("sound", "soundfilter", sound_filter);

        set_config_int("sound", "soundwave", curwave);
        set_config_int("sound", "sidmethod", sidmethod);
        set_config_int("sound", "cursid", cursid);
        set_config_int("sound", "buflen_music5000", buflen_m5);

#ifndef NO_USE_DD_NOISE
        set_config_int("sound", "ddvol", ddnoise_vol);
        set_config_int("sound", "ddtype", ddnoise_type);
#endif

        set_config_int("video", "fullborders", vid_fullborders);
        c = vid_dtype_user;
        if (vid_pal)
            c += 4;
        set_config_int("video", "displaymode", c);

#ifndef NO_USE_TAPE
        set_config_bool("tape", "fasttape", fasttape);
#endif
#ifndef NO_USE_SCSI
        set_config_bool("disc", "scsienable", scsi_enabled);
#endif
#ifndef NO_USE_IDE
        set_config_bool("disc", "ideenable", ide_enable);
#endif
#ifndef NO_USE_VDFS
        set_config_bool("disc", "vdfsenable", vdfs_enabled);
#endif

        set_config_bool(NULL, "key_as", keyas);

#ifndef NO_USE_MOUSE
        set_config_bool(NULL, "mouse_amx", mouse_amx);
#endif

#ifndef NO_USE_KEY_LOOKUP
        for (c = 0; c < 128; c++) {
            snprintf(t, sizeof t, "key_define_%03i", c);
            if (keylookup[c] == c)
                al_remove_config_key(bem_cfg, "user_keyboard", t);
            else
                set_config_int("user_keyboard", t, keylookup[c]);
        }
#endif
        midi_save_config();
        log_debug("config: saving config to %s", cpath);
        al_save_config_file(cpath, bem_cfg);
        al_destroy_path(path);
    } else
        log_error("config: no suitable destination for config file - config will not be saved");
}
#endif
