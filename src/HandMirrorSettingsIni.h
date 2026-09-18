#pragma once

#include "HandMirrorSettings.h"
#include "MirrorRenderDistance.h"

#include <array>
#include <charconv>
#include <cstdlib>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace HandMirrorSettingsIni
{
	inline constexpr std::string_view kSection = "[HandMirror]";
	inline constexpr std::string_view kZoomKey = "fPortraitZoom";
	inline constexpr std::string_view kWheelKey = "bMouseWheelZoom";
	inline constexpr std::string_view kLoweredKey = "bReflectWhenLowered";
	inline constexpr std::string_view kRaisedResolutionKey = "iRaisedResolution";
	inline constexpr std::string_view kLoweredResolutionKey = "iLoweredResolution";
	inline constexpr std::string_view kShadowsKey = "bShadows";
	inline constexpr std::string_view kDistanceKey = "iRenderDistance";
	inline constexpr std::string_view kShadowDistanceKey = "iShadowDistance";

	[[nodiscard]] inline std::string Trim(const std::string_view text)
	{
		const auto first = text.find_first_not_of(" \t\r\n");
		return first == std::string_view::npos ? std::string{} :
			std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
	}

	struct Values
	{
		float zoom{ HandMirrorSettings::kDefaultPortraitZoom };
		bool wheel{ HandMirrorSettings::kDefaultWheelZoom };
		bool lowered{ HandMirrorSettings::kDefaultReflectWhenLowered };
		int raisedResolution{ HandMirrorSettings::kDefaultRaisedResolution };
		int loweredResolution{ HandMirrorSettings::kDefaultLoweredResolution };
		bool shadows{ true };
		int renderDistance{ MirrorRenderDistance::kHandDefault };
		int shadowDistance{ MirrorRenderDistance::kShadowDefault };
	};

	[[nodiscard]] inline Values Read(std::istream& stream)
	{
		Values result;
		bool inSection = false;
		for (std::string line; std::getline(stream, line);) {
			const auto text = Trim(line);
			if (text.empty() || text[0] == ';' || text[0] == '#')
				continue;
			if (text[0] == '[') {
				inSection = text == kSection;
				continue;
			}
			const auto equals = text.find('=');
			if (!inSection || equals == std::string::npos)
				continue;
			const auto key = Trim(text.substr(0, equals));
			const auto value = Trim(std::string_view(text).substr(equals + 1,
				text.find_first_of(";#", equals + 1) - equals - 1));
			if (key == kZoomKey) {
				char* end = nullptr;
				const float zoom = std::strtof(value.c_str(), &end);
				if (end != value.c_str() && *end == '\0' && std::isfinite(zoom))
					result.zoom = HandMirrorSettings::ClampPortraitZoom(zoom);
			} else if (key == kDistanceKey || key == kShadowDistanceKey) {
				int distance{};
				const auto parsed = std::from_chars(value.data(), value.data() + value.size(), distance);
				if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()) {
					if (key == kShadowDistanceKey) result.shadowDistance = MirrorRenderDistance::ClampShadow(distance);
					else result.renderDistance = MirrorRenderDistance::Clamp(distance);
				}
			} else if (key == kRaisedResolutionKey || key == kLoweredResolutionKey) {
				int size{};
				const auto parsed = std::from_chars(value.data(), value.data() + value.size(), size);
				if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()) {
					if (key == kRaisedResolutionKey)
						result.raisedResolution = HandMirrorSettings::NormalizeResolution(size,
							HandMirrorSettings::kDefaultRaisedResolution);
					else
						result.loweredResolution = HandMirrorSettings::NormalizeResolution(size,
							HandMirrorSettings::kDefaultLoweredResolution);
				}
			} else if (key == kWheelKey || key == kLoweredKey || key == kShadowsKey) {
				auto& target = key == kWheelKey ? result.wheel : key == kLoweredKey ? result.lowered : result.shadows;
				if (value == "1" || value == "true" || value == "True")
					target = true;
				else if (value == "0" || value == "false" || value == "False")
					target = false;
			}
		}
		return result;
	}

	/** Update owned keys, including duplicates, preserving all other lines. */
	[[nodiscard]] inline std::vector<std::string> Update(
		const std::vector<std::string>& lines, const Values values)
	{
		const std::array keys{ kZoomKey, kWheelKey, kLoweredKey, kRaisedResolutionKey, kLoweredResolutionKey, kShadowsKey, kDistanceKey, kShadowDistanceKey };
		const std::array replacements{
			std::string(kZoomKey) + "=" + std::to_string(HandMirrorSettings::ClampPortraitZoom(values.zoom)),
			std::string(kWheelKey) + (values.wheel ? "=1" : "=0"),
			std::string(kLoweredKey) + (values.lowered ? "=1" : "=0"),
			std::string(kRaisedResolutionKey) + "=" + std::to_string(HandMirrorSettings::NormalizeResolution(
				values.raisedResolution, HandMirrorSettings::kDefaultRaisedResolution)),
			std::string(kLoweredResolutionKey) + "=" + std::to_string(HandMirrorSettings::NormalizeResolution(
				values.loweredResolution, HandMirrorSettings::kDefaultLoweredResolution)),
			std::string(kShadowsKey) + (values.shadows ? "=1" : "=0"),
			std::string(kDistanceKey) + "=" + std::to_string(MirrorRenderDistance::Clamp(values.renderDistance)),
			std::string(kShadowDistanceKey) + "=" + std::to_string(MirrorRenderDistance::ClampShadow(values.shadowDistance)) };
		std::array<bool, keys.size()> written{};
		std::vector<std::string> output;
		bool inSection = false;
		bool foundSection = false;
		const auto appendMissing = [&] {
			for (std::size_t key = 0; key < keys.size(); ++key) {
				if (!written[key]) {
					output.push_back(replacements[key]);
					written[key] = true;
				}
			}
		};
		for (const auto& line : lines) {
			const auto text = Trim(line);
			if (!text.empty() && text[0] == '[') {
				if (inSection)
					appendMissing();
				inSection = text == kSection;
				foundSection = foundSection || inSection;
			}
			bool replaced = false;
			const auto equals = text.find('=');
			if (inSection && equals != std::string::npos) {
				for (std::size_t key = 0; key < keys.size(); ++key) {
					if (Trim(text.substr(0, equals)) == keys[key]) {
						output.push_back(replacements[key]);
						written[key] = true;
						replaced = true;
						break;
					}
				}
			}
			if (!replaced)
				output.push_back(line);
		}
		if (!foundSection) {
			if (!output.empty() && !output.back().empty())
				output.emplace_back();
			output.emplace_back(kSection);
		}
		appendMissing();
		return output;
	}
}
