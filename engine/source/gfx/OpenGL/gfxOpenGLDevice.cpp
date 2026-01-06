#include "gfx/OpenGL/gfxOpenGLDevice.h"
#include "gfx/gfxShader.h"
#include "core/stringTable.h"
#include "core/fileStream.h"
#include "core/util/safeDelete.h"
#include "platformX86UNIX/platformGL.h"
#include "platform/types.h"
#include <stddef.h>

// forward declarations for globals used
extern _StringTable* StringTable;
#include "console/console.h"
#include "gfx/OpenGL/gfxOpenGLTextureObject.h"
#include "gfx/OpenGL/gfxOpenGLTarget.h"
#include "gfx/OpenGL/gfxOpenGLBuffers.h"

// Minimal GLSL shader wrapper: stores filenames and optionally compiles them into a GL program
class GFXGLShader : public GFXShader
{
public:
    StringTableEntry mVertFile;
    StringTableEntry mPixFile;
    GLuint mProgram;

    GFXGLShader(const char* v, const char* p, F32 pv)
    {
        mVertFile = StringTable->insert(v);
        mPixFile = StringTable->insert(p);
        mPixVersion = pv;
        mProgram = 0;
        // attempt to compile/link if GL functions and files are available
        compileAndLink();
    }

    ~GFXGLShader()
    {
        if (mProgram && glDeleteProgram)
            glDeleteProgram(mProgram);
    }

    void compileAndLink()
    {
        // If GL function pointers are not present, bail out (we still keep filenames)
        if (!glCreateShader || !glCreateProgram || !glShaderSource || !glCompileShader || !glAttachShader || !glLinkProgram)
            return;

        // read files
        FileStream vs;
        if (!vs.open(mVertFile, FileStream::Read))
            return;
    U32 vlen = vs.getSize();
    char* vbuf = new char[vlen + 1];
    vs.read(vlen, vbuf);
    vbuf[vlen] = '\0';
    vs.close();

        FileStream ps;
        if (!ps.open(mPixFile, FileStream::Read))
            return;
    U32 plen = ps.getSize();
    char* pbuf = new char[plen + 1];
    ps.read(plen, pbuf);
    pbuf[plen] = '\0';
    ps.close();

        GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
        const char* vsrc = vbuf;
        glShaderSource(vsh, 1, &vsrc, NULL);
        glCompileShader(vsh);

        GLuint psh = glCreateShader(GL_FRAGMENT_SHADER);
        const char* psrc = pbuf;
        glShaderSource(psh, 1, &psrc, NULL);
        glCompileShader(psh);

        // check compile status
        if (glGetShaderiv)
        {
            GLint status = GL_FALSE;
            glGetShaderiv(vsh, GL_COMPILE_STATUS, &status);
            if (status == GL_FALSE && glGetShaderInfoLog)
            {
                GLint len = 0; glGetShaderiv(vsh, GL_INFO_LOG_LENGTH, &len);
                if (len > 0)
                {
                    char* buf = new char[len+1];
                    glGetShaderInfoLog(vsh, len, NULL, buf);
                    Con::errorf("Vertex shader compile error: %s", buf);
                    delete [] buf;
                }
            }
            status = GL_FALSE;
            glGetShaderiv(psh, GL_COMPILE_STATUS, &status);
            if (status == GL_FALSE && glGetShaderInfoLog)
            {
                GLint len = 0; glGetShaderiv(psh, GL_INFO_LOG_LENGTH, &len);
                if (len > 0)
                {
                    char* buf = new char[len+1];
                    glGetShaderInfoLog(psh, len, NULL, buf);
                    Con::errorf("Fragment shader compile error: %s", buf);
                    delete [] buf;
                }
            }
        }

        GLuint prog = glCreateProgram();
        glAttachShader(prog, vsh);
        glAttachShader(prog, psh);
        glLinkProgram(prog);

        if (glGetProgramiv)
        {
            GLint status = GL_FALSE;
            glGetProgramiv(prog, GL_LINK_STATUS, &status);
            if (status == GL_FALSE && glGetProgramInfoLog)
            {
                GLint len = 0; glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
                if (len > 0)
                {
                    char* buf = new char[len+1];
                    glGetProgramInfoLog(prog, len, NULL, buf);
                    Con::errorf("Program link error: %s", buf);
                    delete [] buf;
                }
            }
        }

        mProgram = prog;

    // free buffers
    delete [] vbuf;
    delete [] pbuf;
    }
    GLuint getProgram() const { return mProgram; }
};

