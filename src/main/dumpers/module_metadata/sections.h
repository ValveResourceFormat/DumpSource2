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

#include <map>
#include <string>
#include <nlohmann/json.hpp>

namespace Dumpers::ModuleMetadata
{

// Writes the sections of a module's metadata as text, by file name. Keys that no section writer knows are moved to unhandled.
// Returns false if a section is not what its writer expects.
bool WriteSections(const char* moduleName, nlohmann::ordered_json metadata, std::map<std::string, std::string>& files, nlohmann::ordered_json& unhandled);

} // namespace Dumpers::ModuleMetadata
