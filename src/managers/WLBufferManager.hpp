#pragma once

#include <vector>
#include "helpers/memory/Memory.hpp"
#include "protocols/types/Buffer.hpp"
#include "wayland.hpp"
#include <aquamarine/buffer/Buffer.hpp>

class CWLSurfaceResource;

class CWLBufferResource {
  public:
    CWLBufferResource(UP<CWlBuffer>&& resource);
    ~CWLBufferResource();

    bool          good();
    void          sendRelease();
    wl_resource*  getResource();

    SP<IHLBuffer> buffer;

  private:
    UP<CWlBuffer> resource;

    friend class IHLBuffer;
};

class CWLBufferManager {
  public:
    CWLBufferManager()  = default;
    ~CWLBufferManager() = default;
    WP<CWLBufferResource> create(UP<CWlBuffer> resource);
    WP<CWLBufferResource> fromResource(wl_resource* res);
    void                  destroyResource(CWLBufferResource* resource);

  private:
    std::vector<UP<CWLBufferResource>> m_vWlBuffers;
};

inline UP<CWLBufferManager> g_pWLBufferManager;
