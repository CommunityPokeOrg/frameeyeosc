// Drawing the gaze fit's target.
#include "target.h"

#include "draw.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

void renderTarget(const FontSet& fonts, int seconds, double progress, std::vector<uint8_t>& rgba,
                  const std::string& pngPath) {
    const int size = kTargetImageSize;
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = cairo_create(surface);
    const Pen pen {cr, &fonts};
    const double c = size / 2.0;

    // A dark disc behind everything keeps the target readable over bright scenes
    pen.color(kBg, 0.72);
    cairo_arc(cr, c, c, size * 0.48, 0, 2 * M_PI);
    cairo_fill(cr);

    // The ring: a faint full circle, and the part left in the accent color, running down clockwise from the top
    const double ring = size * 0.36;
    cairo_set_line_width(cr, size * 0.045);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    pen.color(kBorder, 0.55);
    cairo_arc(cr, c, c, ring, 0, 2 * M_PI);
    cairo_stroke(cr);
    const double left = std::clamp(progress, 0.0, 1.0);
    if (left > 0.001) {
        pen.color(kAccent);
        cairo_arc(cr, c, c, ring, -M_PI / 2, -M_PI / 2 + left * 2 * M_PI);
        cairo_stroke(cr);
    }

    // The dot to look at: light, with a dark edge so it shows on light and dark backgrounds
    pen.color(kBg);
    cairo_arc(cr, c, c, size * 0.06, 0, 2 * M_PI);
    cairo_fill(cr);
    pen.color(kText);
    cairo_arc(cr, c, c, size * 0.042, 0, 2 * M_PI);
    cairo_fill(cr);

    // The seconds left, small and muted under the dot so they don't pull the eyes away
    if (seconds > 0) {
        const std::string text = std::to_string(seconds);
        const double textSize = size * 0.12;
        const double w = pen.measure(text, textSize, true);
        pen.text(c - w / 2, c + size * 0.22, text, textSize, kTextMuted, true);
    }

    cairo_surface_flush(surface);
    surfaceToRgba(surface, rgba);
    if (!pngPath.empty()) cairo_surface_write_to_png(surface, pngPath.c_str());
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}
