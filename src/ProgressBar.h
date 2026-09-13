/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#ifndef D_PROGRESS_BAR_H
#define D_PROGRESS_BAR_H

#include "common.h"

#include <string>

#include "ColorizedStream.h"

namespace aria2 {

// Character set the progress bar is drawn with. AUTO picks SLANT on a
// terminal that can show it and HASH everywhere else.
enum class ProgressBarStyle {
  AUTO,
  BLOCKS, // full blocks, moving in 1/8 column steps
  SHADE,  // medium shade against light shade
  LINE,   // heavy horizontal rule against light one
  DOTS,   // filled circles against hollow ones
  SQUARE, // filled squares against hollow ones
  SLANT,  // filled parallelograms against hollow ones
  ARROW,  // '=' with a '>' head, as wget draws it
  HASH,   // '#' against '-'
};

// Turns a --progress-bar-style value into a style. An unknown name gives
// AUTO, so a typo degrades to something that always works.
ProgressBarStyle toProgressBarStyle(const std::string& name);

// Replaces AUTO by what this terminal can actually draw. Styles other than
// AUTO are returned unchanged, except that a style needing characters the
// terminal cannot show falls back to HASH.
ProgressBarStyle resolveProgressBarStyle(ProgressBarStyle style);

// Draws a bar occupying exactly |width| terminal columns showing |frac| of
// the work done. |frac| is clamped into [0.0, 1.0]; a |width| of 0 gives an
// empty string. Pass a style already put through
// resolveProgressBarStyle().
std::string progressBar(double frac, size_t width, ProgressBarStyle style);

// Looks up the colour named by --progress-bar-color. "none" and any
// unknown name give colors::clear, which leaves the bar uncoloured.
const colors::Color& progressBarColor(const std::string& name);

} // namespace aria2

#endif // D_PROGRESS_BAR_H
