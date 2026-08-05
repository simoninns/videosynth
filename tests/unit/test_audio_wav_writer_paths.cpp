/*
 * File:        test_audio_wav_writer_paths.cpp
 * Module:      audio_wav_writer_tests
 * Purpose:     Validates AudioWavWriter's pure audio-track path derivation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include "videosynth/audio_wav_writer.h"

namespace videosynth {
namespace {

TEST(AudioWavWriterPathTest, StripsCompositeSuffix) {
  EXPECT_EQ(AudioWavWriter::DeriveAudioPath("out/clip.cvbs", 0),
            "out/clip_audio_0.wav");
}

TEST(AudioWavWriterPathTest, StripsLumaSuffix) {
  EXPECT_EQ(AudioWavWriter::DeriveAudioPath("out/clip.cvbsy", 3),
            "out/clip_audio_3.wav");
}

TEST(AudioWavWriterPathTest, AppendsWhenNoKnownSuffix) {
  EXPECT_EQ(AudioWavWriter::DeriveAudioPath("out/clip", 7),
            "out/clip_audio_7.wav");
}

}  // namespace
}  // namespace videosynth
