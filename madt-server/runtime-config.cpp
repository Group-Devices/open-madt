#include "runtime-config.h"

#include <cmath>
#include <limits>
#include <filesystem>

#include "loghelper/log.h"
#include "utils/json_file.hpp"

namespace Secretary::Madt {
	namespace {
		constexpr const char* CONFIG_FILENAME = "madt-config.json";
		constexpr int         MAX_EXTRA_ZONE_DIMENSION = 7680;

		bool isValidMode(const std::string& value,
		                 const std::initializer_list<const char*>& allowed)
		{
			for (const char* candidate : allowed) {
				if (value == candidate) {
					return true;
				}
			}
			return false;
		}

		bool loadOptionalString(const json& document, const char* key, std::string& destination)
		{
			const auto it = document.find(key);
			if (it == document.end() || it->is_null()) {
				return false;
			}
			if (!it->is_string()) {
				ELOG("MADT configuration %s has non-string %s", CONFIG_FILENAME, key);
				return false;
			}
			destination = it->get<std::string>();
			return true;
		}

		bool loadOptionalInt(const json& document,
		                     const char* key,
		                     int&        destination,
		                     int         minValue,
		                     int         maxValue)
		{
			const auto it = document.find(key);
			if (it == document.end() || it->is_null()) {
				return false;
			}
			if (!it->is_number_integer()) {
				ELOG("MADT configuration %s has non-integer %s", CONFIG_FILENAME, key);
				return false;
			}

			const int value = it->get<int>();
			if (value < minValue || value > maxValue) {
				ELOG("MADT configuration %s has out-of-range %s=%d", CONFIG_FILENAME, key, value);
				return false;
			}
			destination = value;
			return true;
		}

		void loadTabBarVisible(const json& document, RuntimeConfig& config)
		{
			const auto visibleIt = document.find("tabBarVisible");
			if (visibleIt == document.end() || visibleIt->is_null()) {
				return;
			}
			if (!visibleIt->is_boolean()) {
				ELOG("MADT configuration %s has non-boolean tabBarVisible", CONFIG_FILENAME);
				return;
			}
			config.tabBarVisible = visibleIt->get<bool>();
		}

		void loadOptionalBool(const json& document, const char* key, bool& destination)
		{
			const auto it = document.find(key);
			if (it == document.end() || it->is_null()) {
				return;
			}
			if (!it->is_boolean()) {
				ELOG("MADT configuration %s has non-boolean %s", CONFIG_FILENAME, key);
				return;
			}
			destination = it->get<bool>();
		}

		void loadTabBarEdge(const json& document, RuntimeConfig& config)
		{
			const auto edgeIt = document.find("tabBarEdge");
			if (edgeIt == document.end() || edgeIt->is_null()) {
				return;
			}
			if (!edgeIt->is_string()) {
				ELOG("MADT configuration %s has non-string tabBarEdge", CONFIG_FILENAME);
				return;
			}

			const auto edge = edgeIt->get<std::string>();
			if (edge == "top") {
				config.tabBarEdge = TabBarEdge::Top;
			} else if (edge == "bottom") {
				config.tabBarEdge = TabBarEdge::Bottom;
			} else if (edge == "left") {
				config.tabBarEdge = TabBarEdge::Left;
			} else if (edge == "right") {
				config.tabBarEdge = TabBarEdge::Right;
			} else {
				ELOG("MADT configuration %s has invalid tabBarEdge '%s'",
				     CONFIG_FILENAME,
				     edge.c_str());
			}
		}

		void loadTabBarDimension(const json& document, const char* key, int& destination)
		{
			const auto it = document.find(key);
			if (it == document.end() || it->is_null()) {
				return;
			}
			if (!it->is_number_integer()) {
				ELOG("MADT configuration %s has non-integer %s", CONFIG_FILENAME, key);
				return;
			}

			const int value = it->get<int>();
			if (value <= 0) {
				ELOG("MADT configuration %s has non-positive %s", CONFIG_FILENAME, key);
				return;
			}
			destination = value;
		}

