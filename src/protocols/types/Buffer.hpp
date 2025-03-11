#pragma once

#include "../../defines.hpp"
#include "../../render/Texture.hpp"
#include "./WLBuffer.hpp"
#include "protocols/DRMSyncobj.hpp"

#include <aquamarine/buffer/Buffer.hpp>

class CSyncReleaser;

class IHLBuffer : public Aquamarine::IBuffer {
  public:
    virtual ~IHLBuffer();
    virtual Aquamarine::eBufferCapability caps()                        = 0;
    virtual Aquamarine::eBufferType       type()                        = 0;
    virtual void                          update(const CRegion& damage) = 0;
    virtual bool                          isSynchronous()               = 0; // whether the updates to this buffer are synchronous, aka happen over cpu
    virtual bool                          good()                        = 0;

    virtual const SP<CTexture>&           getTexture();
    virtual const SP<CWLBufferResource>&  getResource();

  protected:
    SP<CTexture>          texture;
    SP<CWLBufferResource> resource;
};

// for ref-counting. Releases in ~dtor
class CHLAttachedBuffer : public IHLBuffer {
  public:
    explicit CHLAttachedBuffer(SP<IHLBuffer> buffer);
    virtual ~CHLAttachedBuffer();

    virtual Aquamarine::eBufferCapability          caps();
    virtual Aquamarine::eBufferType                type();
    virtual void                                   update(const CRegion& damage);
    virtual bool                                   isSynchronous(); // whether the updates to this buffer are synchronous, aka happen over cpu
    virtual bool                                   good();
    virtual Aquamarine::SDMABUFAttrs               dmabuf();
    virtual Aquamarine::SSHMAttrs                  shm();
    virtual std::tuple<uint8_t*, uint32_t, size_t> beginDataPtr(uint32_t flags);
    virtual void                                   endDataPtr();

    virtual bool                                   getOpaque();
    virtual Hyprutils::Math::Vector2D&             getSize();
    virtual Aquamarine::CAttachmentManager&        getAttachments();
    virtual Hyprutils::Signal::CSignal&            getDestroyEvent();
    virtual const SP<CTexture>&                    getTexture();
    virtual const SP<CWLBufferResource>&           getResource();

    UP<CDRMSyncPointState>                         acquire;
    UP<CDRMSyncPointState>                         release;
    UP<CSyncReleaser>                              syncReleaser;

  private:
    SP<IHLBuffer> buffer;
};
