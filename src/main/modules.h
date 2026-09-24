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

#include <memory>
#include "utils/module.h"
#include <vector>

namespace Modules
{
inline std::vector<CModule> allModules;

inline std::unique_ptr<CModule> schemaSystem = nullptr;
inline std::unique_ptr<CModule> tier0 = nullptr;

// Game structs are read without knowing if their layout still matches, these check that pointers read from them
// point into a loaded module (where names and registration objects are), so a changed layout fails instead of crashing.
inline bool IsInModule(const CModule& module, const void* pointer)
{
	return pointer >= module.m_base && pointer < (const uint8_t*)module.m_base + module.m_size;
}

inline const CModule* FindModuleContaining(const void* pointer)
{
	for (auto module : { tier0.get(), schemaSystem.get() })
	{
		if (module && IsInModule(*module, pointer))
			return module;
	}

	for (const auto& module : allModules)
	{
		if (IsInModule(module, pointer))
			return &module;
	}

	return nullptr;
}

// A null terminated string of printable characters inside a loaded module
inline bool IsValidName(const char* name, size_t maxLength = 256)
{
	auto module = FindModuleContaining(name);
	if (!module)
		return false;

	auto end = (const char*)module->m_base + module->m_size;
	for (size_t i = 0; i < maxLength && name + i < end; i++)
	{
		if (name[i] == '\0')
			return i > 0;

		if ((uint8_t)name[i] < 0x20 || (uint8_t)name[i] > 0x7E)
			return false;
	}

	return false;
}

} // namespace Modules