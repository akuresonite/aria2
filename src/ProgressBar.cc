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
#include "ProgressBar.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#ifdef __MINGW32__
#  include <windows.h>
#endif // __MINGW32__

namespace aria2 {

namespace {
// The eight blocks U+258F..U+2588 fill 1/8 .. 8/8 of one column, so a bar
// built from them slides instead of jumping a whole column at a time.
const char* const BLOCK_PARTIALS[8] = {
    "",             // 0/8: draw nothing
    "\xe2\x96\x8f", // 1/8
    "\xe2\x96\x8e", // 2/8
    "\xe2\x96\x8d", // 3/8
    "\xe2\x96\x8c", // 4/8
    "\xe2\x96\x8b", // 5/8
    "\xe2\x96\x8a", // 6/8
    "\xe2\x96\x89", // 7/8
};

struct StyleChars {
  const char* full;
  const char* empty;
  // Eight sub-column steps, or nullptr for a style that has none.
  const char* const* partials;
  // Drawn once at the leading edge, or 0 for a style without a head.
  char head;
  // False for the two styles that stay inside plain ASCII.
  bool needsUTF8;
};

const StyleChars STYLE_TABLE[] = {
    // AUTO never reaches here; resolveProgressBarStyle() removes it first.
    {"#", "-", nullptr, 0, false},
    {"\xe2\x96\x88", "\xe2\x96\x91", BLOCK_PARTIALS, 0, true}, // BLOCKS
    {"\xe2\x96\x93", "\xe2\x96\x91", nullptr, 0, true},        // SHADE
    {"\xe2\x94\x81", "\xe2\x94\x80", nullptr, 0, true},        // LINE
    {"\xe2\x97\x8f", "\xe2\x97\x8b", nullptr, 0, true},        // DOTS
    {"\xe2\x96\xa0", "\xe2\x96\xa1", nullptr, 0, true},        // SQUARE
    {"\xe2\x96\xb0", "\xe2\x96\xb1", nullptr, 0, true},        // SLANT
    {"=", " ", nullptr, '>', false},                           // ARROW
    {"#", "-", nullptr, 0, false},                             // HASH
};

const StyleChars& charsFor(ProgressBarStyle style)
{
  return STYLE_TABLE[static_cast<size_t>(style)];
}

// True if |s| names a UTF-8 locale, e.g. "en_US.UTF-8" or "C.utf8".
bool namesUTF8(const char* s)
{
  for (; *s; ++s) {
    if ((s[0] == 'u' || s[0] == 'U') && (s[1] == 't' || s[1] == 'T') &&
        (s[2] == 'f' || s[2] == 'F')) {
      return true;
    }
  }
  return false;
}

// True if the terminal encoding can carry the box drawing characters.
bool terminalSpeaksUTF8()
{
#ifdef __MINGW32__
  // Output going to a real console is converted from UTF-8 to UTF-16 and
  // written with WriteConsoleW, so the code page decides nothing there.
  // Output redirected to a file or a pipe is written as raw bytes instead,
  // and only a UTF-8 code page renders those correctly.
  DWORD mode;
  if (::GetConsoleMode(::GetStdHandle(STD_OUTPUT_HANDLE), &mode)) {
    return true;
  }
  return ::GetConsoleOutputCP() == CP_UTF8;
#else  // !__MINGW32__
  const char* enc = getenv("LC_ALL");
  if (!enc || !*enc) {
    enc = getenv("LC_CTYPE");
  }
  if (!enc || !*enc) {
    enc = getenv("LANG");
  }
  return enc && namesUTF8(enc);
#endif // !__MINGW32__
}

struct NamedStyle {
  const char* name;
  ProgressBarStyle style;
};

const NamedStyle STYLE_NAMES[] = {
    {"auto", ProgressBarStyle::AUTO},   {"blocks", ProgressBarStyle::BLOCKS},
    {"shade", ProgressBarStyle::SHADE}, {"line", ProgressBarStyle::LINE},
    {"dots", ProgressBarStyle::DOTS},   {"square", ProgressBarStyle::SQUARE},
    {"slant", ProgressBarStyle::SLANT}, {"arrow", ProgressBarStyle::ARROW},
    {"hash", ProgressBarStyle::HASH},
};

struct NamedColor {
  const char* name;
  const colors::Color* color;
};

const NamedColor COLOR_NAMES[] = {
    {"none", &colors::clear},
    {"black", &colors::black},
    {"red", &colors::red},
    {"green", &colors::green},
    {"yellow", &colors::yellow},
    {"blue", &colors::blue},
    {"magenta", &colors::magenta},
    {"cyan", &colors::cyan},
    {"white", &colors::white},
    {"lightred", &colors::lightred},
    {"lightgreen", &colors::lightgreen},
    {"lightyellow", &colors::lightyellow},
    {"lightblue", &colors::lightblue},
    {"lightmagenta", &colors::lightmagenta},
    {"lightcyan", &colors::lightcyan},
    {"lightwhite", &colors::lightwhite},
};
} // namespace

ProgressBarStyle toProgressBarStyle(const std::string& name)
{
  for (const auto& ns : STYLE_NAMES) {
    if (name == ns.name) {
      return ns.style;
    }
  }
  return ProgressBarStyle::AUTO;
}

ProgressBarStyle resolveProgressBarStyle(ProgressBarStyle style)
{
  const bool utf8 = terminalSpeaksUTF8();
  if (style == ProgressBarStyle::AUTO) {
    return utf8 ? ProgressBarStyle::SLANT : ProgressBarStyle::HASH;
  }
  if (!utf8 && charsFor(style).needsUTF8) {
    return ProgressBarStyle::HASH;
  }
  return style;
}

const colors::Color& progressBarColor(const std::string& name)
{
  for (const auto& nc : COLOR_NAMES) {
    if (name == nc.name) {
      return *nc.color;
    }
  }
  return colors::clear;
}

std::string progressBar(double frac, size_t width, ProgressBarStyle style)
{
  if (width == 0) {
    return std::string();
  }
  frac = std::min(1.0, std::max(0.0, frac));
  // An explicit style is drawn exactly as asked. Only AUTO is resolved
  // here, and only because it has no characters of its own; the caller is
  // expected to have resolved it once already.
  const StyleChars& sc = charsFor(
      style == ProgressBarStyle::AUTO ? resolveProgressBarStyle(style) : style);

  std::string bar;
  bar.reserve(width * 3);
  size_t drawn = 0;

  if (sc.partials) {
    // Truncating rather than rounding keeps a bar that looks finished from
    // appearing before the download really is.
    const size_t eighths = static_cast<size_t>(frac * width * 8);
    const size_t full = eighths / 8;
    const size_t rem = eighths % 8;
    for (size_t i = 0; i < full; ++i) {
      bar += sc.full;
    }
    drawn = full;
    if (rem != 0 && drawn < width) {
      bar += sc.partials[rem];
      ++drawn;
    }
  }
  else {
    const size_t full = static_cast<size_t>(frac * width);
    for (size_t i = 0; i < full; ++i) {
      bar += sc.full;
    }
    drawn = full;
    if (sc.head && drawn < width) {
      bar += sc.head;
      ++drawn;
    }
  }

  for (size_t i = drawn; i < width; ++i) {
    bar += sc.empty;
  }
  return bar;
}

} // namespace aria2
