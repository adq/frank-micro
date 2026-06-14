/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#include <epoxy/gl.h>
#include <epoxy/egl.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <X11/X.h>
#include <X11/Xlib.h>
#include "egl_texture.h"

std::map<int, std::shared_ptr<egl_texture_manager>> egl_texture_manager::managers;

struct basic_texture : public egl_texture {
    struct {
        bool sub_window;
        int x, y, width, height;
    } access;

    void bind() override {
        glBindTexture(GL_TEXTURE_2D, texture_handle);
    }

    void *start_cpu_access() override {
        access.sub_window = false;
        return buffer;
    }

    void *start_cpu_access(int x, int y, int width, int height) {
        access = {true, x, y, width, height};
        return (uint8_t *) buffer + y * pitch + x * 2;
    }

    void end_cpu_access() override;
protected:
    void *buffer = nullptr;

    friend class basic_texture_manager;
};

int egl_texture_manager::init(Display *display) {
    eglDisplay = eglGetDisplay(display);
    return eglDisplay == EGL_NO_DISPLAY ? -1 : 0;
}

struct basic_texture_manager : public egl_texture_manager {
    struct factory {
        factory() {
            egl_texture_manager::add(basic, std::make_shared<basic_texture_manager>());
        }
    };

    basic_texture_manager() : egl_texture_manager() {
    }

    int init(Display *display) override {
        int rc = egl_texture_manager::init(display);
        if (!rc) {
            glEnable(GL_TEXTURE_2D);
        }
        return rc;
    }

    int create_texture(uint width, uint height, uint format, std::shared_ptr<egl_texture> &t) override {
        std::shared_ptr<basic_texture> tex = std::make_shared<basic_texture>();
        glGenTextures(1, &tex->texture_handle);
        tex->width = width;
        tex->height = height;
        tex->pitch = width * 2;
        glBindTexture(GL_TEXTURE_2D, tex->texture_handle);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tex->width, tex->height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, nullptr);
        tex->buffer = malloc(width * height * 2);
        uint8_t *p = (uint8_t *) tex->buffer;
        p += tex->pitch * (height - 2) + width;
        memset(p, 0xff, 24);
        t = tex;
        return 0;
    }

    static void update_texture(basic_texture *tex) {
        glBindTexture(GL_TEXTURE_2D, tex->texture_handle);
        if (!tex->access.sub_window)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex->width, tex->height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
                            tex->buffer);
        else
            glTexSubImage2D(GL_TEXTURE_2D, 0, tex->access.x, tex->access.y, tex->access.width, tex->access.height,
                            GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
                            (uint8_t *) tex->buffer + tex->access.x * 2 + tex->access.y * tex->pitch);
    }

protected:

    void destroy_texture(std::shared_ptr<egl_texture> tex) override {
        if (tex->texture_handle) {
            glDeleteTextures(1, &tex->texture_handle);
            tex->texture_handle = 0;
        }
    }

public:
    std::string define_sampler(const std::string &name) override {
        return "uniform sampler2D " + name + ";\n";
    }
};

void basic_texture::end_cpu_access() {
    basic_texture_manager::update_texture(this);
}

static basic_texture_manager::factory instance;
