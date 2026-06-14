/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#include "x_gui.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/time.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdlib>
#include <cstdarg>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "tsqueue/tsqueue.h"
#include "shader.h"
#include "egl_texture.h"
#include <optional>
#include <sys/time.h>
#include "menu.h"
#include <alsa/asoundlib.h>
#include "font.h"

#define NUM_TEXTURES 4

#define ALSA_DEVICE "default"

#if FORCE_44100_OUTPUT
#define HACK_44100 44100
// for now this requires the buffer copy theat force 44100 does

// ntoe this was designed to help pi zero, but really doesn't... and it makes all other ones worse
//#define SEPARATE_ALSA_THREAD 1
#endif

static const rgb menu_text_color = {26 / 31.0f, 26 / 31.0f, 30 / 31.0f};
static const rgb menu_highlight_fg_color = {1.0, 1.0, 30 / 31.0f};

// hopefully this is sufficient - buy maybe we have triple buffering?
#define VIEWPORT_CHANGE_FRAMES 2

static int viewport_changed;
static bool force_aspect_ratio;

static inline rgb blend(const rgb &a, const rgb &b, float c) {
    return {
            a.r() + (b.r() - a.r()) * c,
            a.g() + (b.g() - a.g()) * c,
            a.b() + (b.b() - a.b()) * c,
    };
}

static Tsqueue<std::shared_ptr<egl_texture>> empty_textures(NUM_TEXTURES);
struct displayable_frame {
    std::shared_ptr<egl_texture> tex;
    bool double_y, half_line;
    struct menu_state ms;
};

static Tsqueue<displayable_frame> displayable_frames(NUM_TEXTURES);
static uint scanline_number; // 0-255 always

void (*platform_key_down)(int scancode, int keysym, int modifiers);
void (*platform_key_up)(int scancode, int keysym, int modifiers);
void (*platform_mouse_move)(int dx, int dy);
void (*platform_mouse_button_down)(int button);
void (*platform_mouse_button_up)(int button);
void (*platform_quit)();

using std::thread;

font menu_font;

#if SEPARATE_ALSA_THREAD
#define ALSA_QUEUE_LENGTH 3
Tsqueue<std::pair<const uint8_t *, uint>> alsa_queue(ALSA_QUEUE_LENGTH); // single for now
#endif
std::mutex cpu_event_mutex;
std::condition_variable cpu_condition;
uint cpu_event_states;

void multicore_launch_core1(void (*entry)()) {
    thread core1([entry] {
        entry();
    });
    core1.detach();
}

void __sev() {
    std::unique_lock<std::mutex> lock(cpu_event_mutex);
    cpu_event_states = (1u << NUM_CORES) - 1;
    cpu_condition.notify_all();
}

void __wfe() {
    std::unique_lock<std::mutex> lock(cpu_event_mutex);
    uint32_t bit = 1 << get_core_num();
    while (!(cpu_event_states & bit)) {
        cpu_condition.wait(lock);
    }
    cpu_event_states &= ~bit;
}

struct _spin_lock_t {
    std::atomic_bool lock;
};

static spin_lock_t spin_locks[NUM_SPIN_LOCKS];

spin_lock_t *spin_lock_addr(uint lock_num) {
    assert(lock_num >= 0 && lock_num < NUM_SPIN_LOCKS);
    return spin_locks + lock_num;
}

void unprotected_spin_lock(spin_lock_t *lock) {
    while (lock->lock.exchange(true));
    atomic_thread_fence(std::memory_order_acquire);
}

void unprotected_spin_unlock(spin_lock_t *lock) {
    std::atomic_thread_fence(std::memory_order_release);
    assert(lock->lock);
    lock->lock.exchange(false);
}

bool is_spin_locked(const spin_lock_t *lock) {
    return lock->lock;
}

uint32_t safe_spin_lock(spin_lock_t *lock) {
    uint32_t save = save_and_disable_interrupts();
    unprotected_spin_lock(lock);
    return save;
}

void safe_spin_unlock(spin_lock_t *lock, uint32_t saved_irq) {
    unprotected_spin_unlock(lock);
    restore_interrupts(saved_irq);
}


#ifndef TEXTURE_TYPE
#define TEXTURE_TYPE basic
#endif

#define NUM_BUFFERS 3

