#pragma once

#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <wayland-server.h>
#include "../../helpers/signal/Signal.hpp"
#include <hyprutils/os/FileDescriptor.hpp>

#include <liburing.h>
#include <list>

#include "EventLoopTimer.hpp"

namespace Aquamarine {
    struct SPollFD;
};

class CEventLoopManager {
  public:
    CEventLoopManager(wl_display* display, wl_event_loop* wlEventLoop);
    ~CEventLoopManager();

    void enterLoop();

    // Note: will remove the timer if the ptr is lost.
    void addTimer(SP<CEventLoopTimer> timer);
    void removeTimer(SP<CEventLoopTimer> timer);

    void onTimerFire();

    // schedules a recalc of the timers
    void scheduleRecalc();

    // schedules a function to run later, aka in a wayland idle event.
    void doLater(const std::function<void()>& fn);

    struct SIdleData {
        wl_event_source*                   eventSource = nullptr;
        std::vector<std::function<void()>> fns;
    };

    struct SReadableWaiter {
        int                            notOwnedFd = -1;
        Hyprutils::OS::CFileDescriptor ownedFd;
        std::function<void()>          fn;

        SReadableWaiter(Hyprutils::OS::CFileDescriptor f, std::function<void()> func) : ownedFd(std::move(f)), fn(std::move(func)) {}
        SReadableWaiter(int f, std::function<void()> func) : notOwnedFd(f), fn(std::move(func)) {}
        ~SReadableWaiter() = default;

        int fd() {
            if (notOwnedFd >= 0)
                return notOwnedFd;

            return ownedFd.get();
        }

        bool rearm() {
            return notOwnedFd >= 0 && !ownedFd.isValid();
        }

        // copy
        SReadableWaiter(const SReadableWaiter&)            = delete;
        SReadableWaiter& operator=(const SReadableWaiter&) = delete;

        // move
        SReadableWaiter(SReadableWaiter&& other) noexcept            = default;
        SReadableWaiter& operator=(SReadableWaiter&& other) noexcept = default;
    };

    void addOneShotPoll(Hyprutils::OS::CFileDescriptor fd, std::function<void()>&& fn);
    void addOneShotPoll(int fd, std::function<void()>&& fn);

  private:
    void initIoUring();
    void processRingCompletions();
    void addWaiter(Hyprutils::Memory::CWeakPointer<SReadableWaiter> waiter);

    void syncPollFDs();
    void nudgeTimers();

    struct SEventSourceData {
        SP<Aquamarine::SPollFD> pollFD;
        wl_event_source*        eventSource = nullptr;
    };

    struct {
        wl_event_loop*   loop        = nullptr;
        wl_display*      display     = nullptr;
        wl_event_source* eventSource = nullptr;
    } m_wayland;

    struct {
        io_uring                       ring;
        Hyprutils::OS::CFileDescriptor eventFd;
        wl_event_source*               eventSource = nullptr;
    } m_io;

    struct {
        std::vector<SP<CEventLoopTimer>> timers;
        Hyprutils::OS::CFileDescriptor   timerfd;
        bool                             recalcScheduled = false;
    } m_timers;

    SIdleData                       m_idle;
    std::list<UP<SReadableWaiter>>  m_readableWaiters;
    std::map<int, SReadableWaiter*> m_aqPipelineWaiters;

    struct {
        CHyprSignalListener pollFDsChanged;
    } m_listeners;

    wl_event_source* m_configWatcherInotifySource = nullptr;

    friend class CAsyncDialogBox;
    friend class CMainLoopExecutor;
};

inline UP<CEventLoopManager> g_pEventLoopManager;
