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

#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

// Closes a written file. Returns false if it could not be opened or written to, like on a full disk.
inline bool CloseOutput(std::ofstream& output, const std::filesystem::path& path)
{
	output.close();

	if (output.fail())
	{
		spdlog::critical("Failed to write {}", path.generic_string());
		return false;
	}

	return true;
}

// Removes files in a folder whose name without extension is not in keep, left over from things the game no longer has
template <typename Names>
inline void RemoveOrphanFiles(const std::filesystem::path& directory, const Names& keep, const char* kind)
{
	for (const auto& entry : std::filesystem::directory_iterator(directory))
	{
		if (!keep.contains(entry.path().stem().string()))
		{
			spdlog::info("Removing orphan {} file {}", kind, entry.path().generic_string());
			std::filesystem::remove(entry.path());
		}
	}
}
