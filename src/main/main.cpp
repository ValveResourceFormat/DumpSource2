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

#include "main.h"
#include "interfaces.h"
#include "globalvariables.h"
#include "appframework.h"
#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

#include "dumpers/concommands/concommands.h"
#include "dumpers/schemas/schemas.h"
#include "dumpers/module_metadata/module_metadata.h"

#include <fmt/format.h>

void Usage()
{
	printf("Usage: DumpSource2 <output path>\n");
}

void WriteStringsIgnore()
{
	std::ofstream file(Globals::outputPath / ".stringsignore");
	file << Globals::stringsIgnoreStream.str();
}

int main(int argc, char** argv)
{
	spdlog::cfg::load_env_levels("LOGLEVEL");

	if (argc <= 1)
	{
		Usage();
		return 0;
	}

	Globals::outputPath = argv[1];

	if (!std::filesystem::is_directory(Globals::outputPath))
	{
		printf("Output path is not a valid folder\n");
		return 0;
	}

	spdlog::info("Starting Source2Dumper");

	// Parse steam.inf for version info
	{
		auto steamInfPath = std::filesystem::current_path() / fmt::format("../../{}/steam.inf", GAME_PATH);
		std::ifstream steamInf(steamInfPath);
		if (steamInf.is_open())
		{
			std::string line;
			while (std::getline(steamInf, line))
			{
				if (line.starts_with("SourceRevision="))
					Globals::sourceRevision = line.substr(15);
				else if (line.starts_with("VersionDate="))
					Globals::versionDate = line.substr(12);
				else if (line.starts_with("VersionTime="))
					Globals::versionTime = line.substr(12);
			}
			spdlog::info("Read steam.inf: revision={} date={} time={}", Globals::sourceRevision, Globals::versionDate, Globals::versionTime);
		}
		else
		{
			spdlog::warn("Failed to open {}", steamInfPath.generic_string());
		}
	}

	// Each part is dumped independently, so one of them breaking does not lose the others
	int exitCode = 0;

	InitializeModules();

	if (!Dumpers::ConCommands::Dump())
		exitCode = 1;

	WriteStringsIgnore();

	if (InitializeSchemas())
	{
		Dumpers::Schemas::Dump();
		Dumpers::ModuleMetadata::Dump();
		WriteStringsIgnore();
	}
	else
	{
		spdlog::critical("Not writing schemas or module metadata, see above");
		exitCode = 1;
	}

	if (exitCode == 0)
		spdlog::info("Dumped successfully");
	else
		spdlog::critical("Dump is incomplete, exiting with code {}", exitCode);

	// skips atexit calls that cause a segfault only while unregistering cvar callbacks
#ifdef WIN32
	TerminateProcess(GetCurrentProcess(), exitCode);
#else
	_Exit(exitCode);
#endif
}