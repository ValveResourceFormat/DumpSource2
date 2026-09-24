/**
 * =============================================================================
 * DumpSource2
 * Copyright (C) 2024 ValveResourceFormat Contributors
 *
 * source2gen
 * Copyright 2024 neverlosecc
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
#include <map>
#include <string>
#include <schemasystem/schematypes.h>
#include "schemas.h"

struct CSchemaVarName
{
	const char* m_pszName;
	const char* m_pszType;
};

struct CSchemaNetworkOverride
{
	const char* m_pszClassName;
	const char* m_pszFieldName;
};

struct CSchemaSendProxyRecipientsFilter
{
	void* unk;
	void* filterFunction;
	const char* m_pszName;
	void* unk2;
};

namespace Dumpers::Schemas
{

// First value of each metadata missing from metadatalist.h, described to help pick its type
extern std::map<std::string, std::string> g_unknownMetadataSamples;

// Set when any KV3 defaults could not be parsed, which means the schemas should not be written
extern bool g_bInvalidKV3Defaults;

// classInfo is the class the metadata is on or in, if any
IntermediateMetadata GetMetadata(const SchemaMetadataEntryData_t& entry, const char* metadataTargetName, const SchemaClassInfoData_t* classInfo = nullptr);

} // namespace Dumpers::Schemas