		void loadShortcutLauncherCorner(const json& document, RuntimeConfig& config)
		{
			const auto cornerIt = document.find("shortcutLauncherCorner");
			if (cornerIt == document.end() || cornerIt->is_null()) {
				return;
			}
			if (!cornerIt->is_string()) {
				ELOG("MADT configuration %s has non-string shortcutLauncherCorner",
				     CONFIG_FILENAME);
				return;
			}

			const auto corner = cornerIt->get<std::string>();
			if (corner == "top-left") {
				config.shortcutLauncherCorner = ShortcutLauncherCorner::TopLeft;
			} else if (corner == "top-right") {
				config.shortcutLauncherCorner = ShortcutLauncherCorner::TopRight;
			} else {
				ELOG("MADT configuration %s has invalid shortcutLauncherCorner '%s'",
				     CONFIG_FILENAME,
				     corner.c_str());
			}
		}

		void loadExtraZonePlacement(const json& document, RuntimeConfig& config)
		{
			const auto placementIt = document.find("extraZonePlacement");
			if (placementIt == document.end() || placementIt->is_null()) {
				return;
			}
			if (!placementIt->is_string()) {
				ELOG("MADT configuration %s has non-string extraZonePlacement", CONFIG_FILENAME);
				return;
			}

			const auto placement = placementIt->get<std::string>();
			if (placement == "top") {
				config.extraZonePlacement = ExtraZonePlacement::Top;
			} else if (placement == "bottom") {
				config.extraZonePlacement = ExtraZonePlacement::Bottom;
			} else if (placement == "free") {
				config.extraZonePlacement = ExtraZonePlacement::Free;
			} else {
				ELOG("MADT configuration %s has invalid extraZonePlacement '%s'",
				     CONFIG_FILENAME,
				     placement.c_str());
			}
		}

		void loadExtraZoneRect(const json& document, RuntimeConfig& config)
		{
			const auto rectIt = document.find("extraZoneRect");
			if (rectIt == document.end() || rectIt->is_null()) {
				return;
			}
			if (!rectIt->is_object()) {
				ELOG("MADT configuration %s has non-object extraZoneRect", CONFIG_FILENAME);
				return;
			}

			auto loadRectInt = [](const json& rect,
			                      const char* key,
			                      int&        destination,
			                      int         minValue,
			                      int         maxValue) {
				const auto valueIt = rect.find(key);
				if (valueIt == rect.end() || valueIt->is_null()) {
					return;
				}
				if (!valueIt->is_number_integer()) {
					ELOG("MADT configuration %s has non-integer extraZoneRect.%s",
					     CONFIG_FILENAME,
					     key);
					return;
				}

				const int value = valueIt->get<int>();
				if (value < minValue || value > maxValue) {
					ELOG("MADT configuration %s has out-of-range extraZoneRect.%s=%d",
					     CONFIG_FILENAME,
					     key,
					     value);
					return;
				}
				destination = value;
			};

			loadRectInt(*rectIt, "x", config.extraZoneRect.x, 0, MAX_EXTRA_ZONE_DIMENSION);
			loadRectInt(*rectIt, "y", config.extraZoneRect.y, 0, MAX_EXTRA_ZONE_DIMENSION);
			loadRectInt(*rectIt, "width", config.extraZoneRect.width, 1, MAX_EXTRA_ZONE_DIMENSION);
			loadRectInt(*rectIt, "height", config.extraZoneRect.height, 1, MAX_EXTRA_ZONE_DIMENSION);
		}

		void loadExtraZoneGeometry(const json& document, RuntimeConfig& config)
		{
			if (config.extraZonePlacement == ExtraZonePlacement::Free)
				loadExtraZoneRect(document, config);
			else
				loadOptionalInt(document, "extraZoneHeight", config.extraZoneHeight, 1, MAX_EXTRA_ZONE_DIMENSION);
		}

