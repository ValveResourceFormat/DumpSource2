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
#include "utils/common.h"
#include "interfaces.h"
#include "globalvariables.h"
#include "appframework.h"

#include "dumpers/concommands/concommands.h"
#include "dumpers/schemas/schemas.h"

void Usage()
{
	printf("Usage: DumpSource2 <output path>\n");
}

int main(int argc, char** argv)
{
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

	Globals::stringsIgnoreStream = std::ofstream(Globals::outputPath / ".stringsignore");

	InitializeCoreModules();
	InitializeAppSystems();

	printf("Dumping\n");

	Dumpers::ConCommands::Dump();
	Dumpers::Schemas::Dump();
}