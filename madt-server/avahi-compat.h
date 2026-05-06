#ifndef MADT_SERVER_AVAHI_COMPAT_H
#define MADT_SERVER_AVAHI_COMPAT_H

#include <cstddef>
#include <cstdint>

typedef int AvahiProtocol;
typedef int AvahiIfIndex;

enum
{
	AVAHI_PROTO_INET   = 0,
	AVAHI_PROTO_INET6  = 1,
	AVAHI_PROTO_UNSPEC = -1
};

enum
{
	AVAHI_IF_UNSPEC = -1
};

enum AvahiServerState
{
	AVAHI_SERVER_INVALID,
	AVAHI_SERVER_REGISTERING,
	AVAHI_SERVER_RUNNING,
	AVAHI_SERVER_COLLISION,
	AVAHI_SERVER_FAILURE
};

enum AvahiEntryGroupState
{
	AVAHI_ENTRY_GROUP_UNCOMMITED,
	AVAHI_ENTRY_GROUP_REGISTERING,
	AVAHI_ENTRY_GROUP_ESTABLISHED,
	AVAHI_ENTRY_GROUP_COLLISION,
	AVAHI_ENTRY_GROUP_FAILURE
};

enum AvahiPublishFlags
{
	AVAHI_PUBLISH_UNIQUE         = 1,
	AVAHI_PUBLISH_NO_PROBE       = 2,
	AVAHI_PUBLISH_NO_ANNOUNCE    = 4,
	AVAHI_PUBLISH_ALLOW_MULTIPLE = 8,
	AVAHI_PUBLISH_UPDATE         = 64
};

enum AvahiClientState
{
	AVAHI_CLIENT_S_REGISTERING = AVAHI_SERVER_REGISTERING,
	AVAHI_CLIENT_S_RUNNING     = AVAHI_SERVER_RUNNING,
	AVAHI_CLIENT_S_COLLISION   = AVAHI_SERVER_COLLISION,
	AVAHI_CLIENT_FAILURE       = 100,
	AVAHI_CLIENT_CONNECTING    = 101
};

enum AvahiClientFlags
{
	AVAHI_CLIENT_IGNORE_USER_CONFIG = 1,
	AVAHI_CLIENT_NO_FAIL            = 2
};

struct AvahiPoll;
struct AvahiClient;
struct AvahiEntryGroup;
struct AvahiThreadedPoll;
struct AvahiStringList;

typedef void (*AvahiClientCallback)(AvahiClient* client, AvahiClientState state, void* userdata);
typedef void (*AvahiEntryGroupCallback)(AvahiEntryGroup* group,
                                        AvahiEntryGroupState state,
                                        void*                userdata);

extern "C" {
AvahiThreadedPoll* avahi_threaded_poll_new(void);
void               avahi_threaded_poll_free(AvahiThreadedPoll* poll);
const AvahiPoll*   avahi_threaded_poll_get(AvahiThreadedPoll* poll);
int                avahi_threaded_poll_start(AvahiThreadedPoll* poll);
int                avahi_threaded_poll_stop(AvahiThreadedPoll* poll);

AvahiClient* avahi_client_new(const AvahiPoll* poll,
                              AvahiClientFlags flags,
                              AvahiClientCallback callback,
                              void* userdata,
                              int*  error);
void         avahi_client_free(AvahiClient* client);
int          avahi_client_errno(AvahiClient* client);

AvahiEntryGroup* avahi_entry_group_new(AvahiClient* client,
                                       AvahiEntryGroupCallback callback,
                                       void* userdata);
int              avahi_entry_group_free(AvahiEntryGroup* group);
int              avahi_entry_group_commit(AvahiEntryGroup* group);
int              avahi_entry_group_reset(AvahiEntryGroup* group);
int              avahi_entry_group_is_empty(AvahiEntryGroup* group);
int              avahi_entry_group_add_service_strlst(AvahiEntryGroup* group,
                                                      AvahiIfIndex interface_,
                                                      AvahiProtocol protocol,
                                                      AvahiPublishFlags flags,
                                                      const char* name,
                                                      const char* type,
                                                      const char* domain,
                                                      const char* host,
                                                      std::uint16_t port,
                                                      AvahiStringList* txt);

AvahiStringList* avahi_string_list_add_pair(AvahiStringList* list,
                                            const char*      key,
                                            const char*      value);
void             avahi_string_list_free(AvahiStringList* list);

const char* avahi_strerror(int error);
}

#endif
