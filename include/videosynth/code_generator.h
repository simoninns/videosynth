/*
 * File:        code_generator.h
 * Module:      code_generator
 * Purpose:     Abstract base class for LaserDisc biphase code generators.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>

namespace videosynth {

// Abstract base class for all LaserDisc biphase code generators.
//
// Each generator produces 24-bit biphase code values for injection into VBI
// lines. Stateful generators (e.g. picture_number, chapter_number) track
// their position via Advance() and can be rewound with Reset(). Stateless
// generators (e.g. lead_in, lead_out, picture_stop) provide no-op defaults
// for Advance() and Reset().
//
// Thread-safety: Subclasses that maintain state are NOT thread-safe. Stateless
// subclasses are effectively thread-safe. Do not share a stateful generator
// instance across threads without external synchronisation.
class CodeGenerator {
 public:
  virtual ~CodeGenerator() = default;

  // Returns the current 24-bit biphase code value for this frame/track.
  virtual uint32_t CurrentCode() const = 0;

  // Advances the generator by one frame (track).
  // Stateless generators implement this as a no-op.
  virtual void Advance() {}

  // Resets the generator to its initial state.
  // Stateless generators implement this as a no-op.
  virtual void Reset() {}
};

}  // namespace videosynth
