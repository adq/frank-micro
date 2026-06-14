/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#include <map>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <cstdbool>
#include <cassert>
#include <cstring>
#include "bbctext.h"

typedef unsigned int uint;

void palette_touch() {
    static uint8_t aa2f[256];
    printf("static uint8_t aa2f[256] = {");
    for (uint i = 0; i < 256; i += 16) {
        printf("\n    ");
        for (uint j = i; j < i + 16; j++) {
            aa2f[j] = ((j & 0x80u) >> 4u) | ((j & 0x20u) >> 3u) | ((j & 0x8u) >> 2u) | ((j & 0x2u) >> 1u);
            printf("0x%x, ", aa2f[j]);
        }
    }
    printf("\n};\n");

    printf("static uint32_t palette_touch[5 * 128] = {\n    ");
    for (uint f = 0; f < 5; f++) {
        for (uint idx = 0; idx < 16; idx++) {
            uint32_t bits = 0;
            for (uint i = 0; i < 256; i++) {
                uint s = i;
                bool touch = false;
                if (f == 4) {
                    for (int j = 0; j < 8; j++) {
                        if (aa2f[(uint8_t) s] == idx) touch = true;
                        s <<= 2u;
                        s |= 3u;
                    }
                } else {
                    assert(f <= 3);
                    for (int j = 0; j < 1u << f; j++) {
                        if (aa2f[(uint8_t) s] == idx) touch = true;
                        s <<= 1u;
                        s |= 1u;
                    }
                }
                if (touch) {
                    bits |= 1u << (i & 31u);
                }
                if (31 == (i & 31u)) {
                    printf("0x%08x, ", bits);
                    bits = 0;
                }
            }
            printf(" // %x", idx);
            if (f != 4 || idx != 15) printf("\n    ");
        }
        if (f != 4) printf("\n    ");
    }
    printf("\n};\n");

}

/*Mode 7 (SAA5050)*/
static uint8_t mode7_chars[96 * 160], mode7_charsi[96 * 160], mode7_graph[96 * 160], mode7_graphi[
        96 * 160], mode7_sepgraph[96 * 160], mode7_sepgraphi[96 * 160], mode7_tempi[96 * 120], mode7_tempi2[96 * 120];
static int mode7_lookup[8][8][16];
static uint8_t *mode7_p[2] = {mode7_chars, mode7_charsi};
static uint8_t *mode7_heldp[2];

std::vector<uint8_t> check_chars(uint8_t **bitmaps, int count) {
    struct pixels {
        uint8_t c[8];
    };
    struct comp {
        bool operator()(const pixels &lhs, const pixels &rhs) const {
            for (int i = 0; i < 8; i++) {
                if (lhs.c[i] < rhs.c[i]) return true;
                if (lhs.c[i] > rhs.c[i]) return false;
            }
            return false;
        }
    };
    std::map<pixels, int, comp> unique;
    for (uint i = 0; i < count; i++) {
        uint8_t *bitmap = bitmaps[i];
        for (int j = 0; j < 96 * 160; j += 16) {
            struct pixels pixels;
            for (int k = 0; k < 16; k++) {
                int c = bitmap[j + k];
                pixels.c[k & 7] = c;
                if (7 == (k & 7)) {
                    if (unique.find(pixels) == unique.end()) {
                        unique[pixels] = unique.size();
                    }
                    bitmap[j + (k >> 3)] = unique[pixels];
                }
            }
        }
    }
    printf("And the answer is %d\n", (int) unique.size());
    std::vector<pixels> ordered_pixels(unique.size());
    for (const auto i : unique) {
        const pixels &pixels = i.first;
        printf("%d: ", i.second);
        for (int i = 0; i < 8; i++) {
            int c = pixels.c[i];
            if (c < 4) c = ' ';
            else if (c < 8) c = '.';
            else if (c < 12) c = 'o';
            else c = '*';
            putchar(c);
        }

        printf(" : ");
        for (int i = 0; i < 8; i++) {
            int c = pixels.c[i];
            static const char *hex = "0123456789abcdef";
            putchar(hex[c]);
        }

        ordered_pixels[i.second] = pixels;
        putchar('\n');
    }
    auto choose_next = [](int i) { return i; };

    std::vector<uint8_t> compact;
    std::vector<uint8_t> pos(unique.size());
    for (int i = 0; i < ordered_pixels.size(); i++) {
        int next = choose_next(i);
        int best_index = -1;
        int best_len = 0;
        for (int j = 8; j > 0; j--) {
            for (int k = 0; k <= (int) compact.size() - j; k++) {
                if (!memcmp(&compact.begin()[k], ordered_pixels[next].c, j)) {
                    if (j == 8 || (j > best_len && (k + j) == compact.size())) {
                        best_index = k;
                        best_len = j;
                    }
                }
            }
        }
        pos[next] = best_len == 0 ? compact.size() : best_index;
        for (int j = best_len; j < 8; j++) {
            compact.push_back(ordered_pixels[next].c[j]);
        }
        printf("For %d, found %d len %d ==> %d\n", next, best_index, best_len, (int) compact.size());
    }
    for (int n = 0; n < pos.size(); n++) {
        printf("%d: ", n);
        for (int i = 0; i < 8; i++) {
            int c = compact[pos[n] + i];
            if (c < 4) c = ' ';
            else if (c < 8) c = '.';
            else if (c < 12) c = 'o';
            else c = '*';
            putchar(c);
        }

        printf(" : ");
        for (int i = 0; i < 8; i++) {
            int c = compact[pos[n] + i];
            static const char *hex = "0123456789abcdef";
            putchar(hex[c]);
        }

        putchar('\n');
    }
    printf("%d\n", compact.size());
    printf("static const uint8_t grey_pixels[%d] = {\n", (int) compact.size());
    for (int i = 0; i < compact.size(); i += 16) {
        printf("    ");
        for (int j = i; j < std::min(i + 16, (int) compact.size()); j++) {
            printf("0x%x, ", compact[j]);
        }
        printf("\n");
    }
    printf("};\n");
    return pos;
}

