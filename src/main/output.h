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

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>
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

inline std::string ToLowerAscii(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return text;
}

// Module names have a / for modules in subfolders, like tools/hammer
inline std::string GetModuleFileName(std::string module)
{
	std::replace(module.begin(), module.end(), '/', '_');
	return module;
}

// The name in keep of a file or folder written by a dumper, by its name without extension for files, or nothing if it's not kept.
// Windows filesystems keep the old name when writing to a file whose name differs only in case, like after a class
// is renamed from CFooID to CFooId, so such a file is renamed to the name in keep.
template <typename Names>
inline std::optional<std::string> GetKeptOutputName(const std::filesystem::directory_entry& entry, const Names& keep)
{
	const auto& path = entry.path();
	const auto name = entry.is_directory() ? path.filename().string() : path.stem().string();
	if (keep.contains(name))
		return name;

#ifdef _WIN32
	const auto lowerName = ToLowerAscii(name);
	for (const std::string& keptName : keep)
	{
		if (ToLowerAscii(keptName) != lowerName)
			continue;

		const auto renamed = path.parent_path() / (keptName + (entry.is_directory() ? "" : path.extension().string()));
		spdlog::info("Renaming {} to {}", path.generic_string(), renamed.filename().generic_string());
		std::filesystem::rename(path, renamed);
		return keptName;
	}
#endif

	return std::nullopt;
}

// Removes files in a folder whose name without extension is not in keep, left over from things the game no longer has
template <typename Names>
inline void RemoveOrphanFiles(const std::filesystem::path& directory, const Names& keep, const char* kind)
{
	// Collected first, as files are renamed and removed
	std::vector<std::filesystem::directory_entry> entries(std::filesystem::directory_iterator(directory), {});

	for (const auto& entry : entries)
	{
		if (!GetKeptOutputName(entry, keep))
		{
			spdlog::info("Removing orphan {} file {}", kind, entry.path().generic_string());
			std::filesystem::remove(entry.path());
		}
	}
}
