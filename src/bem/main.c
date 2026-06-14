/*B-em v2.2 by Tom Walker
 * Pico version (C) 2021 Graham Sanderson
 *
 * Main loop + start/finish code*/

#include "b-em.h"
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_native_dialog.h>
#include <allegro5/allegro_primitives.h>

#include "6502.h"
#include "adc.h"
#include "model.h"
#include "cmos.h"
#include "config.h"
#include "csw.h"
#include "ddnoise.h"
#include "debugger.h"
#include "disc.h"
#include "fdi.h"
#include "gui-allegro.h"
#include "i8271.h"
#include "ide.h"
#include "joystick.h"
#include "keyboard.h"
#include "keydef-allegro.h"
#include "main.h"
#include "6809tube.h"
#include "mem.h"
#include "mouse.h"
#include "midi.h"
#include "music4000.h"
#include "music5000.h"
#include "pal.h"
#include "savestate.h"
#include "scsi.h"
#include "sdf.h"
#include "serial.h"
#include "sid_b-em.h"
#include "sn76489.h"
#include "sound.h"
#include "sysacia.h"
#include "tape.h"
#include "tapecat-allegro.h"
#include "tapenoise.h"
#include "tube.h"
#include "via.h"
#include "sysvia.h"
#include "uef.h"
#include "uservia.h"
#include "vdfs.h"
#include "video.h"
#include "video_render.h"
#include "wd1770.h"
#include "tube.h"
#include "NS32016/32016.h"
#include "6502tube.h"
#include "65816.h"
#include "arm.h"
#include "x86_tube.h"
#include "z80.h"

#ifdef USE_SECTOR_READ
#include "sector_read.h"
#include "discs/discs.h"
#endif

#if defined(PICO_BUILD)
#undef main
// jump through hoops for lots of main functions
#define main _al_mangled_main
#endif
#undef printf

#if USE_SECTOR_READ
#include "menu.h"
#endif

bool quitting = false;
int autoboot=0;
int joybutton[2];
float joyaxes[4];
#ifndef NO_USE_SET_SPEED
int emuspeed = 4;
#endif

static ALLEGRO_TIMER *timer;
static ALLEGRO_EVENT_QUEUE *queue;
static ALLEGRO_EVENT_SOURCE evsrc;

typedef enum {
    FSPEED_NONE,
    FSPEED_SELECTED,
    FSPEED_RUNNING
} fspeed_type_t;

#ifndef PICO_BUILD
static double time_limit;
static int fcount = 0;
static fspeed_type_t fullspeed = FSPEED_NONE;
#else
#define fullspeed FSPEED_NONE
#endif

#ifndef NO_USE_SET_SPEED
static bool bempause  = false;
const emu_speed_t emu_speeds[NUM_EMU_SPEEDS] = {
    {  "10%", 1.0 / (50.0 * 0.10), 1 },
    {  "25%", 1.0 / (50.0 * 0.25), 1 },
    {  "50%", 1.0 / (50.0 * 0.50), 1 },
    {  "75%", 1.0 / (50.0 * 0.75), 1 },
    { "100%", 1.0 / 50.0,          1 },
    { "150%", 1.0 / (50.0 * 1.50), 2 },
    { "200%", 1.0 / (50.0 * 2.00), 2 },
    { "300%", 1.0 / (50.0 * 3.00), 3 },
    { "400%", 1.0 / (50.0 * 4.00), 4 },
    { "500%", 1.0 / (50.0 * 5.00), 5 }
};
#endif