// Minimal shader manager: cache program objects by vertex+fragment filenames
class GFXGLShaderMgr
{
public:
    struct Entry { StringTableEntry vert; StringTableEntry frag; GLuint prog;
        // cached attribute locations
        GLint attrPos;
        GLint attrNormal;
        GLint attrColor;
        GLint attrTexCoord[4];
        // cached common uniforms (optional)
        GLint uniModelViewProj;
    };
    Vector<Entry> mEntries;

    GFXGLShaderMgr() {}
    ~GFXGLShaderMgr()
    {
        for (U32 i = 0; i < mEntries.size(); ++i)
            if (mEntries[i].prog && glDeleteProgram) glDeleteProgram(mEntries[i].prog);
    }

    GLuint getProgram(const char* vertFile, const char* fragFile)
    {
        for (U32 i = 0; i < mEntries.size(); ++i)
        {
            if (mEntries[i].vert == StringTable->insert(vertFile) && mEntries[i].frag == StringTable->insert(fragFile))
                return mEntries[i].prog;
        }
        // Not found: compile by creating a temporary GFXGLShader
        GFXGLShader tmp(vertFile, fragFile, 2.0f);
        GLuint prog = tmp.getProgram();
        Entry e;
        e.vert = StringTable->insert(vertFile);
        e.frag = StringTable->insert(fragFile);
        e.prog = prog;
        // initialize caches
        e.attrPos = e.attrNormal = e.attrColor = -1;
        for (int i = 0; i < 4; ++i) e.attrTexCoord[i] = -1;
        e.uniModelViewProj = -1;
        // query attribute/uniform locations if GL functions available
        if (prog && glGetAttribLocation)
        {
            e.attrPos = glGetAttribLocation(prog, "aPosition");
            e.attrNormal = glGetAttribLocation(prog, "aNormal");
            e.attrColor = glGetAttribLocation(prog, "aColor");
            for (int i = 0; i < 4; ++i)
            {
                char buf[16];
                dSprintf(buf, sizeof(buf), "aTexCoord%d", i);
                e.attrTexCoord[i] = glGetAttribLocation(prog, buf);
            }
            if (glGetUniformLocation)
                e.uniModelViewProj = glGetUniformLocation(prog, "uModelViewProj");
        }
        mEntries.push_back(e);
        return prog;
    }
};

static GFXGLShaderMgr sGLShaderMgr;

GFXOpenGLDevice::GFXOpenGLDevice()
{
    mPixVersion = 2.0f; // default GLSL-level assumption
}

GFXOpenGLDevice::~GFXOpenGLDevice()
{
}

GFXDevice *GFXOpenGLDevice::createInstance( U32 adapterIndex )
{
    return new GFXOpenGLDevice();
}

GFXShader *GFXOpenGLDevice::createShader( const char *vertFile, const char *pixFile, F32 pixVersion )
{
    return new GFXGLShader(vertFile, pixFile, pixVersion);
}

GFXCubemap* GFXOpenGLDevice::createCubemap()
{
    GFXOpenGLCubemap* cube = new GFXOpenGLCubemap();
    cube->registerResourceWithDevice(this);
    return cube;
}

void GFXOpenGLDevice::setShader(GFXShader *shader)
{
    if (!shader)
    {
        if (glUseProgram) glUseProgram(0);
        return;
    }

    GFXGLShader* gls = dynamic_cast<GFXGLShader*>(shader);
    if (gls && glUseProgram)
    {
        GLuint prog = gls->getProgram();
        if (!prog)
        {
            // Ask manager to compile/cache program by filenames
            prog = sGLShaderMgr.getProgram(gls->mVertFile, gls->mPixFile);
        }
        if (prog)
            glUseProgram(prog);
    }
}

