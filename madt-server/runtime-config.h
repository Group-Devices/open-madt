#ifndef MADT_SERVER_RUNTIME_CONFIG_H
#define MADT_SERVER_RUNTIME_CONFIG_H

#include <nlohmann/json.hpp>

#include <map>
#include <string>

namespace Secretary::Madt {
	using json = nlohmann::json;

	inline constexpr int MAX_TABS = 20;

	struct SettingsState
	{
		std::string volume            = "0xFFFFFFFF";
		int         brightness        = 255;
		int         contrast          = 255;
		std::string language          = "en";
		std::string defaultActiveMode = "PRIMARY";
		std::string activeMode        = "PRIMARY";
		std::string defaultVisualMode = "UNMUTE";
		std::string visualMode        = "UNMUTE";
		json        extra1            = nullptr;
		json        extra2            = nullptr;
	};

	enum class TabBarEdge
	{
		Top,
		Bottom,
		Left,
		Right
	};

	enum class ShortcutLauncherCorner
	{
		TopLeft,
		TopRight
	};

	struct RuntimeConfig
	{
		std::string tabMapPassword;
		std::string controlPassword;
		std::string controlPsk;
		int         controlNonceTtlSeconds   = 30;
		std::string listenAddress           = "0.0.0.0";
		bool        dnsSdEnabled            = true;
		std::string dnsSdUniqueId;
		std::string dnsSdRelease            = "unknown";
		std::string dnsSdManufacturer       = "unknown";
		int         dnsSdIntervalMinutes    = 10;
		std::string soundPlayerCommand      = "aplay";
		std::map<std::string, std::string> soundFiles;
		bool        tabLifetimeByConnection = false;
		bool        tabBarVisible           = true;
		TabBarEdge  tabBarEdge              = TabBarEdge::Top;
		int         tabBarWidth             = 96;
		int         tabBarHeight            = 48;
		bool        tabBarShowLabels        = true;
		bool        tabBarShowTooltips      = true;
		bool        tabBarUseScrollButtons  = true;
		bool                   shortcutsEnabled         = true;
		bool                   shortcutLauncherVisible  = true;
		std::string            shortcutLauncherLabel    = "Shortcuts";
		ShortcutLauncherCorner shortcutLauncherCorner   = ShortcutLauncherCorner::TopRight;
		std::string            shortcutPopupTitle       = "Emergency shortcuts";
		int                    shortcutPopupColumns     = 3;
		int                    shortcutIconWidth       = 96;
		int                    shortcutIconHeight      = 96;
		int                    shortcutMaxCount        = 5;
		bool                   shortcutAutoClose       = true;
		SettingsState settings;
	};

	json          settingsToJson(const SettingsState& settings);
	SettingsState mergeSettings(const SettingsState& current, const json& patch);
	bool          isValidSettingsPatch(const json& patch, std::string& error);
	RuntimeConfig loadRuntimeConfig();

} // namespace Secretary::Madt

#endif
