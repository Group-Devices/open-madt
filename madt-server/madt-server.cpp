
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <filesystem>
#include <string>
#include <sys/resource.h>
#include <sys/time.h>
#include <optional>
#include <uuid/uuid.h>

#include <event2/buffer.h>
#include <event2/listener.h>

#include "gui.h"
#include "loghelper/log.h"
#include "madt-server.h"
#include "process/process.h"
#include "utils/json_file.hpp"

namespace Secretary::Madt {

	namespace {
		constexpr const char* SPEC_VERSION        = "3.0.1";
#ifndef MADT_SOFTWARE_VERSION
#define MADT_SOFTWARE_VERSION "unknown"
#endif
		constexpr const char* SOFTWARE_VERSION    = MADT_SOFTWARE_VERSION;
		constexpr const char* EXTRA_INFO          = "madt";
		constexpr int         HAS_VNC             = 0;
		constexpr int         HAS_SHORTCUT        = 0;
		constexpr int         HAS_SOUND           = 0;
		constexpr int         ICON_WIDTH          = 0;
		constexpr int         ICON_HEIGHT         = 0;
		constexpr int         MAX_TABS            = 20;
		constexpr const char* TABMAP_PASSWORD_ENV = "MADT_TABMAP_PASSWORD";
		constexpr const char* CONFIG_FILENAME     = "madt-config.json";
	}

	static std::optional<std::string> loadTabMapPassword(bool& configError)
	{
		if (const char* password = std::getenv(TABMAP_PASSWORD_ENV);
		    password != nullptr && password[0] != '\0') {
			configError = false;
			return std::string(password);
		}

		configError = false;
		json document;
		const auto result =
		  Secretary::utils::loadOptionalJsonFile(std::filesystem::path(CONFIG_FILENAME),
		                                         document,
		                                         "MADT configuration");
		if (result == Secretary::utils::JsonFileLoadResult::Missing) {
			return std::nullopt;
		}
		if (result == Secretary::utils::JsonFileLoadResult::Error) {
			configError = true;
			return std::nullopt;
		}

		if (!document.is_object()) {
			ELOG("MADT configuration %s must contain a JSON object", CONFIG_FILENAME);
			configError = true;
			return std::nullopt;
		}

		const auto passwordIt = document.find("tabMapPassword");
		if (passwordIt == document.end() || passwordIt->is_null()) {
			return std::nullopt;
		}
		if (!passwordIt->is_string()) {
			ELOG("MADT configuration %s has non-string tabMapPassword", CONFIG_FILENAME);
			configError = true;
			return std::nullopt;
		}

		const auto password = passwordIt->get<std::string>();
		if (password.empty()) {
			return std::nullopt;
		}
		return password;
	}

	static std::string generate_uuid()
	{
		uuid_t uuid;
		char   id[36 + 1];

		uuid_generate(uuid);
		uuid_unparse(uuid, id);
		return std::string(id);
	}

	static void readcb(struct bufferevent* bev, void* ctx)
	{
		Server*          s     = (Server*)ctx;
		struct evbuffer* input = bufferevent_get_input(bev);
		int              len   = evbuffer_get_length(input);
		char             buf[1024];
		int              n;
		std::string      req = "";

		while ((n = evbuffer_remove(input, buf, sizeof(buf))) > 0) {
			TLOG("Get data %*.*s", n, n, buf);
			std::string data(buf, n);
			req += data;
		}
		s->request(bev, req);
	}

