// Minimal OpenGL cubemap wrapper
#ifndef _GFXOPENGLCUBEMAP_H_
#define _GFXOPENGLCUBEMAP_H_

#include "gfx/gfxCubemap.h"
#include "platformX86UNIX/platformGL.h"

class GFXOpenGLCubemap : public GFXCubemap
{
   friend class GFXOpenGLDevice;
private:
   virtual void setToTexUnit(U32 tuNum) override;
public:
   GFXOpenGLCubemap();
   virtual ~GFXOpenGLCubemap();

   virtual void initStatic(GFXTexHandle* faces) override;
   virtual void initDynamic(U32 texSize) override;
   virtual void updateDynamic(const Point3F& pos) override;

   virtual void zombify() override;
   virtual void resurrect() override;

protected:
   GLuint mTexId;
   U32 mSize;
};

#endif
