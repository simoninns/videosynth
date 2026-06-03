/*
 * File:        test_vits_definition_provider.cpp
 * Module:      vits_definition_provider_tests
 * Purpose:     Verifies VITS definition lookup catalog coverage and
 * deterministic errors.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "videosynth/timing_constants.h"
#include "videosynth/vits_definition_provider.h"
#include "videosynth/vits_generator.h"

namespace videosynth {
namespace {

TEST(VitsDefinitionProviderTest, ReturnsPalDefinitionByType) {
  VitsDefinitionProvider provider;
  VitsDefinition definition;
  std::string error;

  EXPECT_TRUE(
      provider.TryGetDefinition(Standard::kPal, "vits17", &definition, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(definition.standard, Standard::kPal);
  EXPECT_EQ(definition.vits_type, "vits17");
  EXPECT_EQ(definition.recommended_frame_line, 17);
  EXPECT_EQ(definition.levels_unit, VitsLevelsUnit::kMillivolts);
}

TEST(VitsDefinitionProviderTest, ReturnsNtscDefinitionByType) {
  VitsDefinitionProvider provider;
  VitsDefinition definition;
  std::string error;

  EXPECT_TRUE(
      provider.TryGetDefinition(Standard::kNtsc, "virs", &definition, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(definition.standard, Standard::kNtsc);
  EXPECT_EQ(definition.vits_type, "virs");
  EXPECT_EQ(definition.levels_unit, VitsLevelsUnit::kIre);
}

TEST(VitsDefinitionProviderTest, ReturnsDeterministicErrorForUnknownType) {
  VitsDefinitionProvider provider;
  VitsDefinition definition;
  std::string error;

  EXPECT_FALSE(provider.TryGetDefinition(
      Standard::kPal, "not-a-valid-vits-type", &definition, &error));
  EXPECT_EQ(
      error,
      "Unsupported vits_type 'not-a-valid-vits-type' for standard 'PAL'.");
}

TEST(VitsDefinitionProviderTest, ExposesAllSupportedVitsTypes) {
  VitsDefinitionProvider provider;
  std::string error;
  VitsDefinition definition;

  const std::vector<std::string> pal_types = {
      "vits17", "itu-multiburst", "uk-national",
      "vits20", "itu-composite",  "itu-combination",
  };
  for (const std::string& vits_type : pal_types) {
    EXPECT_TRUE(provider.TryGetDefinition(Standard::kPal, vits_type,
                                          &definition, &error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(definition.standard, Standard::kPal);
    EXPECT_EQ(definition.vits_type, vits_type);
  }

  const std::vector<std::string> ntsc_types = {
      "ntc7-composite", "ntc7-combination", "fcc-multiburst", "fcc-composite",
      "virs",
  };
  for (const std::string& vits_type : ntsc_types) {
    EXPECT_TRUE(provider.TryGetDefinition(Standard::kNtsc, vits_type,
                                          &definition, &error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(definition.standard, Standard::kNtsc);
    EXPECT_EQ(definition.vits_type, vits_type);
  }
}

TEST(VitsDefinitionProviderTest,
     ExposesRenderableDefinitionsForAllSupportedVitsTypes) {
  VitsDefinitionProvider provider;
  VitsGenerator generator;
  std::string error;

  const std::vector<std::pair<Standard, std::string>> supported_types = {
      {Standard::kPal, "vits17"},
      {Standard::kPal, "itu-multiburst"},
      {Standard::kPal, "uk-national"},
      {Standard::kPal, "vits20"},
      {Standard::kPal, "itu-composite"},
      {Standard::kPal, "itu-combination"},
      {Standard::kNtsc, "ntc7-composite"},
      {Standard::kNtsc, "ntc7-combination"},
      {Standard::kNtsc, "fcc-multiburst"},
      {Standard::kNtsc, "fcc-composite"},
      {Standard::kNtsc, "virs"},
  };

  for (const auto& entry : supported_types) {
    VitsDefinition definition;
    ASSERT_TRUE(provider.TryGetDefinition(entry.first, entry.second,
                                          &definition, &error));
    ASSERT_TRUE(error.empty());
    EXPECT_FALSE(definition.primitives.empty()) << entry.second;
    EXPECT_FALSE(definition.render_order.empty()) << entry.second;

    VitsSynthesisPlan plan;
    ASSERT_TRUE(generator.BuildSynthesisPlan(definition, &plan, &error))
        << entry.second;
    ASSERT_TRUE(error.empty()) << entry.second;
    EXPECT_FALSE(plan.primitives.empty()) << entry.second;
    EXPECT_FALSE(plan.render_order.empty()) << entry.second;

    const TimingConstants timing = GetTimingConstants(entry.first);
    VitsRenderedLine rendered;
    ASSERT_TRUE(generator.RenderLine(plan, timing.sample_rate_4fsc_hz,
                                     timing.samples_per_line_4fsc, &rendered,
                                     &error))
        << entry.second;
    ASSERT_TRUE(error.empty()) << entry.second;
    EXPECT_EQ(rendered.y_samples_mv.size(),
              static_cast<std::size_t>(timing.samples_per_line_4fsc))
        << entry.second;
    EXPECT_EQ(rendered.c_samples_mv.size(),
              static_cast<std::size_t>(timing.samples_per_line_4fsc))
        << entry.second;
  }
}

}  // namespace
}  // namespace videosynth
