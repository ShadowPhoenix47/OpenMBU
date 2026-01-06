// Minimal OpenGL texture object
#ifndef _GFXOPENGLTEXTUREOBJECT_H_
#define _GFXOPENGLTEXTUREOBJECT_H_

#include "gfx/gfxTextureObject.h"

class GFXOpenGLTextureObject : public GFXTextureObject
{
public:
    GLuint mTexId;
    bool mIsRenderTarget;

    GFXOpenGLTextureObject(GFXDevice* dev, GFXTextureProfile* profile);
    virtual ~GFXOpenGLTextureObject();

    virtual GFXLockedRect* lock(U32 mipLevel = 0, RectI* inRect = NULL) override { return NULL; }
    virtual void unlock(U32 mipLevel = 0) override {}
    virtual bool copyToBmp(GBitmap* bmp) override;
    virtual void kill() override;
    virtual void describeSelf(char* buffer, U32 sizeOfBuffer) override;
};

#endif
