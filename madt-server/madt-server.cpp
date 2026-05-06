#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <set>
#include <string>
#include <vector>

#include <event2/buffer.h>
#include <event2/listener.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include "AvahiService.h"
#include "gui.h"
#include "loghelper/log.h"
#include "madt-server.h"
#include "process/process.h"

namespace Secretary::Madt {

	namespace {
		constexpr const char* SPEC_VERSION = "3.0.1";
#ifndef MADT_SOFTWARE_VERSION
#define MADT_SOFTWARE_VERSION "unknown"
#endif
		constexpr const char* SOFTWARE_VERSION = MADT_SOFTWARE_VERSION;
		constexpr const char* EXTRA_INFO       = "madt";
		constexpr int         HAS_VNC          = 0;
		constexpr int         HAS_SHORTCUT     = 1;
		constexpr int         HAS_SOUND        = 1;
		constexpr int         LEASE_CHECK_MS   = 1000;
		constexpr int         FLAG_APPEND_ID   = 1;
		constexpr int         TXT_VERSION      = 1;
		constexpr unsigned int SOUNDFLAG_ASYNC      = 0x00000001U;
		constexpr unsigned int SOUNDFLAG_NODEFAULT  = 0x00000002U;
		constexpr unsigned int SOUNDFLAG_NOSTOP     = 0x00000010U;
		constexpr unsigned int SOUNDFLAG_ALIAS      = 0x00010000U;
		constexpr unsigned int SUPPORTED_SOUND_FLAGS =
		  SOUNDFLAG_ASYNC | SOUNDFLAG_NODEFAULT | SOUNDFLAG_NOSTOP | SOUNDFLAG_ALIAS;
		constexpr int CONTROL_NONCE_BYTES = 32;

		std::string sanitizeDnsSdLabel(std::string value)
		{
			for (char& ch : value) {
				if (ch == '_') {
					ch = '-';
				}
			}
			return value;
		}

		std::string generate_uuid()
		{
			uuid_t uuid;
			char   id[36 + 1];

			uuid_generate(uuid);
			uuid_unparse(uuid, id);
			return std::string(id);
		}

		std::string toHex(const unsigned char* data, std::size_t len)
		{
			std::ostringstream stream;
			stream << std::hex << std::setfill('0');
			for (std::size_t i = 0; i < len; ++i) {
				stream << std::setw(2) << static_cast<unsigned int>(data[i]);
			}
			return stream.str();
		}

		std::string base64Encode(const std::string& input)
		{
			if (input.empty()) {
				return std::string();
			}
			std::string output(static_cast<std::size_t>(4 * ((input.size() + 2) / 3)), '\0');
			const int encoded =
			  EVP_EncodeBlock(reinterpret_cast<unsigned char*>(output.data()),
			                  reinterpret_cast<const unsigned char*>(input.data()),
			                  static_cast<int>(input.size()));
			output.resize(static_cast<std::size_t>(encoded));
			return output;
		}

		bool base64Decode(const std::string& input, std::string& output)
		{
			if (input.empty()) {
				output.clear();
				return true;
			}

			std::string buffer(static_cast<std::size_t>((input.size() * 3) / 4 + 3), '\0');
			const int   decoded = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(buffer.data()),
			                                    reinterpret_cast<const unsigned char*>(input.data()),
			                                    static_cast<int>(input.size()));
			if (decoded < 0) {
				return false;
			}

			std::size_t decodedSize = static_cast<std::size_t>(decoded);
			for (auto it = input.rbegin(); it != input.rend() && *it == '='; ++it) {
				if (decodedSize == 0) {
					break;
				}
				--decodedSize;
			}
			buffer.resize(decodedSize);
			output = std::move(buffer);
			return true;
		}

		bool secureEquals(const std::string& left, const std::string& right)
		{
			if (left.size() != right.size()) {
				return false;
			}
			return CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
		}

		std::string computeHmacSha256(const std::string& key, const std::string& message)
		{
			unsigned int digestLength = 0;
			unsigned char digest[EVP_MAX_MD_SIZE];
			if (HMAC(EVP_sha256(),
			         key.data(),
			         static_cast<int>(key.size()),
			         reinterpret_cast<const unsigned char*>(message.data()),
			         message.size(),
			         digest,
			         &digestLength) == nullptr) {
				return std::string();
			}
			return toHex(digest, digestLength);
		}

