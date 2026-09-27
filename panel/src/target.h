// The eye fit's target: a small dot to look at, a ring that runs down while the step is measured, and the seconds
// left; for the eyes-shut step, words and a countdown instead of the dot. It is shown as its own overlay fixed to
// the head (see VrOverlay::showTarget), never on the dashboard.
#pragma once

#include "gaze_fit.h"

#include <cstdint>
#include <string>
#include <vector>

class FontSet;

/** The target image's edge length (px). */
constexpr int kTargetImageSize = 256;

/**
 * Draw the target.
 * @param fonts the fonts
 * @param style dot, or one of the eyes-shut step's looks
 * @param label the words for the eyes-shut step ("Close your eyes"); unused for the dot
 * @param seconds the seconds left (0 = none): small under the dot, large while counting down to closing the eyes
 * @param progress how much of the ring is left (0..1)
 * @param rgba where to write un-premultiplied RGBA (kTargetImageSize squared)
 * @param pngPath also save a PNG here if not empty
 */
void renderTarget(const FontSet& fonts, gaze_fit::TargetStyle style, const std::string& label, int seconds,
                  double progress, std::vector<uint8_t>& rgba, const std::string& pngPath = "");

/** The debug gaze dot image's edge length (px). */
constexpr int kDotImageSize = 64;

/** Which debug gaze dot. */
enum class DotKind { Both, Left, Right };

/**
 * Draw a debug gaze dot: a filled circle with a dark edge (combined: light gray, left eye: light cyan, right eye:
 * darker orange).
 * @param kind which dot
 * @param rgba where to write un-premultiplied RGBA (kDotImageSize squared)
 * @param pngPath also save a PNG here if not empty
 */
void renderGazeDot(DotKind kind, std::vector<uint8_t>& rgba, const std::string& pngPath = "");
