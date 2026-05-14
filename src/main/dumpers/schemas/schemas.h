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
#include <string>
#include <vector>
#include <optional>
#include "schemasystem/schematypes.h"

namespace Dumpers::Schemas
{

struct IntermediateMetadata
{
	std::string name;
	std::optional<std::string> stringValue;
	bool hasValue;
};

struct IntermediateSchemaClassParent
{
	std::string name;
	std::string module;
	int32_t offset;
};

struct IntermediateSchemaClassField
{
	std::string name;
	int32_t offset;
	std::vector<IntermediateMetadata> metadata;
	CSchemaType* type;
};

struct IntermediateSchemaClass
{
	std::string name;
	std::string module;
	int32_t size;
	uint8_t alignment;
	bool isAbstract;
	std::vector<IntermediateMetadata> metadata;
	std::vector<IntermediateSchemaClassParent> parents;
	std::vector<IntermediateSchemaClassField> fields;
};

struct IntermediateSchemaEnumMember
{
	std::string name;
	int64_t value;
	std::vector<IntermediateMetadata> metadata;
};

struct IntermediateSchemaEnum
{
	std::string name;
	std::string module;
	std::optional<std::string> stringAlignment;
	std::vector<IntermediateMetadata> metadata;
	std::vector<IntermediateSchemaEnumMember> members;
};

void Dump();

} // namespace Dumpers::Schemas