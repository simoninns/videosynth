/*
 * File:        test_audio_efm_writer_paths.cpp
 * Module:      audio_efm_writer_tests
 * Purpose:     Validates AudioEfmWriter's pure stream and sidecar path
 *              derivation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include "videosynth/audio_efm_writer.h"

namespace videosynth {
namespace {

TEST(AudioEfmWriterPathTest, StripsCompositeSuffix) {
  EXPECT_EQ(AudioEfmWriter::DeriveAudioPath("out/clip.cvbs"), "out/clip.efm");
  EXPECT_EQ(AudioEfmWriter::DeriveSidecarPath("out/clip.cvbs"),
            "out/clip.efm.meta");
}

TEST(AudioEfmWriterPathTest, StripsLumaSuffix) {
  EXPECT_EQ(AudioEfmWriter::DeriveAudioPath("out/clip.cvbsy"), "out/clip.efm");
  EXPECT_EQ(AudioEfmWriter::DeriveSidecarPath("out/clip.cvbsy"),
            "out/clip.efm.meta");
}

TEST(AudioEfmWriterPathTest, AppendsWhenNoKnownSuffix) {
  EXPECT_EQ(AudioEfmWriter::DeriveAudioPath("out/clip"), "out/clip.efm");
  EXPECT_EQ(AudioEfmWriter::DeriveSidecarPath("out/clip"), "out/clip.efm.meta");
}

}  // namespace
}  // namespace videosynth
