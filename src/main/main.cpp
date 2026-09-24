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

#include "interfaces.h"
#include "globalvariables.h"
#include "appframework.h"
#include "output.h"
#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

#include "dumpers/concommands/concommands.h"
#include "dumpers/entities/entities.h"
#include "dumpers/interfaces/interfaces.h"
#include "dumpers/logging_channels/logging_channels.h"
#include "dumpers/schemas/schemas.h"
#include "dumpers/module_metadata/module_metadata.h"

#include <fmt/format.h>

static bool WriteStringsIgnore()
{
	const auto path = Globals::outputPath / ".stringsignore";
	std::ofstream file(path);
	file << Globals::stringsIgnoreStream.str();
	return CloseOutput(file, path);
}

static bool WriteSchemasJson()
{
	nlohmann::ordered_json root;
	root["generator"] = "https://github.com/ValveResourceFormat/DumpSource2";

	if (!Globals::sourceRevision.empty())
		root["revision"] = std::stoi(Globals::sourceRevision);

	if (!Globals::versionDate.empty())
		root["version_date"] = Globals::versionDate;

	if (!Globals::versionTime.empty())
		root["version_time"] = Globals::versionTime;

	for (const auto& [name, section] : Globals::schemasJson)
		root[name] = section;

	const auto path = Globals::outputPath / "schemas.json";
	std::ofstream output(path);
	output << root;

	if (!CloseOutput(output, path))
		return false;

	spdlog::info("Wrote schemas.json");
	return true;
}

// Parses steam.inf for version info
static void ReadSteamInf()
{
	auto steamInfPath = std::filesystem::current_path() / fmt::format("../../{}/steam.inf", GAME_PATH);
	std::ifstream steamInf(steamInfPath);
	if (!steamInf.is_open())
	{
		spdlog::warn("Failed to open {}, the version will be missing from schemas.json", steamInfPath.lexically_normal().generic_string());
		return;
	}

	std::string line;
	while (std::getline(steamInf, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();

		if (line.starts_with("SourceRevision="))
			Globals::sourceRevision = line.substr(15);
		else if (line.starts_with("VersionDate="))
			Globals::versionDate = line.substr(12);
		else if (line.starts_with("VersionTime="))
			Globals::versionTime = line.substr(12);
	}

	spdlog::info("Game revision {} built {} {}", Globals::sourceRevision, Globals::versionDate, Globals::versionTime);
}

static int Run(int argc, char** argv)
{
	if (argc <= 1)
	{
		printf("Usage: DumpSource2 <output path>\n");
		return 1;
	}

	Globals::outputPath = argv[1];

	if (!std::filesystem::is_directory(Globals::outputPath))
	{
		spdlog::critical("Output path {} is not an existing folder", Globals::outputPath.generic_string());
		return 1;
	}

	spdlog::info("Dumping {} to {}", GAME_PATH, std::filesystem::absolute(Globals::outputPath).generic_string());
	ReadSteamInf();

	// Each part is dumped independently, so one of them breaking does not lose the others
	int exitCode = 0;

	InitializeModules();

	if (!Dumpers::ConCommands::Dump())
		exitCode = 1;

	if (!WriteStringsIgnore())
		exitCode = 1;

	if (InitializeSchemas())
	{
		if (!Dumpers::Schemas::Dump())
		{
			spdlog::critical("Not writing schemas, see above");
			exitCode = 1;
		}

		if (!Dumpers::ModuleMetadata::Dump())
			exitCode = 1;
	}
	else
	{
		spdlog::critical("Not writing schemas or module metadata, see above");
		exitCode = 1;
	}

	if (!WriteStringsIgnore())
		exitCode = 1;

	// These read more game structs directly, so they run last to not lose the dumps above if they crash
	if (!Dumpers::Entities::Dump())
		exitCode = 1;

	if (!Dumpers::Interfaces::Dump())
		exitCode = 1;

	if (!Dumpers::LoggingChannels::Dump())
		exitCode = 1;

	if (!WriteStringsIgnore())
		exitCode = 1;

	if (exitCode != 0)
	{
		spdlog::critical("Dump is incomplete, not writing schemas.json, exiting with code {}", exitCode);
		return exitCode;
	}

	if (!WriteSchemasJson())
		return 1;

	spdlog::info("Dumped successfully");
	return 0;
}

int main(int argc, char** argv)
{
	spdlog::cfg::load_env_levels("LOGLEVEL");

	int exitCode;

	try
	{
		exitCode = Run(argc, argv);
	}
	catch (const std::exception& e)
	{
		spdlog::critical("Unhandled exception: {}", e.what());
		exitCode = 1;
	}

	// skips atexit calls that cause a segfault only while unregistering cvar callbacks
#ifdef WIN32
	TerminateProcess(GetCurrentProcess(), exitCode);
#else
	_Exit(exitCode);
#endif
}