struct {
    Display *dpy;
    EGLDisplay egl_dpy;
    EGLContext ctx;
    EGLSurface surf;
    Window win;
    std::shared_ptr<egl_texture_manager> texture_manager;
    EGLint uniform_beeb_scale_y;
    EGLint uniform_beeb_half_line;
    EGLint uniform_fill_color;
    EGLint prog_beeb;
    EGLint prog_menu;
    snd_pcm_t *pcm_handle;
    uint freq;
} setup;

static inline int warn(const char *file, int line, const char *fmt, ...) {
    int errsv = errno;
    va_list va;
    va_start(va, fmt);
    fprintf(stderr, "WARN(%s:%d): ", file, line);
    vfprintf(stderr, fmt, va);
    va_end(va);
    errno = errsv;
    return 1;
}

#define CHECK_CONDITION(cond, ...) \
do { \
   if (cond) { \
      int errsv = errno; \
      fprintf(stderr, "ERROR(%s:%d) : ", \
         __FILE__, __LINE__); \
      errno = errsv; \
      fprintf(stderr,  __VA_ARGS__); \
      abort(); \
   } \
} while(0)
#define WARN_ON(cond, ...) \
   ((cond) ? warn(__FILE__, __LINE__, __VA_ARGS__) : 0)
#define ERRSTR strerror(errno)
#define CHECK_STATUS(status, ...) WARN_ON(status != MMAL_SUCCESS, __VA_ARGS__); \
   if (status != MMAL_SUCCESS) goto error;

/**
 * Remove window border/decorations.
 */
static void no_border(Display *dpy, Window w) {
    static const unsigned MWM_HINTS_DECORATIONS = (1 << 1);
    static const int PROP_MOTIF_WM_HINTS_ELEMENTS = 5;

    typedef struct {
        unsigned long flags;
        unsigned long functions;
        unsigned long decorations;
        long inputMode;
        unsigned long status;
    } PropMotifWmHints;

    PropMotifWmHints motif_hints;
    Atom prop, proptype;
    unsigned long flags = 0;

    /* setup the property */
    motif_hints.flags = MWM_HINTS_DECORATIONS;
    motif_hints.decorations = flags;

    /* get the atom for the property */
    prop = XInternAtom(dpy, "_MOTIF_WM_HINTS", True);
    if (!prop) {
        /* something went wrong! */
        return;
    }

    /* not sure this is correct, seems to work, XA_WM_HINTS didn't work */
    proptype = prop;

    XChangeProperty(dpy, w,                         /* display, window */
                    prop, proptype,                 /* property, type */
                    32,                             /* format: 32-bit datums */
                    PropModeReplace,                /* mode */
                    (unsigned char *) &motif_hints, /* data */
                    PROP_MOTIF_WM_HINTS_ELEMENTS    /* nelements */
    );
}


/*
 * Create an RGB, double-buffered window.
 * Return the window and context handles.
 */
