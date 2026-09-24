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

#include "interfaces.h"
#include "globalvariables.h"
#include "modules.h"
#include "gamedata.h"
#include "output.h"
#include <interfaces/interfaces.h>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <spdlog/spdlog.h>

namespace Dumpers::Interfaces
{

// The list head load is a RIP relative mov (REX.W 8B with a disp32 operand), anything else means the code changed
static bool IsRipRelativeLoad(const uint8_t* code)
{
	return (code[0] == 0x48 || code[0] == 0x4C) && code[1] == 0x8B && (code[2] & 0xC7) == 0x05;
}

// Returns false if a module exports CreateInterface but its interface list could not be found
bool Dump()
{
	std::map<std::string, std::set<std::string>> interfaces;
	std::string failed;

	std::vector<const CModule*> modules = { Modules::tier0.get(), Modules::schemaSystem.get() };
	for (const auto& module : Modules::allModules)
		modules.push_back(&module);

	for (auto module : modules)
	{
		auto createInterface = (const uint8_t*)dlsym(module->m_hModule, "CreateInterface");
		if (!createInterface)
			continue;

		// The head and registrations are static objects in the module, so anything else means the code changed.
		auto load = createInterface + GameData::g_CreateInterfaceListOffset;
		auto head = IsRipRelativeLoad(load) ? Modules::GetGlobalFromSignatureMatch<InterfaceReg*>(load) : nullptr;
		auto regs = head && Modules::IsInModule(*module, head) ? *head : nullptr;
		bool valid = regs != nullptr;
		std::set<std::string> names;

		for (auto reg = regs; valid && reg; reg = reg->m_pNext)
		{
			valid = Modules::IsInModule(*module, reg) && Modules::IsValidName(reg->m_pName);
			if (valid)
				names.insert(reg->m_pName);
		}

		if (!valid)
		{
			failed += fmt::format("{}{}", failed.empty() ? "" : ", ", module->m_pszModule);
			continue;
		}

		interfaces[module->m_pszModule] = std::move(names);
	}

	if (!failed.empty())
	{
		spdlog::critical("Could not read the interface list from CreateInterface of {}, not writing interfaces.txt", failed);
		return false;
	}

	const auto path = Globals::outputPath / "interfaces.txt";
	std::ofstream output(path);
	size_t count = 0;

	for (const auto& [module, names] : interfaces)
	{
		output << module << "\n";

		for (const auto& name : names)
		{
			output << "\t" << name << "\n";
			Globals::stringsIgnoreStream << name << "\n";
		}

		output << "\n";
		count += names.size();
	}

	if (!CloseOutput(output, path))
		return false;

	spdlog::info("Wrote {} interfaces from {} modules to interfaces.txt", count, interfaces.size());
	return true;
}

} // namespace Dumpers::Interfaces
