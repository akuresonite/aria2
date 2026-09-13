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
#ifndef D_CONSOLE_STAT_CALC_H
#define D_CONSOLE_STAT_CALC_H

#include "StatCalc.h"

#include <ctime>
#include <string>
#include <functional>
#include <memory>

#include "ProgressBar.h"
#include "TimerA2.h"

namespace aria2 {

class SizeFormatter {
protected:
  virtual std::string format(int64_t size) const = 0;

public:
  virtual ~SizeFormatter() = default;

  std::string operator()(int64_t size) const;
};

// How far a whole batch of downloads has got. |total| counts every
// download the session knows about, finished ones included.
struct OverallProgress {
  size_t done = 0;
  size_t active = 0;
  size_t waiting = 0;
  size_t total = 0;
  int error = 0;
};

// Seconds the rest of the batch is expected to need, extrapolated from
// the |elapsed| seconds the finished downloads took. Returns 0 while
// nothing has finished, because there is then nothing to extrapolate
// from, and 0 once everything is done.
time_t overallEta(const OverallProgress& p, time_t elapsed);

class ConsoleStatCalc : public StatCalc {
private:
  Timer cp_;

  Timer lastSummaryNotified_;

  // When this object was made, which is close enough to when the
  // session started. The batch ETA is measured from here.
  Timer startTime_;

  std::chrono::seconds summaryInterval_;

  std::unique_ptr<SizeFormatter> sizeFormatter_;
  bool readoutVisibility_;
  bool truncate_;
  bool isTTY_;
  bool colorOutput_;
  bool barEnabled_;
  ProgressBarStyle barStyle_;
  const colors::Color* barColor_;
  size_t barWidth_;

public:
  ConsoleStatCalc(std::chrono::seconds summaryInterval, bool colorOutput = true,
                  bool humanReadable = true);

  virtual ~ConsoleStatCalc() = default;

  virtual void calculateStat(const DownloadEngine* e) CXX11_OVERRIDE;

  void setReadoutVisibility(bool visibility)
  {
    readoutVisibility_ = visibility;
  }

  void setTruncate(bool truncate) { truncate_ = truncate; }

  // Configures the progress bar drawn in the readout. A |width| of 0 asks
  // for a width derived from the terminal. |style| may still be AUTO; it
  // is resolved once, here, because the terminal encoding cannot change
  // while aria2 runs.
  void setProgressBar(bool enabled, ProgressBarStyle style,
                      const colors::Color& color, size_t width)
  {
    barEnabled_ = enabled;
    barStyle_ = resolveProgressBarStyle(style);
    barColor_ = &color;
    barWidth_ = width;
  }
};

} // namespace aria2

#endif // D_CONSOLE_STAT_CALC_H
