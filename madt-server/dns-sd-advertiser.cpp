#include "dns-sd-advertiser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <string>

#include <unistd.h>

#include "loghelper/log.h"

#ifdef MADT_USE_AVAHI
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-common/thread-watch.h>
#endif

namespace Secretary::Madt {
	namespace {
		constexpr const char* MADT_SERVICE_TYPE = "_itxpt_socket._tcp";
		constexpr const char* MADT_SERVICE_NAME = "madt";

		std::string sanitizeUniqueId(std::string value)
		{
			std::replace(value.begin(), value.end(), '_', '-');
			for (char& ch : value) {
				const unsigned char uch = static_cast<unsigned char>(ch);
				if (!(std::isalnum(uch) || ch == '-')) {
					ch = '-';
				}
			}

			value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](char ch) {
							return ch != '-';
						}));
			value.erase(
			  std::find_if(value.rbegin(), value.rend(), [](char ch) { return ch != '-'; }).base(),
			  value.end());

			if (value.empty()) {
				return "madt";
			}

			constexpr std::size_t MAX_UNIQUE_ID_LENGTH = 48;
			if (value.size() > MAX_UNIQUE_ID_LENGTH) {
				value.resize(MAX_UNIQUE_ID_LENGTH);
				while (!value.empty() && value.back() == '-') {
					value.pop_back();
				}
			}

			return value.empty() ? "madt" : value;
		}

		std::string detectHostUniqueId()
		{
			std::array<char, 256> hostname{};
			if (gethostname(hostname.data(), hostname.size()) != 0) {
				ELOG("Failed to read hostname for MADT DNS-SD: %s", std::strerror(errno));
				return "madt";
			}
			hostname.back() = '\0';
			return sanitizeUniqueId(hostname.data());
		}
	} // namespace

	DnsSdAdvertiser::DnsSdAdvertiser(const RuntimeConfig& config)
	  : m_config(config)
	{
	}

	DnsSdAdvertiser::~DnsSdAdvertiser()
	{
		stop();
	}

	void DnsSdAdvertiser::start(std::uint16_t port)
	{
		m_port = port;
		WLOG("Start MADT DNS-SD advertiser on port %d", m_port);
		if (!m_config.dnsSdEnabled || m_started) {
			return;
		}

#ifndef MADT_USE_AVAHI
		ELOG(
		  "MADT DNS-SD is enabled in configuration but this build does not include Avahi support");
		return;
#else
		int error = 0;
		m_poll    = avahi_threaded_poll_new();
		if (m_poll == nullptr) {
			ELOG("Failed to create Avahi threaded poll");
			return;
		}

		m_serviceName = resolveServiceName();
		m_client      = avahi_client_new(avahi_threaded_poll_get(m_poll),
		                                 (AvahiClientFlags)0,
		                                 &DnsSdAdvertiser::clientCallback,
		                                 this,
		                                 &error);
		if (m_client == nullptr) {
			ELOG("Failed to create Avahi client: %s", avahi_strerror(error));
			stop();
			return;
		}

		if (avahi_threaded_poll_start(m_poll) < 0) {
			ELOG("Failed to start Avahi threaded poll");
			stop();
			return;
		}

		m_started = true;
		WLOG("Start MADT DNS-SD advertiser on port %d end", m_port);
#endif
	}

	void DnsSdAdvertiser::stop()
	{
#ifdef MADT_USE_AVAHI
		if (m_started && m_poll != nullptr) {
			avahi_threaded_poll_stop(m_poll);
		}
		if (m_group != nullptr) {
			avahi_entry_group_free(m_group);
			m_group = nullptr;
		}
		if (m_client != nullptr) {
			avahi_client_free(m_client);
			m_client = nullptr;
		}
		if (m_poll != nullptr) {
			avahi_threaded_poll_free(m_poll);
			m_poll = nullptr;
		}
#endif
		m_started = false;
	}

