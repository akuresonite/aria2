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
#include "ConsoleStatCalc.h"

#ifdef HAVE_TERMIOS_H
#  include <termios.h>
#endif // HAVE_TERMIOS_H
#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#endif // HAVE_SYS_IOCTL_H
#include <unistd.h>

#include <cstdio>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iterator>

#include "DownloadEngine.h"
#include "RequestGroupMan.h"
#include "RequestGroup.h"
#include "FileAllocationMan.h"
#include "FileAllocationEntry.h"
#include "CheckIntegrityMan.h"
#include "CheckIntegrityEntry.h"
#include "util.h"
#include "DownloadContext.h"
#include "wallclock.h"
#include "FileEntry.h"
#include "console.h"
#include "ColorizedStream.h"
#include "Option.h"

#ifdef ENABLE_BITTORRENT
#  include "bittorrent_helper.h"
#  include "PeerStorage.h"
#  include "BtRegistry.h"
#endif // ENABLE_BITTORRENT

namespace aria2 {

std::string SizeFormatter::operator()(int64_t size) const
{
  return format(size);
}

namespace {
class AbbrevSizeFormatter : public SizeFormatter {
protected:
  virtual std::string format(int64_t size) const CXX11_OVERRIDE
  {
    return util::abbrevSize(size);
  }
};
} // namespace

namespace {
class PlainSizeFormatter : public SizeFormatter {
protected:
  virtual std::string format(int64_t size) const CXX11_OVERRIDE
  {
    return util::itos(size);
  }
};
} // namespace

namespace {
void printSizeProgress(ColorizedStream& o,
                       const std::shared_ptr<RequestGroup>& rg,
                       const TransferStat& stat,
                       const SizeFormatter& sizeFormatter)
{
#ifdef ENABLE_BITTORRENT
  if (rg->isSeeder()) {
    o << "SEED(";
    if (rg->getCompletedLength() > 0) {
      std::streamsize oldprec = o.precision();
      o << std::fixed << std::setprecision(1)
        << ((stat.allTimeUploadLength * 10) / rg->getCompletedLength()) / 10.0
        << std::setprecision(oldprec) << std::resetiosflags(std::ios::fixed);
    }
    else {
      o << "--";
    }
    o << ")";
  }
  else
#endif // ENABLE_BITTORRENT
  {
    o << sizeFormatter(rg->getCompletedLength()) << "B/"
      << sizeFormatter(rg->getTotalLength()) << "B";
    if (rg->getTotalLength() > 0) {
      o << colors::cyan << "("
        << 100 * rg->getCompletedLength() / rg->getTotalLength() << "%)";
      o << colors::clear;
    }
  }
}
} // namespace

namespace {
void printProgressCompact(ColorizedStream& o, const DownloadEngine* e,
                          const SizeFormatter& sizeFormatter)
{
  if (!e->getRequestGroupMan()->downloadFinished()) {
    NetStat& netstat = e->getRequestGroupMan()->getNetStat();
    int dl = netstat.calculateDownloadSpeed();
    int ul = netstat.calculateUploadSpeed();
    o << colors::magenta << "[" << colors::clear << "DL:" << colors::green
      << sizeFormatter(dl) << "B" << colors::clear;
    if (ul) {
      o << " UL:" << colors::cyan << sizeFormatter(ul) << "B" << colors::clear;
    }
    o << colors::magenta << "]" << colors::clear;
  }

  const RequestGroupList& groups = e->getRequestGroupMan()->getRequestGroups();
  size_t cnt = 0;
  const size_t MAX_ITEM = 5;
  for (auto i = groups.begin(), eoi = groups.end(); i != eoi && cnt < MAX_ITEM;
       ++i, ++cnt) {
    const std::shared_ptr<RequestGroup>& rg = *i;
    TransferStat stat = rg->calculateStat();
    o << colors::magenta << "[" << colors::clear << "#"
      << GroupId::toAbbrevHex(rg->getGID()) << " ";
    printSizeProgress(o, rg, stat, sizeFormatter);
    o << colors::magenta << "]" << colors::clear;
  }
  if (cnt < groups.size()) {
    o << "(+" << groups.size() - cnt << ")";
  }
}
} // namespace

namespace {
// What the readout needs in order to draw a bar. A width of 0 means no bar.
struct BarConfig {
  size_t width = 0;
  ProgressBarStyle style = ProgressBarStyle::HASH;
  const colors::Color* color = &colors::clear;
};

// A fifth of the line, held between sane bounds, leaves room for the
// numbers that follow the bar even at the 79 column default. A configured
// width wins, but never grows past the line itself.
size_t barWidthFor(size_t cols, size_t configured)
{
  if (configured > 0) {
    return std::min(configured, cols);
  }
  return std::min(static_cast<size_t>(25),
                  std::max(static_cast<size_t>(8), cols / 5));
}
} // namespace

namespace {
void printProgress(ColorizedStream& o, const std::shared_ptr<RequestGroup>& rg,
                   const DownloadEngine* e, const SizeFormatter& sizeFormatter,
                   const BarConfig& bar)
{
  TransferStat stat = rg->calculateStat();
  int eta = 0;
  if (rg->getTotalLength() > 0 && stat.downloadSpeed > 0) {
    eta =
        (rg->getTotalLength() - rg->getCompletedLength()) / stat.downloadSpeed;
  }
  o << colors::magenta << "[" << colors::clear << "#"
    << GroupId::toAbbrevHex(rg->getGID()) << " ";
  if (bar.width > 0 && rg->getTotalLength() > 0) {
    o << *bar.color
      << progressBar(static_cast<double>(rg->getCompletedLength()) /
                         static_cast<double>(rg->getTotalLength()),
                     bar.width, bar.style)
      << colors::clear << " ";
  }
  printSizeProgress(o, rg, stat, sizeFormatter);
  o << " CN:" << rg->getNumConnection();
#ifdef ENABLE_BITTORRENT
  auto btObj = e->getBtRegistry()->get(rg->getGID());
  if (btObj) {
    const PeerSet& peers = btObj->peerStorage->getUsedPeers();
    o << " SD:" << countSeeder(peers.begin(), peers.end());
  }
#endif // ENABLE_BITTORRENT

  if (!rg->downloadFinished()) {
    o << " DL:" << colors::green << sizeFormatter(stat.downloadSpeed) << "B"
      << colors::clear;
  }
  if (stat.sessionUploadLength > 0) {
    o << " UL:" << colors::cyan << sizeFormatter(stat.uploadSpeed) << "B"
      << colors::clear;
    o << "(" << sizeFormatter(stat.allTimeUploadLength) << "B)";
  }
  if (bar.width > 0) {
    // The start time is zero until the group is actually started.
    const Timer& start =
        rg->getDownloadContext()->getNetStat().getDownloadStartTime();
    if (!start.isZero()) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               start.difference(global::wallclock()))
                               .count();
      o << " ELAPSED:" << util::secfmt(elapsed);
    }
  }
  if (eta > 0) {
    o << " ETA:" << colors::yellow << util::secfmt(eta) << colors::clear;
  }
  o << colors::magenta << "]" << colors::clear;
}
} // namespace

