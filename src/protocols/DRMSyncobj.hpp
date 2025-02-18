#pragma once

#include <vector>
#include "WaylandProtocol.hpp"
#include "helpers/sync/SyncReleaser.hpp"
#include "linux-drm-syncobj-v1.hpp"
#include "../helpers/signal/Signal.hpp"
#include <hyprutils/os/FileDescriptor.hpp>

class CWLSurfaceResource;
class CDRMSyncobjTimelineResource;
class CSyncTimeline;

class CDRMSyncobjSurfaceResource {
  public:
    CDRMSyncobjSurfaceResource(SP<CWpLinuxDrmSyncobjSurfaceV1> resource_, SP<CWLSurfaceResource> surface_);
    ~CDRMSyncobjSurfaceResource();

    bool                   good();

    WP<CWLSurfaceResource> surface;
    struct STimeLineState {
        WP<CDRMSyncobjTimelineResource> resource;
        uint64_t                        point = 0;

        STimeLineState()                                     = default;
        STimeLineState(STimeLineState&&) noexcept            = default;
        STimeLineState& operator=(STimeLineState&&) noexcept = default;
        STimeLineState(const STimeLineState&)                = delete;
        STimeLineState& operator=(const STimeLineState&)     = delete;
        ~STimeLineState()                                    = default;
    } acquire, release, pendingAcquire, pendingRelease;

    std::vector<UP<CSyncReleaser>> releasePoints;

  private:
    SP<CWpLinuxDrmSyncobjSurfaceV1> resource;
    bool                            acquireWaiting = false;

    struct {
        CHyprSignalListener surfacePrecommit;
        CHyprSignalListener surfaceRoleCommit;
    } listeners;
};

class CDRMSyncobjTimelineResource {
  public:
    CDRMSyncobjTimelineResource(SP<CWpLinuxDrmSyncobjTimelineV1> resource_, Hyprutils::OS::CFileDescriptor&& fd_);
    ~CDRMSyncobjTimelineResource() = default;
    static SP<CDRMSyncobjTimelineResource> fromResource(wl_resource*);

    bool                                   good();

    WP<CDRMSyncobjTimelineResource>        self;
    Hyprutils::OS::CFileDescriptor         fd;
    SP<CSyncTimeline>                      timeline;

  private:
    SP<CWpLinuxDrmSyncobjTimelineV1> resource;
};

class CDRMSyncobjManagerResource {
  public:
    CDRMSyncobjManagerResource(SP<CWpLinuxDrmSyncobjManagerV1> resource_);

    bool good();

  private:
    SP<CWpLinuxDrmSyncobjManagerV1> resource;
};

class CDRMSyncobjProtocol : public IWaylandProtocol {
  public:
    CDRMSyncobjProtocol(const wl_interface* iface, const int& ver, const std::string& name);

    virtual void bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id);

  private:
    void destroyResource(CDRMSyncobjManagerResource* resource);
    void destroyResource(CDRMSyncobjTimelineResource* resource);
    void destroyResource(CDRMSyncobjSurfaceResource* resource);

    //
    std::vector<SP<CDRMSyncobjManagerResource>>  m_vManagers;
    std::vector<SP<CDRMSyncobjTimelineResource>> m_vTimelines;
    std::vector<SP<CDRMSyncobjSurfaceResource>>  m_vSurfaces;

    //
    int drmFD = -1;

    friend class CDRMSyncobjManagerResource;
    friend class CDRMSyncobjTimelineResource;
    friend class CDRMSyncobjSurfaceResource;
};

namespace PROTO {
    inline UP<CDRMSyncobjProtocol> sync;
};
