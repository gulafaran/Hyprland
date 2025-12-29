#include "EventLoopManager.hpp"
#include "../../debug/log/Logger.hpp"
#include "../../Compositor.hpp"
#include "../../config/ConfigWatcher.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <ranges>

#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <ctime>

#include <poll.h>

#include <aquamarine/backend/Backend.hpp>
using namespace Hyprutils::OS;

#define TIMESPEC_NSEC_PER_SEC 1000000000L

CEventLoopManager::CEventLoopManager(wl_display* display, wl_event_loop* wlEventLoop) {
    m_timers.timerfd  = CFileDescriptor{timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC)};
    m_wayland.loop    = wlEventLoop;
    m_wayland.display = display;

    initIoUring();
}

CEventLoopManager::~CEventLoopManager() {
    if (m_wayland.eventSource)
        wl_event_source_remove(m_wayland.eventSource);
    if (m_idle.eventSource)
        wl_event_source_remove(m_idle.eventSource);
    if (m_configWatcherInotifySource)
        wl_event_source_remove(m_configWatcherInotifySource);
}

static int configWatcherWrite(int fd, uint32_t mask, void* data) {
    g_pConfigWatcher->onInotifyEvent();
    return 0;
}

void CEventLoopManager::enterLoop() {
    addOneShotPoll(m_timers.timerfd.get(), [this]() {
        // IMPORTANT: We must read the timerfd to clear the 'readable' signal.
        // If we don't, io_uring will trigger again immediately.
        uint64_t expirations;
        if (read(m_timers.timerfd.get(), &expirations, sizeof(expirations)) > 0) {
            onTimerFire();
        }
    });

    if (const auto& FD = g_pConfigWatcher->getInotifyFD(); FD.isValid())
        m_configWatcherInotifySource = wl_event_loop_add_fd(m_wayland.loop, FD.get(), WL_EVENT_READABLE, configWatcherWrite, nullptr);

    syncPollFDs();
    m_listeners.pollFDsChanged = g_pCompositor->m_aqBackend->events.pollFDsChanged.listen([this] { syncPollFDs(); });

    // if we have a session, dispatch it to get the pending input devices
    if (g_pCompositor->m_aqBackend->hasSession())
        g_pCompositor->m_aqBackend->session->dispatchPendingEventsAsync();

    wl_display_run(m_wayland.display);

    Log::logger->log(Log::DEBUG, "Kicked off the event loop! :(");
}

void CEventLoopManager::onTimerFire() {
    const auto CPY = m_timers.timers;
    for (auto const& t : CPY) {
        if (t.strongRef() > 2 /* if it's 2, it was lost. Don't call it. */ && t->passed() && !t->cancelled())
            t->call(t);
    }

    scheduleRecalc();
}

void CEventLoopManager::addTimer(SP<CEventLoopTimer> timer) {
    if (std::ranges::contains(m_timers.timers, timer))
        return;
    m_timers.timers.emplace_back(timer);
    scheduleRecalc();
}

void CEventLoopManager::removeTimer(SP<CEventLoopTimer> timer) {
    if (!std::ranges::contains(m_timers.timers, timer))
        return;
    std::erase_if(m_timers.timers, [timer](const auto& t) { return timer == t; });
    scheduleRecalc();
}

static void timespecAddNs(timespec* pTimespec, int64_t delta) {
    auto delta_ns_low = delta % TIMESPEC_NSEC_PER_SEC;
    auto delta_s_high = delta / TIMESPEC_NSEC_PER_SEC;

    pTimespec->tv_sec += delta_s_high;

    pTimespec->tv_nsec += delta_ns_low;
    if (pTimespec->tv_nsec >= TIMESPEC_NSEC_PER_SEC) {
        pTimespec->tv_nsec -= TIMESPEC_NSEC_PER_SEC;
        ++pTimespec->tv_sec;
    }
}

void CEventLoopManager::scheduleRecalc() {
    // do not do it instantly, do it later. Avoid recursive access to the timer
    // vector, it could be catastrophic if we modify it while iterating

    if (m_timers.recalcScheduled)
        return;

    m_timers.recalcScheduled = true;

    doLater([this] { nudgeTimers(); });
}

void CEventLoopManager::nudgeTimers() {
    m_timers.recalcScheduled = false;

    // remove timers that have gone missing
    std::erase_if(m_timers.timers, [](const auto& t) { return t.strongRef() <= 1; });

    long nextTimerUs = 10L * 1000 * 1000; // 10s

    for (auto const& t : m_timers.timers) {
        if (auto const& µs = t->leftUs(); µs < nextTimerUs)
            nextTimerUs = µs;
    }

    nextTimerUs = std::clamp(nextTimerUs + 1, 1L, std::numeric_limits<long>::max());

    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    timespecAddNs(&now, nextTimerUs * 1000L);

    itimerspec ts = {.it_value = now};

    timerfd_settime(m_timers.timerfd.get(), TFD_TIMER_ABSTIME, &ts, nullptr);
}