void main_reset()
{
    m6502_reset();
    crtc_reset();
    video_reset();
    sysvia_reset();
    uservia_reset();
#ifndef NO_USE_ACIA
    serial_reset();
    acia_reset(&sysacia);
#endif
    wd1770_reset();
#ifndef NO_USE_I8271
    i8271_reset();
#endif
#ifndef NO_USE_SCSI
    scsi_reset();
#endif
#ifndef NO_USE_VDFS
    vdfs_reset();
#endif
#ifndef NO_USE_SID
    sid_reset();
#endif
#ifndef NO_USE_MUSIC5000
    music4000_reset();
    music5000_reset();
#endif
    sn_init();
#ifndef NO_USE_TUBE
    if (curtube != -1) tubes[curtube].reset();
    else               tube_exec = NULL;
    tube_reset();
#endif

#if DISPLAY_MENU
    options_init();
#endif
    memset(ram, 0, 64 * 1024);
}

#ifndef NO_USE_CMD_LINE
static const char helptext[] =
    VERSION_STR " command line options:\n\n"
#ifndef PICO_BUILD
    "-mx             - start as model x (see readme.txt for models)\n"
#endif
#ifndef NO_USE_TUBE
    "-tx             - start with tube x (see readme.txt for tubes)\n"
#endif
    "-disc disc.ssd  - load disc.ssd into drives :0/:2\n"
    "-disc1 disc.ssd - load disc.ssd into drives :1/:3\n"
    "-autoboot       - boot disc in drive :0\n"
#ifndef NO_USE_TAPE
    "-tape tape.uef  - load tape.uef\n"
    "-fasttape       - set tape speed to fast\n"
#endif
    "-Fx             - set maximum video frames skipped\n"
    "-s              - scanlines display mode\n"
    "-i              - interlace display mode\n"
#ifndef NO_USE_DEBUGGER
    "-debug          - start debugger\n"
#endif
#ifndef NO_USE_TUBE
    "-debugtube      - start debugging tube processor\n"
#endif
    "\n";
#endif