static void make_window(Display *dpy, EGLDisplay egl_dpy, const char *name,
                        int x, int y, int width, int height,
                        Window *winRet, EGLContext *ctxRet, EGLSurface *surfRet) {
    int scrnum = DefaultScreen(dpy);
    XSetWindowAttributes attr;
    unsigned long mask;
    Window root = RootWindow(dpy, scrnum);
    Window win;
    EGLContext ctx;
    bool fullscreen = false; /* Hook this up to a command line arg */

    if (fullscreen) {
        int scrnum = DefaultScreen(dpy);

        x = 0;
        y = 0;
        width = DisplayWidth(dpy, scrnum);
        height = DisplayHeight(dpy, scrnum);
    }

    static const EGLint attribs[] = {
            EGL_RED_SIZE, 1,
            EGL_GREEN_SIZE, 1,
            EGL_BLUE_SIZE, 1,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
    };
    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(egl_dpy, attribs, &config, 1, &num_configs)) {
        printf("Error: couldn't get an EGL visual config\n");
        exit(1);
    }

    EGLint vid;
    if (!eglGetConfigAttrib(egl_dpy, config, EGL_NATIVE_VISUAL_ID, &vid)) {
        printf("Error: eglGetConfigAttrib() failed\n");
        exit(1);
    }

    XVisualInfo visTemplate = {
            .visualid = (VisualID) vid,
    };
    int num_visuals;
    XVisualInfo *visinfo = XGetVisualInfo(dpy, VisualIDMask,
                                          &visTemplate, &num_visuals);

    /* window attributes */
    attr.background_pixel = 0;
    attr.border_pixel = 0;
    attr.colormap = XCreateColormap(dpy, root, visinfo->visual, AllocNone);
    attr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask | KeyReleaseMask;
    /* XXX this is a bad way to get a borderless window! */
    mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

    win = XCreateWindow(dpy, root, x, y, width, height,
                        0, visinfo->depth, InputOutput,
                        visinfo->visual, mask, &attr);

    if (fullscreen)
        no_border(dpy, win);

    /* set hints and properties */
    {
        XSizeHints sizehints;
        sizehints.x = x;
        sizehints.y = y;
        sizehints.width = width;
        sizehints.height = height;
        sizehints.flags = USSize | USPosition;
        XSetNormalHints(dpy, win, &sizehints);
        XSetStandardProperties(dpy, win, name, name,
                               None, (char **) nullptr, 0, &sizehints);
    }

    eglBindAPI(EGL_OPENGL_ES_API);

    static const EGLint ctx_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
    };
    ctx = eglCreateContext(egl_dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    if (!ctx) {
        printf("Error: eglCreateContext failed\n");
        exit(1);
    }

    XFree(visinfo);

    XMapWindow(dpy, win);

    EGLSurface surf = eglCreateWindowSurface(egl_dpy, config, (EGLNativeWindowType) win, nullptr);
    if (!surf) {
        printf("Error: eglCreateWindowSurface failed\n");
        exit(1);
    }

    if (!eglMakeCurrent(egl_dpy, surf, surf, ctx)) {
        printf("Error: eglCreateContext failed\n");
        exit(1);
    }

    *winRet = win;
    *ctxRet = ctx;
    *surfRet = surf;
}

GLint compile_shader(GLenum target, std::string source) {
    GLuint s = glCreateShader(target);
    const char *c_source = source.c_str();
    glShaderSource(s, 1, (const GLchar **) &c_source, nullptr);
    glCompileShader(s);

    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

    if (!ok) {
        GLchar *info;
        GLint size;

        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &size);
        info = (GLchar *) malloc(size);

        glGetShaderInfoLog(s, size, nullptr, info);
        fprintf(stderr, "Failed to compile shader: %s\n", info);

        fprintf(stderr, "source:\n%s", source.c_str());

        exit(1);
    }

    return s;
}

GLint link_program(GLint vs, GLint fs) {
    GLint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        /* Some drivers return a size of 1 for an empty log.  This is the size
         * of a log that contains only a terminating NUL character.
         */
        GLint size;
        GLchar *info = nullptr;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &size);
        if (size > 1) {
            info = (GLchar *) malloc(size);
            glGetProgramInfoLog(prog, size, nullptr, info);
        }

        fprintf(stderr, "Failed to link: %s\n",
                (info != nullptr) ? info : "<empty log>");
        exit(1);
    }

    return prog;
}

static const float beeb_verts[] = {
        -1, -1,
        1, -1,
        1, 1,
        -1, 1,
};

#define XPOS_TO_COORD(x) (((x) / 320.f) - 1.f)
#define YPOS_TO_COORD(y) -(((y) / 128.f) - 1.f)