namespace {
class PrintSummary {
private:
  size_t cols_;
  const DownloadEngine* e_;
  const SizeFormatter& sizeFormatter_;
  const BarConfig& bar_;

public:
  PrintSummary(size_t cols, const DownloadEngine* e,
               const SizeFormatter& sizeFormatter, const BarConfig& bar)
      : cols_(cols), e_(e), sizeFormatter_(sizeFormatter), bar_(bar)
  {
  }

  void operator()(const RequestGroupList::value_type& rg)
  {
    const char SEP_CHAR = '-';
    ColorizedStream o;
    printProgress(o, rg, e_, sizeFormatter_, bar_);
    const std::vector<std::shared_ptr<FileEntry>>& fileEntries =
        rg->getDownloadContext()->getFileEntries();
    o << "\nFILE: ";
    writeFilePath(fileEntries.begin(), fileEntries.end(), o,
                  rg->inMemoryDownload());
    o << "\n" << std::setfill(SEP_CHAR) << std::setw(cols_) << SEP_CHAR << "\n";
    auto str = o.str(false);
    global::cout()->write(str.c_str());
  }
};
} // namespace

namespace {
void printProgressSummary(const RequestGroupList& groups, size_t cols,
                          const DownloadEngine* e,
                          const SizeFormatter& sizeFormatter,
                          const BarConfig& bar, const OverallProgress& op,
                          time_t elapsed)
{
  const char SEP_CHAR = '=';
  time_t now;
  time(&now);
  std::stringstream o;
  o << " *** Download Progress Summary";
  {
    time_t now;
    struct tm* staticNowtmPtr;
    char buf[26];
    if (time(&now) != (time_t)-1 &&
        (staticNowtmPtr = localtime(&now)) != nullptr &&
        asctime_r(staticNowtmPtr, buf) != nullptr) {
      char* lfptr = strchr(buf, '\n');
      if (lfptr) {
        *lfptr = '\0';
      }
      o << " as of " << buf;
    }
  }
  o << " *** \n"
    << std::setfill(SEP_CHAR) << std::setw(cols) << SEP_CHAR << "\n";
  global::cout()->write(o.str().c_str());
  if (op.total > 1) {
    std::stringstream f;
    f << " Files: " << op.done << "/" << op.total << " done ("
      << 100 * op.done / op.total << "%)  Active: " << op.active
      << "  Waiting: " << op.waiting;
    if (op.error > 0) {
      f << "  Error: " << op.error;
    }
    if (elapsed > 0) {
      f << "  Elapsed: " << util::secfmt(elapsed);
    }
    const time_t eta = overallEta(op, elapsed);
    if (eta > 0) {
      f << "  ETA: " << util::secfmt(eta);
    }
    f << "\n";
    global::cout()->write(f.str().c_str());
  }
  std::for_each(groups.begin(), groups.end(),
                PrintSummary(cols, e, sizeFormatter, bar));
}
} // namespace