void main_init(int argc, char *argv[])
{
#ifndef NO_USE_CMD_LINE
    int c;
#ifndef NO_USE_TAPE
    int tapenext = 0;
#endif
    int discnext = 0;
#endif
    ALLEGRO_DISPLAY *display;

    if (!al_init()) {
#ifndef PICO_BUILD
        fputs("Failed to initialise Allegro!\n", stderr);
#endif
        exit(1);
    }

#ifndef NO_USE_ALLEGRO_GUI
    al_init_native_dialog_addon();
    al_set_new_window_title(VERSION_STR);
    al_init_primitives_addon();
#endif

    config_load();
    log_open();
    log_info("main: starting %s", VERSION_STR);

    model_loadcfg();

#ifndef NO_USE_CMD_LINE
    for (c = 1; c < argc; c++) {
        if (!strcasecmp(argv[c], "--help")) {
            fwrite(helptext, sizeof helptext - 1, 1, stdout);
            exit(1);
        }
#ifndef NO_USE_TAPE
        else if (!strcasecmp(argv[c], "-tape"))
            tapenext = 2;
#endif
        else if (!strcasecmp(argv[c], "-disc") || !strcasecmp(argv[c], "-disk"))
            discnext = 1;
        else if (!strcasecmp(argv[c], "-disc1"))
            discnext = 2;
#ifndef PICO_BUILD
        else if (argv[c][0] == '-' && (argv[c][1] == 'm' || argv[c][1] == 'M')) {
            int tmp;
            sscanf(&argv[c][2], "%i", &tmp);
            curmodel = tmp;
        }
#endif
#ifndef NO_USE_TUBE
        else if (argv[c][0] == '-' && (argv[c][1] == 't' || argv[c][1] == 'T')) {
            int tmp;
            sscanf(&argv[c][2], "%i", &tmp);
            curtube = tmp;
        }
#endif
#ifndef NO_USE_TAPE
        else if (!strcasecmp(argv[c], "-fasttape"))
            fasttape = true;
#endif
        else if (!strcasecmp(argv[c], "-autoboot"))
            autoboot = 150;
#ifndef NO_USE_ALLEGRO_GUI
        else if (argv[c][0] == '-' && (argv[c][1] == 'f' || argv[c][1]=='F')) {
            sscanf(&argv[c][2], "%i", &vid_fskipmax);
            if (vid_fskipmax < 1) vid_fskipmax = 1;
            if (vid_fskipmax > 9) vid_fskipmax = 9;
        }
#endif
        else if (argv[c][0] == '-' && (argv[c][1] == 's' || argv[c][1] == 'S'))
            vid_dtype_user = VDT_SCANLINES;
#ifndef NO_USE_DEBUGGER
        else if (!strcasecmp(argv[c], "-debug"))
            debug_core = 1;
#ifndef NO_USE_TUBE
        else if (!strcasecmp(argv[c], "-debugtube"))
            debug_tube = 1;
#endif
#endif
        else if (argv[c][0] == '-' && (argv[c][1] == 'i' || argv[c][1] == 'I'))
            vid_dtype_user = VDT_INTERLACE;
#ifndef NO_USE_TAPE
        else if (tapenext) {
            if (tape_fn)
                al_destroy_path(tape_fn);
            tape_fn = al_create_path(argv[c]);
        }
#endif
        else if (discnext) {
            if (discfns[discnext-1])
                al_destroy_path(discfns[discnext-1]);
            discfns[discnext-1] = al_create_path(argv[c]);
            discnext = 0;
        }
        else {
            ALLEGRO_PATH *path = al_create_path(argv[c]);
            const char *ext = al_get_path_extension(path);
#ifndef NO_USE_SAVE_STATE
            if (ext && !strcasecmp(ext, ".snp")) {
                savestate_load(argv[c]);
            }
#else
            if (false) {

            }
#endif
#ifndef NO_USE_TAPE
            else if (ext && (!strcasecmp(ext, ".uef") || !strcasecmp(ext, ".csw"))) {
                if (tape_fn)
                    al_destroy_path(tape_fn);
                tape_fn = path;
                tapenext = 0;
            }
#endif
            else {
                if (discfns[0])
                    al_destroy_path(discfns[0]);
                discfns[0] = path;
                discnext = 0;
                autoboot = 150;
            }
        }
#ifndef NO_USE_TAPE
        if (tapenext) tapenext--;
#endif
    }
#endif

    display = video_init();
#ifndef PICO_BUILD
    mode7_makechars();
#endif
#ifndef NO_USE_ALLEGRO_GUI
    al_init_image_addon();
#endif

    mem_init();

    if (!(queue = al_create_event_queue())) {
        log_fatal("main: unable to create event queue");
        exit(1);
    }
    al_register_event_source(queue, al_get_display_event_source(display));

    if (!al_install_audio()) {
        log_fatal("main: unable to initialise audio");
        exit(1);
    }
    if (!al_reserve_samples(3)) {
        log_fatal("main: unable to reserve audio samples");
        exit(1);
    }
    if (!al_init_acodec_addon()) {
        log_fatal("main: unable to initialise audio codecs");
        exit(1);
    }

    sound_init();
#ifndef NO_USE_SID
    sid_init();
    sid_settype(sidmethod, cursid);
#endif
#ifndef NO_USE_MUSIC5000
    music5000_init(queue);
#endif
#ifndef NO_USE_DD_NOISE
    ddnoise_init();
#endif
#ifndef NO_USE_TAPE
    tapenoise_init(queue);
#endif

#ifndef NO_USE_ADC
    adc_init();
#endif
#ifndef NO_USE_PAL
    pal_init();
#endif
    disc_init();
#ifndef NO_USE_FDI
    fdi_init();
#endif
#ifndef NO_USE_SCSI
    scsi_init();
#endif
#ifndef NO_USE_IDE
    ide_init();
#endif
#ifndef NO_USE_VDFS
    vdfs_init();
#endif

    model_init();

    midi_init();
    main_reset();

#ifndef NO_USE_JOYSTICK
    joystick_init(queue);
#endif

#ifndef NO_USE_ALLEGRO_GUI
    gui_allegro_init(queue, display);
#endif

#ifndef PICO_BUILD
    time_limit = 2.0 / 50.0;
    if (!(timer = al_create_timer(1.0 / 50.0))) {
        log_fatal("main: unable to create timer");
        exit(1);
    }
#endif
    al_register_event_source(queue, al_get_timer_event_source(timer));
    al_init_user_event_source(&evsrc);
    al_register_event_source(queue, &evsrc);

    if (!al_install_keyboard()) {
        log_fatal("main: unable to install keyboard");
        exit(1);
    }
    al_register_event_source(queue, al_get_keyboard_event_source());

    oldmodel = curmodel;

#ifndef NO_USE_MOUSE
    al_install_mouse();
    al_register_event_source(queue, al_get_mouse_event_source());
#endif
#if !defined(USE_SECTOR_READ)
#ifndef NO_USE_MMB
    if (mmb_fn)
        mmb_load(mmb_fn);
    else
        disc_load(0, discfns[0]);
#else
    disc_load(0, discfns[0]);
#endif
    disc_load(1, discfns[1]);
#else
#ifndef NO_USE_CMD_LINE
    disc_load(0, discfns[0]);
#endif
    // noop dic should be loaded by menu init
#endif
#ifndef NO_USE_TAPE
    tape_load(tape_fn);
#endif
#ifndef NO_USE_DISC_WRITE
    if (defaultwriteprot)
        writeprot[0] = writeprot[1] = 1;
    if (discfns[0])
        gui_set_disc_wprot(0, writeprot[0]);
    if (discfns[1])
        gui_set_disc_wprot(1, writeprot[1]);
#endif
#ifndef NO_USE_DEBUGGER
    debug_start();
#endif
}