		void loadBacklight(const json& document, RuntimeConfig& config)
		{
			const auto backlightIt = document.find("backlight");
			if (backlightIt == document.end() || backlightIt->is_null()) {
				return;
			}
			if (!backlightIt->is_object()) {
				ELOG("MADT configuration %s has non-object backlight", CONFIG_FILENAME);
				return;
			}

			loadOptionalString(*backlightIt, "command", config.backlight.command);
			loadOptionalString(*backlightIt, "path", config.backlight.path);
			loadOptionalString(*backlightIt, "maxValuePath", config.backlight.maxValuePath);
			loadOptionalInt(*backlightIt, "maxValue", config.backlight.maxValue, 1, 65535);
		}

		void loadAudioVolume(const json& document, RuntimeConfig& config)
		{
			const auto audioVolumeIt = document.find("audioVolume");
			if (audioVolumeIt == document.end() || audioVolumeIt->is_null()) {
				return;
			}
			if (!audioVolumeIt->is_object()) {
				ELOG("MADT configuration %s has non-object audioVolume", CONFIG_FILENAME);
				return;
			}

			loadOptionalString(*audioVolumeIt, "command", config.audioVolume.command);
			loadOptionalString(*audioVolumeIt, "controlName", config.audioVolume.controlName);
			loadOptionalString(*audioVolumeIt, "script", config.audioVolume.script);
		}

		void loadSettings(const json& document, RuntimeConfig& config)
		{
			loadOptionalString(document, "volume", config.settings.volume);
			loadOptionalInt(document, "brightness", config.settings.brightness, 0, 255);
			loadOptionalInt(document, "contrast", config.settings.contrast, 0, 255);
			loadOptionalString(document, "language", config.settings.language);
			loadOptionalString(document, "defaultActiveMode", config.settings.defaultActiveMode);
			loadOptionalString(document, "activeMode", config.settings.activeMode);
			loadOptionalString(document, "defaultVisualMode", config.settings.defaultVisualMode);
			loadOptionalString(document, "visualMode", config.settings.visualMode);

			const auto extra1It = document.find("extra1");
			if (extra1It != document.end()) {
				config.settings.extra1 = *extra1It;
			}
			const auto extra2It = document.find("extra2");
			if (extra2It != document.end()) {
				config.settings.extra2 = *extra2It;
			}
		}

		void loadSoundFiles(const json& document, RuntimeConfig& config)
		{
			const auto it = document.find("soundFiles");
			if (it == document.end() || it->is_null()) {
				return;
			}
			if (!it->is_object()) {
				ELOG("MADT configuration %s has non-object soundFiles", CONFIG_FILENAME);
				return;
			}

			for (const auto& entry : it->items()) {
				if (!entry.value().is_string()) {
					ELOG("MADT configuration %s has non-string soundFiles.%s",
					     CONFIG_FILENAME,
					     entry.key().c_str());
					continue;
				}
				config.soundFiles[entry.key()] = entry.value().get<std::string>();
			}
		}
	} // namespace

	json settingsToJson(const SettingsState& settings)
	{
		return json{
		  { "volume", settings.volume },
		  { "brightness", settings.brightness },
		  { "contrast", settings.contrast },
		  { "language", settings.language },
		  { "defaultActiveMode", settings.defaultActiveMode },
		  { "activeMode", settings.activeMode },
		  { "defaultVisualMode", settings.defaultVisualMode },
		  { "visualMode", settings.visualMode },
		  { "extra1", settings.extra1 },
		  { "extra2", settings.extra2 },
		};
	}