static void gl_setup(void) {
    std::string vs =
            "attribute vec4 pos;\n"
            "uniform float scale_y;\n"
            "uniform float half_line;\n"
            "varying vec2 texcoord;\n"
            //          "varying float unscaled_texcoord_y;\n"
            "\n"
            "void main() {\n"
            "  gl_Position = pos;\n"
            "  gl_Position.y += half_line / 512.0;\n"
            "  texcoord.x = (pos.x + 1.0) / 2.0;\n"
            "  float unscaled_texcoord_y = ((pos.y - 1.0) / -2.0);\n"
            "  texcoord.y = unscaled_texcoord_y / scale_y;\n"
            "}\n";
    GLint vs_s = compile_shader(GL_VERTEX_SHADER, vs);
    auto fs =
            setup.texture_manager->define_sampler("s");
    fs +=
            "precision mediump float;\n"
            "varying vec2 texcoord;\n"
            "void main() {\n"
            "  gl_FragColor = texture2D(s, texcoord);\n"// * vec4(1.0, 0.0, 1.0, 1.0);\n"
            "}\n";

    GLint fs_s = compile_shader(GL_FRAGMENT_SHADER, fs);
    setup.prog_beeb = link_program(vs_s, fs_s);
    glUseProgram(setup.prog_beeb);

    vs =
            "attribute vec4 pos;\n"
            "const float left = 0.;\n"
            "const float right = 640.;\n"
            "const float top = 0.;\n"
            "const float bottom = 256.;\n"
            "const float far = 1.0;\n"
            "const float near = -1.0;\n"
            "const mat4 ortho = mat4(\n"
            "vec4(2.0 / (right - left), 0, 0, 0),\n"
            "vec4(0, 2.0 / (top - bottom), 0, 0),\n"
            "vec4(0, 0, -2.0 / (far - near), 0),\n"
            "vec4(-(right + left) / (right - left), -(top + bottom) / (top - bottom), -(far + near) / (far - near), 1)\n"
            ");\n"
            "\n"
            "void main() {\n"
            "  gl_Position = ortho * pos;\n"
            "}\n";
    vs_s = compile_shader(GL_VERTEX_SHADER, vs);

    fs = std::string("") +
         "precision mediump float;\n"
         "uniform vec4 fill_color;\n"
         "void main() {\n"
         "  gl_FragColor = fill_color;\n"
         "}\n";

    fs_s = compile_shader(GL_FRAGMENT_SHADER, fs);
    setup.prog_menu = link_program(vs_s, fs_s);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, beeb_verts);
    glEnableVertexAttribArray(0);
    setup.uniform_beeb_scale_y = glGetUniformLocation(setup.prog_beeb, "scale_y");
    setup.uniform_beeb_half_line = glGetUniformLocation(setup.prog_beeb, "half_line");
    setup.uniform_fill_color = glGetUniformLocation(setup.prog_menu, "fill_color");

    menu_font.init();
}

static float fw = 640, fh = 512;

static void update_layout() {
    if (force_aspect_ratio) {
        static float desired_ar = 640.f/512.0f;
        const float aspectRatio = fw / fh;
        float sides = 0.0f;
        float above = 0.0f;
        if (aspectRatio > desired_ar) {
            sides = (fw - fh * desired_ar);
        } else {
            above = (fh - fw / desired_ar);
        }
        glViewport(sides/2.f, above/2.f, fw - sides, fh - above);
    } else {
        // stretch
        glViewport(0, 0, fw, fh);
    }
}


