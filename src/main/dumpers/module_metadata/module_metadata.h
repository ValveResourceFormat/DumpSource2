/**
 * =============================================================================
 * DumpSource2
 * Copyright (C) 2026 ValveResourceFormat Contributors
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <string_view>
#include <nlohmann/json.hpp>

class CModule;

namespace Dumpers::ModuleMetadata
{

// Returns false if any module's metadata could not be read or written
bool Dump();

// The module's metadata converted to JSON, null if the module has none, discarded if it could not be converted
nlohmann::ordered_json GetJSON(const CModule& module);

// Converts KV3 to JSON with tier0's SaveKV3AsJSON, discarded if it fails.
// NaN and infinity are strings like "-nan", which JSON has no value for.
nlohmann::ordered_json KV3ToJSON(void* kv3);

// Whether a string from KV3ToJSON is a float that is NaN or infinity
bool IsNonFiniteFloat(std::string_view text);

} // namespace Dumpers::ModuleMetadata
