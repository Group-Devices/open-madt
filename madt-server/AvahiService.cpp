#include "AvahiService.h"
#include <iostream>
#include <string.h>

#include "loghelper/log.h"

AvahiService::AvahiService(std::string name, std::string type, uint16_t port)
  : m_baseName(std::move(name))
  , m_currentName(m_baseName)
  , m_type(std::move(type))
  , m_port(port)
{
}

AvahiService::~AvahiService()
{
	stop();
}

#ifndef MADT_USE_AVAHI
void AvahiService::start()
{
	ILOG("Avahi support disabled at build time, skipping DNS-SD publication");
	m_running = true;
}

void AvahiService::stop()
{
	m_running = false;
}

void AvahiService::setTxt(const std::vector<std::pair<std::string, std::string>>& txt)
{
	m_txt = txt;
}

void AvahiService::clientCallback(AvahiClient*, AvahiClientState, void*) {}

void AvahiService::groupCallback(AvahiEntryGroup*, AvahiEntryGroupState, void*) {}

void AvahiService::handleClientState(AvahiClient*, AvahiClientState) {}

void AvahiService::handleGroupState(AvahiEntryGroup*, AvahiEntryGroupState) {}

void AvahiService::createOrUpdateService(AvahiClient*) {}

void AvahiService::resetGroup(AvahiEntryGroup*) {}

std::string AvahiService::nextName()
{
	return m_baseName;
}
#else
void AvahiService::start()
{
	if (m_running)
		return;

	int error;
	m_poll = avahi_threaded_poll_new();
	if (!m_poll)
		throw std::runtime_error("Failed to create poll");

	m_client = avahi_client_new(
	  avahi_threaded_poll_get(m_poll), (AvahiClientFlags)0, clientCallback, this, &error);

	if (!m_client)
		throw std::runtime_error(avahi_strerror(error));

	if (avahi_threaded_poll_start(m_poll) < 0)
		throw std::runtime_error("Failed to start poll");

	m_running = true;
}

void AvahiService::stop()
{
	if (!m_running)
		return;

	avahi_threaded_poll_lock(m_poll);

	if (m_group) {
		avahi_entry_group_free(m_group);
		m_group = nullptr;
	}

	if (m_client) {
		avahi_client_free(m_client);
		m_client = nullptr;
	}

	avahi_threaded_poll_unlock(m_poll);

	avahi_threaded_poll_stop(m_poll);
	avahi_threaded_poll_free(m_poll);
	m_poll = nullptr;

	m_running = false;
}

void AvahiService::setTxt(const std::vector<std::pair<std::string, std::string>>& txt)
{
	m_txt = txt;

	if (!m_running)
		return;

	avahi_threaded_poll_lock(m_poll);
	resetGroup(m_group);
	createOrUpdateService(m_client);
	avahi_threaded_poll_unlock(m_poll);
}

void AvahiService::clientCallback(AvahiClient* client, AvahiClientState state, void* userdata)
{
	static_cast<AvahiService*>(userdata)->handleClientState(client, state);
}

void AvahiService::groupCallback(AvahiEntryGroup* group, AvahiEntryGroupState state, void* userdata)
{
	static_cast<AvahiService*>(userdata)->handleGroupState(group, state);
}

void AvahiService::handleClientState(AvahiClient* client, AvahiClientState state)
{
	WLOG("Client state %d", state);
	switch (state) {
		case AVAHI_CLIENT_S_RUNNING:
			createOrUpdateService(client);
			break;

		case AVAHI_CLIENT_S_COLLISION:
		case AVAHI_CLIENT_S_REGISTERING:
			// resetGroup();
			break;

		case AVAHI_CLIENT_FAILURE:
			ELOG("Client failure");
			break;

		default:
			break;
	}
}

void AvahiService::handleGroupState(AvahiEntryGroup* group, AvahiEntryGroupState state)
{
	TLOG("Group state %d", state);
	switch (state) {
		case AVAHI_ENTRY_GROUP_ESTABLISHED:
			ILOG("Service established: %s", m_currentName.c_str());
			break;

		case AVAHI_ENTRY_GROUP_COLLISION:
			WLOG("Collision detected");
			m_currentName = nextName();
			resetGroup(group);
			createOrUpdateService(avahi_entry_group_get_client(group));
			break;

		case AVAHI_ENTRY_GROUP_FAILURE:
			ELOG("Group failure");
			break;

		default:
			break;
	}
}

void AvahiService::resetGroup(AvahiEntryGroup* group)
{
	if (group)
		avahi_entry_group_reset(group);
}

std::string AvahiService::nextName()
{
	return m_baseName + "-" + std::to_string(++m_renameCount);
}

void AvahiService::createOrUpdateService(AvahiClient* client)
{
	TLOG("Creating service");
	if (!m_group) {
		TLOG("Create avahi entry group");
		m_group = avahi_entry_group_new(client, groupCallback, this);
		if (!m_group)
			return;
	}

	if (!avahi_entry_group_is_empty(m_group)) {
		WLOG("Group already existing");
		return;
	}

	AvahiStringList* txtList = nullptr;
	for (auto& [k, v] : m_txt) {
		txtList = avahi_string_list_add_pair(txtList, k.c_str(), v.c_str());
	}

	TLOG("Add service %s%s", m_currentName.c_str(), m_type.c_str());
	int ret = avahi_entry_group_add_service_strlst(m_group,
	                                               AVAHI_IF_UNSPEC,
	                                               AVAHI_PROTO_UNSPEC,
	                                               (AvahiPublishFlags)0,
	                                               strdup(m_currentName.c_str()),
	                                               strdup(m_type.c_str()),
	                                               nullptr,
	                                               nullptr,
	                                               m_port,
	                                               txtList);

	avahi_string_list_free(txtList);

	if (ret < 0) {
		ELOG("Add service failed: %s", avahi_strerror(ret));
		return;
	}

	TLOG("Committing");
	ret = avahi_entry_group_commit(m_group);
	if (ret < 0) {
		ELOG("Commit failed: %s", avahi_strerror(ret));
	}
}
#endif
