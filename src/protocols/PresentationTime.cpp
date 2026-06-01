#include "PresentationTime.hpp"
#include <algorithm>
#include "../helpers/Monitor.hpp"
#include "../event/EventBus.hpp"
#include "core/Compositor.hpp"
#include "core/Output.hpp"
#include <aquamarine/output/Output.hpp>

CQueuedPresentationData::CQueuedPresentationData(SP<CWLSurfaceResource> surf) : m_surface(surf) {
    ;
}

void CQueuedPresentationData::setPresentationType(bool zeroCopy_) {
    m_zeroCopy = zeroCopy_;
}

void CQueuedPresentationData::attachMonitor(PHLMONITOR pMonitor_) {
    m_monitor = pMonitor_;
}

void CQueuedPresentationData::addFeedbacks(std::vector<WP<CPresentationFeedback>>&& feedbacks) {
    m_feedbacks = std::move(feedbacks);
}

void CQueuedPresentationData::presented() {
    m_wasPresented = true;
}

void CQueuedPresentationData::discarded() {
    m_wasPresented = false;
}

/* wp_presentation::feedback
 * Request presentation feedback for the current content submission on the given surface. This creates a new presentation_feedback object, which will deliver the feedback information once.
 * If multiple presentation_feedback objects are created for the same submission, they will all deliver the same information.
 * A presentation_feedback object returns an indication that a wl_surface content update has become visible to the user.
 * One object corresponds to one content update submission (wl_surface.commit)
 */
CPresentationFeedback::CPresentationFeedback(UP<CWpPresentationFeedback>&& resource_, SP<CWLSurfaceResource> surf) : m_resource(std::move(resource_)), m_surface(surf) {
    if UNLIKELY (!good())
        return;

    m_resource->setOnDestroy([this](CWpPresentationFeedback* pMgr) {
        if (!m_done) // if it's done, it's probably already destroyed. If not, it will be in a sec.
            PROTO::presentation->destroyResource(this);
    });

    // If multiple presentation_feedback objects are created for the same submission, they will all deliver the same information.
    m_listeners.surfaceStateCommit = m_surface->m_events.stateCommit.listen([this](auto state) {
        if (m_added)
            return;

        state->presentationFeedbacks.emplace_back(CPresentationFeedback::fromResource(m_resource.get()));
        state->updated.bits.presentation = true;
        m_added                          = true;
    });
}

CPresentationFeedback::~CPresentationFeedback() {
    if (!m_done && m_resource)
        m_resource->sendDiscarded();
}

WP<CPresentationFeedback> CPresentationFeedback::fromResource(CWpPresentationFeedback* res) {
    for (const auto& r : PROTO::presentation->m_feedbacks) {
        if (r && r->m_resource && r->m_resource.get() == res)
            return r;
    }

    return {};
}

bool CPresentationFeedback::good() {
    return m_resource->resource();
}

void CPresentationFeedback::send(const CQueuedPresentationData& data, const timespec& when, uint32_t untilRefreshNs, uint64_t seq, uint32_t reportedFlags) {
    if (!data.m_monitor) // maybe send discard on monitor gone
        return;

    auto client = m_resource->client();

    if LIKELY (PROTO::outputs.contains(data.m_monitor->m_name) && data.m_wasPresented) {
        if LIKELY (auto outputResources = PROTO::outputs.at(data.m_monitor->m_name)->outputResourcesFrom(client); !outputResources.empty()) {
            for (const auto& r : outputResources) {
                m_resource->sendSyncOutput(r->getResource()->resource());
            }
        }
    }

    if (data.m_wasPresented) {
        uint32_t flags = 0;
        if (!data.m_monitor->m_tearingState.activelyTearing)
            flags |= WP_PRESENTATION_FEEDBACK_KIND_VSYNC;
        if (data.m_zeroCopy)
            flags |= WP_PRESENTATION_FEEDBACK_KIND_ZERO_COPY;
        if (reportedFlags & Aquamarine::IOutput::AQ_OUTPUT_PRESENT_HW_CLOCK)
            flags |= WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK;
        if (reportedFlags & Aquamarine::IOutput::AQ_OUTPUT_PRESENT_HW_COMPLETION)
            flags |= WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION;

        time_t tv_sec = 0;
        if (sizeof(time_t) > 4)
            tv_sec = when.tv_sec >> 32;

        uint32_t refreshNs = m_resource->version() == 1 && data.m_monitor->m_vrrActive && data.m_monitor->m_output->vrrCapable ? 0 : untilRefreshNs;

        m_resource->sendPresented(sc<uint32_t>(tv_sec), sc<uint32_t>(when.tv_sec & 0xFFFFFFFF), sc<uint32_t>(when.tv_nsec), refreshNs, sc<uint32_t>(seq >> 32),
                                  sc<uint32_t>(seq & 0xFFFFFFFF), sc<wpPresentationFeedbackKind>(flags));
    } else
        m_resource->sendDiscarded();

    m_done = true;
}

