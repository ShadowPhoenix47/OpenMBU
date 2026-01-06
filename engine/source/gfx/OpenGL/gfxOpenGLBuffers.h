#ifndef _GFXOPENGLBUFFERS_H_
#define _GFXOPENGLBUFFERS_H_

#include "gfx/gfxVertexBuffer.h"
#include "gfx/gfxPrimitiveBuffer.h"
#include "platformX86UNIX/platformGL.h"

class GFXOpenGLVertexBuffer : public GFXVertexBuffer
{
    GLuint mVBO;
    U8* mStaging;
    U8* mCPUBackup; // backup of buffer data for static buffers (auto-reupload on resurrect)
public:
    GFXOpenGLVertexBuffer(GFXDevice* dev, U32 numVerts, U32 vertexType, U32 vertexSize, GFXBufferType bufferType)
        : GFXVertexBuffer(dev, numVerts, vertexType, vertexSize, bufferType), mVBO(0), mStaging(NULL), mCPUBackup(NULL) {}
    ~GFXOpenGLVertexBuffer() { if (mCPUBackup) delete [] mCPUBackup; }
    virtual void lock(U32 vertexStart, U32 vertexEnd, void** vertexPtr) override;
    virtual void unlock() override;
    virtual void prepare() override;
    virtual void zombify() override;
    virtual void resurrect() override;
};

class GFXOpenGLPrimitiveBuffer : public GFXPrimitiveBuffer
{
    GLuint mIBO;
    U16* mStaging;
    U16* mCPUBackup; // backup index data for static primitive buffers
public:
    GFXOpenGLPrimitiveBuffer(GFXDevice* dev, U32 indexCount, U32 primitiveCount, GFXBufferType bufferType)
        : GFXPrimitiveBuffer(dev, indexCount, primitiveCount, bufferType), mIBO(0), mStaging(NULL), mCPUBackup(NULL) {}
    ~GFXOpenGLPrimitiveBuffer() { if (mCPUBackup) delete [] mCPUBackup; }
    virtual void lock(U16 indexStart, U16 indexEnd, U16** indexPtr) override;
    virtual void unlock() override;
    virtual void prepare() override;
    virtual void zombify() override;
    virtual void resurrect() override;
};

#endif