		bool generateRandomToken(std::string& token)
		{
			std::string bytes(static_cast<std::size_t>(CONTROL_NONCE_BYTES), '\0');
			if (RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), CONTROL_NONCE_BYTES) != 1) {
				return false;
			}
			token = base64Encode(bytes);
			return true;
		}

		void readcb(struct bufferevent* bev, void* ctx)
		{
			Server*          s     = static_cast<Server*>(ctx);
			struct evbuffer* input = bufferevent_get_input(bev);
			char             buf[1024];
			int              n;

			while ((n = evbuffer_remove(input, buf, sizeof(buf))) > 0) {
				TLOG("Get data %*.*s", n, n, buf);
				s->request(bev, std::string(buf, n));
			}
		}

		void eventcb(struct bufferevent* bev, short events, void* ctx)
		{
			if (events & BEV_EVENT_ERROR) {
				ELOG("Error from bufferevent");
			}
			if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
				Server* s = static_cast<Server*>(ctx);
				bufferevent_free(bev);
				s->eraseOwnedTabs(bev);
				s->eraseConnection(bev);
			}
		}

		void accept_conn_cb(struct evconnlistener* listener,
		                    evutil_socket_t        fd,
		                    struct sockaddr*       address,
		                    int                    socklen,
		                    void*                  ctx)
		{
			char host[NI_MAXHOST];
			char port[NI_MAXSERV];
			getnameinfo(address,
			            socklen,
			            host,
			            NI_MAXHOST,
			            port,
			            NI_MAXSERV,
			            NI_NUMERICHOST | NI_NUMERICSERV);
			TLOG("Got new connection from %s:%s", host, port);
			struct event_base*  base = evconnlistener_get_base(listener);
			struct bufferevent* bev  = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
			bufferevent_setcb(bev, readcb, NULL, eventcb, ctx);
			bufferevent_enable(bev, EV_READ | EV_WRITE);
		}

		void accept_error_cb(struct evconnlistener* listener, void*)
		{
			struct event_base* base = evconnlistener_get_base(listener);
			const int          err  = EVUTIL_SOCKET_ERROR();
			fprintf(stderr,
			        "Got an error %d (%s) on the listener. Shutting down.\n",
			        err,
			        evutil_socket_error_to_string(err));
			event_base_loopexit(base, NULL);
		}

		void lease_cleanup_cb(evutil_socket_t, short, void* ctx)
		{
			static_cast<Server*>(ctx)->pruneExpiredTabs();
		}
	}

	void Server::rememberOwnedTab(struct bufferevent* bev, const std::string& tabId)
	{
		if (bev == nullptr || tabId.empty()) {
			return;
		}

		tabLeases.emplace(tabId, std::nullopt);
		if (runtimeConfig.tabLifetimeByConnection) {
			connectionTabs[bev].insert(tabId);
		}
	}

	void Server::forgetOwnedTab(const std::string& tabId)
	{
		if (tabId.empty()) {
			return;
		}

		tabLeases.erase(tabId);
		for (auto it = connectionTabs.begin(); it != connectionTabs.end();) {
			it->second.erase(tabId);
			if (it->second.empty()) {
				it = connectionTabs.erase(it);
			} else {
				++it;
			}
		}
	}

	namespace {
		bool hasLease(const std::map<std::string, std::optional<std::chrono::steady_clock::time_point>> &leases,
		              const std::string& id)
		{
			return leases.find(id) != leases.end();
		}
	}

	bool Server::hasTab(const std::string& tabId) const
	{
		return tabLeases.find(tabId) != tabLeases.end();
	}

	void Server::eraseOwnedTabs(struct bufferevent* bev)
	{
		(void)bev;
	}

	Server::requestCode Server::convertStringToMadtRequest(const std::string& str)
	{
			const std::map<std::string, requestCode> mapCmd = {
			{ "GetRandom", requestCode::GetRandom },
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
			{ "GetShortcuts", requestCode::GetShortcuts }
		};

		const auto it = mapCmd.find(str);
		return it != mapCmd.end() ? it->second : requestCode::Unknown;
	}

	bool Server::authorizeWithPassword(const json& request, const std::string& password) const
	{
		if (password.empty()) {
			return true;
		}
		return request.contains("password") && request["password"].is_string() &&
		       request["password"].get<std::string>() == password;
	}

	bool Server::authorizeControlRequest(const json& request, const std::string& reqName)
	{
		if (runtimeConfig.controlPsk.empty()) {
			return authorizeWithPassword(request, runtimeConfig.controlPassword);
		}
		if (!request.contains("password") || !request["password"].is_string()) {
			return false;
		}

		std::string decodedPassword;
		if (!base64Decode(request["password"].get<std::string>(), decodedPassword)) {
			return false;
		}

		json token;
		try {
			token = json::parse(decodedPassword);
		} catch (...) {
			return false;
		}
		if (!token.is_object() || !token.contains("nonceId") || !token.contains("timestamp") ||
		    !token.contains("auth") || !token["nonceId"].is_string() ||
		    !token["timestamp"].is_string() || !token["auth"].is_string()) {
			return false;
		}

		const std::string nonceId    = token["nonceId"].get<std::string>();
		const std::string timestamp  = token["timestamp"].get<std::string>();
		const std::string auth       = token["auth"].get<std::string>();
		const auto        nonceIt    = controlNonces.find(nonceId);
		const auto        now        = std::chrono::steady_clock::now();
		if (nonceIt == controlNonces.end() || nonceIt->second.used || nonceIt->second.expiresAt < now) {
			controlNonces.erase(nonceId);
			return false;
		}

		const std::string message =
		  reqName + "\n" + nonceId + "\n" + nonceIt->second.nonce + "\n" + timestamp;
		const std::string expectedAuth = computeHmacSha256(runtimeConfig.controlPsk, message);
		if (expectedAuth.empty() || !secureEquals(expectedAuth, auth)) {
			return false;
		}

		nonceIt->second.used = true;
		return true;
	}

	std::string Server::buildWebTabUrl(const std::string& requestedUrl,
	                                   const std::string& tabId,
	                                   int                flags) const
	{
		if ((flags & FLAG_APPEND_ID) == 0) {
			return requestedUrl;
		}

		const char separator = requestedUrl.find('?') == std::string::npos ? '?' : '&';
		return requestedUrl + separator + std::string("tabid=") + tabId;
	}

	std::string Server::buildDnsSdServiceName() const
	{
		if (!runtimeConfig.dnsSdUniqueId.empty()) {
			return sanitizeDnsSdLabel(runtimeConfig.dnsSdUniqueId) + "_madt";
		}

		char hostname[256] = {};
		if (gethostname(hostname, sizeof(hostname) - 1) == 0 && hostname[0] != '\0') {
			return sanitizeDnsSdLabel(hostname) + "_madt";
		}

		return "madt-host_madt";
	}

	std::string Server::currentTimestampUtc() const
	{
		const time_t now = time(nullptr);
		struct tm    utc {};
		gmtime_r(&now, &utc);

		char timestamp[32];
		strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
		return timestamp;
	}

	std::string Server::currentActiveModeDnsSdValue() const
	{
		std::string value = settingsState.activeMode;
		for (char& ch : value) {
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}
		return value;
	}

	std::string Server::buildControlPasswordToken(const std::string& reqName)
	{
		if (runtimeConfig.controlPsk.empty()) {
			return std::string();
		}

		std::string nonce;
		if (!generateRandomToken(nonce)) {
			return std::string();
		}
		const std::string nonceId    = generate_uuid();
		const std::string timestamp  = currentTimestampUtc();
		const std::string message    = reqName + "\n" + nonceId + "\n" + nonce + "\n" + timestamp;
		const std::string auth       = computeHmacSha256(runtimeConfig.controlPsk, message);
		if (auth.empty()) {
			return std::string();
		}

		controlNonces[nonceId] = ControlNonce{
			nonce,
			std::chrono::steady_clock::now() +
			  std::chrono::seconds(runtimeConfig.controlNonceTtlSeconds),
			false,
		};

		return base64Encode(json{
		  { "v", 1 },
		  { "nonceId", nonceId },
		  { "timestamp", timestamp },
		  { "auth", auth },
		}.dump());
	}

	bool Server::parseSoundFlags(const json& request, unsigned int& flags) const
	{
		if (!request.contains("soundFlags") || !request["soundFlags"].is_string()) {
			return false;
		}

		const std::string encodedFlags = request["soundFlags"].get<std::string>();
		char*             end          = nullptr;
		errno                           = 0;
		const unsigned long parsed = std::strtoul(encodedFlags.c_str(), &end, 0);
		if (errno != 0 || end == nullptr || *end != '\0') {
			return false;
		}

		flags = static_cast<unsigned int>(parsed);
		return true;
	}

	bool Server::isSupportedSoundAlias(const std::string& soundId) const
	{
		static const std::set<std::string> supportedAliases = {
			"SystemAsterisk",
			"SystemExclamation",
			"SystemExit",
			"SystemHand",
			"SystemNotification",
			"SystemQuestion",
			"SystemStart",
		};
		return supportedAliases.find(soundId) != supportedAliases.end();
	}

	std::string Server::resolveSoundFile(const std::string& soundId) const
	{
		const auto it = runtimeConfig.soundFiles.find(soundId);
		return it != runtimeConfig.soundFiles.end() ? it->second : std::string();
	}

	bool Server::extractSingleJsonObject(std::string& buffer, std::string& requestText) const
	{
		std::size_t start = 0;
		while (start < buffer.size() &&
		       std::isspace(static_cast<unsigned char>(buffer[start])) != 0) {
			++start;
		}
		if (start == buffer.size()) {
			buffer.clear();
			return false;
		}
		if (buffer[start] != '{') {
			requestText = buffer.substr(start);
			buffer.clear();
			return true;
		}

		int  depth     = 0;
		bool inString  = false;
		bool escaping  = false;
		for (std::size_t i = start; i < buffer.size(); ++i) {
			const char ch = buffer[i];
			if (inString) {
				if (escaping) {
					escaping = false;
				} else if (ch == '\\') {
					escaping = true;
				} else if (ch == '"') {
					inString = false;
				}
				continue;
			}

			if (ch == '"') {
				inString = true;
				continue;
			}
			if (ch == '{') {
				++depth;
				continue;
			}
			if (ch == '}') {
				--depth;
				if (depth == 0) {
					const std::size_t end = i + 1;
					for (std::size_t tail = end; tail < buffer.size(); ++tail) {
						if (std::isspace(static_cast<unsigned char>(buffer[tail])) == 0) {
							requestText = buffer.substr(start);
							buffer.clear();
							return true;
						}
					}
					requestText = buffer.substr(start, end - start);
					buffer.erase(0, end);
					return true;
				}
			}
		}

		return false;
	}

	void Server::start(struct event_base* base)
	{
		runtimeConfig = Secretary::Madt::loadRuntimeConfig();
		settingsState = runtimeConfig.settings;

		struct sockaddr_in sin;
		memset(&sin, 0, sizeof(sin));
		sin.sin_family      = AF_INET;
		sin.sin_addr.s_addr = resolveListenAddress();
		sin.sin_port        = htons(m_port);

		m_listener = evconnlistener_new_bind(base,
		                                     accept_conn_cb,
		                                     this,
		                                     LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
		                                     8192,
		                                     reinterpret_cast<struct sockaddr*>(&sin),
		                                     sizeof(sin));
		if (m_listener == nullptr) {
			ELOG(
			  "Failed to bind MADT listener on %s:%d", runtimeConfig.listenAddress.c_str(), m_port);
			event_base_loopexit(base, nullptr);
			return;
		}
		evconnlistener_set_error_cb(m_listener, accept_error_cb);

		printf("MADT server listening on %s:%d\n", runtimeConfig.listenAddress.c_str(), m_port);
		if (runtimeConfig.dnsSdEnabled) {
			dnsSdAdvertiser = std::make_unique<AvahiService>(buildDnsSdServiceName(),
			                                                 "_itxpt_socket._tcp",
			                                                 static_cast<uint16_t>(m_port));
			dnsSdAdvertiser->setTxt({
			  { "txtvers", std::to_string(TXT_VERSION) },
			  { "version", SPEC_VERSION },
			  { "release", runtimeConfig.dnsSdRelease },
			  { "swvers", SOFTWARE_VERSION },
			  { "manufacturer", runtimeConfig.dnsSdManufacturer },
			  { "atdatetime", currentTimestampUtc() },
			  { "interval", std::to_string(runtimeConfig.dnsSdIntervalMinutes) },
			  { "activemode", currentActiveModeDnsSdValue() },
			});
			dnsSdAdvertiser->start();
		}

		m_leaseCleanupEvent = event_new(base, -1, EV_PERSIST, lease_cleanup_cb, this);
		const timeval interval{ 1, 0 };
		event_add(m_leaseCleanupEvent, &interval);
	}

	std::uint32_t Server::resolveListenAddress() const
	{
		struct in_addr address;
		if (inet_pton(AF_INET, runtimeConfig.listenAddress.c_str(), &address) == 1) {
			return address.s_addr;
		}

		ELOG("Invalid MADT listenAddress '%s', falling back to 127.0.0.1",
		     runtimeConfig.listenAddress.c_str());
		return htonl(0x7F000001);
	}

	void Server::pruneExpiredTabs()
	{
		const auto               now = std::chrono::steady_clock::now();
		std::vector<std::string> expiredTabs;
		for (const auto& entry : tabLeases) {
			if (entry.second.has_value() && entry.second.value() <= now) {
				expiredTabs.push_back(entry.first);
			}
		}

		for (const auto& tabId : expiredTabs) {
			ILOG("Lease expired for tab %s", tabId.c_str());
			Gui::KillTab(tabId, nullptr);
			forgetOwnedTab(tabId);
		}

		std::vector<std::string> expiredShortcuts;
		for (const auto& entry : shortcutLeases) {
			if (entry.second.has_value() && entry.second.value() <= now) {
				expiredShortcuts.push_back(entry.first);
			}
		}
		for (const auto& shortcutId : expiredShortcuts) {
			ILOG("Lease expired for shortcut %s", shortcutId.c_str());
			Gui::KillShortcut(shortcutId, nullptr);
			shortcutLeases.erase(shortcutId);
		}

		for (auto it = controlNonces.begin(); it != controlNonces.end();) {
			if (it->second.used || it->second.expiresAt <= now) {
				it = controlNonces.erase(it);
			} else {
				++it;
			}
		}
	}

	Server::returnCode Server::convertGuiResultToMadt(Gui::CmdResponse::ResultCode guiCode)
	{
		switch (guiCode) {
			case Gui::CmdResponse::ResultCode::OK:
				return returnCode::MTSRC_OK;
			case Gui::CmdResponse::ResultCode::EXEC_ERROR:
				return returnCode::MTSRC_EXEC_ERROR;
			case Gui::CmdResponse::ResultCode::TAB_NOT_FOUND:
				return returnCode::MTSRC_INVALID_TAB;
		}
		return returnCode::MTSRC_EXEC_ERROR;
	}

	void Server::sendResponse(struct bufferevent* bev, const json& response)
	{
		const std::string resp = response.dump();
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
					   { "hasShortcut", runtimeConfig.shortcutsEnabled ? HAS_SHORTCUT : 0 },
					   { "hasSound", HAS_SOUND },
					   { "extraInfo", EXTRA_INFO },
					 });
	}

	void Server::handleGetRandom(struct bufferevent* bev)
	{
		if (runtimeConfig.controlPsk.empty()) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}

		std::string nonce;
		if (!generateRandomToken(nonce)) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_EXEC_ERROR } });
			return;
		}
		const std::string nonceId = generate_uuid();
		controlNonces[nonceId] = ControlNonce{
			nonce,
			std::chrono::steady_clock::now() +
			  std::chrono::seconds(runtimeConfig.controlNonceTtlSeconds),
			false,
		};

		sendResponse(bev,
		             json{
		               { "retCode", returnCode::MTSRC_OK },
		               { "nonceId", nonceId },
		               { "nonce", nonce },
		               { "ttl", runtimeConfig.controlNonceTtlSeconds },
		               { "algo", "HMAC-SHA256" },
		             });
	}

	void Server::handleGetCharacteristics(struct bufferevent* bev)
	{
		json             response;
		Gui::CmdResponse gResp;
		const bool       retcode = Gui::GetCharacteristics(&gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
			if (gResp.result == Gui::CmdResponse::ResultCode::OK) {
				response["winWidth"]   = gResp.payload.value("winWidth", 0);
				response["winHeight"]  = gResp.payload.value("winHeight", 0);
				response["iconWidth"]  = runtimeConfig.tabBarWidth;
				response["iconHeight"] = runtimeConfig.tabBarHeight;
				response["maxTabs"]    = MAX_TABS;
			}
		} else {
			response["retCode"] = returnCode::MTSRC_EXEC_ERROR;
		}
		sendResponse(bev, response);
	}

	void Server::handleGetTabMap(struct bufferevent* bev, const json& request)
	{
		if (!authorizeWithPassword(request, runtimeConfig.tabMapPassword)) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}

		json             response;
		Gui::CmdResponse gResp;
		const bool       retcode = Gui::GetTabMap(&gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
			if (gResp.result == Gui::CmdResponse::ResultCode::OK) {
				response["maxTabs"] = MAX_TABS;
				response["tabMap"]  = gResp.payload;
			}
		} else {
			response["retCode"] = returnCode::MTSRC_EXEC_ERROR;
		}
		sendResponse(bev, response);
	}

	void Server::handleGetShortcuts(struct bufferevent* bev, const json& request)
	{
		if (!authorizeWithPassword(request, runtimeConfig.tabMapPassword)) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}

		json             response;
		Gui::CmdResponse gResp;
		const bool       retcode = Gui::GetShortcuts(&gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
			if (gResp.result == Gui::CmdResponse::ResultCode::OK) {
				response["shortcuts"] = gResp.payload;
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
		const bool       retcode = Gui::NavigateTo(request["tabId"], request["url"], &gResp);
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
		if (!request.contains("TTL") || !request["TTL"].is_number_integer()) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}

		const bool hasTabId      = request.contains("tabId") && request["tabId"].is_string();
		const bool hasShortcutId = request.contains("shortcutId") && request["shortcutId"].is_string();
		if (hasTabId == hasShortcutId) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}

		const int ttl = request["TTL"].get<int>();
		if (ttl < 0) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}

		if (hasTabId) {
			const std::string tabId = request["tabId"].get<std::string>();
			if (!hasTab(tabId)) {
				sendResponse(bev, json{ { "retCode", returnCode::MTSRC_INVALID_TAB } });
				return;
			}
			if (ttl == 0) {
				handleKillTab(bev, json{ { "tabId", tabId } });
				return;
			}

			tabLeases[tabId] = std::chrono::steady_clock::now() + std::chrono::seconds(ttl);
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_OK } });
			return;
		}

		const std::string shortcutId = request["shortcutId"].get<std::string>();
		if (!hasLease(shortcutLeases, shortcutId)) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_INVALID_TAB } });
			return;
		}
		if (ttl == 0) {
			handleKillShortcut(bev, json{ { "shortcutId", shortcutId } });
			return;
		}
		shortcutLeases[shortcutId] = std::chrono::steady_clock::now() + std::chrono::seconds(ttl);
		sendResponse(bev, json{ { "retCode", returnCode::MTSRC_OK } });
	}

	void Server::handlePlaySound(struct bufferevent* bev, const json& request)
	{
		if (!request.contains("soundId") || !request["soundId"].is_string()) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}

		unsigned int soundFlags = 0;
		if (!parseSoundFlags(request, soundFlags)) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}
		if ((soundFlags & ~SUPPORTED_SOUND_FLAGS) != 0U) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}

		std::string soundId = request["soundId"].get<std::string>();
		const bool  alias   = (soundFlags & SOUNDFLAG_ALIAS) != 0U;
		if (!alias) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}

		if (!isSupportedSoundAlias(soundId)) {
			if ((soundFlags & SOUNDFLAG_NODEFAULT) != 0U) {
				sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
				return;
			}
			soundId = "SystemNotification";
		}

		json             response;
		Gui::CmdResponse gResp;
		const bool       retcode = Gui::PlaySound(soundId,
		                                          soundFlags,
		                                          resolveSoundFile(soundId),
		                                          runtimeConfig.soundPlayerCommand,
		                                          &gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
		} else {
			response["retCode"] = returnCode::MTSRC_EXEC_ERROR;
		}
		sendResponse(bev, response);
	}

	void Server::handleGetSettings(struct bufferevent* bev)
	{
		json response       = settingsToJson(settingsState);
		response["retCode"] = returnCode::MTSRC_OK;
		sendResponse(bev, response);
	}

	void Server::handleSetSettings(struct bufferevent* bev, const json& request)
	{
		std::string error;
		if (!isValidSettingsPatch(request, error)) {
			ELOG("Invalid MADT settings patch: %s", error.c_str());
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}

		settingsState = mergeSettings(settingsState, request);
		if (dnsSdAdvertiser != nullptr) {
			dnsSdAdvertiser->setTxt({
			  { "txtvers", std::to_string(TXT_VERSION) },
			  { "version", SPEC_VERSION },
			  { "release", runtimeConfig.dnsSdRelease },
			  { "swvers", SOFTWARE_VERSION },
			  { "manufacturer", runtimeConfig.dnsSdManufacturer },
			  { "atdatetime", currentTimestampUtc() },
			  { "interval", std::to_string(runtimeConfig.dnsSdIntervalMinutes) },
			  { "activemode", currentActiveModeDnsSdValue() },
			});
		}
		sendResponse(bev, json{ { "retCode", returnCode::MTSRC_OK } });
	}

	void Server::handleStop(struct bufferevent* bev, const json& request)
	{
		if (!authorizeControlRequest(request, "Stop")) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}
		sendResponse(bev, json{ { "retCode", returnCode::MTSRC_OK } });
		Process::stop();
		Gui::requestExit(0);
	}

	void Server::handleRestart(struct bufferevent* bev, const json& request)
	{
		if (!authorizeControlRequest(request, "Restart")) {
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
			return;
		}
		sendResponse(bev, json{ { "retCode", returnCode::MTSRC_OK } });
		Process::stop();
		Gui::requestExit(1);
	}

	void Server::handleNewWebTab(struct bufferevent* bev, const json& request)
	{
		const std::string uuid         = generate_uuid();
		const std::string iconUrl      = request.value("iconUrl", std::string());
		const int         preferredPos = request.value("preferredPos", -1);
		const int         flags        = request.value("flags", 0);
		const std::string url          = buildWebTabUrl(request["url"], uuid, flags);

		json             response;
		Gui::CmdResponse gResp;
		const bool       retcode = Gui::NewWebTab(url, iconUrl, preferredPos, flags, uuid, &gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
		} else {
			response["retCode"] = returnCode::MTSRC_EXEC_ERROR;
		}
		if (response["retCode"].get<int>() == static_cast<int>(returnCode::MTSRC_OK)) {
			response["tabId"] = uuid;
			rememberOwnedTab(bev, uuid);
		}
		sendResponse(bev, response);
	}

	void Server::handleNewShortcut(struct bufferevent* bev, const json& request)
	{
		const std::string shortcutId   = generate_uuid();
		const std::string iconUrl      = request.value("iconUrl", std::string());
		const int         preferredPos = request.value("preferredPos", -1);
		const int         flags        = request.value("flags", 0);
		const std::string url          = request["url"];

		json             response;
		Gui::CmdResponse gResp;
		const bool       retcode =
		  Gui::NewShortcut(url, iconUrl, preferredPos, flags, shortcutId, &gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
		} else {
			response["retCode"] = returnCode::MTSRC_EXEC_ERROR;
		}
		if (response["retCode"].get<int>() == static_cast<int>(returnCode::MTSRC_OK)) {
			response["shortcutId"] = shortcutId;
			shortcutLeases.emplace(shortcutId, std::nullopt);
		}
		sendResponse(bev, response);
	}

	void Server::handleBlinkTab(struct bufferevent* bev, const json& request)
	{
		json             response;
		Gui::CmdResponse gResp;
		const bool       retcode = Gui::BlinkTab(request["tabId"], &gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
		} else {
			response["retCode"] = returnCode::MTSRC_EXEC_ERROR;
		}
		sendResponse(bev, response);
	}

	void Server::handleActivateTab(struct bufferevent* bev, const json& request)
	{
		json             response;
		Gui::CmdResponse gResp;
		const bool       retcode = Gui::ActivateTab(request["tabId"], &gResp);
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
		const bool       retcode = Gui::KillTab(request["tabId"], &gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
			if (gResp.result == Gui::CmdResponse::ResultCode::OK) {
				forgetOwnedTab(request["tabId"].get<std::string>());
			}
		} else {
			response["retCode"] = returnCode::MTSRC_EXEC_ERROR;
		}
		sendResponse(bev, response);
	}

	void Server::handleKillShortcut(struct bufferevent* bev, const json& request)
	{
		json             response;
		Gui::CmdResponse gResp;
		const bool       retcode = Gui::KillShortcut(request["shortcutId"], &gResp);
		if (retcode) {
			std::unique_lock<std::mutex> lock(gResp.mtx);
			gResp.cv.wait(lock, [&gResp] { return gResp.ready; });
			response["retCode"] = convertGuiResultToMadt(gResp.result);
			if (gResp.result == Gui::CmdResponse::ResultCode::OK) {
				shortcutLeases.erase(request["shortcutId"].get<std::string>());
			}
		} else {
			response["retCode"] = returnCode::MTSRC_EXEC_ERROR;
		}
		sendResponse(bev, response);
	}

	void Server::request(struct bufferevent* bev, const std::string& data)
	{
		std::string& bufferedRequest = connectionBuffers[bev];
		bufferedRequest.append(data);

		std::string requestText;
		if (!extractSingleJsonObject(bufferedRequest, requestText)) {
			return;
		}

		try {
			const json request = json::parse(requestText);
			TLOG("Get madt request %s", request.dump().c_str());
				switch (convertStringToMadtRequest(request["req"])) {
					case requestCode::GetRandom:
						handleGetRandom(bev);
						break;
					case requestCode::GetInfo:
					handleGetInfo(bev);
					break;
					case requestCode::GetSettings:
						handleGetSettings(bev);
						break;
					case requestCode::PlaySound:
						handlePlaySound(bev, request);
						break;
					case requestCode::SetSettings:
						handleSetSettings(bev, request);
						break;
				case requestCode::GetCharacteristics:
					handleGetCharacteristics(bev);
					break;
					case requestCode::GetTabMap:
						handleGetTabMap(bev, request);
						break;
					case requestCode::GetShortcuts:
						handleGetShortcuts(bev, request);
						break;
					case requestCode::NavigateTo:
						handleNavigateTo(bev, request);
						break;
				case requestCode::IAmAlive:
					handleIAmAlive(bev, request);
					break;
				case requestCode::Stop:
					handleStop(bev, request);
					break;
				case requestCode::Restart:
					handleRestart(bev, request);
					break;
					case requestCode::NewWebTab:
						handleNewWebTab(bev, request);
						break;
					case requestCode::NewShortcut:
						handleNewShortcut(bev, request);
						break;
					case requestCode::ActivateTab:
						handleActivateTab(bev, request);
						break;
					case requestCode::KillTab:
						handleKillTab(bev, request);
						break;
					case requestCode::KillShortcut:
						handleKillShortcut(bev, request);
						break;
					case requestCode::BlinkTab:
						handleBlinkTab(bev, request);
						break;
					case requestCode::NewVncTab:
					default:
						sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
						break;
			}
		} catch (...) {
			ELOG("Error parsing madt request");
			sendResponse(bev, json{ { "retCode", returnCode::MTSRC_BAD_REQUEST } });
		}
	}
}
