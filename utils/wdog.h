#ifndef MADT_UTILS_WDOG_H
#define MADT_UTILS_WDOG_H

namespace Secretary::utils {

void wdogInit();
void wdogKick();
void wdogDone();
void wdogReady();

} // namespace Secretary::utils

#define WDOGINIT()  Secretary::utils::wdogInit()
#define WDOGKICK()  Secretary::utils::wdogKick()
#define WDOGDONE()  Secretary::utils::wdogDone()
#define WDOGREADY() Secretary::utils::wdogReady()

#endif
