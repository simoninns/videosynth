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

class IVitsDefinitionProvider {
 public:
  virtual ~IVitsDefinitionProvider() = default;

  virtual bool TryGetDefinition(Standard standard,
                                const std::string& vits_type,
                                VitsDefinition* out_definition,
                                std::string* error) const = 0;
};

class VitsDefinitionProvider final : public IVitsDefinitionProvider {
 public:
  bool TryGetDefinition(Standard standard,
                        const std::string& vits_type,
                        VitsDefinition* out_definition,
                        std::string* error) const override;
};

}  // namespace videosynth
