#pragma once

#include <atomic>
#ifdef MADT_USE_AVAHI
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-common/thread-watch.h>
#else
#include "avahi-compat.h"
#endif
#include <string>
#include <vector>

class AvahiService
{
  public:
	AvahiService(std::string name, std::string type, uint16_t port);

	~AvahiService();

	void start();
	void stop();

	void setTxt(const std::vector<std::pair<std::string, std::string>>& txt);

  private:
	// Avahi objects
	AvahiThreadedPoll* m_poll   = nullptr;
	AvahiClient*       m_client = nullptr;
	AvahiEntryGroup*   m_group  = nullptr;

	// config
	std::string m_baseName;
	std::string m_currentName;
	std::string m_type;
	uint16_t    m_port;

	std::vector<std::pair<std::string, std::string>> m_txt;

	std::atomic<bool> m_running{ false };
	int               m_renameCount = 0;

  private:
	static void clientCallback(AvahiClient*, AvahiClientState, void*);
	static void groupCallback(AvahiEntryGroup*, AvahiEntryGroupState, void*);

	void handleClientState(AvahiClient* client, AvahiClientState state);
	void handleGroupState(AvahiEntryGroup* group, AvahiEntryGroupState state);

	void        createOrUpdateService(AvahiClient* client);
	void        resetGroup(AvahiEntryGroup* group);
	std::string nextName();
};