	bool isValidSettingsPatch(const json& patch, std::string& error)
	{
		if (!patch.is_object()) {
			error = "settings patch must be a JSON object";
			return false;
		}

		auto reject = [&error](const std::string& message) {
			error = message;
			return false;
		};

		if (patch.contains("volume") && !patch["volume"].is_string()) {
			return reject("volume must be a string");
		}
		if (patch.contains("brightness") &&
		    (!patch["brightness"].is_number_integer() || patch["brightness"].get<int>() < 0 ||
		     patch["brightness"].get<int>() > 255)) {
			return reject("brightness must be an integer in [0,255]");
		}
		if (patch.contains("contrast") &&
		    (!patch["contrast"].is_number_integer() || patch["contrast"].get<int>() < 0 ||
		     patch["contrast"].get<int>() > 255)) {
			return reject("contrast must be an integer in [0,255]");
		}
		if (patch.contains("language") && !patch["language"].is_string()) {
			return reject("language must be a string");
		}
		if (patch.contains("defaultActiveMode") &&
		    (!patch["defaultActiveMode"].is_string() ||
		     !isValidMode(patch["defaultActiveMode"].get<std::string>(),
		                  { "PRIMARY", "SUBORDINATED" }))) {
			return reject("defaultActiveMode must be PRIMARY or SUBORDINATED");
		}
		if (patch.contains("activeMode") &&
		    (!patch["activeMode"].is_string() ||
		     !isValidMode(patch["activeMode"].get<std::string>(), { "PRIMARY", "SUBORDINATED" }))) {
			return reject("activeMode must be PRIMARY or SUBORDINATED");
		}
		if (patch.contains("defaultVisualMode") &&
		    (!patch["defaultVisualMode"].is_string() ||
		     !isValidMode(patch["defaultVisualMode"].get<std::string>(),
		                  { "MUTE", "UNMUTE", "DEGRADED" }))) {
			return reject("defaultVisualMode must be MUTE, UNMUTE, or DEGRADED");
		}
		if (patch.contains("visualMode") &&
		    (!patch["visualMode"].is_string() ||
		     !isValidMode(patch["visualMode"].get<std::string>(),
		                  { "MUTE", "UNMUTE", "DEGRADED" }))) {
			return reject("visualMode must be MUTE, UNMUTE, or DEGRADED");
		}
		return true;
	}

	int settingsVolumeToPercent(const std::string& volume)
	{
		if (volume.empty()) {
			return 100;
		}

		try {
			std::size_t     parsedChars = 0;
			const bool      isHex = volume.size() > 2 && volume[0] == '0' &&
			                   (volume[1] == 'x' || volume[1] == 'X');
			const std::uint64_t raw = std::stoull(volume, &parsedChars, isHex ? 16 : 10);
			if (parsedChars != volume.size()) {
				return 100;
			}

			if (!isHex && raw <= 100U) {
				return static_cast<int>(raw);
			}

			const auto clamped =
			  std::min<std::uint64_t>(raw, std::numeric_limits<std::uint32_t>::max());
			return static_cast<int>(std::lround(
			  (static_cast<double>(clamped) * 100.0) /
			  static_cast<double>(std::numeric_limits<std::uint32_t>::max())));
		} catch (...) {
			return 100;
		}
	}

	double settingsVolumeToScalar(const std::string& volume)
	{
		return static_cast<double>(settingsVolumeToPercent(volume)) / 100.0;
	}

	SettingsState mergeSettings(const SettingsState& current, const json& patch)
	{
		SettingsState merged = current;

		if (patch.contains("volume")) {
			merged.volume = patch["volume"].get<std::string>();
		}
		if (patch.contains("brightness")) {
			merged.brightness = patch["brightness"].get<int>();
		}
		if (patch.contains("contrast")) {
			merged.contrast = patch["contrast"].get<int>();
		}
		if (patch.contains("language")) {
			merged.language = patch["language"].get<std::string>();
		}
		if (patch.contains("defaultActiveMode")) {
			merged.defaultActiveMode = patch["defaultActiveMode"].get<std::string>();
		}
		if (patch.contains("activeMode")) {
			merged.activeMode = patch["activeMode"].get<std::string>();
		}
		if (patch.contains("defaultVisualMode")) {
			merged.defaultVisualMode = patch["defaultVisualMode"].get<std::string>();
		}
		if (patch.contains("visualMode")) {
			merged.visualMode = patch["visualMode"].get<std::string>();
		}
		if (patch.contains("extra1")) {
			merged.extra1 = patch["extra1"];
		}
		if (patch.contains("extra2")) {
			merged.extra2 = patch["extra2"];
		}

		return merged;
	}