GFXTextureObject* GFXOpenGLDevice::createRenderSurface( U32 width, U32 height, GFXFormat format, U32 mipLevel )
{
    // Minimal: create a GL texture and return wrapper
    GFXTextureProfile* prof = NULL; // engine creates profiles elsewhere; nullptr acceptable for now
    GFXOpenGLTextureObject* tex = new GFXOpenGLTextureObject(this, prof);
    tex->mTextureSize.set(width, height, 1);
    tex->mBitmapSize.set(width, height, 1);
    tex->mFormat = format;
    tex->mIsRenderTarget = true;

    // create GL texture
    if (glGenTextures)
    {
        glGenTextures(1, &tex->mTexId);
        glBindTexture(GL_TEXTURE_2D, tex->mTexId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    return tex;
}

GFXTextureTarget *GFXOpenGLDevice::allocRenderToTextureTarget()
{
    GFXOpenGLTextureTarget *t = new GFXOpenGLTextureTarget();
    t->mDevice = this;
    t->registerResourceWithDevice(this);
    return t;
}

void GFXOpenGLDevice::resurrectTextureManager()
{
    if (mTextureManager)
        mTextureManager->resurrect();

    // Walk resource list and resurrect each resource
    GFXResource* walk = mResourceListHead;
    while (walk)
    {
        walk->resurrect();
        walk = walk->getNextResource();
    }
}

void GFXOpenGLDevice::zombifyTextureManager()
{
    if (mTextureManager)
        mTextureManager->zombify();

    // Walk resource list and zombify each resource
    GFXResource* walk = mResourceListHead;
    while (walk)
    {
        walk->zombify();
        walk = walk->getNextResource();
    }
}

void GFXOpenGLDevice::setActiveRenderTarget(GFXTarget *target)
{
    // If null, bind the default framebuffer (0)
    if (!target)
    {
        if (glBindFramebuffer) glBindFramebuffer(GL_FRAMEBUFFER, 0);
        mCurrentRT = NULL;
        return;
    }

    // If it's a texture target, ensure FBO and attach textures
    GFXOpenGLTextureTarget* tt = dynamic_cast<GFXOpenGLTextureTarget*>(target);
    if (!tt)
    {
        // Not a texture target; bind default
        if (glBindFramebuffer) glBindFramebuffer(GL_FRAMEBUFFER, 0);
        mCurrentRT = target;
        return;
    }

    // Build or rebuild FBO
    tt->ensureFBO();
    if (!tt->mFBO || !glBindFramebuffer) { mCurrentRT = target; return; }

    glBindFramebuffer(GL_FRAMEBUFFER, tt->mFBO);

    // Attach all color attachments (Color0..Color4) and build draw buffer list
    GLenum drawBuffers[5];
    int dbCount = 0;
    for (int slot = GFXTextureTarget::Color0; slot <= GFXTextureTarget::Color4; ++slot)
    {
        GFXTextureObject* attached = tt->mAttached[slot];
        GLenum attEnum = GL_COLOR_ATTACHMENT0 + (slot - GFXTextureTarget::Color0);

        if (attached)
        {
            GFXOpenGLTextureObject* to = dynamic_cast<GFXOpenGLTextureObject*>(attached);
            if (to && glFramebufferTexture2D)
            {
                glFramebufferTexture2D(GL_FRAMEBUFFER, attEnum, GL_TEXTURE_2D, to->mTexId, tt->mAttachedMip[slot]);
                // add to draw buffers list
                drawBuffers[dbCount++] = attEnum;
            }
            else
            {
                // cannot attach: detach
                if (glFramebufferTexture2D)
                    glFramebufferTexture2D(GL_FRAMEBUFFER, attEnum, GL_TEXTURE_2D, 0, 0);
            }
        }
        else
        {
            // ensure the corresponding GL color attachment is detached
            if (glFramebufferTexture2D)
                glFramebufferTexture2D(GL_FRAMEBUFFER, attEnum, GL_TEXTURE_2D, 0, 0);
        }
    }

    // If multiple draw buffers supported, enable them. Otherwise fall back to single render target semantics
    if (glDrawBuffers && dbCount > 0)
    {
        glDrawBuffers(dbCount, drawBuffers);
    }
    else
    {
        // Fallback: nothing to do; ensure at least color0 is attached if present
        if (tt->mAttached[GFXTextureTarget::Color0])
        {
            GFXOpenGLTextureObject* to = dynamic_cast<GFXOpenGLTextureObject*>(tt->mAttached[GFXTextureTarget::Color0]);
            if (to && glFramebufferTexture2D)
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, to->mTexId, tt->mAttachedMip[GFXTextureTarget::Color0]);
        }
    }

    // attach depth if present
    if (tt->mAttached[GFXTextureTarget::DepthStencil])
    {
        // attach renderbuffer as depth
        if (tt->mDepthRB && glBindRenderbuffer && glRenderbufferStorage && glFramebufferRenderbuffer)
        {
            glBindRenderbuffer(GL_RENDERBUFFER, tt->mDepthRB);
            // assume size matches attached color
            Point2I s = tt->getSize();
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, s.x, s.y);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, tt->mDepthRB);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        }
    }
    else
    {
        if (glFramebufferRenderbuffer) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
    }

    // check FBO status
    if (glCheckFramebufferStatus)
    {
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (st != GL_FRAMEBUFFER_COMPLETE)
        {
            Con::errorf("OpenGL FBO incomplete: 0x%x", st);
        }
    }

    mCurrentRT = target;
}

