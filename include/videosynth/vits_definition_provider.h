/*
 * File:        vits_definition_provider.h
 * Module:      vits
 * Purpose:     Declares VITS definition lookup interfaces and default provider.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>

#include "videosynth/model.h"
#include "videosynth/vits_definition.h"

namespace videosynth {

// Thread-safety: Implementations of IVitsDefinitionProvider must be
// thread-safe. TryGetDefinition may be called concurrently from multiple
// threads.
class IVitsDefinitionProvider {
 public:
  virtual ~IVitsDefinitionProvider() = default;

  // Ownership: out_definition and error are output parameters. The caller owns
  // the pointed-to memory and must ensure the pointers are valid (non-null).
  // The implementation writes to these locations but does not take ownership.
  virtual bool TryGetDefinition(Standard standard, const std::string& vits_type,
                                VitsDefinition* out_definition,
                                std::string* error) const = 0;
};

// Thread-safety: VitsDefinitionProvider is thread-safe for concurrent calls to
// TryGetDefinition. All member access is read-only.
class VitsDefinitionProvider final : public IVitsDefinitionProvider {
 public:
  // Retrieves a VITS definition by standard and type.
  //
  // Ownership: out_definition and error are output parameters. The caller owns
  // the pointed-to memory and must ensure the pointers are valid (non-null).
  // The implementation writes to these locations but does not take ownership.
  //
  // Args:
  //   standard: The video standard (PAL or NTSC).
  //   vits_type: The type of VITS definition to retrieve.
  //   out_definition: Output pointer for the retrieved definition.
  //   error: Output pointer for any error message.
  //
  // Returns:
  //   true if the definition was found, false otherwise.
  bool TryGetDefinition(Standard standard, const std::string& vits_type,
                        VitsDefinition* out_definition,
                        std::string* error) const override;
};

}  // namespace videosynth
