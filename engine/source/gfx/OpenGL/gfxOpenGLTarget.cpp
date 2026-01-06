#include "gfx/OpenGL/gfxOpenGLTarget.h"
#include "gfx/OpenGL/gfxOpenGLDevice.h"
#include "gfx/OpenGL/gfxOpenGLTextureObject.h"
#include "platformX86UNIX/platformGL.h"
#include "console/console.h"

GFXOpenGLTextureTarget::GFXOpenGLTextureTarget()
 : mDevice(NULL), mFBO(0), mDepthRB(0)
{
    for (S32 i=0;i<MaxRenderSlotId;i++) { mAttached[i]=NULL; mAttachedMip[i]=0; mAttachedZ[i]=0; }
}

GFXOpenGLTextureTarget::~GFXOpenGLTextureTarget()
{
    releaseFBO();
}

const Point2I GFXOpenGLTextureTarget::getSize()
{
    // find first color attachment
    for (S32 i=Color0;i<MaxRenderSlotId;i++)
    {
        if (mAttached[i])
        {
            return Point2I(mAttached[i]->getWidth(), mAttached[i]->getHeight());
        }
    }
    return Point2I(0,0);
}

void GFXOpenGLTextureTarget::ensureFBO()
{
    if (mFBO) return;
    if (!glGenFramebuffers) return; // no FBO support

    glGenFramebuffers(1, &mFBO);
    glGenRenderbuffers(1, &mDepthRB);

    // Setup draw buffers if multiple attachments are present
    // We'll set them later in setActiveRenderTarget when attachments are known
}

// Map engine RenderSlot to GL enum
GLenum GFXOpenGLTextureTarget::slotToGLAttachment(RenderSlot slot, U32 drawIndex)
{
    switch(slot)
    {
        case Color0: return GL_COLOR_ATTACHMENT0 + drawIndex;
        case Color1: return GL_COLOR_ATTACHMENT1 + drawIndex;
        case Color2: return GL_COLOR_ATTACHMENT2 + drawIndex;
        case Color3: return GL_COLOR_ATTACHMENT3 + drawIndex;
        case Color4: return GL_COLOR_ATTACHMENT4 + drawIndex;
        case DepthStencil: return GL_DEPTH_ATTACHMENT;
        default: return GL_NONE;
    }
}

void GFXOpenGLTextureTarget::releaseFBO()
{
    if (mFBO && glDeleteFramebuffers)
    {
        glDeleteFramebuffers(1, &mFBO);
        mFBO = 0;
    }
    if (mDepthRB && glDeleteRenderbuffers)
    {
        glDeleteRenderbuffers(1, &mDepthRB);
        mDepthRB = 0;
    }
}

void GFXOpenGLTextureTarget::attachTexture(RenderSlot slot, GFXTextureObject *tex, U32 mipLevel/*=0*/, U32 zOffset/*=0*/)
{
    AssertFatal(slot < MaxRenderSlotId, "out of range");
    invalidateState();

    // store reference
    mAttached[slot] = tex;
    mAttachedMip[slot] = mipLevel;
    mAttachedZ[slot] = zOffset;

    // mark FBO rebuild required
    releaseFBO();
}

void GFXOpenGLTextureTarget::attachTexture(RenderSlot slot, GFXCubemap *tex, U32 face, U32 mipLevel/*=0*/)
{
    AssertFatal(slot < MaxRenderSlotId, "out of range");
    invalidateState();
    mAttached[slot] = NULL; // primary texture pointer left empty for cubemaps
    mAttachedCube[slot] = tex;
    mAttachedIsCube[slot] = true;
    mAttachedFace[slot] = face;
    mAttachedMip[slot] = mipLevel;
    releaseFBO();
}

void GFXOpenGLTextureTarget::clearAttachments()
{
    for (S32 i=0;i<MaxRenderSlotId;i++)
    {
        mAttached[i] = NULL;
        mAttachedMip[i] = 0;
        mAttachedZ[i] = 0;
    }
    releaseFBO();
}

void GFXOpenGLTextureTarget::deactivate()
{
    // When deactivating, if we had attached textures that expect their contents
    // to be filled, copy from the FBO into those textures.
    if (!glBindFramebuffer || !glBindTexture)
        return;

    // bind our FBO for reading
    if (mFBO && glBindFramebuffer)
        glBindFramebuffer(GL_FRAMEBUFFER, mFBO);

    Point2I sz = getSize();
    if (sz.x == 0 || sz.y == 0)
    {
        if (glBindFramebuffer) glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    // For each color attachment, copy back into the texture
    for (S32 slot = Color0; slot <= Color4; ++slot)
    {
        GFXTextureObject* to = mAttached[slot];
        if (!to)
            continue;

        GLenum att = slotToGLAttachment((RenderSlot)slot, slot - Color0);
        if (glReadBuffer)
            glReadBuffer(att);

        GFXOpenGLTextureObject* glto = dynamic_cast<GFXOpenGLTextureObject*>(to);
        if (!glto) continue;

        // bind texture target (2D)
        glBindTexture(GL_TEXTURE_2D, glto->mTexId);

        if (glCopyTexSubImage2D)
        {
            // copy from currently bound FBO
            glCopyTexSubImage2D(GL_TEXTURE_2D, mAttachedMip[slot], 0, 0, 0, 0, sz.x, sz.y);
        }
        else if (glGetTexImage)
        {
            // fallback: read pixels then upload (use TexSubImage to avoid realloc)
            U32 bufSize = sz.x * sz.y * 4;
            U8* tmp = new U8[bufSize];
            glReadPixels(0, 0, sz.x, sz.y, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
            glTexSubImage2D(GL_TEXTURE_2D, mAttachedMip[slot], 0, 0, sz.x, sz.y, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
            delete [] tmp;
        }

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // handle cubemap attachments
    for (S32 slot = Color0; slot <= Color4; ++slot)
    {
        if (!mAttachedIsCube[slot]) continue;
        GFXCubemap* cube = mAttachedCube[slot];
        if (!cube) continue;

        // determine GL attachment
        GLenum att = slotToGLAttachment((RenderSlot)slot, slot - Color0);
        if (glReadBuffer) glReadBuffer(att);

        // copy into cubemap face
        GFXOpenGLCubemap* oglCube = dynamic_cast<GFXOpenGLCubemap*>(cube);
        if (!oglCube) continue;

        GLuint texId = oglCube->mTexId;
        if (!texId) continue;

        // bind cubemap and copy
        if (glBindTexture)
        {
            glBindTexture(GL_TEXTURE_CUBE_MAP, texId);
            U32 face = mAttachedFace[slot];
            if (glCopyTexSubImage2D)
            {
                glCopyTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mAttachedMip[slot], 0, 0, 0, 0, sz.x, sz.y);
            }
            else
            {
                // fallback: read and upload
                U32 bufSize = sz.x * sz.y * 4;
                U8* tmp = new U8[bufSize];
                glReadPixels(0, 0, sz.x, sz.y, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
                glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mAttachedMip[slot], 0, 0, sz.x, sz.y, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
                delete [] tmp;
            }
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }
    }

    // restore default framebuffer
    if (glBindFramebuffer) glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GFXOpenGLTextureTarget::zombify()
{
    // release GL objects
    releaseFBO();
}

void GFXOpenGLTextureTarget::resurrect()
{
    // Release any previous FBO so ensureFBO() will recreate it when needed
    releaseFBO();
}
