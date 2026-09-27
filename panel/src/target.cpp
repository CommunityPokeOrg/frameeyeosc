// Drawing the eye fit's target.
#include "target.h"

#include "draw.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

namespace {

/**
 * Draw text centered on a point's x, shrinking it to fit a width.
 * @param pen drawing tools
 * @param cx center x
 * @param baseline text baseline
 * @param text the text
 * @param size the size to start from
 * @param maxWidth the width to fit
 * @param c the color
 */
void centeredText(const Pen& pen, double cx, double baseline, const std::string& text, double size, double maxWidth,
                  Color c) {
    while (size > 10 && pen.measure(text, size, true) > maxWidth) size -= 1;
    const double w = pen.measure(text, size, true);
    pen.text(cx - w / 2, baseline, text, size, c, true);
}

}  // namespace

void renderTarget(const FontSet& fonts, gaze_fit::TargetStyle style, const std::string& label, int seconds,
                  double progress, std::vector<uint8_t>& rgba, const std::string& pngPath) {
    using gaze_fit::TargetStyle;
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
    if (style != TargetStyle::OpenEyes) {
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
    }

    if (style == TargetStyle::Dot) {
        // The dot to look at: light, with a dark edge so it shows on light and dark backgrounds
        pen.color(kBg);
        cairo_arc(cr, c, c, size * 0.06, 0, 2 * M_PI);
        cairo_fill(cr);
        pen.color(kText);
        cairo_arc(cr, c, c, size * 0.042, 0, 2 * M_PI);
        cairo_fill(cr);
        // The seconds left, small and muted under the dot so they don't pull the eyes away
        if (seconds > 0) centeredText(pen, c, c + size * 0.22, std::to_string(seconds), size * 0.12, size, kTextMuted);
    } else {
        // The eyes-shut step: its words, and while counting down to closing, a large number
        const bool counting = style == TargetStyle::CloseEyes && seconds > 0;
        centeredText(pen, c, counting ? c - size * 0.08 : c + size * 0.045, label, size * 0.12, size * 0.6, kText);
        if (counting) centeredText(pen, c, c + size * 0.2, std::to_string(seconds), size * 0.2, size * 0.5, kAccent);
    }

    cairo_surface_flush(surface);
    surfaceToRgba(surface, rgba);
    if (!pngPath.empty()) cairo_surface_write_to_png(surface, pngPath.c_str());
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}
