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
#include <interfaces/interfaces.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <spdlog/spdlog.h>

namespace Dumpers::Interfaces
{

// Returns false if a module exports CreateInterface but its interface list could not be found
bool Dump()
{
#ifdef _WIN32
	std::map<std::string, std::set<std::string>> interfaces;
	std::string failed;

	std::vector<const CModule*> modules = { Modules::tier0.get(), Modules::schemaSystem.get() };
	for (const auto& module : Modules::allModules)
		modules.push_back(&module);

	for (auto module : modules)
	{
		auto createInterface = dlsym(module->m_hModule, "CreateInterface");
		if (!createInterface)
			continue;

		// CreateInterface walks the InterfaceReg list, and its first instruction loads the list head.
		// The head and registrations are static objects in the module, so anything else means the code changed.
		auto head = Modules::GetGlobalFromSignatureMatch<InterfaceReg*>(createInterface);
		auto regs = Modules::IsInModule(*module, head) ? *head : nullptr;
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

	std::ofstream output(Globals::outputPath / "interfaces.txt");
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

	spdlog::info("Wrote {} interfaces from {} modules to interfaces.txt", count, interfaces.size());
#else
	spdlog::info("Interfaces are not supported on this platform yet");
#endif

	return true;
}

} // namespace Dumpers::Interfaces
