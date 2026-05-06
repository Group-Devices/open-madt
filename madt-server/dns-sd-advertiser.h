#ifndef MADT_SERVER_DNS_SD_ADVERTISER_H
#define MADT_SERVER_DNS_SD_ADVERTISER_H

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef MADT_USE_AVAHI
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-common/thread-watch.h>
#endif

#include "runtime-config.h"

namespace Secretary::Madt {

	class DnsSdAdvertiser
	{
	  public:
		explicit DnsSdAdvertiser(const RuntimeConfig& config);
		~DnsSdAdvertiser();

		DnsSdAdvertiser(const DnsSdAdvertiser&)            = delete;
		DnsSdAdvertiser& operator=(const DnsSdAdvertiser&) = delete;

		void start(std::uint16_t port);

	  private:
		void stop();

#ifdef MADT_USE_AVAHI
		static void clientCallback(AvahiClient* client, AvahiClientState state, void* userdata);
		static void
			 entryGroupCallback(AvahiEntryGroup* group, AvahiEntryGroupState state, void* userdata);
		void createServices(AvahiClient* client);
		void handleClientState(AvahiClient* client, AvahiClientState state);
		void handleEntryGroupState(AvahiEntryGroupState state);
		std::string resolveServiceName(std::size_t attempt = 0) const;
#endif

		RuntimeConfig      m_config;
		std::uint16_t      m_port    = 0;
		bool               m_started = false;
		AvahiThreadedPoll* m_poll    = nullptr;
		AvahiClient*       m_client  = nullptr;
		AvahiEntryGroup*   m_group   = nullptr;
		std::string        m_serviceName;
		std::size_t        m_collisionCount = 0;
	};

} // namespace Secretary::Madt

#endif
