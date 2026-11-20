#pragma once

#include <wtf/Function.h>

// WebCore runs on one thread for the whole process: the thread that calls initializeMainThread is the main
// thread of WTF, so it must outlive every core and the interface thread never touches WebCore objects
namespace YozoraCoreThread {

// runs the work on the core thread after the work queued before it, and returns at once
void post(WTF::Function<void()>&&);
// runs the work on the core thread and returns when it has finished
void call(WTF::Function<void()>&&);
// called on the core thread once WTF made it the main thread: from here it sleeps in the WebCore run loop and
// wakes for a timer, network callback or queued work the moment it is due, not at its next tick
void attachRunLoop();
// runs a tick now instead of at its next interval
void wake();
// returns true while another frame is needed, quiet pages publish state at a lower rate and timers and
// network callbacks run in the run loop between passes
void setTick(bool (*tick)());
// a paused thread runs queued work only, the app uses it while it is in the background
void setPaused(bool);
bool isCurrent();
// a quiet thread keeps its run loop going, so network callbacks of downloads arrive, and makes no pass over the pages
void setQuiet(bool);
// true while the app is in the background and the thread holds still
bool isPaused();
// processor time the calling thread has used, for the share of it that is not a tick
long long cpuMilliseconds();

}