void main_restart()
{
    main_pause();

#ifndef NO_USE_CMOS_SAVE
    cmos_save(models[oldmodel]);
#endif

    model_init();
    main_reset();
    main_resume();
}

int resetting = 0;
int framesrun = 0;

#ifndef PICO_BUILD
void main_cleardrawit()
{
    fcount = 0;
}
#endif

#ifndef NO_USE_SET_SPEED
static void main_start_fullspeed(void)
{
    ALLEGRO_EVENT event;

    log_debug("main: starting full-speed");
    al_stop_timer(timer);
    fullspeed = FSPEED_RUNNING;
    event.type = ALLEGRO_EVENT_TIMER;
    al_emit_user_event(&evsrc, &event, NULL);
}
#endif

void main_key_down(ALLEGRO_EVENT *event)
{
    int code = key_map(event);

    log_debug("main: key down, code=%d, fullspeed=%d", event->keyboard.keycode, fullspeed);

    switch(code) {
#ifndef NO_USE_SET_SPEED
        case ALLEGRO_KEY_PGUP:
            if (fullspeed != FSPEED_RUNNING)
                main_start_fullspeed();
            break;
#endif
        case ALLEGRO_KEY_NUMLOCK: // numlock clear
            nula_default_palette();
            main_restart();
            break;
#ifndef NO_USE_SET_SPEED
        case ALLEGRO_KEY_PGDN:
            if (bempause) {
                if (emuspeed != EMU_SPEED_PAUSED) {
                    bempause = false;
                    if (emuspeed != EMU_SPEED_FULL)
                        al_start_timer(timer);
                }
            } else {
                al_stop_timer(timer);
                bempause = true;
            }
#endif
#ifndef PICO_BUILD
        case ALLEGRO_KEY_ENTER: {
            ALLEGRO_KEYBOARD_STATE kstate;
            al_get_keyboard_state(&kstate);
            if (al_key_down(&kstate, ALLEGRO_KEY_ALT)) {
                video_toggle_fullscreen();
                return;
            }
            break;
        }
#endif
#ifndef NO_USE_DEBUGGER
        case ALLEGRO_KEY_F10:
            if (debug_core || debug_tube)
                debug_step = 1;
            break;
#endif
        case ALLEGRO_KEY_F12:
        case ALLEGRO_KEY_PRINTSCREEN:
            m6502_reset();
            video_reset();
#ifndef NO_USE_I8271
            i8271_reset();
#endif
            wd1770_reset();
#ifndef NO_USE_SID
            sid_reset();
#endif
#ifndef NO_USE_MUSIC5000
            music5000_reset();
#endif
#ifndef NO_USE_TUBE
            if (curtube != -1)
                tubes[curtube].reset();
            tube_reset();
#endif
            break;
        default:
#ifndef NO_USE_SET_SPEED
            if (fullspeed == FSPEED_RUNNING) {
                fullspeed = FSPEED_SELECTED;
                if (emuspeed != EMU_SPEED_PAUSED)
                    al_start_timer(timer);
            }
#endif
            break;
    }
    key_down(code);
}

