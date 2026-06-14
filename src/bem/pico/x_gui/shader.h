/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_SHADER_H
#define B_EM_PICO_SHADER_H

#include <epoxy/gl.h>
#include <epoxy/egl.h>

#include <string>
#include <array>

GLint compile_shader(GLenum target, std::string source);
GLint link_program(GLint vs, GLint fs);

struct rgb : public std::array<float, 3> {
    const float &r() const { return (*this)[0]; }

    const float &g() const { return (*this)[1]; }

    const float &b() const { return (*this)[2]; }
};

#endif