#ifdef __MINGW32__
namespace {
// True only for a handle that really is a console. GetConsoleMode fails on
// a pipe or a file, which is the same test aria2 already uses to decide
// whether colour is supported.
bool winIsConsole()
{
  DWORD mode;
  return ::GetConsoleMode(::GetStdHandle(STD_OUTPUT_HANDLE), &mode) != 0;
}
} // namespace
#endif // __MINGW32__

ConsoleStatCalc::ConsoleStatCalc(std::chrono::seconds summaryInterval,
                                 bool colorOutput, bool humanReadable)
    : summaryInterval_(std::move(summaryInterval)),
      readoutVisibility_(true),
      truncate_(true),
#ifdef __MINGW32__
      // isatty() reports true for a pipe on Windows, so ask the console
      // API instead. Treating a redirected stream as a terminal fills the
      // log with carriage returns and pad spaces, and would now put bar
      // characters in there too.
      isTTY_(winIsConsole()),
#else  // !__MINGW32__
      isTTY_(isatty(STDOUT_FILENO) == 1),
#endif // !__MINGW32__
      colorOutput_(colorOutput),
      barEnabled_(true),
      barStyle_(resolveProgressBarStyle(ProgressBarStyle::AUTO)),
      barColor_(&colors::green),
      barWidth_(0)
{
  if (humanReadable) {
    sizeFormatter_ = make_unique<AbbrevSizeFormatter>();
  }
  else {
    sizeFormatter_ = make_unique<PlainSizeFormatter>();
  }
}

namespace {
// Every download the session knows about, split by what it is doing now.
// getNumStoppedTotal() is used rather than counting download results,
// because results are evicted once max-download-result is reached while
// this counter is not.
OverallProgress calcOverallProgress(const RequestGroupMan& rgman)
{
  OverallProgress p;
  p.done = rgman.getNumStoppedTotal();
  p.active = rgman.countRequestGroup();
  p.waiting = rgman.getReservedGroups().size();
  p.total = p.done + p.active + p.waiting;
  p.error = rgman.getDownloadStat().getError();
  return p;
}

// The [FILES ...] block that leads the readout when a batch is running.
void printOverall(ColorizedStream& o, const OverallProgress& p, time_t elapsed,
                  const BarConfig& bar)
{
  o << colors::magenta << "[" << colors::clear << "FILES";
  if (bar.width > 0) {
    o << " " << *bar.color
      << progressBar(static_cast<double>(p.done) / static_cast<double>(p.total),
                     bar.width, bar.style)
      << colors::clear;
  }
  o << " " << p.done << "/" << p.total << "(" << 100 * p.done / p.total << "%)";
  if (p.error > 0) {
    o << " " << colors::red << "ERR:" << util::itos(p.error) << colors::clear;
  }
  if (elapsed > 0) {
    o << " ELAPSED:" << util::secfmt(elapsed);
  }
  const time_t eta = overallEta(p, elapsed);
  if (eta > 0) {
    o << " ETA:" << colors::yellow << util::secfmt(eta) << colors::clear;
  }
  o << colors::magenta << "]" << colors::clear;
}
} // namespace

time_t overallEta(const OverallProgress& p, time_t elapsed)
{
  if (p.done == 0 || p.done >= p.total || elapsed <= 0) {
    return 0;
  }
  return static_cast<time_t>(static_cast<double>(elapsed) *
                             static_cast<double>(p.total - p.done) /
                             static_cast<double>(p.done));
}