void main_key_up(ALLEGRO_EVENT *event)
{
    int code = key_map(event);

    log_debug("main: key up, code=%d, fullspeed=%d", event->keyboard.keycode, fullspeed);

#ifndef NO_USE_SET_SPEED
    ALLEGRO_KEYBOARD_STATE kstate;
    switch(code) {
        case ALLEGRO_KEY_PGUP:
            if (emuspeed != EMU_SPEED_FULL) {
                al_get_keyboard_state(&kstate);
                if (!al_key_down(&kstate, ALLEGRO_KEY_LSHIFT) && !al_key_down(&kstate, ALLEGRO_KEY_RSHIFT)) {
                    log_debug("main: stopping fullspeed (PgUp)");
                    if (fullspeed == FSPEED_RUNNING && emuspeed != EMU_SPEED_PAUSED)
                        al_start_timer(timer);
                    fullspeed = FSPEED_NONE;
                }
                else
                    fullspeed = FSPEED_SELECTED;
            }
            break;
    }
    if (fullspeed == FSPEED_SELECTED)
        main_start_fullspeed();
#endif
    key_up(code);
}

#ifndef PICO_BUILD
static bool event_delay_ok(ALLEGRO_EVENT *event) {
    double delay = al_get_time() - event->any.timestamp;
    return (delay < time_limit);
}
#else
static bool event_delay_ok(ALLEGRO_EVENT *event) {
    return true;
}
#endif

static void main_timer(ALLEGRO_EVENT *event)
{
    if (event_delay_ok(event)) {
        if (autoboot)
            autoboot--;
        framesrun++;

        if (x65c02)
            m65c02_exec();
        else
            m6502_exec();

#ifndef NO_USE_DD_NOISE
        if (ddnoise_ticks > 0 && --ddnoise_ticks == 0)
            ddnoise_headdown();
#endif
#ifndef NO_USE_SAVE_STATE
        if (savestate_wantload)
            savestate_doload();
        if (savestate_wantsave)
            savestate_dosave();
#endif
        if (fullspeed == FSPEED_RUNNING)
            al_emit_user_event(&evsrc, event, NULL);
    }
}

