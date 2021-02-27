// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2016 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

#include "framebuffer.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace timg {

Framebuffer::Framebuffer(int w, int h)
    : width_(w), height_(h), pixels_(new rgba_t [ width_ * height_]) {
    strides_[0] = (int)sizeof(rgba_t) * width_;
    strides_[1] = 0;  // empty sentinel value.
    Clear();
}

Framebuffer::~Framebuffer() {
    delete [] row_data_;
    delete [] pixels_;
}

void Framebuffer::SetPixel(int x, int y, rgba_t value) {
    if (x < 0 || x >= width() || y < 0 || y >= height()) return;
    pixels_[width_ * y + x] = value;
}

Framebuffer::rgba_t Framebuffer::at(int x, int y) const {
    assert(x >= 0 && x < width() && y >= 0 && y < height());
    return pixels_[width_ * y + x];
}

void Framebuffer::Clear() {
    memset(pixels_, 0, sizeof(*pixels_) * width_ * height_);
}

Framebuffer::rgba_t Framebuffer::to_rgba(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return htole32((uint32_t)r <<  0 |
                   (uint32_t)g <<  8 |
                   (uint32_t)b << 16 |
                   (uint32_t)a << 24);
}

uint8_t** Framebuffer::row_data() {
    if (!row_data_) {
        row_data_ = new uint8_t* [ height_ + 1];
        for (int i = 0; i < height_; ++i)
            row_data_[i] = (uint8_t*)pixels_ + i * width_ * sizeof(rgba_t);
        row_data_[height_] = nullptr;  // empty sentinel value.
    }
    return row_data_;
}

Framebuffer::rgba_t Framebuffer::ParseColor(const char *color) {
    if (!color) return to_rgba(0, 0, 0, 0);

    // If it is a named color, convert it first to its #rrggbb string.
#include "html-colors.inc"
    for (const auto &c : html_colors) {
        if (strcasecmp(color, c.name) == 0) {
            color = c.translation;
            break;
        }
    }
    uint32_t r, g, b;
    if ((sscanf(color, "#%02x%02x%02x", &r, &g, &b) == 3) ||
        (sscanf(color, "rgb(%d, %d, %d)", &r, &g, &b) == 3) ||
        (sscanf(color, "rgb(0x%x, 0x%x, 0x%x)", &r, &g, &b) == 3)) {
        return to_rgba(r, g, b, 0xff);
    }
    fprintf(stderr, "Couldn't parse color '%s'\n", color);
    return to_rgba(0, 0, 0, 0);
}

static Framebuffer::rgba_t AlphaBlend(const uint32_t *bg,
                                      Framebuffer::rgba_t c) {
    const uint32_t col = le32toh(c);  // NOP on common LE platforms
    const uint32_t alpha = c >> 24;
    if (alpha == 0xff) return c;

    uint32_t fgc[3] = { (col>>0) & 0xff, (col>>8) & 0xff, (col>>16) & 0xff };
    uint32_t out[3];
    for (int i = 0; i < 3; ++i) {
        out[i] = sqrtf((fgc[i]*fgc[i]        * alpha +
                        bg[i] * (0xFF - alpha)) / 0xff);
    }
    return Framebuffer::to_rgba(out[0], out[1], out[2], 0xff);
}

void Framebuffer::AlphaComposeBackground(rgba_t bgcolor, rgba_t pattern_col) {
    uint32_t linear_bg[3];
    uint32_t linear_pattern[3];
    uint32_t *bg_choice[2] = { linear_bg, linear_bg };

    // Create pre-multiplied version
    const uint32_t bcol = le32toh(bgcolor);
    const uint32_t bg_alpha = bcol >> 24;
    if (bg_alpha == 0x00) return; // nothing to do.
    assert(bg_alpha == 0xff);   // We don't support partially transparent bg.

    linear_bg[0] = (bcol & 0xff) * (bcol & 0xff);
    linear_bg[1] = ((bcol>> 8) & 0xff) * ((bcol>> 8) & 0xff);
    linear_bg[2] = ((bcol>>16) & 0xff) * ((bcol>>16) & 0xff);

    // If alpha channel indicates that pattern is used, also prepare it and
    // use as 2nd choice.
    const uint32_t pcol = le32toh(pattern_col);
    if (pcol >> 24 == 0xff) {
        linear_pattern[0] = (pcol & 0xff) * (pcol & 0xff);
        linear_pattern[1] = ((pcol>> 8) & 0xff) * ((pcol>> 8) & 0xff);
        linear_pattern[2] = ((pcol>>16) & 0xff) * ((pcol>>16) & 0xff);
        bg_choice[1] = linear_pattern;
    }

    rgba_t *pos = pixels_;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            *pos = AlphaBlend(bg_choice[(x + y) % 2], *pos);
            pos++;
        }
    }
}

}  // namespace timg
