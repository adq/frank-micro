/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_FONT_H
#define B_EM_PICO_FONT_H

#include "shader.h"
#include <vector>

struct font {
    static const int MAX_VERTICES = 512;
    struct measure_result {
        int width;
        int ellipsis_at;
    };

    struct glyph_sequence {
        glyph_sequence(const rgb &color, float alpha, uint count) : color(color), alpha(alpha), count(count) {}

        rgb color;
        float alpha;
        uint count;
    };

    struct {
        struct {
            float x, y, u, v;
        } p[4];
    } vertices[MAX_VERTICES];
    std::vector<glyph_sequence> sequences;
    uint vertex_count;

    GLuint texture;
    GLuint vao, vbo;
    GLuint program;
    GLuint u_text_color;
    int init();
    void reset_text();
    int add_text(std::string text, uint x, uint y, const rgb &color, float a,
                 int ellipsis_at = std::numeric_limits<int>::max());
    void draw_text();
    measure_result measure(std::string text, int max = std::numeric_limits<int>::max());
};

#endif //B_EM_FONT_H