// Map engine primitive types to GL primitives
static const GLenum GFXGLPrimType[] = {
    GL_POINTS,         // GFXPointList
    GL_LINES,          // GFXLineList
    GL_LINE_STRIP,     // GFXLineStrip
    GL_TRIANGLES,      // GFXTriangleList
    GL_TRIANGLE_STRIP, // GFXTriangleStrip
    GL_TRIANGLE_FAN    // GFXTriangleFan
};

void GFXOpenGLDevice::drawPrimitive( GFXPrimitiveType primType, U32 vertexStart, U32 primitiveCount )
{
    // Ensure device state is up to date
    if (mStateDirty)
        updateStates();

    // Ensure no open locks
    AssertFatal(mCurrentOpenAllocVB == NULL, "Calling drawPrimitive() when a vertex buffer is still open for editing");
    AssertFatal(mCurrentVertexBuffer.isValid(), "Trying to call draw primitive with no current vertex buffer, call setVertexBuffer()");

    // Prepare vertex buffer (bind VBO)
    if (mCurrentVertexBuffer.isValid())
        mCurrentVertexBuffer->prepare();

    // Setup vertex attribute pointers for fixed-function pipeline
    // We'll set pointers based on current vertex format
    if (mCurrentVertexBuffer.isValid())
    {
        // determine if a GLSL program is active and has cached attribute locations
        GLint currentProg = 0;
        if (glGetIntegerv && glGetFloatv) // rough check for GL availability
        {
            if (glGetIntegerv) glGetIntegerv(GL_CURRENT_PROGRAM, &currentProg);
        }
        GFXGLShaderMgr::Entry* progEntry = NULL;
        if (currentProg)
        {
            for (U32 i = 0; i < sGLShaderMgr.mEntries.size(); ++i)
            {
                if (sGLShaderMgr.mEntries[i].prog == (GLuint)currentProg)
                {
                    progEntry = &sGLShaderMgr.mEntries[i];
                    break;
                }
            }
        }

        U32 vertFlags = mCurrentVertexBuffer->mVertexType;
        U32 stride = mCurrentVertexBuffer->mVertexSize;
        // position
        if (progEntry && progEntry->attrPos >= 0 && glVertexAttribPointer)
        {
            GLint posSize = (vertFlags & GFXVertexFlagXYZW) ? 4 : 3;
            glEnableVertexAttribArray((GLuint)progEntry->attrPos);
            glVertexAttribPointer((GLuint)progEntry->attrPos, posSize, GL_FLOAT, GL_FALSE, stride, (const void*)( (uintptr_t)(vertexStart * stride) + 0 ));
        }
        else if (glEnableClientState && glVertexPointer)
        {
            glEnableClientState(GL_VERTEX_ARRAY);
            GLint posSize = (vertFlags & GFXVertexFlagXYZW) ? 4 : 3;
            const void* posPtr = (const void*)( (uintptr_t)(vertexStart * stride) + 0 );
            glVertexPointer(posSize, GL_FLOAT, stride, posPtr);
        }

        // compute offsets by walking fields in expected order: position, normal, color, specular, texcoords
        U32 offset = ( (vertFlags & GFXVertexFlagXYZW) ? 4 : 3 ) * sizeof(float);

        if (vertFlags & GFXVertexFlagNormal)
        {
            if (progEntry && progEntry->attrNormal >= 0 && glVertexAttribPointer)
            {
                glEnableVertexAttribArray((GLuint)progEntry->attrNormal);
                glVertexAttribPointer((GLuint)progEntry->attrNormal, 3, GL_FLOAT, GL_FALSE, stride, (const void*)( (uintptr_t)(vertexStart * stride) + offset ));
            }
            else if (glEnableClientState && glNormalPointer)
            {
                glEnableClientState(GL_NORMAL_ARRAY);
                glNormalPointer(GL_FLOAT, stride, (const void*)( (uintptr_t)(vertexStart * stride) + offset ));
            }
            offset += 3 * sizeof(float);
        }

        if (vertFlags & GFXVertexFlagDiffuse)
        {
            if (progEntry && progEntry->attrColor >= 0 && glVertexAttribPointer)
            {
                glEnableVertexAttribArray((GLuint)progEntry->attrColor);
                glVertexAttribPointer((GLuint)progEntry->attrColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void*)( (uintptr_t)(vertexStart * stride) + offset ));
            }
            else if (glEnableClientState && glColorPointer)
            {
                glEnableClientState(GL_COLOR_ARRAY);
                // color stored as 4 bytes
                glColorPointer(4, GL_UNSIGNED_BYTE, stride, (const void*)( (uintptr_t)(vertexStart * stride) + offset ));
            }
            offset += sizeof(U32);
        }

        if (vertFlags & GFXVertexFlagSpecular)
        {
            // specular after diffuse if present
            offset += sizeof(U32);
        }

        // Texture coords: get count from bits 8-11
        U32 texCount = (vertFlags >> 8) & 0xF;
        for (U32 t = 0; t < texCount; ++t)
        {
            // decode dimension from bits at (t*2 + 16)
            U32 dimCode = (vertFlags >> (t*2 + 16)) & 0x3;
            U32 dim = 2; // default
            if (dimCode == 0) dim = 2;
            else if (dimCode == 1) dim = 3;
            else if (dimCode == 2) dim = 4;
            else if (dimCode == 3) dim = 1; // U

            if (progEntry && progEntry->attrTexCoord[t] >= 0 && glVertexAttribPointer)
            {
                glEnableVertexAttribArray((GLuint)progEntry->attrTexCoord[t]);
                glVertexAttribPointer((GLuint)progEntry->attrTexCoord[t], (GLint)dim, GL_FLOAT, GL_FALSE, stride, (const void*)( (uintptr_t)(vertexStart * stride) + offset ));
            }
            else if (glClientActiveTexture && glEnableClientState && glTexCoordPointer)
            {
                glClientActiveTexture(GL_TEXTURE0 + t);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer((GLint)dim, GL_FLOAT, stride, (const void*)( (uintptr_t)(vertexStart * stride) + offset ));
            }
            offset += dim * sizeof(float);
        }
    }

    // Translate primitive count to vertex count for glDrawArrays where needed
    GLenum glPrim = GFXGLPrimType[primType];

    // Calculate the number of vertices for the primitiveCount based on primitive type
    GLsizei vertexCount = 0;
    switch (primType)
    {
    case GFXPointList:
        vertexCount = primitiveCount;
        break;
    case GFXLineList:
        vertexCount = primitiveCount * 2;
        break;
    case GFXLineStrip:
        vertexCount = primitiveCount + 1;
        break;
    case GFXTriangleList:
        vertexCount = primitiveCount * 3;
        break;
    case GFXTriangleStrip:
        vertexCount = primitiveCount + 2;
        break;
    case GFXTriangleFan:
        vertexCount = primitiveCount + 2;
        break;
    default:
        vertexCount = primitiveCount;
        break;
    }

    // Issue draw call
    if (glDrawArrays)
        glDrawArrays(glPrim, (GLint)vertexStart, vertexCount);

    // cleanup: disable texture coord arrays
    if (glClientActiveTexture && glDisableClientState)
    {
        // disable all texture coord arrays we may have enabled
        if (mCurrentVertexBuffer.isValid())
        {
            U32 vertFlags = mCurrentVertexBuffer->mVertexType;
            U32 texCount = (vertFlags >> 8) & 0xF;
            for (U32 t = 0; t < texCount; ++t)
            {
                // if program attributes were used, disable them; otherwise disable client state
                GLint currentProg = 0;
                if (glGetIntegerv && glGetIntegerv) glGetIntegerv(GL_CURRENT_PROGRAM, &currentProg);
                GFXGLShaderMgr::Entry* progEntry = NULL;
                if (currentProg)
                {
                    for (U32 i = 0; i < sGLShaderMgr.mEntries.size(); ++i)
                    {
                        if (sGLShaderMgr.mEntries[i].prog == (GLuint)currentProg)
                        {
                            progEntry = &sGLShaderMgr.mEntries[i];
                            break;
                        }
                    }
                }
                if (progEntry && progEntry->attrTexCoord[t] >= 0 && glDisableVertexAttribArray)
                {
                    glDisableVertexAttribArray((GLuint)progEntry->attrTexCoord[t]);
                }
                else
                {
                    glClientActiveTexture(GL_TEXTURE0 + t);
                    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                }
            }
        }
    }

    // Also disable generic attribute arrays we may have enabled (pos, normal, color)
    {
        GLint currentProg = 0;
        if (glGetIntegerv && glGetIntegerv) glGetIntegerv(GL_CURRENT_PROGRAM, &currentProg);
        GFXGLShaderMgr::Entry* progEntry = NULL;
        if (currentProg)
        {
            for (U32 i = 0; i < sGLShaderMgr.mEntries.size(); ++i)
            {
                if (sGLShaderMgr.mEntries[i].prog == (GLuint)currentProg)
                {
                    progEntry = &sGLShaderMgr.mEntries[i];
                    break;
                }
            }
        }
        if (progEntry)
        {
            if (progEntry->attrPos >= 0 && glDisableVertexAttribArray) glDisableVertexAttribArray((GLuint)progEntry->attrPos);
            if (progEntry->attrNormal >= 0 && glDisableVertexAttribArray) glDisableVertexAttribArray((GLuint)progEntry->attrNormal);
            if (progEntry->attrColor >= 0 && glDisableVertexAttribArray) glDisableVertexAttribArray((GLuint)progEntry->attrColor);
        }
        else
        {
            if (glDisableClientState && glDisableClientState) glDisableClientState(GL_VERTEX_ARRAY);
            if (glDisableClientState && glDisableClientState) glDisableClientState(GL_NORMAL_ARRAY);
            if (glDisableClientState && glDisableClientState) glDisableClientState(GL_COLOR_ARRAY);
        }
    }
}

