#pragma once
#include "HandMirrorSettingsIni.h"
#include "MirrorFleetPolicy.h"
#include "MirrorPrivateShadow.h"
#include "MirrorQualityPreset.h"
#include "MirrorPerformance.h"
#include "MirrorSurfaceLightPolicy.h"
#include <charconv>

namespace MirrorFleetSettingsIni
{
	struct Values { int resolution{4096}, refreshHz{0}; bool dynamicResolution{true}, shadows{true}; int renderDistance{MirrorRenderDistance::kPlacedDefault}; int handRefreshHz{30}; int handLoweredRefreshHz{30};
		// Private sun-shadow map sizes and the player-as-caster switch. Owner
		// 2026-09-15: lower shadow resolution is wanted if it does not shimmer
		// (it cannot here -- the map origin snaps to a 16-texel grid derived
		// from its own size), and the player's own shadow needs an off switch to
		// test the reflected face.
		int shadowResolution{static_cast<int>(MirrorPrivateShadow::kDefaultResolution)};
		int shadowDetailResolution{static_cast<int>(MirrorPrivateShadow::kDefaultDetailResolution)};
		bool playerShadow{true};
		// Shadow reach, separate from object reach since run 36 showed a wider
		// object slider dragging in caster work that moves no visible shadow.
		int shadowDistance{MirrorRenderDistance::kShadowDefault};
		// Percent, not a fraction, so the reader stays integer-only. Below this
		// share of full sunlight the private maps are not generated: at night,
		// under heavy overcast and in rain nothing they hold would be visible.
		int minimumSunLight{15};
		// Legacy preset toggle; adaptive Automatic is represented by qualityMode.
		bool automaticQuality{false};
		int qualityLevel{static_cast<int>(MirrorQualityPreset::Level::kMedium)};
		// Owner 2026-09-16: Manual / Preset / Automatic. bAutomaticQuality stays
		// for older INIs and readers (1 = Preset); iQualityMode wins when present.
		int qualityMode{static_cast<int>(MirrorQualityPreset::kDefaultMode)};
		int targetFPS{static_cast<int>(MirrorAdaptiveQuality::kDefaultTargetFPS)};
		// Owner 2026-09-16: development hotkeys (F8 panel, F11 mirrors) off by default.
		bool debugHotkeys{false};
		// Owner 2026-09-17: reflected face light strength, percent (0 = off).
		int faceLight{MirrorSurfaceLightPolicy::kDefaultFaceLightPercent}; };
	inline constexpr std::string_view kSection = "[PlacedMirrors]";
	// All live controls must be saved together. A positional aggregate covering
	// only the first seven fields silently reset later controls to defaults.
	[[nodiscard]] inline Values CurrentValues() noexcept
	{
		return {
			.resolution = MirrorFleetPolicy::resolution.load(),
			.refreshHz = MirrorFleetPolicy::refreshHz.load(),
			.dynamicResolution = MirrorFleetPolicy::dynamicResolution.load(),
			.shadows = MirrorShadowSettings::placed.load(),
			.renderDistance = MirrorRenderDistance::Get(false),
			.handRefreshHz = MirrorFleetPolicy::handRefreshHz.load(),
			.handLoweredRefreshHz = MirrorFleetPolicy::handLoweredRefreshHz.load(),
			.shadowResolution = static_cast<int>(MirrorPrivateShadow::Resolution(false)),
			.shadowDetailResolution = static_cast<int>(MirrorPrivateShadow::Resolution(true)),
			.playerShadow = MirrorShadowSettings::playerShadow.load(),
			.shadowDistance = MirrorRenderDistance::GetShadow(false),
			.minimumSunLight = static_cast<int>((std::clamp)(MirrorShadowSettings::minimumSunLuminance.load(), 0.0f, 1.0f) * 100.0f + 0.5f),
			.automaticQuality = MirrorQualityPreset::Automatic(),
			.qualityLevel = static_cast<int>(MirrorQualityPreset::CurrentLevel()),
			.qualityMode = static_cast<int>(MirrorQualityPreset::CurrentMode()),
			.targetFPS = MirrorQualityPreset::targetFPS.load(),
			.debugHotkeys = MirrorPerformance::DebugHotkeysEnabled(),
			.faceLight = MirrorSurfaceLightPolicy::ClampFaceLight(MirrorSurfaceLightPolicy::faceLightPercent.load())
		};
	}
	inline constexpr std::array<std::string_view, 18> kKeys{ "iResolution", "iRefreshHz", "bDynamicResolution", "bShadows", "iRenderDistance", "iHandRefreshHz", "iHandLoweredRefreshHz", "iShadowResolution", "iShadowDetailResolution", "bPlayerShadow", "iShadowDistance", "iMinimumSunLight", "bAutomaticQuality", "iQualityLevel", "iQualityMode", "iTargetFPS", "bDebugHotkeys", "iFaceLight" };
	[[nodiscard]] inline int ShadowResolution(int value, bool detail) noexcept
	{
		return MirrorPrivateShadow::ValidResolution(static_cast<unsigned>(value)) ? value :
			static_cast<int>(detail ? MirrorPrivateShadow::kDefaultDetailResolution :
				MirrorPrivateShadow::kDefaultResolution);
	}
	inline Values Read(std::istream& stream)
	{
		Values result;
		bool active = false, modeExplicit = false;
		for (std::string line; std::getline(stream, line);) {
			const auto text = HandMirrorSettingsIni::Trim(line);
			if (text.empty() || text[0] == ';' || text[0] == '#') continue;
			if (text[0] == '[') { active = text == kSection; continue; }
			const auto equals = text.find('=');
			if (!active || equals == std::string::npos) continue;
			const auto key = HandMirrorSettingsIni::Trim(text.substr(0, equals));
			const auto value = HandMirrorSettingsIni::Trim(std::string_view(text).substr(equals + 1,
				text.find_first_of(";#", equals + 1) - equals - 1));
			int parsed{};
			const auto read = std::from_chars(value.data(), value.data() + value.size(), parsed);
			if (read.ec != std::errc{} || read.ptr != value.data() + value.size()) continue;
			if (key == kKeys[0]) result.resolution = MirrorFleetPolicy::Resolution(parsed);
			if (key == kKeys[1]) result.refreshHz = MirrorFleetPolicy::Refresh(parsed);
			if (key == kKeys[2] && (parsed == 0 || parsed == 1)) result.dynamicResolution = parsed != 0;
			if (key == kKeys[3] && (parsed == 0 || parsed == 1)) result.shadows = parsed != 0;
			if (key == kKeys[4]) result.renderDistance = MirrorRenderDistance::Clamp(parsed);
			if (key == kKeys[5]) result.handRefreshHz = MirrorFleetPolicy::Refresh(parsed);
			if (key == kKeys[6]) result.handLoweredRefreshHz = MirrorFleetPolicy::Refresh(parsed);
			if (key == kKeys[7]) result.shadowResolution = ShadowResolution(parsed, false);
			if (key == kKeys[8]) result.shadowDetailResolution = ShadowResolution(parsed, true);
			if (key == kKeys[9] && (parsed == 0 || parsed == 1)) result.playerShadow = parsed != 0;
			if (key == kKeys[10]) result.shadowDistance = MirrorRenderDistance::ClampShadow(parsed);
			if (key == kKeys[11] && parsed >= 0 && parsed <= 100) result.minimumSunLight = parsed;
			if (key == kKeys[12] && (parsed == 0 || parsed == 1) && !modeExplicit) {
				result.automaticQuality = parsed != 0;
				result.qualityMode = static_cast<int>(parsed ? MirrorQualityPreset::Mode::kPreset : MirrorQualityPreset::Mode::kManual);
			}
			if (key == kKeys[14] && parsed >= 0 && parsed <= static_cast<int>(MirrorQualityPreset::Mode::kAutomatic)) {
				result.qualityMode = parsed;
				result.automaticQuality = parsed == static_cast<int>(MirrorQualityPreset::Mode::kPreset);
				modeExplicit = true;
			}
			if (key == kKeys[15]) result.targetFPS = static_cast<int>(MirrorAdaptiveQuality::TargetFPS(parsed));
			if (key == kKeys[16] && (parsed == 0 || parsed == 1)) result.debugHotkeys = parsed != 0;
			if (key == kKeys[17]) result.faceLight = MirrorSurfaceLightPolicy::ClampFaceLight(parsed);
			if (key == kKeys[13]) result.qualityLevel =
				static_cast<int>(MirrorQualityPreset::Clamp(static_cast<std::uint32_t>(
					parsed < 0 ? 1 : parsed)));
		}
		return result;
	}
	inline std::vector<std::string> Update(const std::vector<std::string>& lines, Values values)
	{
		const std::array replacements{
			std::string(kKeys[0]) + "=" + std::to_string(MirrorFleetPolicy::Resolution(values.resolution)),
			std::string(kKeys[1]) + "=" + std::to_string(MirrorFleetPolicy::Refresh(values.refreshHz)),
			std::string(kKeys[2]) + (values.dynamicResolution ? "=1" : "=0"),
			std::string(kKeys[3]) + (values.shadows ? "=1" : "=0"),
			std::string(kKeys[4]) + "=" + std::to_string(MirrorRenderDistance::Clamp(values.renderDistance)),
			std::string(kKeys[5]) + "=" + std::to_string(MirrorFleetPolicy::Refresh(values.handRefreshHz)),
			std::string(kKeys[6]) + "=" + std::to_string(MirrorFleetPolicy::Refresh(values.handLoweredRefreshHz)),
			std::string(kKeys[7]) + "=" + std::to_string(ShadowResolution(values.shadowResolution, false)),
			std::string(kKeys[8]) + "=" + std::to_string(ShadowResolution(values.shadowDetailResolution, true)),
			std::string(kKeys[9]) + (values.playerShadow ? "=1" : "=0"),
			std::string(kKeys[10]) + "=" + std::to_string(MirrorRenderDistance::ClampShadow(values.shadowDistance)),
			std::string(kKeys[11]) + "=" + std::to_string(
				(std::clamp)(values.minimumSunLight, 0, 100)),
			std::string(kKeys[12]) + (values.automaticQuality ? "=1" : "=0"),
			std::string(kKeys[13]) + "=" + std::to_string(static_cast<int>(
				MirrorQualityPreset::Clamp(static_cast<std::uint32_t>(
					values.qualityLevel < 0 ? 1 : values.qualityLevel)))),
			std::string(kKeys[14]) + "=" + std::to_string(static_cast<int>(MirrorQualityPreset::ClampMode(values.qualityMode))),
			std::string(kKeys[15]) + "=" + std::to_string(MirrorAdaptiveQuality::TargetFPS(values.targetFPS)),
			std::string(kKeys[16]) + (values.debugHotkeys ? "=1" : "=0"),
			std::string(kKeys[17]) + "=" + std::to_string(MirrorSurfaceLightPolicy::ClampFaceLight(values.faceLight)) };
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
