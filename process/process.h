#ifndef MADT_PROCESS_EVENT_H
#define MADT_PROCESS_EVENT_H

#include <event2/event.h>
#include <stdint.h>

namespace Process {

void setup(int         argc,
           char*       argv[],
           const char* serviceName,
           const char* moduleName,
           bool        log         = true,
           bool        catchSignal = true);
void setupLog(int argc, char* argv[], const char* moduleName);
void run();
void stop();
void end();

void* timerCreate(void (*callback)(int fd, short what, void* arg), int timeout, void* arg);
void* timerSingleCreate(void (*callback)(int fd, short what, void* arg), int timeout, void* params);
void  timerDelete(void* timer);

struct event* fdCreate(int fd, void (*callback)(int fd, short what, void* arg), void* args);
void          fdDelete(struct event*);

struct event_base* base();

} // namespace Process

#endif
