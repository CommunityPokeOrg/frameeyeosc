// The gaze fit's target: a small dot to look at, a ring that runs down while the point is measured, and the seconds
// left. It is shown as its own overlay fixed to the head (see VrOverlay::showTarget), never on the dashboard.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class FontSet;

/** The target image's edge length (px). */
constexpr int kTargetImageSize = 256;

/**
 * Draw the target.
 * @param fonts the fonts
 * @param seconds the seconds left, shown under the dot (0 = none)
 * @param progress how much of the ring is left (0..1)
 * @param rgba where to write un-premultiplied RGBA (kTargetImageSize squared)
 * @param pngPath also save a PNG here if not empty
 */
void renderTarget(const FontSet& fonts, int seconds, double progress, std::vector<uint8_t>& rgba,
                  const std::string& pngPath = "");
