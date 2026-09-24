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
#include <optional>
#include <string>
#include <schemasystem/schematypes.h>

struct CSchemaVarName
{
	const char* m_pszName;
	const char* m_pszType;
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

bool HasMetadataValue(const SchemaMetadataEntryData_t& entry);
std::optional<std::string> GetMetadataValue(const SchemaMetadataEntryData_t& entry, const char* metadataTargetName);

} // namespace Dumpers::Schemas