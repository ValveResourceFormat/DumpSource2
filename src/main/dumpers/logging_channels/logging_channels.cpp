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

#include "logging_channels.h"
#include "globalvariables.h"
#include "modules.h"
#include <tier0/logging.h>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <spdlog/spdlog.h>

namespace Dumpers::LoggingChannels
{

using LoggingChannel_t = CLoggingSystem::LoggingChannel_t;

static const char* GetVerbosityName(LoggingVerbosity_t verbosity)
{
	switch (verbosity)
	{
		case LV_OFF:
			return "off";
		case LV_ESSENTIAL:
			return "essential";
		case LV_DEFAULT:
			return "default";
		case LV_DETAILED:
			return "detailed";
		case LV_MAX:
			return "max";
	}

	return "unknown";
}

// Channels are registered by each module when it is loaded, and are enumerated with tier0 exports
bool Dump()
{
	typedef LoggingChannelID_t (*GetFirstChannelIDFn)();
	typedef LoggingChannelID_t (*GetNextChannelIDFn)(LoggingChannelID_t channelID);
	typedef const LoggingChannel_t* (*GetChannelFn)(LoggingChannelID_t channelID);
	typedef int (*GetChannelIntFn)(LoggingChannelID_t channelID);

	auto tier0 = Modules::tier0->m_hModule;
	auto getFirstChannelID = (GetFirstChannelIDFn)dlsym(tier0, "LoggingSystem_GetFirstChannelID");
	auto getNextChannelID = (GetNextChannelIDFn)dlsym(tier0, "LoggingSystem_GetNextChannelID");
	auto getChannel = (GetChannelFn)dlsym(tier0, "LoggingSystem_GetChannel");
	auto getFlags = (GetChannelIntFn)dlsym(tier0, "LoggingSystem_GetChannelFlags");
	auto getVerbosity = (GetChannelIntFn)dlsym(tier0, "LoggingSystem_GetChannelVerbosity");
	auto getColor = (GetChannelIntFn)dlsym(tier0, "LoggingSystem_GetChannelColor");

	if (!getFirstChannelID || !getNextChannelID || !getChannel || !getFlags || !getVerbosity || !getColor)
	{
		spdlog::critical("Could not find LoggingSystem exports in tier0, not writing logging_channels.txt");
		return false;
	}

	std::map<std::string, std::string> channels;
	std::set<std::string> tagNames;

	// Nothing changes channels before this, so the current flags and verbosity are the defaults
	for (auto id = getFirstChannelID(); id != INVALID_LOGGING_CHANNEL_ID; id = getNextChannelID(id))
	{
		auto channel = getChannel(id);

		// tier0 can change the layout while the exports stay the same, so compare the fields with what they return.
		// Tags are allocated from a static array in tier0.
		bool valid = channel->m_ID == id && channel->m_Flags == getFlags(id) && channel->m_Verbosity == getVerbosity(id) && channel->m_SpewColor.GetRawColor() == getColor(id) && memchr(channel->m_Name, '\0', sizeof(channel->m_Name));

		for (auto tag = channel->m_pFirstTag; valid && tag; tag = tag->m_pNextTag)
			valid = Modules::IsInModule(*Modules::tier0, tag) && Modules::IsValidName(tag->m_pTagName);

		if (!valid)
		{
			spdlog::critical("Logging channel {} does not match tier0, LoggingChannel_t in the SDK needs updating. Not writing logging_channels.txt", id);
			return false;
		}

		std::string line = fmt::format(" (verbosity: {}", GetVerbosityName(channel->m_Verbosity));

		if (channel->m_Flags & LCF_CONSOLE_ONLY)
			line += ", console only";
		if (channel->m_Flags & LCF_DO_NOT_ECHO)
			line += ", do not echo";

		const auto& color = channel->m_SpewColor;
		line += fmt::format(", color: {:02X}{:02X}{:02X}{:02X}", color.r(), color.g(), color.b(), color.a());

		std::string tags;
		for (auto tag = channel->m_pFirstTag; tag; tag = tag->m_pNextTag)
		{
			tags += fmt::format("{}{}", tags.empty() ? "" : ", ", tag->m_pTagName);
			tagNames.insert(tag->m_pTagName);
		}

		if (!tags.empty())
			line += fmt::format(", tags: {}", tags);

		channels[channel->m_Name] = line + ")";
	}

	std::ofstream output(Globals::outputPath / "logging_channels.txt");
	for (const auto& [name, line] : channels)
	{
		output << name << line << "\n";
		Globals::stringsIgnoreStream << name << "\n";
	}

	for (const auto& tag : tagNames)
		Globals::stringsIgnoreStream << tag << "\n";

	spdlog::info("Wrote {} logging channels to logging_channels.txt", channels.size());
	return true;
}

} // namespace Dumpers::LoggingChannels
