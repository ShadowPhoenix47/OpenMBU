// Minimal OpenGL texture target (FBO-backed)
#ifndef _GFXOPENGLTEXTURETARGET_H_
#define _GFXOPENGLTEXTURETARGET_H_

#include "gfx/gfxTarget.h"
#include "platformX86UNIX/platformGL.h"

class GFXOpenGLDevice;
class GFXTextureObject;

class GFXOpenGLTextureTarget : public GFXTextureTarget
{
   friend class GFXOpenGLDevice;

public:
   GFXOpenGLTextureTarget();
   virtual ~GFXOpenGLTextureTarget();

   // GFXTarget
   virtual const Point2I getSize() override;
   virtual void attachTexture(RenderSlot slot, GFXTextureObject *tex, U32 mipLevel=0, U32 zOffset = 0) override;
   virtual void attachTexture(RenderSlot slot, GFXCubemap *tex, U32 face, U32 mipLevel=0) override;
   virtual void clearAttachments() override;
   virtual void deactivate() override;

   void zombify();
   void resurrect();

protected:
   // helper to ensure FBO exists and attachments are up-to-date
   void ensureFBO();
   void releaseFBO();

   // map RenderSlot to GL color attachment enum
   static GLenum slotToGLAttachment(RenderSlot slot, U32 drawIndex = 0);

   // owning device
   GFXOpenGLDevice* mDevice;

   // GL objects
   GLuint mFBO;
   GLuint mDepthRB;

   // attached textures (raw pointers - the engine manages lifetime elsewhere)
   GFXTextureObject* mAttached[MaxRenderSlotId];
   GFXCubemap*      mAttachedCube[MaxRenderSlotId];
   bool             mAttachedIsCube[MaxRenderSlotId];
   U32              mAttachedFace[MaxRenderSlotId];
   U32              mAttachedMip[MaxRenderSlotId];
   U32              mAttachedZ[MaxRenderSlotId];
};

#endif