void ConsoleStatCalc::calculateStat(const DownloadEngine* e)
{
  if (cp_.difference(global::wallclock()) + A2_DELTA_MILLIS <
      std::chrono::milliseconds(1000)) {
    return;
  }
  cp_ = global::wallclock();
  const SizeFormatter& sizeFormatter = *sizeFormatter_.get();

  // Some terminals (e.g., Windows terminal) prints next line when the
  // character reached at the last column.
  unsigned short int cols = 79;

  if (isTTY_) {
#ifndef __MINGW32__
#  ifdef HAVE_TERMIOS_H
    struct winsize size;
    // A terminal that reports no width at all, which some pseudo terminals
    // do, would otherwise truncate the whole readout away. Keep the default
    // width in that case rather than printing nothing.
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 1) {
      cols = size.ws_col - 1;
    }
#  endif // HAVE_TERMIOS_H
#else    // __MINGW32__
    CONSOLE_SCREEN_BUFFER_INFO info;
    // As above: a console that reports no usable width keeps the default.
    if (::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE),
                                     &info) &&
        info.dwSize.X > 2) {
      cols = info.dwSize.X - 2;
    }
#endif   // !__MINGW32__
    std::string line(cols, ' ');
    global::cout()->printf("\r%s\r", line.c_str());
  }

  BarConfig bar;
  if (barEnabled_ && isTTY_) {
    bar.width = barWidthFor(cols, barWidth_);
    bar.style = barStyle_;
    bar.color = barColor_;
  }
  const auto& rgman = *e->getRequestGroupMan();
  const OverallProgress op = calcOverallProgress(rgman);
  const time_t elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                             startTime_.difference(global::wallclock()))
                             .count();

  ColorizedStream o;
  if (e->getRequestGroupMan()->countRequestGroup() > 0) {
    if ((summaryInterval_ > 0_s) &&
        lastSummaryNotified_.difference(global::wallclock()) +
                A2_DELTA_MILLIS >=
            summaryInterval_) {
      lastSummaryNotified_ = global::wallclock();
      printProgressSummary(rgman.getRequestGroups(), cols, e, sizeFormatter,
                           bar, op, elapsed);
      global::cout()->write("\n");
      global::cout()->flush();
    }
  }
  if (!readoutVisibility_) {
    return;
  }
  size_t numGroup = rgman.countRequestGroup();
  const bool color = global::cout()->supportsColor() && isTTY_ && colorOutput_;
  // With a batch queued, how far the batch has got matters more than which
  // individual files happen to be moving, so it leads the line. Truncation
  // eats the right hand end, never this.
  if (op.total > 1) {
    printOverall(o, op, elapsed, bar);
  }
  if (numGroup == 1) {
    const std::shared_ptr<RequestGroup>& rg = *rgman.getRequestGroups().begin();
    printProgress(o, rg, e, sizeFormatter, bar);
  }
  else if (numGroup > 1) {
    // For more than 2 RequestGroups, use compact readout form
    printProgressCompact(o, e, sizeFormatter);
  }

  {
    auto& entry = e->getFileAllocationMan()->getPickedEntry();
    if (entry) {
      o << " [FileAlloc:#"
        << GroupId::toAbbrevHex(entry->getRequestGroup()->getGID()) << " "
        << sizeFormatter(entry->getCurrentLength()) << "B/"
        << sizeFormatter(entry->getTotalLength()) << "B(";
      if (entry->getTotalLength() > 0) {
        o << 100LL * entry->getCurrentLength() / entry->getTotalLength();
      }
      else {
        o << "--";
      }
      o << "%)]";
      if (e->getFileAllocationMan()->hasNext()) {
        o << "(+" << e->getFileAllocationMan()->countEntryInQueue() << ")";
      }
    }
  }
  {
    auto& entry = e->getCheckIntegrityMan()->getPickedEntry();
    if (entry) {
      o << " [Checksum:#"
        << GroupId::toAbbrevHex(entry->getRequestGroup()->getGID()) << " "
        << sizeFormatter(entry->getCurrentLength()) << "B/"
        << sizeFormatter(entry->getTotalLength()) << "B(";
      if (entry->getTotalLength() > 0) {
        o << 100LL * entry->getCurrentLength() / entry->getTotalLength();
      }
      else {
        o << "--";
      }
      o << "%)]";
      if (e->getCheckIntegrityMan()->hasNext()) {
        o << "(+" << e->getCheckIntegrityMan()->countEntryInQueue() << ")";
      }
    }
  }
  if (isTTY_) {
    if (truncate_) {
      auto str = o.str(color, cols);
      global::cout()->write(str.c_str());
    }
    else {
      auto str = o.str(color);
      global::cout()->write(str.c_str());
    }
    global::cout()->flush();
  }
  else {
    auto str = o.str(false);
    global::cout()->write(str.c_str());
    global::cout()->write("\n");
  }
}

} // namespace aria2
