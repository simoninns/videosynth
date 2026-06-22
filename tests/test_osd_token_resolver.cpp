/*
 * File:        test_osd_token_resolver.cpp
 * Module:      osd_token_resolver_tests
 * Purpose:     Unit tests for OsdTokenResolver — per-frame token expansion
 *              in OSD overlay text strings.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "videosynth/biphase_injection_manager.h"
#include "videosynth/osd_token_resolver.h"

namespace videosynth {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

PerFrameContext MakeContext(int picture_number,
                            const std::vector<uint32_t>& biphase_words,
                            int colour_frame_index) {
  PerFrameContext ctx;
  ctx.picture_number = picture_number;
  ctx.biphase_words = biphase_words;
  ctx.colour_frame_index = colour_frame_index;
  return ctx;
}

// ---------------------------------------------------------------------------
// Resolve() tests
// ---------------------------------------------------------------------------

TEST(OsdTokenResolverTest, StaticTextPassesThrough) {
  OsdTokenResolver resolver;
  const PerFrameContext ctx = MakeContext(1, {}, 0);
  EXPECT_EQ(resolver.Resolve("HELLO WORLD", ctx, "SecA"), "HELLO WORLD");
}

TEST(OsdTokenResolverTest, PictureNumberZeroPadded) {
  OsdTokenResolver resolver;
  const PerFrameContext ctx = MakeContext(42, {}, 0);
  EXPECT_EQ(resolver.Resolve("PN:{picture_number}", ctx, "S"), "PN:00042");
}

TEST(OsdTokenResolverTest, PictureNumberMaxFiveDigits) {
  OsdTokenResolver resolver;
  const PerFrameContext ctx = MakeContext(99999, {}, 0);
  EXPECT_EQ(resolver.Resolve("{picture_number}", ctx, "S"), "99999");
}

TEST(OsdTokenResolverTest, PictureNumberZeroShowsDashes) {
  OsdTokenResolver resolver;
  const PerFrameContext ctx = MakeContext(0, {}, 0);
  EXPECT_EQ(resolver.Resolve("{picture_number}", ctx, "S"), "-----");
}

TEST(OsdTokenResolverTest, BiphaseHexSingleWord) {
  OsdTokenResolver resolver;
  // NOLINTBEGIN(readability-magic-numbers)
  const PerFrameContext ctx = MakeContext(1, {0xF12345U}, 0);
  // NOLINTEND(readability-magic-numbers)
  EXPECT_EQ(resolver.Resolve("{biphase_hex}", ctx, "S"), "F12345");
}

TEST(OsdTokenResolverTest, BiphaseHexMultipleWordsSpaceSeparated) {
  OsdTokenResolver resolver;
  // NOLINTBEGIN(readability-magic-numbers)
  const PerFrameContext ctx = MakeContext(1, {0xABCDEFU, 0x001234U}, 0);
  // NOLINTEND(readability-magic-numbers)
  EXPECT_EQ(resolver.Resolve("{biphase_hex}", ctx, "S"), "ABCDEF 001234");
}

TEST(OsdTokenResolverTest, BiphaseHexEmptyShowsDashes) {
  OsdTokenResolver resolver;
  const PerFrameContext ctx = MakeContext(0, {}, 0);
  EXPECT_EQ(resolver.Resolve("{biphase_hex}", ctx, "S"), "--------");
}

TEST(OsdTokenResolverTest, PhaseIdReplacedWithIndex) {
  OsdTokenResolver resolver;
  // NOLINTBEGIN(readability-magic-numbers)
  const PerFrameContext ctx = MakeContext(0, {}, 3);
  // NOLINTEND(readability-magic-numbers)
  EXPECT_EQ(resolver.Resolve("PH:{phase_id}", ctx, "S"), "PH:3");
}

TEST(OsdTokenResolverTest, SectionNameReplaced) {
  OsdTokenResolver resolver;
  const PerFrameContext ctx = MakeContext(0, {}, 0);
  EXPECT_EQ(resolver.Resolve("{section_name}", ctx, "MySection"), "MySection");
}

TEST(OsdTokenResolverTest, AllTokensCombined) {
  OsdTokenResolver resolver;
  // NOLINTBEGIN(readability-magic-numbers)
  const PerFrameContext ctx = MakeContext(7, {0xF00007U}, 2);
  // NOLINTEND(readability-magic-numbers)
  const std::string text =
      "PN:{picture_number} HEX:{biphase_hex} PH:{phase_id} S:{section_name}";
  EXPECT_EQ(resolver.Resolve(text, ctx, "Alpha"),
            "PN:00007 HEX:F00007 PH:2 S:Alpha");
}

TEST(OsdTokenResolverTest, UnknownTokenPassesThrough) {
  OsdTokenResolver resolver;
  const PerFrameContext ctx = MakeContext(1, {}, 0);
  EXPECT_EQ(resolver.Resolve("{foobar}", ctx, "S"), "{foobar}");
}

TEST(OsdTokenResolverTest, UnclosedBracePassesThrough) {
  OsdTokenResolver resolver;
  const PerFrameContext ctx = MakeContext(1, {}, 0);
  EXPECT_EQ(resolver.Resolve("abc{def", ctx, "S"), "abc{def");
}

TEST(OsdTokenResolverTest, EmptyTextReturnsEmpty) {
  OsdTokenResolver resolver;
  const PerFrameContext ctx = MakeContext(0, {}, 0);
  EXPECT_EQ(resolver.Resolve("", ctx, "S"), "");
}

// ---------------------------------------------------------------------------
// HasOnlyKnownTokens() tests
// ---------------------------------------------------------------------------

TEST(OsdTokenResolverTest, StaticTextHasOnlyKnownTokens) {
  EXPECT_TRUE(OsdTokenResolver::HasOnlyKnownTokens("HELLO"));
}

TEST(OsdTokenResolverTest, KnownTokensPass) {
  EXPECT_TRUE(OsdTokenResolver::HasOnlyKnownTokens(
      "PN:{picture_number} HEX:{biphase_hex} PH:{phase_id} {section_name}"));
}

TEST(OsdTokenResolverTest, UnknownTokenFails) {
  std::string unknown;
  EXPECT_FALSE(
      OsdTokenResolver::HasOnlyKnownTokens("text {bad_token} more", &unknown));
  EXPECT_EQ(unknown, "bad_token");
}

TEST(OsdTokenResolverTest, UnclosedBraceIgnored) {
  EXPECT_TRUE(OsdTokenResolver::HasOnlyKnownTokens("{unclosed"));
}

}  // namespace
}  // namespace videosynth
