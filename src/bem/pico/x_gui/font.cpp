/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#include <cstring>
#include <algorithm>
#include <limits>
#include "font.h"
#include "font_data.h"

static const font_definition *font_def = &font_Ubuntu;

//static int vbo_indices[MAX_TEXT_LENGTH];
//static int vbo_counts[MAX_TEXT_LENGTH];

int font::init() {
    std::string vs =
            "attribute vec2 pos;\n"
            "attribute vec2 st;\n"
            "varying vec2 texcoord;\n"
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
            "  gl_Position = ortho * vec4(pos.x, pos.y, 0., 1.);\n"
            "  texcoord = st;\n"
            "}\n";
    GLint vs_s = compile_shader(GL_VERTEX_SHADER, vs);

    std::string fs = std::string("") +
                     "precision mediump float;\n"
                     "uniform sampler2D s;\n"
                     "uniform vec4 text_color;\n"
                     "varying vec2 texcoord;\n"
                     "void main() {\n"
                     " float x = texture2D(s, texcoord).a; gl_FragColor = vec4(text_color.rgb, x * text_color.a);\n"
                     "}\n";
    GLint fs_s = compile_shader(GL_FRAGMENT_SHADER, fs);
    program = link_program(vs_s, fs_s);

    u_text_color = glGetUniformLocation(program, "text_color");
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // red doesn't seem to work (doesn't complain, but samples as 0)
//    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, font_def->width, font_def->height, 0, GL_RED, GL_UNSIGNED_BYTE, font_def->alphas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, font_def->width, font_def->height, 0, GL_ALPHA, GL_UNSIGNED_BYTE,
                 font_def->alphas);
//    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, font_def->width, font_def->height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, font_def->alphas);
    int res = glGetError();
    if (res) {
        printf("Argh %0x\n", res);
    }

    // Configure VAO/VBO for texture quads
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (const void *) 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (const void *) 8);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return 0;
}

void font::reset_text() {
    sequences.clear();
    vertex_count = 0;
}

void font::draw_text() {
    glUseProgram(program);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

//    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    rgb last_color = {-1, -1, -1};
    float last_alpha = -1;
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count * sizeof(vertices[0]), vertices);
    // argh glMultiDrawArrays not available
    int offset = 0;
    for (const auto &p : sequences) {
        if (p.color != last_color || p.alpha != last_alpha) {
            glUniform4f(u_text_color, p.color.r(), p.color.g(), p.color.b(), p.alpha);
            last_color = p.color;
            last_alpha = p.alpha;
        }
        for (uint i = 0; i < p.count; i++) {
            glDrawArrays(GL_TRIANGLE_FAN, offset, 4);
            offset += 4;
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);

    reset_text();
}

int font::add_text(std::string text, uint x, uint y, const rgb &color, float a, int ellipsis_at) {
    int len = 0;
    uint x0 = x;
    float xscale = 1.0f / font_def->width;
    float yscale = 1.0f / font_def->height;

    // Iterate through all characters
    std::string::const_iterator c;
    int in_ellipsis = 0;
    for (c = text.begin(); (c != text.end()) || in_ellipsis; c++, len++) {
        if (vertex_count == MAX_VERTICES) {
            // todo could just draw and continue
            break;
        }
        int i;
        if (len >= ellipsis_at && !in_ellipsis) {
            in_ellipsis = 4;
        }
        if (in_ellipsis) {
            if (!--in_ellipsis) break;
            i = '.' - ' ';
        } else {
            i = *c - ' ';
        }
        if (i < 0 || i >= font_def->characterCount) i = 0;
        const auto &ch = font_def->characters[i];

        GLfloat xpos = x - ch.originX;
        GLfloat ypos = y + (font_def->glyph_height - ch.originY) / 2.0f;

        GLfloat w = ch.width;
        GLfloat h = ch.height;

        // Update VBO for each character
        vertices[vertex_count++] = {.p = {
                {xpos,     ypos,         xscale * ((float) ch.x),     yscale * ((float) ch.y)},
                {xpos + w, ypos,         xscale * ((float) ch.x + w), yscale * ((float) ch.y)},
                {xpos + w, ypos + h / 2, xscale * ((float) ch.x + w), yscale * ((float) ch.y + h)},
                {xpos,     ypos + h / 2, xscale * ((float) ch.x),     yscale * ((float) ch.y + h)},
        }};
        // Update content of VBO memory
        x += ch.advance;
    }
    sequences.emplace_back(color, a, len);
    return (int) x - (int) x0;
}

font::measure_result font::measure(std::string text, int max) {
    uint len = 0;
    int ellipsis_width = 3 * font_def->characters['.' - ' '].advance;
    int x = 0;
    int ellipsis_pos = 0;
    int ellipsis_x = 0;

    for (auto c = text.begin(); c != text.end(); c++, len++) {
        int i = *c - ' ';
        if (i < 0 || i >= font_def->characterCount) i = 0;
        const auto &ch = font_def->characters[i];
        int r = x + ch.width - ch.originX;
        if (r <= max - ellipsis_width) {
            ellipsis_pos = len;
            ellipsis_x = x;
        }
        if (r >= max) break;

        // Update content of VBO memory
        x += ch.advance;
    }
    if (len == text.length()) {
        return {
                .width = x,
                .ellipsis_at = std::numeric_limits<int>::max(),
        };
    } else {
        return {
                .width = ellipsis_x + ellipsis_width,
                .ellipsis_at = ellipsis_pos,
        };
    }
}
