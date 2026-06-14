/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#include <epoxy/gl.h>
#include <epoxy/egl.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xf86drm.h>
#include <linux/dma-buf.h>

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>

#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <drm_fourcc.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <string>
#include "egl_texture.h"

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

struct drm_prime_texture : public egl_texture {
    explicit drm_prime_texture() : egl_texture() {}

    void bind() override {
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture_handle);
    }

    void *start_cpu_access() override {
        struct dma_buf_sync sync_args;
        sync_args.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE;
        int ret;
        do {
            ret = ioctl(dbuf_fd, DMA_BUF_IOCTL_SYNC, &sync_args);
        } while (ret == EAGAIN);
        return ret ? nullptr : buffer;
    }

    void end_cpu_access() override {
        struct dma_buf_sync sync_args;
        sync_args.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
        int ret = ioctl(dbuf_fd, DMA_BUF_IOCTL_SYNC, &sync_args);
        if (ret) {
            printf("Unlock failed\n");
        }
    }

    int prime_handle = 0;
    int dbuf_fd = 0;
    uint dbuf_pitch = 0;
    uint dbuf_size = 0;
    void *buffer; // access to this must be protected by begin/end cpu_access
};

struct drm_prime_texture_manager : public egl_texture_manager {
    typedef egl_texture_manager super;

protected:
    int drm_fd = 0;
public:
    struct factory {
        factory() {
            egl_texture_manager::add(drm_prime, std::make_shared<drm_prime_texture_manager>());
        }
    };

    int init(Display *display) override {
        int rc = super::init(display);
        if (!rc) {
            drm_fd = get_drm_fd(display);
            rc = drm_fd == 0 ? -1 : 0;
        }
        return rc;
    }

    int create_texture(uint width, uint height, uint format, std::shared_ptr<egl_texture> &tex_out) override {
        auto tex = std::make_shared<drm_prime_texture>();

//        printf("Epoxy EGL v %d\n", epoxy_egl_version(eglDisplay));
        struct drm_mode_create_dumb gem;
        int ret;

        memset(&gem, 0, sizeof gem);
        gem.width = width;
        gem.height = height;
        gem.bpp = 16;
        ret = ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &gem);
        if (ret) {
            printf("CREATE_DUMB failed: %s\n", ERRSTR);
            return -1;
        }
//        printf("size %lld pitch %d handle %08x\n", gem.size, gem.pitch, gem.handle);
        tex->dbuf_size = gem.size;
        tex->dbuf_pitch = gem.pitch;
        tex->width = width;
        tex->height = height;
        tex->pitch = gem.pitch;
//        printf("bo %u %ux%u bpp %u size %lu (%u)\n", gem.handle, gem.width, gem.height, gem.bpp, (long) gem.size,
//               tex->dbuf_size);
        tex->prime_handle = gem.handle;

        struct drm_prime_handle prime;
        memset(&prime, 0, sizeof prime);
        prime.handle = tex->prime_handle;

        ret = ioctl(drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime);
        if (ret) {
            printf("PRIME_HANDLE_TO_FD failed: %s\n", ERRSTR);
            //goto fail_gem;
            return -1;
        }
//        printf("dbuf_fd = %d\n", prime.fd);
        tex->dbuf_fd = prime.fd;

        struct drm_mode_map_dumb mreq;

        memset(&mreq, 0, sizeof(struct drm_mode_map_dumb));
        mreq.handle = tex->prime_handle;

        ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
        if (ret) {
            printf("drmIoctl DRM_IOCTL_MODE_MAP_DUMB failed: %s\n", ERRSTR);
            exit(1);
        }
//        printf("offset %llx\n", mreq.offset);
        tex->buffer = mmap(0, tex->dbuf_size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, mreq.offset);
        if (tex->buffer == MAP_FAILED) {
            printf("Failed to map buffer %d\n", errno);
            return -1;
        }

        unsigned int fourcc = DRM_FORMAT_RGB565;//XRGB8888;

        EGLint attribs[] = {
                EGL_WIDTH, (EGLint) width,
                EGL_HEIGHT, (EGLint) height,
                EGL_LINUX_DRM_FOURCC_EXT, (EGLint) fourcc,
                EGL_DMA_BUF_PLANE0_FD_EXT, tex->dbuf_fd,
                EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
                EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint) tex->dbuf_pitch,
                EGL_NONE
        };

        EGLImage image = eglCreateImageKHR(eglDisplay,
                                           EGL_NO_CONTEXT,
                                           EGL_LINUX_DMA_BUF_EXT,
                                           nullptr, attribs);
        if (!image) {
            fprintf(stderr, "Failed to import fd %d %08x\n", tex->dbuf_fd, eglGetError());
            exit(1);
        }

        glGenTextures(1, &tex->texture_handle);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex->texture_handle);
        glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);

        eglDestroyImageKHR(eglDisplay, image);

        tex_out = tex;
        return 0;
    }

protected:
    void destroy_texture(std::shared_ptr<egl_texture> t) override {
        auto tex = reinterpret_cast<drm_prime_texture *>(t.get());
        if (tex->buffer) {
            munmap(tex->buffer, tex->dbuf_size);
            tex->buffer = nullptr;
        }
        if (tex->texture_handle) {
            glDeleteTextures(1, &tex->texture_handle);
            tex->texture_handle = 0;
        }
        if (tex->dbuf_fd) {
            close(tex->dbuf_fd);
            tex->dbuf_fd = 0;
        }

        if (tex->prime_handle) {
            struct drm_mode_destroy_dumb gem_destroy;
            memset(&gem_destroy, 0, sizeof gem_destroy);
            gem_destroy.handle = tex->prime_handle;
            ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &gem_destroy);
            tex->prime_handle = 0;
        }
    }

public:
    std::string define_sampler(const std::string &name) override {
        return "#extension GL_OES_EGL_image_external : enable\nuniform samplerExternalOES " + name + ";\n";
    }


    static int get_drm_fd(Display *dpy) {
        xcb_connection_t *c = XGetXCBConnection(dpy);
        xcb_window_t root = RootWindow(dpy, DefaultScreen(dpy));
        int fd;

        const xcb_query_extension_reply_t *extension =
                xcb_get_extension_data(c, &xcb_dri3_id);
        if (!(extension && extension->present))
            return -1;

        xcb_dri3_open_cookie_t cookie =
                xcb_dri3_open(c, root, None);

        xcb_dri3_open_reply_t *reply = xcb_dri3_open_reply(c, cookie, nullptr);
        if (!reply)
            return -1;

        if (reply->nfd != 1) {
            free(reply);
            return -1;
        }

        fd = xcb_dri3_open_reply_fds(c, reply)[0];
        free(reply);
        fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);

        return fd;
    }
};

static drm_prime_texture_manager::factory instance;