CPresentationProtocol::CPresentationProtocol(const wl_interface* iface, const int& ver, const std::string& name) : IWaylandProtocol(iface, ver, name) {
    ;
}

void CPresentationProtocol::bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) {
    const auto RESOURCE = m_managers.emplace_back(makeUnique<CWpPresentation>(client, ver, id)).get();
    RESOURCE->setOnDestroy([this](CWpPresentation* p) { this->onManagerResourceDestroy(p->resource()); });

    RESOURCE->setDestroy([this](CWpPresentation* pMgr) { this->onManagerResourceDestroy(pMgr->resource()); });
    RESOURCE->setFeedback([this](CWpPresentation* pMgr, wl_resource* surf, uint32_t id) { this->onSetFeedback(pMgr, surf, id); });
    RESOURCE->sendClockId(CLOCK_MONOTONIC);
}

void CPresentationProtocol::onManagerResourceDestroy(wl_resource* res) {
    std::erase_if(m_managers, [&](const auto& other) { return other->resource() == res; });
}

void CPresentationProtocol::destroyResource(CPresentationFeedback* feedback) {
    std::erase_if(m_feedbacks, [&](const auto& other) { return other.get() == feedback; });
}

void CPresentationProtocol::onSetFeedback(CWpPresentation* pMgr, wl_resource* surf, uint32_t id) {
    const auto  CLIENT = pMgr->client();
    const auto& RESOURCE =
        m_feedbacks.emplace_back(makeUnique<CPresentationFeedback>(makeUnique<CWpPresentationFeedback>(CLIENT, pMgr->version(), id), CWLSurfaceResource::fromResource(surf))).get();

    if UNLIKELY (!RESOURCE->good()) {
        pMgr->noMemory();
        m_feedbacks.pop_back();
        return;
    }
}

void CPresentationProtocol::onPresented(PHLMONITOR pMonitor, const timespec& when, uint32_t untilRefreshNs, uint64_t seq, uint32_t reportedFlags) {
    for (auto const& q : m_queuedData) {
        if (!q.first) // surface gone
            continue;

        auto& d = q.second;

        if (!d.m_monitor || d.m_monitor != pMonitor)
            continue;

        for (auto& f : d.m_feedbacks) {
            if (!f) // feedback destroyed
                continue;

            f->send(d, when, untilRefreshNs, seq, reportedFlags);
            f->m_done = true;
        }
    }

    if (m_feedbacks.size() > 10000) {
        LOGM(Log::ERR, "FIXME: presentation has a feedback leak, and has grown to {} pending entries!!! Dropping!!!!!", m_feedbacks.size());

        // Move the elements from the 9000th position to the end of the vector.
        std::vector<UP<CPresentationFeedback>> newFeedbacks;
        newFeedbacks.reserve(m_feedbacks.size() - 9000);

        for (auto it = m_feedbacks.begin() + 9000; it != m_feedbacks.end(); ++it) {
            newFeedbacks.push_back(std::move(*it));
        }

        m_feedbacks = std::move(newFeedbacks);
    }

    std::erase_if(m_queuedData, [](const auto& other) { return !other.first; });
    std::erase_if(m_feedbacks, [](const auto& other) { return !other->m_surface || other->m_done; });
}

void CPresentationProtocol::queueData(WP<CWLSurfaceResource> surf, CQueuedPresentationData&& data) {
    m_queuedData.insert_or_assign(surf, std::move(data));
}
