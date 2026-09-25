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

#include <charconv>
#include <iterator>
#include <string>
#include <fmt/format.h>

// Floats are written as the shortest text that reads back as the same value, like 100.1 or 1000000 rather than 1e+06,
// with at most 6 decimals for values that aren't exact in binary, like 0.015686275
template <typename T>
std::string FormatFloat(T value)
{
	char buffer[512];
	std::string text(buffer, std::to_chars(buffer, std::end(buffer), value, std::chars_format::fixed).ptr);

	if (auto dot = text.find('.'); dot != std::string::npos && text.size() - dot - 1 > 6)
	{
		text = fmt::format("{:.6f}", value);
		text.erase(text.find_last_not_of('0') + 1);

		if (text.back() == '.')
			text.pop_back();
	}

	return text == "-0" ? "0" : text;
}