#ifdef MADT_USE_AVAHI
	void DnsSdAdvertiser::clientCallback(AvahiClient* cli, AvahiClientState state, void* userdata)
	{
		static_cast<DnsSdAdvertiser*>(userdata)->handleClientState(cli, state);
	}

	void DnsSdAdvertiser::entryGroupCallback(AvahiEntryGroup*,
	                                         AvahiEntryGroupState state,
	                                         void*                userdata)
	{
		WLOG("Avahi entry group callback: state=%d", state);
		static_cast<DnsSdAdvertiser*>(userdata)->handleEntryGroupState(state);
	}

	void DnsSdAdvertiser::handleClientState(AvahiClient* client, AvahiClientState state)
	{
		WLOG("Avahi client state change: %d", state);
		switch (state) {
			case AVAHI_CLIENT_S_RUNNING:
				WLOG("Avahi client is running");
				createServices(client);
				break;
			case AVAHI_CLIENT_S_COLLISION:
			case AVAHI_CLIENT_S_REGISTERING:
				WLOG("Avahi client registering or name collision, resetting services");
				if (m_group != nullptr) {
					avahi_entry_group_reset(m_group);
				}
				break;
			case AVAHI_CLIENT_FAILURE:
				ELOG("Avahi client failure: %s", avahi_strerror(avahi_client_errno(m_client)));
				break;
			case AVAHI_CLIENT_CONNECTING:
				WLOG("Avahi client connecting");
				break;
			default:
				WLOG("Avahi client unknown statee: %d", state);
				break;
		}
	}

	void DnsSdAdvertiser::handleEntryGroupState(AvahiEntryGroupState state)
	{
		WLOG("Avahi entry group state change: %d", state);
		switch (state) {
			case AVAHI_ENTRY_GROUP_UNCOMMITED:
				WLOG("Group created but not committed");
				break;
			case AVAHI_ENTRY_GROUP_REGISTERING:
				WLOG("Registering service...");
				break;
			case AVAHI_ENTRY_GROUP_ESTABLISHED:
				WLOG("Service successfully established 🎉");
				ILOG("Published MADT DNS-SD service");
				break;
			case AVAHI_ENTRY_GROUP_COLLISION: {
				WLOG("Service name collision, renaming service");
				//++m_collisionCount;
				// m_serviceName = resolveServiceName(m_collisionCount);
				// avahi_entry_group_reset(m_group);
				// createServices();
				break;
			}
			case AVAHI_ENTRY_GROUP_FAILURE:
				ELOG("Avahi entry group failure: %s", avahi_strerror(avahi_client_errno(m_client)));
				break;
			default:
				break;
		}
	}

	void DnsSdAdvertiser::createServices(AvahiClient* client)
	{
		WLOG("Creating MADT DNS-SD service with name '%s'", m_serviceName.c_str());
		/*if (m_client == nullptr) {
		    return;
		}*/
		if (m_group == nullptr) {
			WLOG("Creating new Avahi entry group");
			m_group = avahi_entry_group_new(
			  client, nullptr, nullptr); //&DnsSdAdvertiser::entryGroupCallback, this);
			if (m_group == nullptr) {
				ELOG("Failed to create Avahi entry group: %s",
				     avahi_strerror(avahi_client_errno(client)));
				return;
			}
		}
		WLOG("Avahi entry group state: %d", avahi_entry_group_get_state(m_group));
		/*if (!avahi_entry_group_is_empty(m_group)) {
		    return;
		}*/

		AvahiStringList* txt = nullptr;
		txt                  = avahi_string_list_add_pair(txt, "txtversion", "1");
		if (txt == nullptr) {
			ELOG("Failed to allocate MADT DNS-SD TXT records");
			return;
		}
		WLOG("Adding MADT DNS-SD service '%s' to Avahi entry group", m_serviceName.c_str());
		const int addResult = avahi_entry_group_add_service(m_group,
		                                                    AVAHI_IF_UNSPEC,
		                                                    AVAHI_PROTO_UNSPEC,
		                                                    static_cast<AvahiPublishFlags>(0),
		                                                    m_serviceName.c_str(),
		                                                    MADT_SERVICE_TYPE,
		                                                    nullptr,
		                                                    nullptr,
		                                                    m_port,
		                                                    txt);
		WLOG("Freeing ressource");
		avahi_string_list_free(txt);

		if (addResult < 0) {
			ELOG("Failed to add MADT DNS-SD service: %s", avahi_strerror(addResult));
			return;
		}
		WLOG("Committing MADT DNS-SD service '%s' to Avahi", m_serviceName.c_str());
		const int commitResult = avahi_entry_group_commit(m_group);
		if (commitResult < 0) {
			ELOG("Failed to commit MADT DNS-SD service: %s", avahi_strerror(commitResult));
		}
	}

	std::string DnsSdAdvertiser::resolveServiceName(std::size_t attempt) const
	{
		const std::string uniqueId = m_config.dnsSdUniqueId.empty()
		                               ? detectHostUniqueId()
		                               : sanitizeUniqueId(m_config.dnsSdUniqueId);
		if (attempt == 0) {
			return uniqueId + "_" + MADT_SERVICE_NAME;
		}

		std::ostringstream candidate;
		candidate << uniqueId << "-" << (attempt + 1) << "_" << MADT_SERVICE_NAME;
		return candidate.str();
	}
#endif

} // namespace Secretary::Madt
