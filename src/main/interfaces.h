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

#include <eiface.h>

class IFileSystem;
class IVEngineServer2;
class IGameResourceService;
class INetworkServerService;
class CSchemaSystem;

namespace Interfaces {

inline IFileSystem* fileSystem = nullptr;
inline IVEngineServer2* engineServer = nullptr;
inline IGameResourceService* gameResourceServiceServer = nullptr;
inline INetworkServerService* networkServerService = nullptr;
inline CSchemaSystem* schemaSystem = nullptr;
inline IServerGameDLL* server = nullptr;
inline ICvar* cvar = nullptr;

} // namespace Interfaces