static void present_frame(EGLDisplay dpy, EGLContext ctx, EGLSurface surf,
                          const struct displayable_frame &frame) {
    if (viewport_changed) {
        update_layout();
        glClear(GL_COLOR_BUFFER_BIT);
        viewport_changed--;
    }

    const auto &menu_state = frame.ms;
    glUseProgram(setup.prog_beeb);
    glUniform1f(setup.uniform_beeb_scale_y, frame.double_y ? 2.0 : 1.0);
    glUniform1f(setup.uniform_beeb_half_line, frame.half_line ? 1.0 : 0.0);
    frame.tex->bind();
#if 0
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#else
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#endif
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    if (menu_state.opacity) {
        static int menu_area_height;
        if (!menu_area_height) {
            int menu_lines = 0;
            for (const auto &line : menu_state.lines) {
                if (line.left_text.text) {
                    menu_lines++;
                }
            }
            menu_lines = std::max(menu_lines, MENU_MIN_LINES);
            menu_area_height = (MENU_AREA_BORDER_HEIGHT * 2 + MENU_LINE_HEIGHT * menu_lines);
        }

        glUseProgram(setup.prog_menu);
        glUniform4f(setup.uniform_fill_color, 0.0f, 0.0f, 0.4f, menu_state.opacity / 32.0f);

        float x = MENU_AREA_OFFSET_X;
        float y = (256 - menu_area_height) / 2 + MENU_AREA_OFFSET_Y;
        float menu_verts[] = {
                x, y,
                x + MENU_AREA_WIDTH, y,
                x + MENU_AREA_WIDTH, y + menu_area_height,
                x, y + menu_area_height,
        };
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, menu_verts);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glUniform4f(setup.uniform_fill_color, 48.0 / 256.0f, 138.0 / 256.f, 208.0f / 256.f,
                    menu_state.opacity / (1.2f * MENU_OPACITY_MAX));
        y += MENU_AREA_BORDER_HEIGHT;
        float menu_verts2[] = {
                x + MENU_LINE_BORDER_WIDTH, y + menu_state.selection_top_pixel,
                x + MENU_AREA_WIDTH - MENU_LINE_BORDER_WIDTH, y + menu_state.selection_top_pixel,
                x + MENU_AREA_WIDTH - MENU_LINE_BORDER_WIDTH, y + menu_state.selection_top_pixel + MENU_LINE_HEIGHT,
                x + MENU_LINE_BORDER_WIDTH, y + menu_state.selection_top_pixel + MENU_LINE_HEIGHT,
        };
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, menu_verts2);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDisable(GL_BLEND);

        x += MENU_LINE_BORDER_WIDTH * 2;
        float y0 = y;
        float opacitiy = menu_state.opacity / (float) MENU_OPACITY_MAX;
        for (const auto &line : menu_state.lines) {
            if (!line.left_text.text) break;
            float highlightness = (y - y0) - menu_state.selection_top_pixel;
            highlightness = abs(highlightness);
            highlightness = (MENU_LINE_HEIGHT / 2) - highlightness;
            highlightness = std::max(highlightness, 0.0f);
            rgb fg_color = blend(menu_text_color, menu_highlight_fg_color, highlightness);

            int width = menu_font.add_text(line.left_text.text, x, y, fg_color, opacitiy);

            if (menu_state.selected_line == &line - menu_state.lines) {
                static int foo;
                if (menu_state.flashing) {
                    int flash_pos = menu_state.flash_pos;
                    foo++;
                    int level;
                    if (flash_pos < 16)
                        level = flash_pos;
                    else if ((MENU_FLASH_LENGTH - flash_pos) < 16)
                        level = (MENU_FLASH_LENGTH - flash_pos);
                    else
                        level = 16;
                    fg_color = blend({0.7f, 0.7f, 0.7f}, fg_color, (float) level / 16.0f);
                }
                if (menu_state.error_level) {
                    fg_color = blend(fg_color, {1.f, 0.3f, 0.3f}, (float) menu_state.error_level / 16.0f);
                }
            }

            if (line.right_text.text) {
                const char *text = line.right_text.text;
                auto m = menu_font.measure(text, MENU_AREA_WIDTH - MENU_LINE_BORDER_WIDTH * 4 - width -
                                                 16); // allow a gap of 16
                menu_font.add_text(text, x + MENU_AREA_WIDTH - MENU_LINE_BORDER_WIDTH * 4 - m.width, y, fg_color,
                                   opacitiy, m.ellipsis_at);
            }
            y += MENU_LINE_HEIGHT;
        }
        menu_font.draw_text();
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, beeb_verts);
    }

    eglSwapBuffers(dpy, surf);
}

void windowResizeHandler(int w_width, int w_height) {
    // todo expose also?
    if (w_width && w_height) {
        fw = (float)w_width;
        fh = (float)w_height;
        viewport_changed = VIEWPORT_CHANGE_FRAMES; // we only draw black borders on viewport change
    }
}


void check_keyboard() {
    XEvent event;
    if (XCheckWindowEvent(setup.dpy, setup.win, KeyPressMask | KeyReleaseMask, &event)) {
        if (event.xkey.type == KeyPress) {
            platform_key_down(event.xkey.keycode, 0, 0);
        } else if (event.xkey.type == KeyRelease) {
            platform_key_up(event.xkey.keycode, 0, 0);
        }
    }
    if (XCheckWindowEvent(setup.dpy, setup.win, StructureNotifyMask, &event)) {
        if (event.type == ConfigureNotify) {
            //printf("Resize %d, %d\n", event.xconfigure.width, event.xconfigure.height);
            windowResizeHandler(event.xconfigure.width, event.xconfigure.height);
        }
    }
}