void GFXOpenGLDevice::drawIndexedPrimitive( GFXPrimitiveType primType, U32 minIndex, U32 numVerts, U32 startIndex, U32 primitiveCount )
{
    if (mStateDirty)
        updateStates();

    AssertFatal(mCurrentOpenAllocVB == NULL, "Calling drawIndexedPrimitive() when a vertex buffer is still open for editing");
    AssertFatal(mCurrentVertexBuffer.isValid(), "Trying to call drawIndexedPrimitive with no current vertex buffer, call setVertexBuffer()");
    AssertFatal(mCurrentOpenAllocPB == NULL, "Calling drawIndexedPrimitive() when a index buffer is still open for editing");
    AssertFatal(mCurrentPrimitiveBuffer.isValid(), "Trying to call drawIndexedPrimitive with no current primitive buffer, call setPrimitiveBuffer()");

    // Bind VBO and IBO
    if (mCurrentVertexBuffer.isValid())
        mCurrentVertexBuffer->prepare();
    if (mCurrentPrimitiveBuffer.isValid())
        mCurrentPrimitiveBuffer->prepare();

    GLenum glPrim = GFXGLPrimType[primType];

    // Determine index count (number of indices to draw)
    GLsizei indexCount = 0;
    switch (primType)
    {
    case GFXPointList:
        indexCount = primitiveCount;
        break;
    case GFXLineList:
        indexCount = primitiveCount * 2;
        break;
    case GFXLineStrip:
        indexCount = primitiveCount + 1;
        break;
    case GFXTriangleList:
        indexCount = primitiveCount * 3;
        break;
    case GFXTriangleStrip:
        indexCount = primitiveCount + 2;
        break;
    case GFXTriangleFan:
        indexCount = primitiveCount + 2;
        break;
    default:
        indexCount = primitiveCount;
        break;
    }

    // Setup vertex attribute pointers (same as non-indexed draw)
    if (mCurrentVertexBuffer.isValid())
    {
        U32 vertFlags = mCurrentVertexBuffer->mVertexType;
        U32 stride = mCurrentVertexBuffer->mVertexSize;
        if (glEnableClientState && glVertexPointer)
        {
            glEnableClientState(GL_VERTEX_ARRAY);
            GLint posSize = (vertFlags & GFXVertexFlagXYZW) ? 4 : 3;
            glVertexPointer(posSize, GL_FLOAT, stride, (const void*)0);
        }

        U32 offset = ( (vertFlags & GFXVertexFlagXYZW) ? 4 : 3 ) * sizeof(float);
        if (vertFlags & GFXVertexFlagNormal)
        {
            if (glEnableClientState && glNormalPointer)
            {
                glEnableClientState(GL_NORMAL_ARRAY);
                glNormalPointer(GL_FLOAT, stride, (const void*)offset);
            }
            offset += 3 * sizeof(float);
        }

        if (vertFlags & GFXVertexFlagDiffuse)
        {
            if (glEnableClientState && glColorPointer)
            {
                glEnableClientState(GL_COLOR_ARRAY);
                glColorPointer(4, GL_UNSIGNED_BYTE, stride, (const void*)offset);
            }
            offset += sizeof(U32);
        }

        if (vertFlags & GFXVertexFlagSpecular)
            offset += sizeof(U32);

        U32 texCount = (vertFlags >> 8) & 0xF;
        for (U32 t = 0; t < texCount; ++t)
        {
            U32 dimCode = (vertFlags >> (t*2 + 16)) & 0x3;
            U32 dim = 2;
            if (dimCode == 0) dim = 2;
            else if (dimCode == 1) dim = 3;
            else if (dimCode == 2) dim = 4;
            else if (dimCode == 3) dim = 1;

            if (glClientActiveTexture && glEnableClientState && glTexCoordPointer)
            {
                glClientActiveTexture(GL_TEXTURE0 + t);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer((GLint)dim, GL_FLOAT, stride, (const void*)offset);
            }
            offset += dim * sizeof(float);
        }
    }

    // glDrawElements expects the offset into the bound IBO in bytes
    const void* iboOffset = (const void*)(startIndex * sizeof(U16));

    if (glDrawElements)
        glDrawElements(glPrim, indexCount, GL_UNSIGNED_SHORT, iboOffset);

    // cleanup texture coord arrays
    if (glClientActiveTexture && glDisableClientState)
    {
        if (mCurrentVertexBuffer.isValid())
        {
            U32 vertFlags = mCurrentVertexBuffer->mVertexType;
            U32 texCount = (vertFlags >> 8) & 0xF;
            for (U32 t = 0; t < texCount; ++t)
            {
                glClientActiveTexture(GL_TEXTURE0 + t);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            }
        }
    }
}

