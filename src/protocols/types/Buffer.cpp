#include "Buffer.hpp"
#include "protocols/types/WLBuffer.hpp"
#include <stdexcept>

IHLBuffer::~IHLBuffer() {}

const SP<CTexture>& IHLBuffer::getTexture() {
    return texture;
}

const SP<CWLBufferResource>& IHLBuffer::getResource() {
    return resource;
}

CHLAttachedBuffer::CHLAttachedBuffer(SP<IHLBuffer> buffer_) : buffer(buffer_) {
    if (!buffer)
        throw std::runtime_error("Tried to create a CHLAttachedBuffer out of a null buffer");
}

CHLAttachedBuffer::~CHLAttachedBuffer() {
    const auto& resource = buffer->getResource();
    if (resource)
        resource->sendRelease();
}

Aquamarine::eBufferCapability CHLAttachedBuffer::caps() {
    return buffer->caps();
}

Aquamarine::eBufferType CHLAttachedBuffer::type() {
    return buffer->type();
}

void CHLAttachedBuffer::update(const CRegion& damage) {
    buffer->update(damage);
}

bool CHLAttachedBuffer::isSynchronous() {
    return buffer->isSynchronous();
}

bool CHLAttachedBuffer::good() {
    return buffer->good();
}

Aquamarine::SDMABUFAttrs CHLAttachedBuffer::dmabuf() {
    return buffer->dmabuf();
}

Aquamarine::SSHMAttrs CHLAttachedBuffer::shm() {
    return buffer->shm();
}

std::tuple<uint8_t*, uint32_t, size_t> CHLAttachedBuffer::beginDataPtr(uint32_t flags) {
    return buffer->beginDataPtr(flags);
}

void CHLAttachedBuffer::endDataPtr() {
    return buffer->endDataPtr();
}

bool CHLAttachedBuffer::getOpaque() {
    return buffer->getOpaque();
}

Hyprutils::Math::Vector2D& CHLAttachedBuffer::getSize() {
    return buffer->getSize();
}

Aquamarine::CAttachmentManager& CHLAttachedBuffer::getAttachments() {
    return buffer->getAttachments();
}

Hyprutils::Signal::CSignal& CHLAttachedBuffer::getDestroyEvent() {
    return buffer->getDestroyEvent();
}

const SP<CTexture>& CHLAttachedBuffer::getTexture() {
    return buffer->getTexture();
}

const SP<CWLBufferResource>& CHLAttachedBuffer::getResource() {
    return buffer->getResource();
}