void mode7_makechars() {
    int c, d, y;
    int offs1 = 0, offs2 = 0;
    int x;
    int x2;
    int stat;
    uint8_t *p = teletext_characters, *p2 = mode7_tempi;
    for (c = 0; c < (96 * 60); c++)
        teletext_characters[c] *= 15;
    for (c = 0; c < (96 * 60); c++)
        teletext_graphics[c] *= 15;
    for (c = 0; c < (96 * 60); c++)
        teletext_separated_graphics[c] *= 15;
    for (c = 0; c < (96 * 120); c++)
        mode7_tempi2[c] = teletext_characters[c >> 1];
    for (c = 0; c < 960; c++) {
        x = 0;
        x2 = 0;
        for (d = 0; d < 16; d++) {
            mode7_graph[offs2 + d] =
                    ((3 - x) * teletext_graphics[offs1 + x2] + x * teletext_graphics[offs1 + x2 + 1]) / 3;
            mode7_sepgraph[offs2 + d] = ((3 - x) * teletext_separated_graphics[offs1 + x2] +
                                         x * teletext_separated_graphics[offs1 + x2 + 1]) / 3;
            if (!d) {
                mode7_graph[offs2 + d] = mode7_graphi[offs2 + d] = teletext_graphics[offs1];
                mode7_sepgraph[offs2 + d] = mode7_sepgraphi[offs2 + d] = teletext_separated_graphics[offs1];
            } else if (d == 15) {
                mode7_graph[offs2 + d] = mode7_graphi[offs2 + d] = teletext_graphics[offs1 + 5];
                mode7_sepgraph[offs2 + d] = mode7_sepgraphi[offs2 + d] = teletext_separated_graphics[offs1 + 5];
            } else {
                mode7_graph[offs2 + d] = mode7_graphi[offs2 + d] = teletext_graphics[offs1 + x2];
                mode7_sepgraph[offs2 + d] = mode7_sepgraphi[offs2 + d] = teletext_separated_graphics[offs1 + x2];
            }
            if (++x == 3) {
                x2++;
                x = 0;
            }
            mode7_charsi[offs2 + d] = 0;
        }

        offs1 += 6;
        offs2 += 16;
    }
    for (c = 0; c < 96; c++) {
        for (y = 0; y < 10; y++) {
            for (d = 0; d < 6; d++) {
                stat = 0;
                if (y < 9 && p[(y * 6) + d] && p[(y * 6) + d + 6])
                    stat |= 3;  /*Above + below - set both */
                if (y < 9 && d > 0 && p[(y * 6) + d] && p[(y * 6) + d + 5] && !p[(y * 6) + d - 1])
                    stat |= 1;  /*Above + left  - set left */
                if (y < 9 && d > 0 && p[(y * 6) + d + 6] && p[(y * 6) + d - 1] && !p[(y * 6) + d + 5])
                    stat |= 1;  /*Below + left  - set left */
                if (y < 9 && d < 5 && p[(y * 6) + d] && p[(y * 6) + d + 7] && !p[(y * 6) + d + 1])
                    stat |= 2;  /*Above + right - set right */
                if (y < 9 && d < 5 && p[(y * 6) + d + 6] && p[(y * 6) + d + 1] && !p[(y * 6) + d + 7])
                    stat |= 2;  /*Below + right - set right */

                p2[0] = (stat & 1) ? 15 : 0;
                p2[1] = (stat & 2) ? 15 : 0;
                p2 += 2;
            }
        }
        p += 60;
    }
    offs1 = offs2 = 0;
    for (c = 0; c < 960; c++) {
        x = 2;
        x2 = 0;
        for (d = 0; d < 16; d++) {
            mode7_chars[offs2 + d] = ((3 - x) * mode7_tempi2[offs1 + x2] + x * mode7_tempi2[offs1 + x2 + 1]) / 3;
            mode7_charsi[offs2 + d] = ((3 - x) * mode7_tempi[offs1 + x2] + x * mode7_tempi[offs1 + x2 + 1]) / 3;
            x += 2;
            if (x >= 3) {
                x2++;
                x -= 3;
            }
            if (c >= 320 && c < 640) {
                mode7_graph[offs2 + d] = mode7_sepgraph[offs2 + d] = mode7_chars[offs2 + d];
                mode7_graphi[offs2 + d] = mode7_sepgraphi[offs2 + d] = mode7_charsi[offs2 + d];
            }
        }
        offs1 += 12;
        offs2 += 16;
    }

    uint8_t *foo[6] = {mode7_chars, mode7_charsi, mode7_graph, mode7_graphi, mode7_sepgraph, mode7_sepgraphi};
    const char *names[6] = {"mode7_chars", "mode7_charsi", "mode7_graph", "mode7_graphi", "mode7_sepgraph",
                            "mode7_sepgraphi"};
    auto pos = check_chars(&foo[0], 6);
    for (int n = 0; n < 6; n++) {
        printf("static uint8_t %s[%d] = {\n", names[n], 960 * 2);
        for (int i = 0; i < 960; i += 10) {
            printf("    ");
            for (int j = i; j < i + 10; j++) {
                printf("0x%02x, 0x%02x,  ", pos[foo[n][j * 16]], pos[foo[n][j * 16 + 1]]);
            }
            printf("\n");
        }
        printf("};\n");

    }
}

int main() {
    palette_touch();
    mode7_makechars();
    return 0;
}
