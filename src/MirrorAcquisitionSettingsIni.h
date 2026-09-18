#pragma once
#include "HandMirrorSettingsIni.h"

namespace MirrorAcquisitionSettingsIni
{
	struct Values { bool merchants{ true }, crafting{ true }, homes{ true }, inns{ true }; };
	inline constexpr std::string_view kSection = "[Acquisition]";
	inline constexpr std::array<std::string_view, 4> kKeys{ "bMerchants", "bCrafting", "bHomes", "bInns" };
	inline Values Read(std::istream& stream)
	{
		Values result;
		bool active = false;
		for (std::string line; std::getline(stream, line);) {
			const auto text = HandMirrorSettingsIni::Trim(line);
			if (text.empty() || text[0] == ';' || text[0] == '#') continue;
			if (text[0] == '[') { active = text == kSection; continue; }
			const auto equals = text.find('=');
			if (!active || equals == std::string::npos) continue;
			const auto key = HandMirrorSettingsIni::Trim(text.substr(0, equals));
			const auto value = HandMirrorSettingsIni::Trim(std::string_view(text).substr(equals + 1,
				text.find_first_of(";#", equals + 1) - equals - 1));
			const bool yes = value == "1" || value == "true" || value == "True";
			const bool no = value == "0" || value == "false" || value == "False";
			if (!yes && !no) continue;
			if (key == kKeys[0]) result.merchants = yes;
			if (key == kKeys[1]) result.crafting = yes;
			if (key == kKeys[2]) result.homes = yes;
			if (key == kKeys[3]) result.inns = yes;
		}
		return result;
	}
	inline std::vector<std::string> Update(const std::vector<std::string>& lines, Values values)
	{
		const std::array replacements{ std::string(kKeys[0]) + (values.merchants ? "=1" : "=0"),
			std::string(kKeys[1]) + (values.crafting ? "=1" : "=0"),
			std::string(kKeys[2]) + (values.homes ? "=1" : "=0"),
			std::string(kKeys[3]) + (values.inns ? "=1" : "=0") };
		std::array<bool, kKeys.size()> written{};
		std::vector<std::string> output;
		bool active = false, found = false;
		const auto appendMissing = [&] {
			for (std::size_t i = 0; i < kKeys.size(); ++i) if (!written[i]) {
				output.push_back(replacements[i]); written[i] = true;
			}
		};
		for (const auto& line : lines) {
			const auto text = HandMirrorSettingsIni::Trim(line);
			if (!text.empty() && text[0] == '[') {
				if (active) appendMissing();
				active = text == kSection; found = found || active;
			}
			bool replaced = false;
			const auto equals = text.find('=');
			if (active && equals != std::string::npos) for (std::size_t i = 0; i < kKeys.size(); ++i) {
				if (HandMirrorSettingsIni::Trim(text.substr(0, equals)) == kKeys[i]) {
					output.push_back(replacements[i]); written[i] = true; replaced = true; break;
				}
			}
			if (!replaced) output.push_back(line);
		}
		if (!found) { if (!output.empty()) output.emplace_back(); output.emplace_back(kSection); }
		appendMissing();
		return output;
	}
}