void main_run()
{
    ALLEGRO_EVENT event;

    log_debug("main: about to start timer");
    al_start_timer(timer);

    log_debug("main: entering main loop");
    while (!quitting) {
        al_wait_for_event(queue, &event);
        switch(event.type) {
            case ALLEGRO_EVENT_KEY_DOWN:
                main_key_down(&event);
                break;
            case ALLEGRO_EVENT_KEY_UP:
                main_key_up(&event);
                break;
#ifndef NO_USE_MOUSE
            case ALLEGRO_EVENT_MOUSE_AXES:
                mouse_axes(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
                log_debug("main: mouse button down");
                mouse_btn_down(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_BUTTON_UP:
                log_debug("main: mouse button up");
                mouse_btn_up(&event);
                break;
#endif
#ifndef NO_USE_JOYSTICK
            case ALLEGRO_EVENT_JOYSTICK_AXIS:
                joystick_axis(&event);
                break;
            case ALLEGRO_EVENT_JOYSTICK_BUTTON_DOWN:
                joystick_button_down(&event);
                break;
            case ALLEGRO_EVENT_JOYSTICK_BUTTON_UP:
                joystick_button_up(&event);
                break;
#endif
            case ALLEGRO_EVENT_DISPLAY_CLOSE:
                log_debug("main: event display close - quitting");
                quitting = true;
                break;
            case ALLEGRO_EVENT_TIMER:
                main_timer(&event);
                break;
#ifndef NO_USE_ALLEGRO_GUI
            case ALLEGRO_EVENT_MENU_CLICK:
                main_pause();
                gui_allegro_event(&event);
                main_resume();
                break;
#endif
#ifndef NO_USE_MUSIC5000
            case ALLEGRO_EVENT_AUDIO_STREAM_FRAGMENT:
                music5000_streamfrag();
                break;
#endif
            case ALLEGRO_EVENT_DISPLAY_RESIZE:
                video_update_window_size(&event);
                break;
        }
    }
    log_debug("main: end loop");
}

void main_close()
{
#ifndef NO_USE_CLOSE

#ifndef NO_USE_ALLEGRO_GUI
    gui_tapecat_close();
    gui_keydefine_close();
#endif

#ifndef NO_USE_DEBUGGER
    debug_kill();
#endif

    config_save();
    cmos_save(models[curmodel]);

    midi_close();
    mem_close();
#ifndef NO_USE_UEF
    uef_close();
#endif
#ifndef NO_USE_CSW
    csw_close();
#endif
#ifndef NO_USE_TUBE
    tube_6502_close();
    arm_close();
    x86_close();
    z80_close();
    w65816_close();
    n32016_close();
    mc6809nc_close();
#endif
    disc_close(0);
    disc_close(1);
#ifndef NO_USE_SCSI
    scsi_close();
#endif
#ifndef NO_USE_IDE
    ide_close();
#endif
#ifndef NO_USE_VDFS
    vdfs_close();
#endif
#ifndef NO_USE_MUSIC5000
    music5000_close();
#endif
    ddnoise_close();
#ifndef NO_USE_TAPE
    tapenoise_close();
#endif

    video_close();
    log_close();
#endif
}

#ifndef NO_USE_SET_SPEED
void main_setspeed(int speed)
{
    log_debug("main: setspeed %d", speed);
    if (speed == EMU_SPEED_FULL)
        main_start_fullspeed();
    else {
        al_stop_timer(timer);
        fullspeed = FSPEED_NONE;
        if (speed != EMU_SPEED_PAUSED) {
            if (speed >= NUM_EMU_SPEEDS) {
                log_warn("main: speed #%d out of range, defaulting to 100%%", speed);
                speed = 4;
            }
            al_set_timer_speed(timer, emu_speeds[speed].timer_interval);
            time_limit = emu_speeds[speed].timer_interval * 2.0;
            vid_fskipmax = emu_speeds[speed].fskipmax;
            log_debug("main: new speed#%d, timer interval=%g, vid_fskipmax=%d", speed, emu_speeds[speed].timer_interval, vid_fskipmax);
            al_start_timer(timer);
        }
    }
    emuspeed = speed;
}
#endif

void main_pause(void)
{
    al_stop_timer(timer);
}

void main_resume(void)
{
#ifndef NO_USE_SET_SPEED
    if (emuspeed != EMU_SPEED_PAUSED && emuspeed != EMU_SPEED_FULL)
        al_start_timer(timer);
#endif
}

void main_setquit(void)
{
    quitting = 1;
}

int main(int argc, char **argv)
{
    main_init(argc, argv);
//    key_down(215); // LSHIFT
    main_run();
    main_close();
    return 0;
}
