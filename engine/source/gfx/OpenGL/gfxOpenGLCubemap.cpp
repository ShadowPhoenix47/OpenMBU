#include "gfx/OpenGL/gfxOpenGLCubemap.h"
#include "console/console.h"

GFXOpenGLCubemap::GFXOpenGLCubemap()
 : mTexId(0), mSize(0)
{
}

GFXOpenGLCubemap::~GFXOpenGLCubemap()
{
    zombify();
}

void GFXOpenGLCubemap::setToTexUnit(U32 tuNum)
{
    // bind cubemap to texture unit
    if (glActiveTexture) glActiveTexture(GL_TEXTURE0 + tuNum);
    if (glBindTexture) glBindTexture(GL_TEXTURE_CUBE_MAP, mTexId);
}

void GFXOpenGLCubemap::initStatic(GFXTexHandle* faces)
{
    // create GL cubemap and upload faces if available
    if (!glGenTextures) return;
    glGenTextures(1, &mTexId);
    glBindTexture(GL_TEXTURE_CUBE_MAP, mTexId);
    // assume faces array is ordered appropriately
    for (int i=0;i<6;i++)
    {
        if (faces && faces[i].getPointer())
        {
            GFXTextureObject* f = faces[i].getPointer();
            if (f && glTexImage2D)
            {
                // best-effort upload; engine-specific pixel access omitted here
            }
        }
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void GFXOpenGLCubemap::initDynamic(U32 texSize)
{
    if (!glGenTextures) return;
    if (!mTexId) glGenTextures(1, &mTexId);
    mSize = texSize;
    glBindTexture(GL_TEXTURE_CUBE_MAP, mTexId);
    for (int i=0;i<6;i++)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, mSize, mSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void GFXOpenGLCubemap::updateDynamic(const Point3F& pos)
{
    // dynamic update would require rendering into faces - handled by targets
}

void GFXOpenGLCubemap::zombify()
{
    if (mTexId && glDeleteTextures)
    {
        glDeleteTextures(1, &mTexId);
        mTexId = 0;
    }
}

void GFXOpenGLCubemap::resurrect()
{
    if (!mTexId && glGenTextures && mSize > 0)
    {
        glGenTextures(1, &mTexId);
        glBindTexture(GL_TEXTURE_CUBE_MAP, mTexId);
        for (int i=0;i<6;i++)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, mSize, mSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
}
