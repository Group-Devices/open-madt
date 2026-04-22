#include "process.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "loghelper/log.h"
#include <event2/thread.h>

namespace {

struct EventDeleter
{
	void operator()(struct event* ev) const
	{
		if (ev != nullptr) {
			event_free(ev);
		}
	}
};

struct EventBaseDeleter
{
	void operator()(struct event_base* evbase) const
	{
		if (evbase != nullptr) {
			event_base_free(evbase);
		}
	}
};

using EventHandle     = std::unique_ptr<struct event, EventDeleter>;
using EventBaseHandle = std::unique_ptr<struct event_base, EventBaseDeleter>;

struct Context
{
	EventBaseHandle          evbase;
	std::vector<EventHandle> events;
	std::string              module_name;
};

Context ctx;

const int kSignals[] = {
	SIGQUIT,
	SIGTERM,
	SIGHUP,
};

struct event* registerEvent(EventHandle ev, const struct timeval* timeout = nullptr)
{
	if (ev == nullptr) {
		ELOG("Failed to create libevent event");
		return nullptr;
	}

	if (event_add(ev.get(), timeout) != 0) {
		ELOG("Failed to register libevent event");
		return nullptr;
	}

	auto* rawEvent = ev.get();
	ctx.events.push_back(std::move(ev));
	return rawEvent;
}

struct event* createSignalEvent(int signum, event_callback_fn callback, void* arg)
{
	return registerEvent(EventHandle(evsignal_new(ctx.evbase.get(), signum, callback, arg)));
}

struct event* createPersistentEvent(int fd, short flags, event_callback_fn callback, void* arg)
{
	return registerEvent(EventHandle(event_new(ctx.evbase.get(), fd, flags, callback, arg)));
}

struct event*
createTimerEvent(short flags, event_callback_fn callback, void* arg, const timeval* timeout)
{
	return registerEvent(EventHandle(event_new(ctx.evbase.get(), -1, flags, callback, arg)), timeout);
}

void deleteRegisteredEvent(struct event* ev)
{
	for (auto it = ctx.events.begin(); it != ctx.events.end(); ++it) {
		if (it->get() == ev) {
			ctx.events.erase(it);
			return;
		}
	}
}

void stopProcess(evutil_socket_t, short, void* arg)
{
	const int signum = event_get_signal(static_cast<struct event*>(arg));
	if (signum == SIGQUIT) {
		event_base_loopexit(ctx.evbase.get(), nullptr);
	} else {
		event_base_loopbreak(ctx.evbase.get());
	}
}

} // namespace

void Process::setupLog(int, char*[], const char* moduleName)
{
	ctx.module_name = moduleName != nullptr ? moduleName : "madt";
	std::setvbuf(stderr, nullptr, _IONBF, 0);
}

void Process::setup(int         argc,
                    char*       argv[],
                    const char*,
                    const char* moduleName,
                    bool        log,
                    bool        catchSignal)
{
	if (log) {
		setupLog(argc, argv, moduleName);
	}

	std::signal(SIGPIPE, SIG_IGN);
	evthread_use_pthreads();
	ctx.evbase.reset(event_base_new());
	if (ctx.evbase == nullptr) {
		throw std::runtime_error("Failed to create libevent base");
	}

	if (catchSignal) {
		for (int signum : kSignals) {
			createSignalEvent(signum, stopProcess, event_self_cbarg());
		}
	}
}

void* Process::timerCreate(void (*callback)(int fd, short what, void* arg), int timeout, void* arg)
{
	const timeval t = { timeout / 1000, (timeout % 1000) * 1000 };
	return createTimerEvent(EV_PERSIST, callback, arg, &t);
}

namespace {
struct timerCBParams_t
{
	void* params;
	void (*callback)(int fd, short what, void* arg);
	EventHandle event;
};

void timerCallBack(int fd, short what, void* arg)
{
	std::unique_ptr<timerCBParams_t> cbParams(static_cast<timerCBParams_t*>(arg));
	cbParams->callback(fd, what, cbParams->params);
}
} // namespace

void* Process::timerSingleCreate(void (*callback)(int fd, short what, void* arg),
                                 int   timeout,
                                 void* params)
{
	const timeval t = { timeout / 1000, (timeout % 1000) * 1000 };

	auto cbParams      = std::make_unique<timerCBParams_t>();
	cbParams->params   = params;
	cbParams->callback = callback;
	cbParams->event.reset(event_new(ctx.evbase.get(), -1, 0, timerCallBack, cbParams.get()));
	if (cbParams->event == nullptr) {
		ELOG("Failed to create one-shot timer event");
		return nullptr;
	}
	if (event_add(cbParams->event.get(), &t) != 0) {
		ELOG("Failed to arm one-shot timer event");
		return nullptr;
	}

	auto* event = cbParams->event.get();
	cbParams.release();
	return event;
}

void Process::timerDelete(void* timer)
{
	deleteRegisteredEvent(static_cast<struct event*>(timer));
}

struct event* Process::fdCreate(int fd, void (*callback)(int fd, short what, void* arg), void* args)
{
	return createPersistentEvent(fd, EV_READ | EV_PERSIST, callback, args);
}

void Process::fdDelete(struct event* ev)
{
	deleteRegisteredEvent(ev);
}

void Process::run()
{
	event_base_loop(ctx.evbase.get(), EVLOOP_NO_EXIT_ON_EMPTY);
}

void Process::stop()
{
	if (ctx.evbase != nullptr) {
		event_base_loopbreak(ctx.evbase.get());
	}
}

void Process::end()
{
	ctx.events.clear();
	ctx.evbase.reset();
	ctx.module_name.clear();
}

struct event_base* Process::base()
{
	return ctx.evbase.get();
}
