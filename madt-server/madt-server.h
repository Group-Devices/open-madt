#ifndef __MADT_SERVER_H
#define __MADT_SERVER_H

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <nlohmann/json.hpp>

#include "AvahiService.h"
#include "gui.h"
#include "runtime-config.h"

using json = nlohmann::json;

namespace Secretary::Madt {

	class Server
	{
	  public:
		enum class returnCode
		{
			MTSRC_OK,
			MTSRC_BAD_REQUEST,
			MTSRC_INVALID_TAB,
			MTSRC_EXEC_ERROR,
			MTSRC_BAD_ANSWER
		};
		enum class requestCode
		{
			GetRandom,
			GetInfo,
			PlaySound,
			GetSettings,
			SetSettings,
			Stop,
			Restart,
			GetCharacteristics,
			NewWebTab,
			NewVncTab,
			NewShortcut,
			NavigateTo,
			BlinkTab,
			ActivateTab,
			KillTab,
			KillShortcut,
			IAmAlive,
			GetTabMap,
			GetShortcuts,
			CaptureScreenshot,

			Unknown
		};
		Server(int port)
		  : m_port(port)
		  , m_listener(nullptr)
		{
		}
		void start(struct event_base* base);

		void request(struct bufferevent* bev, const std::string& data);
		void eraseOwnedTabs(struct bufferevent* bev);
		void eraseConnection(struct bufferevent* bev)
		{
			connectionTabs.erase(bev);
			connectionBuffers.erase(bev);
		}
		void pruneExpiredTabs();
		void rememberOwnedTab(struct bufferevent* bev, const std::string& tabId);
		void forgetOwnedTab(const std::string& tabId);
		bool hasTab(const std::string& tabId) const;
		bool isExtraTab(const std::string& tabId) const;
		bool isTabLifetimeByConnection() const { return runtimeConfig.tabLifetimeByConnection; }

	  private:
		requestCode convertStringToMadtRequest(const std::string& str);
		returnCode  convertGuiResultToMadt(Gui::CmdResponse::ResultCode guiCode);
		returnCode  killTabInternal(const std::string& tabId, bool forceDestroy);
		void        sendResponse(struct bufferevent* bev, const json& response);
		void        handleGetInfo(struct bufferevent* bev);
		void        handleGetRandom(struct bufferevent* bev);
		void        handleGetCharacteristics(struct bufferevent* bev);
		void        handleGetTabMap(struct bufferevent* bev, const json& request);
		void        handleGetShortcuts(struct bufferevent* bev, const json& request);
		void        handleNavigateTo(struct bufferevent* bev, const json& request);
		void        handleIAmAlive(struct bufferevent* bev, const json& request);
		void        handlePlaySound(struct bufferevent* bev, const json& request);
		void        handleGetSettings(struct bufferevent* bev);
		void        handleSetSettings(struct bufferevent* bev, const json& request);
		void        handleStop(struct bufferevent* bev, const json& request);
		void        handleRestart(struct bufferevent* bev, const json& request);
		void        handleNewWebTab(struct bufferevent* bev, const json& request);
		void        handleNewShortcut(struct bufferevent* bev, const json& request);
		void        handleBlinkTab(struct bufferevent* bev, const json& request);
		void        handleActivateTab(struct bufferevent* bev, const json& request);
		void        handleKillTab(struct bufferevent* bev, const json& request);
		void        handleKillShortcut(struct bufferevent* bev, const json& request);
		void        handleCaptureScreenshot(struct bufferevent* bev, const json& request);
		bool        authorizeWithPassword(const json& request, const std::string& password) const;
		bool        authorizeControlRequest(const json& request, const std::string& reqName);
		std::string
		buildWebTabUrl(const std::string& requestedUrl, const std::string& tabId, int flags) const;
		std::string buildDnsSdServiceName() const;
		std::string currentTimestampUtc() const;
		std::string currentActiveModeDnsSdValue() const;
		std::string buildControlPasswordToken(const std::string& reqName);
		bool        parseSoundFlags(const json& request, unsigned int& flags) const;
		bool        isSupportedSoundAlias(const std::string& soundId) const;
		std::string resolveSoundFile(const std::string& soundId) const;
		bool        extractSingleJsonObject(std::string& buffer, std::string& requestText) const;
		std::uint32_t                       resolveListenAddress() const;
		struct evconnlistener*              m_listener;
		struct event*                       m_leaseCleanupEvent = nullptr;
		int                                 m_port;
		RuntimeConfig                       runtimeConfig;
		SettingsState                       settingsState;
		struct ControlNonce
		{
			std::string                                    nonce;
			std::chrono::steady_clock::time_point          expiresAt;
			bool                                           used = false;
		};
		std::unique_ptr<class AvahiService> dnsSdAdvertiser;
		std::map<std::string, std::optional<std::chrono::steady_clock::time_point>> tabLeases;
		std::map<std::string, std::optional<std::chrono::steady_clock::time_point>> shortcutLeases;
		std::map<std::string, ControlNonce>                                      controlNonces;
		std::map<struct bufferevent*, std::set<std::string>>                        connectionTabs;
		std::map<struct bufferevent*, std::string>                                  connectionBuffers;
		std::string                                                                 extraTabId;
	};

}
#endif
