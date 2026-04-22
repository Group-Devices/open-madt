#ifndef __MADT_SERVER_H
#define __MADT_SERVER_H

#include <map>

#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <nlohmann/json.hpp>

#include "gui.h"

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

			Unknown
		};
		Server(int port)
		  : m_port(port)
		  , m_listener(nullptr) {};
		void start(struct event_base* base);

		void request(struct bufferevent* bev, const std::string& data);
		void eraseWindow(struct bufferevent* bev)
		{
			if (windows.count(bev))
				windows.erase(bev);
		}
		std::string getUuid(struct bufferevent* bev)
		{
			return windows.count(bev) ? windows[bev] : "";
		}

	  private:
		requestCode            convertStringToMadtRequest(const std::string& str);
		returnCode             convertGuiResultToMadt(Gui::CmdResponse::ResultCode guiCode);
		void                   sendResponse(struct bufferevent* bev, const json& response);
		void                   handleGetInfo(struct bufferevent* bev);
		void                   handleGetCharacteristics(struct bufferevent* bev);
		void                   handleGetTabMap(struct bufferevent* bev, const json& request);
		void                   handleNavigateTo(struct bufferevent* bev, const json& request);
		void                   handleIAmAlive(struct bufferevent* bev, const json& request);
		void                   handleStop(struct bufferevent* bev);
		void                   handleRestart(struct bufferevent* bev);
		void                   handleNewWebTab(struct bufferevent* bev, const json& request);
		void                   handleActivateTab(struct bufferevent* bev, const json& request);
		void                   handleKillTab(struct bufferevent* bev, const json& request);
		struct evconnlistener* m_listener;
		int                    m_port;
		std::map<struct bufferevent*, std::string> windows;
	};

}
#endif
