#include "ProgressBar.h"

#include <cppunit/extensions/HelperMacros.h>

#include "ColorizedStream.h"
#include "ConsoleStatCalc.h"

namespace aria2 {

namespace {
// Terminal columns a UTF-8 string occupies: every byte that is not a
// continuation byte starts one new character.
size_t columns(const std::string& s)
{
  size_t n = 0;
  for (auto c : s) {
    if ((static_cast<unsigned char>(c) & 0xc0) != 0x80) {
      ++n;
    }
  }
  return n;
}
} // namespace

class ProgressBarTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(ProgressBarTest);
  CPPUNIT_TEST(testAsciiBarFillsInProportion);
  CPPUNIT_TEST(testBarIsAlwaysExactlyTheRequestedWidth);
  CPPUNIT_TEST(testZeroWidthGivesEmptyString);
  CPPUNIT_TEST(testFractionIsClamped);
  CPPUNIT_TEST(testBarOnlyFillsAtOneHundredPercent);
  CPPUNIT_TEST(testBlocksAdvanceInEighths);
  CPPUNIT_TEST(testArrowHasOneHead);
  CPPUNIT_TEST(testStyleNamesRoundTrip);
  CPPUNIT_TEST(testUnknownStyleNameGivesAuto);
  CPPUNIT_TEST(testResolveNeverReturnsAuto);
  CPPUNIT_TEST(testColorLookup);
  CPPUNIT_TEST(testOverallEtaNeedsAFinishedDownload);
  CPPUNIT_TEST(testOverallEtaExtrapolates);
  CPPUNIT_TEST(testOverallEtaIsZeroWhenDone);
  CPPUNIT_TEST(testTruncationCountsColumnsNotBytes);
  CPPUNIT_TEST(testTruncationNeverSplitsACharacter);
  CPPUNIT_TEST_SUITE_END();

public:
  void setUp() {}
  void tearDown() {}

  void testAsciiBarFillsInProportion()
  {
    CPPUNIT_ASSERT_EQUAL(std::string("----------"),
                         progressBar(0.0, 10, ProgressBarStyle::HASH));
    CPPUNIT_ASSERT_EQUAL(std::string("#####-----"),
                         progressBar(0.5, 10, ProgressBarStyle::HASH));
    CPPUNIT_ASSERT_EQUAL(std::string("##########"),
                         progressBar(1.0, 10, ProgressBarStyle::HASH));
  }