// Vertex / Primitive buffer allocation for OpenGL
GFXVertexBuffer* GFXOpenGLDevice::allocVertexBuffer(U32 numVerts, U32 vertFlags, U32 vertSize, GFXBufferType bufferType)
{
    return new GFXOpenGLVertexBuffer(this, numVerts, vertFlags, vertSize, bufferType);
}

GFXPrimitiveBuffer* GFXOpenGLDevice::allocPrimitiveBuffer(U32 numIndices, U32 numPrimitives, GFXBufferType bufferType)
{
    return new GFXOpenGLPrimitiveBuffer(this, numIndices, numPrimitives, bufferType);
}

// -- GFXOpenGLVertexBuffer methods --
void GFXOpenGLVertexBuffer::lock(U32 vertexStart, U32 vertexEnd, void** vertexPtr)
{
    U32 count = vertexEnd - vertexStart;
    U32 size = count * mVertexSize;
    mStaging = new U8[size];
    *vertexPtr = mStaging;
    lockedVertexStart = vertexStart;
    lockedVertexEnd = vertexEnd;
}

void GFXOpenGLVertexBuffer::unlock()
{
    // upload to VBO if available
    if (glGenBuffers && mStaging)
    {
        if (!mVBO)
            glGenBuffers(1, &mVBO);
        glBindBuffer(GL_ARRAY_BUFFER, mVBO);
        GLenum usage = (mGFXBufferType == GFXBufferTypeImmutable) ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW;
        // allocate full buffer (if not already allocated) then update subrange
        glBufferData(GL_ARRAY_BUFFER, mNumVerts * mVertexSize, NULL, usage);
        glBufferSubData(GL_ARRAY_BUFFER, lockedVertexStart * mVertexSize, (lockedVertexEnd - lockedVertexStart) * mVertexSize, mStaging);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    // For immutable/static buffers keep a CPU-side backup for resurrect
    if (mGFXBufferType == GFXBufferTypeImmutable && mStaging)
    {
        if (!mCPUBackup)
            mCPUBackup = new U8[mNumVerts * mVertexSize];
        memcpy(mCPUBackup + lockedVertexStart * mVertexSize, mStaging, (lockedVertexEnd - lockedVertexStart) * mVertexSize);
    }

    delete [] mStaging;
    mStaging = NULL;
}

void GFXOpenGLVertexBuffer::prepare()
{
    if (mVBO && glBindBuffer)
        glBindBuffer(GL_ARRAY_BUFFER, mVBO);
}

void GFXOpenGLVertexBuffer::zombify()
{
    if (mVBO && glDeleteBuffers)
    {
        glDeleteBuffers(1, &mVBO);
        mVBO = 0;
    }
}

void GFXOpenGLVertexBuffer::resurrect()
{
    // Recreate GL VBO handle if needed. Data reupload is the client's responsibility.
    if (!mVBO && glGenBuffers)
    {
        glGenBuffers(1, &mVBO);
        // allocate empty buffer of expected size so size queries succeed
        glBindBuffer(GL_ARRAY_BUFFER, mVBO);
        GLenum usage = (mGFXBufferType == GFXBufferTypeImmutable) ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW;
        if (mCPUBackup)
            glBufferData(GL_ARRAY_BUFFER, mNumVerts * mVertexSize, mCPUBackup, usage);
        else
            glBufferData(GL_ARRAY_BUFFER, mNumVerts * mVertexSize, NULL, usage);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

// -- GFXOpenGLPrimitiveBuffer methods --
void GFXOpenGLPrimitiveBuffer::lock(U16 indexStart, U16 indexEnd, U16** indexPtr)
{
    U32 count = indexEnd - indexStart;
    mStaging = new U16[count];
    *indexPtr = mStaging;
}

void GFXOpenGLPrimitiveBuffer::unlock()
{
    if (glGenBuffers && mStaging)
    {
        if (!mIBO)
            glGenBuffers(1, &mIBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIBO);
        GLenum usage = (mGFXBufferType == GFXBufferTypeImmutable) ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW;
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mNumIndices * sizeof(U16), NULL, usage);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, mNumIndices * sizeof(U16), mStaging);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    // maintain CPU backup for immutable index buffers
    if (mGFXBufferType == GFXBufferTypeImmutable && mStaging)
    {
        if (!mCPUBackup)
            mCPUBackup = new U16[mNumIndices];
        memcpy(mCPUBackup, mStaging, mNumIndices * sizeof(U16));
    }

    delete [] mStaging;
    mStaging = NULL;
}

void GFXOpenGLPrimitiveBuffer::prepare()
{
    if (mIBO && glBindBuffer)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIBO);
}

void GFXOpenGLPrimitiveBuffer::zombify()
{
    if (mIBO && glDeleteBuffers)
    {
        glDeleteBuffers(1, &mIBO);
        mIBO = 0;
    }
}

void GFXOpenGLPrimitiveBuffer::resurrect()
{
    if (!mIBO && glGenBuffers)
    {
        glGenBuffers(1, &mIBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIBO);
        GLenum usage = (mGFXBufferType == GFXBufferTypeImmutable) ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW;
        if (mCPUBackup)
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, mNumIndices * sizeof(U16), mCPUBackup, usage);
        else
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, mNumIndices * sizeof(U16), NULL, usage);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
}
