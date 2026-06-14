/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_EGL_TEXTURE_H
#define B_EM_PICO_EGL_TEXTURE_H

#include <memory>
#include <map>

typedef unsigned int uint;

enum texture_type {
    basic,
    drm_prime
};

struct egl_texture;

// He 'managhar"
struct egl_texture_manager {
    static std::shared_ptr<egl_texture_manager> get(texture_type t) {
        const auto f = managers.find(t);
        if (f == managers.end()) {
            return nullptr;
        }
        return f->second;
    }

    virtual int create_texture(uint width, uint height, uint format, std::shared_ptr<egl_texture> &tex) = 0;
    virtual std::string define_sampler(const std::string &name) = 0;
    friend struct egl_texture;

    static void add(texture_type type, std::shared_ptr<egl_texture_manager> manager) {
        managers.insert(managers.begin(), std::make_pair(type, manager));
    }

    virtual int init(Display *display);
protected:
    static std::map<int, std::shared_ptr<egl_texture_manager>> managers;
    EGLDisplay eglDisplay;

    virtual void destroy_texture(std::shared_ptr<egl_texture> texture) = 0;

};

struct egl_texture {
    uint get_width() const { return width; }

    uint get_height() const { return height; }

    uint get_pitch() const { return pitch; }

    uint get_size() const { return pitch * height; }

    uint get_texture_handle() const { return texture_handle; }

    virtual void bind() = 0;
    virtual void *start_cpu_access() = 0;
    virtual void end_cpu_access() = 0;

    uint width, height, pitch;
    GLuint texture_handle;
};

#endif
