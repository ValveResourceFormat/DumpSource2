/**
 * =============================================================================
 * CS2Dumper
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

#include "schemas.h"
#include "interfaces.h"
#include "schemasystem/schemasystem.h"

namespace Dumpers::Schemas
{

void Dump()
{
	auto schemaSystem = Interfaces::schemaSystem;

	const auto& typeScopes = schemaSystem->m_TypeScopes;

	for (auto i = 0; i < typeScopes.GetNumStrings(); ++i)
	{
		const auto& typeScope = typeScopes[i];
		printf("Type scope: %s\n", typeScope->m_szScopeName);

		const auto& classes = typeScope->m_ClassBindings;

		UtlTSHashHandle_t* handles = new UtlTSHashHandle_t[classes.Count()];
		classes.GetElements(0, classes.Count(), handles);

		for (int j = 0; j < classes.Count(); ++j) {
			auto class_info = classes[handles[j]];

			printf("\t - %s\n", class_info->m_pszName);
		}
	}
}

} // namespace Dumpers::Schemas