  void testBarIsAlwaysExactlyTheRequestedWidth()
  {
    const ProgressBarStyle styles[] = {
        ProgressBarStyle::BLOCKS, ProgressBarStyle::SHADE,
        ProgressBarStyle::LINE,   ProgressBarStyle::DOTS,
        ProgressBarStyle::SQUARE, ProgressBarStyle::SLANT,
        ProgressBarStyle::ARROW,  ProgressBarStyle::HASH};
    for (auto style : styles) {
      for (int pct = 0; pct <= 100; ++pct) {
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(17),
                             columns(progressBar(pct / 100.0, 17, style)));
      }
    }
  }

  void testZeroWidthGivesEmptyString()
  {
    CPPUNIT_ASSERT_EQUAL(std::string(),
                         progressBar(0.5, 0, ProgressBarStyle::BLOCKS));
  }

  void testFractionIsClamped()
  {
    CPPUNIT_ASSERT_EQUAL(std::string("----"),
                         progressBar(-3.0, 4, ProgressBarStyle::HASH));
    CPPUNIT_ASSERT_EQUAL(std::string("####"),
                         progressBar(9.0, 4, ProgressBarStyle::HASH));
  }

  void testBarOnlyFillsAtOneHundredPercent()
  {
    // 99.9% must still show one unfinished column, or a download that has
    // not finished looks as though it has.
    CPPUNIT_ASSERT(progressBar(0.999, 10, ProgressBarStyle::HASH) !=
                   progressBar(1.0, 10, ProgressBarStyle::HASH));
  }

  void testBlocksAdvanceInEighths()
  {
    // One column wide, one eighth done: the narrowest partial block.
    CPPUNIT_ASSERT_EQUAL(std::string("\xe2\x96\x8f"),
                         progressBar(0.125, 1, ProgressBarStyle::BLOCKS));
    // Half of one column.
    CPPUNIT_ASSERT_EQUAL(std::string("\xe2\x96\x8c"),
                         progressBar(0.5, 1, ProgressBarStyle::BLOCKS));
    // Nothing at all yet: the empty shade, not a partial block.
    CPPUNIT_ASSERT_EQUAL(std::string("\xe2\x96\x91"),
                         progressBar(0.0, 1, ProgressBarStyle::BLOCKS));
  }

  void testArrowHasOneHead()
  {
    CPPUNIT_ASSERT_EQUAL(std::string("===>      "),
                         progressBar(0.3, 10, ProgressBarStyle::ARROW));
    // At the end there is no room for a head, and none is drawn.
    CPPUNIT_ASSERT_EQUAL(std::string("=========="),
                         progressBar(1.0, 10, ProgressBarStyle::ARROW));
  }

  void testStyleNamesRoundTrip()
  {
    CPPUNIT_ASSERT(toProgressBarStyle("blocks") == ProgressBarStyle::BLOCKS);
    CPPUNIT_ASSERT(toProgressBarStyle("shade") == ProgressBarStyle::SHADE);
    CPPUNIT_ASSERT(toProgressBarStyle("line") == ProgressBarStyle::LINE);
    CPPUNIT_ASSERT(toProgressBarStyle("dots") == ProgressBarStyle::DOTS);
    CPPUNIT_ASSERT(toProgressBarStyle("square") == ProgressBarStyle::SQUARE);
    CPPUNIT_ASSERT(toProgressBarStyle("slant") == ProgressBarStyle::SLANT);
    CPPUNIT_ASSERT(toProgressBarStyle("arrow") == ProgressBarStyle::ARROW);
    CPPUNIT_ASSERT(toProgressBarStyle("hash") == ProgressBarStyle::HASH);
    CPPUNIT_ASSERT(toProgressBarStyle("auto") == ProgressBarStyle::AUTO);
  }

  void testUnknownStyleNameGivesAuto()
  {
    CPPUNIT_ASSERT(toProgressBarStyle("bloks") == ProgressBarStyle::AUTO);
    CPPUNIT_ASSERT(toProgressBarStyle("") == ProgressBarStyle::AUTO);
  }

  void testResolveNeverReturnsAuto()
  {
    // AUTO is not in the character table, so resolving must always replace
    // it. Whichever way the terminal is set up, one of these two applies.
    const ProgressBarStyle got =
        resolveProgressBarStyle(ProgressBarStyle::AUTO);
    CPPUNIT_ASSERT(got == ProgressBarStyle::SLANT ||
                   got == ProgressBarStyle::HASH);
  }

  void testColorLookup()
  {
    CPPUNIT_ASSERT(&progressBarColor("cyan") == &colors::cyan);
    CPPUNIT_ASSERT(&progressBarColor("lightgreen") == &colors::lightgreen);
    CPPUNIT_ASSERT(&progressBarColor("none") == &colors::clear);
    CPPUNIT_ASSERT(&progressBarColor("chartreuse") == &colors::clear);
  }

  void testOverallEtaNeedsAFinishedDownload()
  {
    OverallProgress p;
    p.done = 0;
    p.total = 50;
    CPPUNIT_ASSERT_EQUAL(static_cast<time_t>(0), overallEta(p, 30));
  }

  void testOverallEtaExtrapolates()
  {
    // 10 of 50 done in 30 s means 3 s each, so 40 left is 120 s.
    OverallProgress p;
    p.done = 10;
    p.total = 50;
    CPPUNIT_ASSERT_EQUAL(static_cast<time_t>(120), overallEta(p, 30));
  }

  void testOverallEtaIsZeroWhenDone()
  {
    OverallProgress p;
    p.done = 50;
    p.total = 50;
    CPPUNIT_ASSERT_EQUAL(static_cast<time_t>(0), overallEta(p, 30));
  }

  void testTruncationCountsColumnsNotBytes()
  {
    // Ten block characters are 30 bytes but only 10 columns, and all ten
    // must survive a cut at 10 columns.
    ColorizedStream o;
    o << progressBar(1.0, 10, ProgressBarStyle::BLOCKS);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(10), columns(o.str(false, 10)));
  }

  void testTruncationNeverSplitsACharacter()
  {
    ColorizedStream o;
    o << progressBar(1.0, 10, ProgressBarStyle::BLOCKS);
    const std::string cut = o.str(false, 3);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), columns(cut));
    // A split sequence would leave a byte count that is not a multiple of 3.
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(9), cut.size());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ProgressBarTest);

} // namespace aria2
