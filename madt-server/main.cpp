#include <thread>

#include "process/process.h"
#include "utils/wdog.h"

#include "gui.h"
#include "madt-server.h"

using namespace Secretary::Madt;
namespace {
	constexpr int WATCHDOG_KICK_MS = 18 * 1000;
}

static void wdogRearm(evutil_socket_t fd, short what, void* arg)
{
	WDOGKICK();
}

int main(int argc, char* argv[])
{
	Server madtServer(25000);
	std::thread t([argc, argv, &madtServer]() {
		Process::setup(argc, argv, "madt", "madt");
		WDOGINIT();
		Process::timerCreate(wdogRearm, WATCHDOG_KICK_MS, NULL);
		madtServer.start(Process::base());
		// Service readiness should reflect control-socket availability, not GUI completion.
		WDOGREADY();
		Process::run();
		WDOGDONE();
		Gui::stop();
		Process::end();
	});
	const int ret = Gui::run(argc, argv);
	Process::stop();
	t.join();
	return ret;
}