	static void eventcb(struct bufferevent* bev, short events, void* ctx)
	{
		if (events & BEV_EVENT_ERROR)
			ELOG("Error from bufferevent");
		if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
			Server* s = (Server*)ctx;
			bufferevent_free(bev);
			TLOG("Delete windows");
			std::string uuid = s->getUuid(bev);
			TLOG("Delete windows with uuid %s", uuid.c_str());
			Gui::KillTab(uuid, nullptr);

			s->eraseWindow(bev);
		}
	}

	static void accept_conn_cb(struct evconnlistener* listener,
	                           evutil_socket_t        fd,
	                           struct sockaddr*       address,
	                           int                    socklen,
	                           void*                  ctx)
	{
		char host[NI_MAXHOST];
		char port[NI_MAXSERV];
		getnameinfo(
		  address, socklen, host, NI_MAXHOST, port, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
		TLOG("Got new connection from %s:%s", host, port);
		/* Setup a bufferevent */
		struct event_base*  base = evconnlistener_get_base(listener);
		struct bufferevent* bev  = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
		bufferevent_setcb(bev, readcb, NULL, eventcb, ctx);
		bufferevent_enable(bev, EV_READ | EV_WRITE);
	}

	static void accept_error_cb(struct evconnlistener* listener, void* ctx)
	{
		struct event_base* base = evconnlistener_get_base(listener);
		int                err  = EVUTIL_SOCKET_ERROR();
		fprintf(stderr,
		        "Got an error %d (%s) on the listener. "
		        "Shutting down.\n",
		        err,
		        evutil_socket_error_to_string(err));

		event_base_loopexit(base, NULL);
	}

	Server::requestCode Server::convertStringToMadtRequest(const std::string& str)
	{
		std::map<std::string, requestCode> mapCmd = {
			{ "GetInfo", requestCode::GetInfo },
			{ "PlaySound", requestCode::PlaySound },
			{ "GetSettings", requestCode::GetSettings },
			{ "SetSettings", requestCode::SetSettings },
			{ "Stop", requestCode::Stop },
			{ "Restart", requestCode::Restart },
			{ "GetCharacteristics", requestCode::GetCharacteristics },
			{ "NewWebTab", requestCode::NewWebTab },
			{ "NewVncTab", requestCode::NewVncTab },
			{ "NewShortcut", requestCode::NewShortcut },
			{ "NavigateTo", requestCode::NavigateTo },
			{ "BlinkTab", requestCode::BlinkTab },
			{ "ActivateTab", requestCode::ActivateTab },
			{ "KillTab", requestCode::KillTab },
			{ "KillShortcut", requestCode::KillShortcut },
			{ "IAmAlive", requestCode::IAmAlive },
			{ "GetTabMap", requestCode::GetTabMap },
			{ "GetShortcut", requestCode::GetShortcuts }
		};

		if (mapCmd.count(str) > 0)
			return mapCmd[str];
		return requestCode::Unknown;
	}

	void Server::start(struct event_base* base)
	{
		struct sockaddr_in sin;

		memset(&sin, 0, sizeof(sin));
		sin.sin_family      = AF_INET;
		sin.sin_addr.s_addr = htonl(0x7F000001);
		sin.sin_port        = htons(m_port);

		m_listener = evconnlistener_new_bind(base,
		                                     accept_conn_cb,
		                                     this,
		                                     LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
		                                     8192,
		                                     (struct sockaddr*)&sin,
		                                     sizeof(sin));
		evconnlistener_set_error_cb(m_listener, accept_error_cb);
	}

	Server::returnCode Server::convertGuiResultToMadt(Gui::CmdResponse::ResultCode guiCode)
	{
		switch (guiCode) {
			case Gui::CmdResponse::ResultCode::OK:
				return Madt::Server::returnCode::MTSRC_OK;
			case Gui::CmdResponse::ResultCode::EXEC_ERROR:
				return Server::returnCode::MTSRC_EXEC_ERROR;
			case Gui::CmdResponse::ResultCode::TAB_NOT_FOUND:
				return Server::returnCode::MTSRC_INVALID_TAB;
		}
		return Server::returnCode::MTSRC_EXEC_ERROR;
	}

	void Server::sendResponse(struct bufferevent* bev, const json& response)
	{
		std::string resp = response.dump();
		bufferevent_write(bev, resp.c_str(), resp.length());
	}

	void Server::handleGetInfo(struct bufferevent* bev)
	{
		sendResponse(bev,
		             json{
		               { "retCode", returnCode::MTSRC_OK },
		               { "version", SPEC_VERSION },
		               { "swvers", SOFTWARE_VERSION },
		               { "hasVNC", HAS_VNC },
		               { "hasShortcut", HAS_SHORTCUT },
		               { "hasSound", HAS_SOUND },
		               { "extraInfo", EXTRA_INFO },
		             });
	}

	void Server::handleGetCharacteristics(struct bufferevent* bev)
	{
		json             response;
		Gui::CmdResponse gResp;
		bool             retcode = Gui::GetCharacteristics(&gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
			if (gResp.result == Gui::CmdResponse::ResultCode::OK) {
				response["winWidth"] = gResp.payload.value("winWidth", 0);
				response["winHeight"] = gResp.payload.value("winHeight", 0);
				response["iconWidth"] = ICON_WIDTH;
				response["iconHeight"] = ICON_HEIGHT;
				response["maxTabs"] = MAX_TABS;
			}
		} else {
			response["retCode"] = returnCode::MTSRC_EXEC_ERROR;
		}
		sendResponse(bev, response);
	}

	void Server::handleGetTabMap(struct bufferevent* bev, const json& request)
	{
		bool configError = false;
		const auto password = loadTabMapPassword(configError);
		if (configError) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_EXEC_ERROR } });
			return;
		}
		if (password.has_value()) {
			if (!request.contains("password") || request["password"] != password.value()) {
				sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
				return;
			}
		}

		json             response;
		Gui::CmdResponse gResp;
		bool             retcode = Gui::GetTabMap(&gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
			if (gResp.result == Gui::CmdResponse::ResultCode::OK) {
				response["maxTabs"] = MAX_TABS;
				response["tabMap"] = gResp.payload;
			}
		} else {
			response["retCode"] = returnCode::MTSRC_EXEC_ERROR;
		}
		sendResponse(bev, response);
	}

	void Server::handleNavigateTo(struct bufferevent* bev, const json& request)
	{
		json             response;
		Gui::CmdResponse gResp;
		bool             retcode = Gui::NavigateTo(request["tabId"], request["url"], &gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
		} else {
			response["retCode"] = returnCode::MTSRC_EXEC_ERROR;
		}
		sendResponse(bev, response);
	}

	void Server::handleIAmAlive(struct bufferevent* bev, const json& request)
	{
		if (request.contains("shortcutId")) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}
		if (!request.contains("tabId") || !request.contains("TTL")) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}

		const auto tabId = request["tabId"].get<std::string>();
		const auto ttl   = request["TTL"].get<int>();
		if (ttl == 0) {
			handleKillTab(bev, json{ { "tabId", tabId } });
			return;
		}

		Gui::CmdResponse gResp;
		bool             retcode = Gui::GetTabMap(&gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			if (gResp.result == Gui::CmdResponse::ResultCode::OK) {
				for (const auto& entry : gResp.payload) {
					if (entry.is_string() && entry.get<std::string>() == tabId) {
						sendResponse(bev, json{ { "retCode", returnCode::MTSRC_OK } });
						return;
					}
				}
				sendResponse(bev, json{ { "retCode", returnCode::MTSRC_INVALID_TAB } });
				return;
			}
		}
		sendResponse(bev, json{ { "retCode", returnCode::MTSRC_EXEC_ERROR } });
	}

	void Server::handleStop(struct bufferevent* bev)
	{
		sendResponse(bev, json{ { "retCode", returnCode::MTSRC_OK } });
		Process::stop();
		Gui::requestExit(0);
	}

	void Server::handleRestart(struct bufferevent* bev)
	{
		sendResponse(bev, json{ { "retCode", returnCode::MTSRC_OK } });
		Process::stop();
		Gui::requestExit(1);
	}

	void Server::handleNewWebTab(struct bufferevent* bev, const json& request)
	{
		std::string uuid = generate_uuid();
		json        response;
		bool        retcode = Gui::NewWebTab(request["url"], uuid);
		response["retCode"] = retcode ? returnCode::MTSRC_OK : returnCode::MTSRC_BAD_REQUEST;
		if (retcode)
			response["tabId"] = uuid;
		windows[bev] = uuid;
		sendResponse(bev, response);
	}

	void Server::handleActivateTab(struct bufferevent* bev, const json& request)
	{
		json             response;
		Gui::CmdResponse gResp;
		bool             retcode = Gui::ActivateTab(request["tabId"], &gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
		} else {
			response["retCode"] = returnCode::MTSRC_EXEC_ERROR;
		}
		sendResponse(bev, response);
	}

	void Server::handleKillTab(struct bufferevent* bev, const json& request)
	{
		json             response;
		Gui::CmdResponse gResp;
		bool             retcode = Gui::KillTab(request["tabId"], &gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
			if (gResp.result == Gui::CmdResponse::ResultCode::OK) {
				eraseWindow(bev);
			}
		} else {
			response["retCode"] = returnCode::MTSRC_EXEC_ERROR;
		}
		sendResponse(bev, response);
	}

	void Server::request(struct bufferevent* bev, const std::string& data)
	{
		try {
			json request = json::parse(data);
			TLOG("Get madt request %s", request.dump().c_str());
			switch (convertStringToMadtRequest(request["req"])) {
				case requestCode::GetInfo:
					handleGetInfo(bev);
					break;
				case requestCode::GetCharacteristics:
					handleGetCharacteristics(bev);
					break;
				case requestCode::GetTabMap:
					handleGetTabMap(bev, request);
					break;
				case requestCode::NavigateTo:
					handleNavigateTo(bev, request);
					break;
				case requestCode::IAmAlive:
					handleIAmAlive(bev, request);
					break;
				case requestCode::Stop:
					handleStop(bev);
					break;
				case requestCode::Restart:
					handleRestart(bev);
					break;
				case requestCode::NewWebTab:
					handleNewWebTab(bev, request);
					break;
				case requestCode::ActivateTab:
					handleActivateTab(bev, request);
					break;
				case requestCode::KillTab:
					handleKillTab(bev, request);
					break;
				case requestCode::NewVncTab:
				case requestCode::NewShortcut:
				case requestCode::KillShortcut:
				case requestCode::GetShortcuts:
					sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
					break;
				case requestCode::PlaySound:
				case requestCode::GetSettings:
				case requestCode::SetSettings:
				case requestCode::BlinkTab:
				default:
					sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
					break;
			}
		} catch (...) {
			ELOG("Error parsing madt request");
			// struct evbuffer* output   = bufferevent_get_output(bev);
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
		}
	}
}
