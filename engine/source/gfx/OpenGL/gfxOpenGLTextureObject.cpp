#include "gfx/OpenGL/gfxOpenGLTextureObject.h"
#include "core/stream.h"
#include "core/fileStream.h"
#include "core/frameAllocator.h"
#include "console/console.h"
#include "platformX86UNIX/platformGL.h"

GFXOpenGLTextureObject::GFXOpenGLTextureObject(GFXDevice* dev, GFXTextureProfile* profile)
: GFXTextureObject(dev, profile), mTexId(0), mIsRenderTarget(false)
{
}

GFXOpenGLTextureObject::~GFXOpenGLTextureObject()
{
    kill();
}

void GFXOpenGLTextureObject::kill()
{
    if (mTexId && glDeleteTextures)
    {
        glDeleteTextures(1, &mTexId);
        mTexId = 0;
    }
    mDead = true;
}

void GFXOpenGLTextureObject::zombify()
{
    // delete GL texture until resurrect
    if (mTexId && glDeleteTextures)
    {
        glDeleteTextures(1, &mTexId);
        mTexId = 0;
    }
}

void GFXOpenGLTextureObject::resurrect()
{
    // recreate GL texture if bitmap data exists
    if (!mTexId && glGenTextures)
    {
        glGenTextures(1, &mTexId);
        glBindTexture(GL_TEXTURE_2D, mTexId);
        if (mBitmap)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, getWidth(), getHeight(), 0, GL_RGBA, GL_UNSIGNED_BYTE, mBitmap->getAddress(0,0));
        }
        else if (mIsRenderTarget)
        {
            // create an empty texture for render-to-texture use
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, getWidth(), getHeight(), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

bool GFXOpenGLTextureObject::copyToBmp(GBitmap* bmp)
{
    if (!bmp) return false;
    if (!glBindTexture) return false;

    glBindTexture(GL_TEXTURE_2D, mTexId);
    // assume RGBA8
    U32 w = getWidth();
    U32 h = getHeight();
    bmp->allocate(w, h);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, bmp->getAddress(0,0));
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void GFXOpenGLTextureObject::describeSelf(char* buffer, U32 sizeOfBuffer)
{
    dSprintf(buffer, sizeOfBuffer, "OpenGLTexture id=%u size=%dx%d", mTexId, getWidth(), getHeight());
}
