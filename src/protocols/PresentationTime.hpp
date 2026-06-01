#pragma once

#include <ctime>
#include <vector>
#include <cstdint>
#include "WaylandProtocol.hpp"
#include "presentation-time.hpp"
#include "../helpers/time/Time.hpp"
#include "../helpers/signal/Signal.hpp"

class CMonitor;
class CWLSurfaceResource;
class CPresentationFeedback;

class CQueuedPresentationData {
  public:
    CQueuedPresentationData(SP<CWLSurfaceResource> surf);

    void setPresentationType(bool zeroCopy);
    void attachMonitor(PHLMONITOR pMonitor);
    void addFeedbacks(std::vector<WP<CPresentationFeedback>>&& feedbacks);

    void presented();
    void discarded();

    bool m_done = false;

  private:
    bool                                   m_wasPresented = false;
    bool                                   m_zeroCopy     = false;
    PHLMONITORREF                          m_monitor;
    WP<CWLSurfaceResource>                 m_surface;
    std::vector<WP<CPresentationFeedback>> m_feedbacks;

    friend class CPresentationFeedback;
    friend class CPresentationProtocol;
};

class CPresentationFeedback {
  public:
    CPresentationFeedback(UP<CWpPresentationFeedback>&& resource_, SP<CWLSurfaceResource> surf);
    ~CPresentationFeedback();
    static WP<CPresentationFeedback> fromResource(CWpPresentationFeedback*);

    bool                             good();
    void                             send(const CQueuedPresentationData& data, const timespec& when, uint32_t untilRefreshNs, uint64_t seq, uint32_t reportedFlags);

  private:
    UP<CWpPresentationFeedback> m_resource;
    WP<CWLSurfaceResource>      m_surface;
    bool                        m_done  = false;
    bool                        m_added = false;

    struct {
        CHyprSignalListener surfaceStateCommit;
    } m_listeners;

    friend class CPresentationProtocol;
};

class CPresentationProtocol : public IWaylandProtocol {
  public:
    CPresentationProtocol(const wl_interface* iface, const int& ver, const std::string& name);

    virtual void bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id);

    void         onPresented(PHLMONITOR pMonitor, const timespec& when, uint32_t untilRefreshNs, uint64_t seq, uint32_t reportedFlags);
    void         queueData(WP<CWLSurfaceResource> surf, CQueuedPresentationData&& data);

  private:
    void onManagerResourceDestroy(wl_resource* res);
    void destroyResource(CPresentationFeedback* feedback);
    void onSetFeedback(CWpPresentation* pMgr, wl_resource* surf, uint32_t id);

    //
    std::vector<UP<CWpPresentation>>                                    m_managers;
    std::vector<UP<CPresentationFeedback>>                              m_feedbacks;
    std::unordered_map<WP<CWLSurfaceResource>, CQueuedPresentationData> m_queuedData;

    friend class CPresentationFeedback;
};

namespace PROTO {
    inline UP<CPresentationProtocol> presentation;
};
