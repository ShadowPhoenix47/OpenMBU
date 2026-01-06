// Minimal OpenGL GFX backend - lightweight wrapper over Null device
#ifndef _GFXOPENGLDEVICE_H_
#define _GFXOPENGLDEVICE_H_

#include "gfx/Null/gfxNullDevice.h"
#include "gfx/gfxShader.h"

class GFXOpenGLDevice : public GFXNullDevice
{
   typedef GFXNullDevice Parent;
public:
   GFXOpenGLDevice();
   virtual ~GFXOpenGLDevice();

   static GFXDevice *createInstance( U32 adapterIndex );

   virtual GFXAdapterType getAdapterType() override { return OpenGL; }

   virtual F32 getPixelShaderVersion() const override { return mPixVersion; }
   virtual void setPixelShaderVersion( F32 version ) override { mPixVersion = version; }

   virtual GFXShader * createShader( const char *vertFile, const char *pixFile, F32 pixVersion ) override;
   virtual GFXShader * createShader( GFXShaderFeatureData &featureData, GFXVertexFlags vertFlags ) override { return NULL; }
   virtual void setShader(GFXShader *shader) override;

   virtual GFXTextureTarget *allocRenderToTextureTarget() override;
   virtual void setActiveRenderTarget(GFXTarget *target) override;
   virtual GFXCubemap* createCubemap() override;

protected:
   F32 mPixVersion;
};

#endif // _GFXOPENGLDEVICE_H_