static void x_gui() {
    setup.dpy = XOpenDisplay(nullptr);
    CHECK_CONDITION(!setup.dpy, "Couldn't open X display\n");

    setup.egl_dpy = eglGetDisplay(setup.dpy);
    CHECK_CONDITION(!setup.egl_dpy, "eglGetDisplay() failed\n");

    EGLint egl_major, egl_minor;
    CHECK_CONDITION(!eglInitialize(setup.egl_dpy, &egl_major, &egl_minor), "Error: eglInitialize() failed\n");

    printf("Texture type = %s\n", TEXTURE_TYPE == basic ? "basic" : "drm_prime");
    setup.texture_manager = egl_texture_manager::get(TEXTURE_TYPE);
    CHECK_CONDITION(!setup.texture_manager, "no texture manager %d\n", TEXTURE_TYPE);

    setup.texture_manager->init(setup.dpy);

    make_window(setup.dpy, setup.egl_dpy, VERSION,
                0, 0, 640, 512, &setup.win, &setup.ctx, &setup.surf);

    gl_setup();

    std::array<std::shared_ptr<egl_texture>, NUM_TEXTURES> textures;
    for (auto &t : textures) {
        int ret = setup.texture_manager->create_texture(640, 512, 0, t);
        CHECK_CONDITION(ret || !t, "failed to create texture\n");
        empty_textures.push(t);
    }

    static int fps_counter;
    __time_t last_sec = 0;

    static char window_name_buf[128];
    std::shared_ptr<egl_texture> last_texture;
    bool last_double_y = false;
    /* This is the main processing loop */
    while (true) {
        struct timeval tv = {0,0};
        gettimeofday(&tv, NULL);
        if (tv.tv_sec != last_sec) {
            last_sec = tv.tv_sec;
            sprintf(window_name_buf, "%s - %d fps", VERSION, fps_counter);
            XStoreName(setup.dpy, setup.win, window_name_buf);
            fps_counter = 0;
        }

        check_keyboard();
        if (displayable_frames.empty()) {
            sleep_ms(1);
        } else {
            auto df = displayable_frames.pop().value();
            if (df.tex) {
                df.tex->end_cpu_access();
                if (last_texture) empty_textures.push(last_texture);
                last_texture = df.tex;
                last_double_y = df.double_y;
            } else {
                df.tex = last_texture;
                df.double_y = last_double_y;
            }
            present_frame(setup.egl_dpy, setup.ctx, setup.surf, df);
            fps_counter++;
        }
    }
}

void alsa_worker_thread();

int x_gui_init() {
    thread video_thread([] {
        printf("Starting GUI thread\n");
        x_gui();
        printf("Ending GUI thread\n");
    });
    video_thread.detach();
#if SEPARATE_ALSA_THREAD
    thread alsa_thread([] {
        printf("Starting ALSA thread\n");
        alsa_worker_thread();
        printf("Ending ALSA thread\n");
    });
    sched_param sched_param {
        .sched_priority = 10
    };
    pthread_setschedparam(alsa_thread.native_handle(), SCHED_RR, &sched_param);
    alsa_thread.detach();
#endif
    return 0;
}

static uint32_t frame_number_part;
static std::shared_ptr<egl_texture> current_beeb_texture;
struct scanvideo_scanline_buffer buffer;
static int nesting;

struct scanvideo_scanline_buffer *x_gui_begin_scanline() {
    assert(!nesting);
    nesting++;
    if (!scanline_number++) {
//        if (frame_number_part & 0xf0000) {
//            buffer.row0 = nullptr;
//        } else {
        if (empty_textures.empty()) {
            // no textures
            current_beeb_texture = nullptr;
            buffer.row0 = nullptr;
        } else {
            current_beeb_texture = empty_textures.pop().value();
            buffer.row0 = (uint16_t *) current_beeb_texture->start_cpu_access();
            assert(buffer.row0);
        }
//        }
        frame_number_part += 0x10000;
    } else if (buffer.row0) {
        buffer.row0 += current_beeb_texture->get_pitch() / (buffer.double_height ? 1 : 2);
    }
    buffer.row1 = buffer.row0 ? buffer.row0 + current_beeb_texture->get_pitch() / 2 : nullptr;
    buffer.scanline_id = frame_number_part | scanline_number;
    return &buffer;
}

void x_gui_end_scanline(struct scanvideo_scanline_buffer *buffer) {
    assert(nesting);
    nesting--;
    if (scanline_number == 256) {
        if (current_beeb_texture) {
            displayable_frames.push(displayable_frame({current_beeb_texture, !buffer->double_height, buffer->half_line, menu_state}));
            current_beeb_texture = nullptr;
        }
        scanline_number = 0;
    }
}

void x_gui_refresh_menu_display() {
    menu_display_blanking();
    displayable_frames.push(displayable_frame({nullptr, false, false, menu_state}));
}

bool x_gui_audio_init_failed;