void CEventLoopManager::doLater(const std::function<void()>& fn) {
    m_idle.fns.emplace_back(fn);

    if (m_idle.eventSource)
        return;

    m_idle.eventSource = wl_event_loop_add_idle(
        m_wayland.loop,
        [](void* data) {
            auto IDLE = sc<CEventLoopManager::SIdleData*>(data);
            auto cpy  = IDLE->fns;
            IDLE->fns.clear();
            IDLE->eventSource = nullptr;
            for (auto const& c : cpy) {
                if (c)
                    c();
            }
        },
        &m_idle);
}

void CEventLoopManager::initIoUring() {
    io_uring_params params{};
    params.flags = IORING_SETUP_SINGLE_ISSUER;

    int ret = io_uring_queue_init_params(256, &m_io.ring, &params);
    if (ret < 0) {
        Log::logger->log(Log::ERR, "io_uring_queue_init_params failed: {}", ret);
        return;
    }

    m_io.eventFd = CFileDescriptor{eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
    if (!m_io.eventFd.isValid()) {
        Log::logger->log(Log::ERR, "eventfd() failed");
        io_uring_queue_exit(&m_io.ring);
        return;
    }

    ret = io_uring_register_eventfd(&m_io.ring, m_io.eventFd.get());
    if (ret < 0) {
        Log::logger->log(Log::ERR, "io_uring_register_eventfd failed: {}", ret);
        io_uring_queue_exit(&m_io.ring);
        return;
    }

    m_io.eventSource = wl_event_loop_add_fd(
        m_wayland.loop, m_io.eventFd.get(), WL_EVENT_READABLE,
        [](int, uint32_t, void* data) {
            sc<CEventLoopManager*>(data)->processRingCompletions();
            return 1;
        },
        this);
}

void CEventLoopManager::processRingCompletions() {
    // Drain eventfd
    eventfd_t val;
    while (read(m_io.eventFd.get(), &val, sizeof(val)) > 0) {}

    io_uring_cqe*                 cqe;
    unsigned                      head;
    unsigned                      count = 0;

    std::vector<SReadableWaiter*> finished;
    io_uring_for_each_cqe(&m_io.ring, head, cqe) {
        ++count;
        auto* waiter = rc<SReadableWaiter*>(io_uring_cqe_get_data64(cqe));
        if (!waiter)
            continue;

        if (cqe->res < 0) {
            if (cqe->res != -ECANCELED) // Ignore cancellations
                Log::logger->log(Log::ERR, "io_uring waiter poll error on fd {}: {}", waiter->fd(), cqe->res);
        } else if (waiter->fn)
            waiter->fn();

        finished.push_back(waiter);
    }

    if (count)
        io_uring_cq_advance(&m_io.ring, count);

    for (auto* w : finished) {
        if (w->rearm()) {
            addOneShotPoll(w->fd(), std::move(w->fn));

            if (m_aqPipelineWaiters.contains(w->fd()))
                m_aqPipelineWaiters[w->fd()] = m_readableWaiters.back().get();
        }

        std::erase_if(m_readableWaiters, [&](const auto& up) { return up.get() == w; });
    }
}

void CEventLoopManager::addOneShotPoll(CFileDescriptor fd, std::function<void()>&& fn) {
    auto& waiter = m_readableWaiters.emplace_back(makeUnique<SReadableWaiter>(std::move(fd), std::move(fn)));
    addWaiter(waiter);
}

void CEventLoopManager::addOneShotPoll(int fd, std::function<void()>&& fn) {
    auto& waiter = m_readableWaiters.emplace_back(makeUnique<SReadableWaiter>(fd, std::move(fn)));
    addWaiter(waiter);
}

void CEventLoopManager::addWaiter(Hyprutils::Memory::CWeakPointer<SReadableWaiter> waiter) {
    io_uring_sqe* sqe = io_uring_get_sqe(&m_io.ring);
    if (!sqe) {
        m_readableWaiters.pop_back();
        Log::logger->log(Log::ERR, "io_uring SQ full");
        return;
    }

    io_uring_prep_poll_add(sqe, waiter->fd(), POLLIN);
    io_uring_sqe_set_data64(sqe, rc<uint64_t>(waiter.get()));

    if (io_uring_submit(&m_io.ring) < 0) {
        Log::logger->log(Log::ERR, "io_uring_submit failed");
        m_readableWaiters.pop_back();
    }
}

void CEventLoopManager::syncPollFDs() {
    auto aqPollFDs = g_pCompositor->m_aqBackend->getPollFDs();
    std::erase_if(m_aqPipelineWaiters, [&](const auto& item) {
        int  monitoredFd = item.first;
        bool stillExists = std::ranges::any_of(aqPollFDs, [&](const auto& p) { return p->fd == monitoredFd; });

        if (!stillExists) {
            SReadableWaiter* w = item.second;
            std::erase_if(m_readableWaiters, [w](const auto& up) { return up.get() == w; });
            return true;
        }
        return false;
    });

    for (auto& p : aqPollFDs) {
        if (m_aqPipelineWaiters.contains(p->fd))
            continue;

        addOneShotPoll(p->fd, [pollFD = p] { pollFD->onSignal(); });

        SReadableWaiter* newWaiter = m_readableWaiters.back().get();
        m_aqPipelineWaiters[p->fd] = newWaiter;
    }
}
