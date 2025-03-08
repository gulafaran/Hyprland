#include "WLBufferManager.hpp"
#include "macros.hpp"

CWLBufferResource::CWLBufferResource(UP<CWlBuffer>&& resource_) : resource(std::move(resource_)) {
    resource->setOnDestroy([this](CWlBuffer* r) {
        buffer->events.destroy.emit();
        g_pWLBufferManager->destroyResource(this);
    });
    resource->setDestroy([this](CWlBuffer* r) {
        buffer->events.destroy.emit();
        g_pWLBufferManager->destroyResource(this);
    });

    resource->setData(this);
}

CWLBufferResource::~CWLBufferResource() {
    buffer->events.destroy.emit();
    sendRelease();
}

bool CWLBufferResource::good() {
    return resource->resource();
}

void CWLBufferResource::sendRelease() {
    resource->sendRelease();
}

wl_resource* CWLBufferResource::getResource() {
    return resource->resource();
}

WP<CWLBufferResource> CWLBufferManager::create(UP<CWlBuffer> resource) {
    const auto& res = m_vWlBuffers.emplace_back(makeUnique<CWLBufferResource>(std::move(resource)));

    if UNLIKELY (!res->good()) {
        m_vWlBuffers.pop_back();
        return {};
    }

    return res;
}

WP<CWLBufferResource> CWLBufferManager::fromResource(wl_resource* res) {
    for (auto& r : m_vWlBuffers) {
        if (r->getResource() == res)
            return r;
    }

    return {};
}

void CWLBufferManager::destroyResource(CWLBufferResource* resource) {
    std::erase_if(m_vWlBuffers, [resource](const auto& e) { return e.get() == resource; });
}