struct audio_buffer_pool *x_gui_audio_init(uint freq) {
#ifdef NO_USE_SOUND
    return nullptr;
#else
    int err;
    setup.freq = freq;
#if FORCE_44100_OUTPUT
    freq = HACK_44100;
#endif
    x_gui_audio_init_failed = true;
    if ((err = snd_pcm_open(&setup.pcm_handle, ALSA_DEVICE, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        printf("Playback open error: %s\n", snd_strerror(err));
        return nullptr;
    }
    if ((err = snd_pcm_set_params(setup.pcm_handle,
                                  SND_PCM_FORMAT_S16,
                                  SND_PCM_ACCESS_RW_INTERLEAVED,
                                  1,
                                  freq,
                                  1,
                                  50000)) < 0) {   /* 0.2sec */
        printf("Playback open error: %s\n", snd_strerror(err));
        return nullptr;
    }
    printf("ALSA initialized OK freq = %d\n", freq);
    snd_pcm_uframes_t buf_size, period_size;

    snd_pcm_get_params(setup.pcm_handle, &buf_size, &period_size);
    printf("Buf %d Period %d ", (int) buf_size, (int) period_size);
    //snd_pcm_nonblock(setup.pcm_handle, 1);
    static struct audio_buffer_pool dummy;
    x_gui_audio_init_failed = false;
    return &dummy;
#endif
}

static int8_t alsa_first_time = 1;

uint8_t mbytes[1024];

struct mem_buffer mbuffer = {
        .size = sizeof(mbytes),
        .bytes = mbytes,
        .flags = 0,
};

const struct audio_format aformat = {
        .sample_freq = 31250,
        .format = AUDIO_BUFFER_FORMAT_PCM_S16,
        .channel_count = 1,
};

const struct audio_buffer_format abformat = {
        .format = &aformat,
        .sample_stride = 2,
};

static struct audio_buffer abuffer = {
        .buffer = &mbuffer,
        .format = &abformat,
        .max_sample_count = sizeof(mbytes) / 2,
};

void send_samples(const uint8_t *output_data, uint sample_count) {
    int err;
    while (sample_count > 0) {
        if ((err = snd_pcm_writei(setup.pcm_handle, output_data, sample_count)) < 0) {
            if (snd_pcm_state(setup.pcm_handle) == SND_PCM_STATE_XRUN) {
                if ((err = snd_pcm_prepare(setup.pcm_handle)) < 0)
                    printf("\nsnd_pcm_prepare() failed.\n");
                alsa_first_time = 1;
                continue;
            }
            panic("failed to send sound data");
        }
        sample_count -= err;
        output_data += snd_pcm_frames_to_bytes(setup.pcm_handle, err);
        if (alsa_first_time) {
            alsa_first_time = 0;
            snd_pcm_start(setup.pcm_handle);
        }
    }
}

void give_audio_buffer(struct audio_buffer_pool *ac, struct audio_buffer *buffer) {
#if FORCE_44100_OUTPUT
#if SEPARATE_ALSA_THREAD
    static int16_t dests[ALSA_QUEUE_LENGTH][sizeof(mbytes)]; // this allows for up sampling by two
    static int dest_index;
    int16_t *dest = dests[dest_index];
    dest_index = (dest_index + 1) % ALSA_QUEUE_LENGTH;
#else
    static int16_t dest[sizeof(mbytes)];
#endif
    auto src = (int16_t *)buffer->buffer->bytes;
    int sample_count = 0;
    static int frac;
    for(uint i=0; i<buffer->sample_count; ) {
        dest[sample_count++] = src[i];
        frac -= setup.freq;
        if (frac < 0) {
            frac += HACK_44100;
            i++;
        }
    }
    uint8_t *output_data = (uint8_t *)dest;
#else
    uint8_t *output_data = buffer->buffer->bytes;
    int sample_count = buffer->sample_count;
#endif
#if SEPARATE_ALSA_THREAD
    alsa_queue.push(std::pair(output_data, sample_count));
#else
    send_samples(output_data, sample_count);
#endif
}

struct audio_buffer *take_audio_buffer(struct audio_buffer_pool *ac, bool block) {
    abuffer.sample_count = 0;
    return &abuffer;
}

void x_gui_set_force_aspect_ratio(bool far) {
    force_aspect_ratio = far;
    viewport_changed = VIEWPORT_CHANGE_FRAMES;
}

#if SEPARATE_ALSA_THREAD
void alsa_worker_thread() {
    while (true) {
        auto p = alsa_queue.peek();
        if (!p.has_value()) break;
        send_samples(p.value().first, p.value().second);
        alsa_queue.pop();
    }
}
#endif