	RuntimeConfig loadRuntimeConfig()
	{
		RuntimeConfig config;
		json          document;
		const auto result =
		  Secretary::utils::loadOptionalJsonFile(std::filesystem::path(CONFIG_FILENAME),
		                                         document,
		                                         "MADT configuration");
		if (result == Secretary::utils::JsonFileLoadResult::Missing) {
			return config;
		}
		if (result == Secretary::utils::JsonFileLoadResult::Error) {
			ELOG("MADT runtime configuration is invalid, using defaults");
			return config;
		}
		if (!document.is_object()) {
			ELOG("MADT configuration %s must contain a JSON object", CONFIG_FILENAME);
			return config;
		}

		loadOptionalString(document, "tabMapPassword", config.tabMapPassword);
		loadOptionalString(document, "controlPassword", config.controlPassword);
		loadOptionalString(document, "controlPsk", config.controlPsk);
		loadOptionalString(document, "listenAddress", config.listenAddress);
		loadOptionalInt(document,
		                "controlNonceTtlSeconds",
		                config.controlNonceTtlSeconds,
		                1,
		                300);

		const auto dnsSdEnabledIt = document.find("dnsSdEnabled");
		if (dnsSdEnabledIt != document.end() && !dnsSdEnabledIt->is_null()) {
			if (!dnsSdEnabledIt->is_boolean()) {
				ELOG("MADT configuration %s has non-boolean dnsSdEnabled", CONFIG_FILENAME);
			} else {
				config.dnsSdEnabled = dnsSdEnabledIt->get<bool>();
			}
		}
		loadOptionalString(document, "dnsSdUniqueId", config.dnsSdUniqueId);
		loadOptionalString(document, "dnsSdRelease", config.dnsSdRelease);
		loadOptionalString(document, "dnsSdManufacturer", config.dnsSdManufacturer);
		loadOptionalString(document, "soundPlayerCommand", config.soundPlayerCommand);
		loadOptionalInt(document,
		                "dnsSdIntervalMinutes",
		                config.dnsSdIntervalMinutes,
		                1,
		                30);
		loadSoundFiles(document, config);

		const auto lifetimeIt = document.find("tabLifetimeByConnection");
		if (lifetimeIt != document.end() && !lifetimeIt->is_null()) {
			if (!lifetimeIt->is_boolean()) {
				ELOG("MADT configuration %s has non-boolean tabLifetimeByConnection",
				     CONFIG_FILENAME);
			} else {
				config.tabLifetimeByConnection = lifetimeIt->get<bool>();
			}
		}

		loadTabBarVisible(document, config);
		loadTabBarEdge(document, config);
		loadTabBarDimension(document, "tabBarWidth", config.tabBarWidth);
		loadTabBarDimension(document, "tabBarHeight", config.tabBarHeight);
		loadOptionalBool(document, "tabBarShowLabels", config.tabBarShowLabels);
		loadOptionalBool(document, "tabBarShowTooltips", config.tabBarShowTooltips);
		loadOptionalBool(document, "tabBarUseScrollButtons", config.tabBarUseScrollButtons);
		loadOptionalBool(document, "shortcutsEnabled", config.shortcutsEnabled);
		loadOptionalBool(document,
		                 "shortcutLauncherVisible",
		                 config.shortcutLauncherVisible);
		loadOptionalString(document, "shortcutLauncherLabel", config.shortcutLauncherLabel);
		loadShortcutLauncherCorner(document, config);
		loadOptionalString(document, "shortcutPopupTitle", config.shortcutPopupTitle);
		loadOptionalInt(document, "shortcutPopupColumns", config.shortcutPopupColumns, 1, 8);
		loadOptionalInt(document, "shortcutIconWidth", config.shortcutIconWidth, 1, 512);
		loadOptionalInt(document, "shortcutIconHeight", config.shortcutIconHeight, 1, 512);
		loadOptionalInt(document, "shortcutMaxCount", config.shortcutMaxCount, 1, 20);
		loadOptionalBool(document, "shortcutAutoClose", config.shortcutAutoClose);
		loadExtraZonePlacement(document, config);
		loadExtraZoneGeometry(document, config);
		loadBacklight(document, config);
		loadAudioVolume(document, config);
		loadSettings(document, config);
		return config;
	}

} // namespace Secretary::Madt
