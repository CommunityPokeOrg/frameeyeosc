// The eye fit's words on the panel: point names, why a fit stopped, and the numbers behind it. Kept apart from the
// drawing so they are tested on their own (text_test.cpp).
#pragma once

#include "gaze_fit.h"
#include "i18n.h"

#include <string>

/**
 * The name of an eye fit point.
 * @param t texts
 * @param point the point
 * @return the name ("正面" / "center")
 */
const char* pointName(const UiText& t, gaze_fit::Point point);

/**
 * Why an eye fit stopped, in words.
 * @param t texts
 * @param fit the session
 * @return the text ("" if it did not fail)
 */
std::string failureText(const UiText& t, const gaze_fit::View& fit);

/**
 * The numbers behind a failure, one line: where, what was measured against what was needed, and the tries.
 * @param t texts
 * @param fit the session
 * @return the line, e.g. "正面の点: 使えたサンプル 30/45・ばらつき 3.4°（2.7° まで）・3 回" ("" if there are none)
 */
std::string failureDetailText(const UiText& t, const gaze_fit::View& fit);
