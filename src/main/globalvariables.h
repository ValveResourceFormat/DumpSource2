/**
 * =============================================================================
 * DumpSource2
 * Copyright (C) 2024 ValveResourceFormat Contributors
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

#include <string>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

namespace Globals
{

inline std::filesystem::path outputPath;
inline std::stringstream stringsIgnoreStream;

// Top-level arrays of schemas.json by name, like classes or convars, written only when every dumper succeeded
inline std::map<std::string, nlohmann::json> schemasJson;

inline std::string sourceRevision;
inline std::string versionDate;
inline std::string versionTime;

} // namespace Globals