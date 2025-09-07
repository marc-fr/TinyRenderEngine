#include "tre_rendertarget.h"

#ifdef TRE_PRINTS
#include <iostream>
#endif

namespace tre {

// ============================================================================
// Helpers ...

static int kNativeDepthSize = 0;
static int kMaxMultiSampling = 0;

// ----------------------------------------------------------------------------

static void createColorAttachment(int w, int h, bool isHDR, bool isMultisampled, bool isSamplable,
                                  GLenum attachment,
                                  GLuint &outTextureHandle, GLuint &outBufferHandle)
{
  TRE_ASSERT(w != 0 && h != 0);
  TRE_ASSERT(!isMultisampled || !isSamplable); // not implemented (possibe with GL_TEXTURE_2D_MULTISAMPLED)
  TRE_ASSERT(outTextureHandle == 0 && outBufferHandle == 0);

  if (!isMultisampled && isSamplable)
  {
    //create
    glGenTextures(1,&outTextureHandle);
    glBindTexture(GL_TEXTURE_2D,outTextureHandle);
    if (isHDR) glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,nullptr);
    else       glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA   ,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR); //tmp ?
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); //tmp ?
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D,0);
    //bind
    glFramebufferTexture2D(GL_FRAMEBUFFER,attachment,GL_TEXTURE_2D,outTextureHandle,0);
  }
  else
  {
    //create
    glGenRenderbuffers(1,&outBufferHandle);
    glBindRenderbuffer(GL_RENDERBUFFER,outBufferHandle);
    if (isMultisampled)
    {
      if (isHDR) glRenderbufferStorageMultisample(GL_RENDERBUFFER,4,GL_RGBA16F,w,h);
      else       glRenderbufferStorageMultisample(GL_RENDERBUFFER,4,GL_RGBA8  ,w,h);
    }
    else
    {
      if (isHDR) glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA16F,w,h);
      else       glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8  ,w,h);
    }
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    // bind
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,attachment,GL_RENDERBUFFER,outBufferHandle);
  }

  IsOpenGLok("createColorAttachment");
}

// ----------------------------------------------------------------------------

static void createDepthAttachment(int w, int h, int depthSize, bool isMultisampled, bool isSamplable,
                                  GLuint &outTextureHandle, GLuint &outBufferHandle)
{
  TRE_ASSERT(w != 0 && h != 0);
  TRE_ASSERT(depthSize == 16 || depthSize == 24 || depthSize == 32);
  TRE_ASSERT(!isMultisampled || !isSamplable);
  TRE_ASSERT(outTextureHandle == 0 && outBufferHandle == 0);

  if (!isMultisampled && isSamplable)
  {
    //create
    glGenTextures(1,&outTextureHandle);
    glBindTexture(GL_TEXTURE_2D,outTextureHandle);
    if      (depthSize == 16) glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT16 ,w,h,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_SHORT,nullptr);
    else if (depthSize == 24) glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24 ,w,h,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,nullptr);
    else if (depthSize == 32) glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32F,w,h,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);

    // GL_DEPTH_COMPONENT16 is required on WebGL (TODO: check this !)

    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);

#ifndef TRE_OPENGL_ES
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
#else // no border on OpenGL ES
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#endif

    glBindTexture(GL_TEXTURE_2D,0);

    //bind
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,outTextureHandle,0);
  }
  else
  {
    //create
    glGenRenderbuffers(1,&outBufferHandle);
    glBindRenderbuffer(GL_RENDERBUFFER,outBufferHandle);
    GLenum depthFormat = 0;
    if      (depthSize == 16) depthFormat = GL_DEPTH_COMPONENT16;
    else if (depthSize == 24) depthFormat = GL_DEPTH_COMPONENT24;
    else if (depthSize == 32) depthFormat = GL_DEPTH_COMPONENT32F;
    if (isMultisampled) glRenderbufferStorageMultisample(GL_RENDERBUFFER,4,depthFormat,w,h);
    else                glRenderbufferStorage(GL_RENDERBUFFER,depthFormat,w,h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    //bind
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,outBufferHandle);
  }

  IsOpenGLok("createDepthAttachment");
}

// ----------------------------------------------------------------------------

static void clearAttachment(GLuint &outTextureHandle, GLuint &outBufferHandle)
{
  if (outTextureHandle != 0) glDeleteTextures(1, &outTextureHandle);
  outTextureHandle = 0;

  if (outBufferHandle != 0) glDeleteRenderbuffers(1, &outBufferHandle);
  outBufferHandle = 0;
}

// ============================================================================

bool renderTarget::load(const int pwidth, const int pheigth)
{
  TRE_ASSERT(m_drawFBO == 0);
  TRE_ASSERT(pwidth != 0 && pheigth != 0);

  if (isMultisampled() && kMaxMultiSampling == 0)
  {
    glGetIntegerv(GL_MAX_SAMPLES, &kMaxMultiSampling);
    TRE_LOG("GL_MAX_SAMPLES = " << kMaxMultiSampling);
    TRE_ASSERT(kMaxMultiSampling >= 4);
  }
  if (hasDepth() && kNativeDepthSize == 0)
  {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &kNativeDepthSize);
    TRE_LOG("GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE = " << kNativeDepthSize);
    TRE_ASSERT(kNativeDepthSize == 16 || kNativeDepthSize == 24 || kNativeDepthSize == 32);
  }

  // create frame buffer object
  glGenFramebuffers(1,&m_drawFBO);

  return resize(pwidth, pheigth);
}

// ----------------------------------------------------------------------------

bool renderTarget::resize(const int pwidth, const int pheigth)
{
  if (pwidth == m_w && pheigth == m_h) return true;
  if (m_drawFBO == 0) return true; // not loaded yet, just return.

  m_w = pwidth;
  m_h = pheigth;

  // clear previous attachments

  clearAttachment(m_colorhandle, m_colorbuffer);
  clearAttachment(m_depthhandle, m_depthbuffer);

  // bind

  glBindFramebuffer(GL_FRAMEBUFFER, m_drawFBO);

  // color0-attachment point

  if (hasColor())
  {
    createColorAttachment(m_w, m_h, isHDR(), isMultisampled(), (m_flags & RT_COLOR_SAMPLABLE) != 0,
                          GL_COLOR_ATTACHMENT0,
                          m_colorhandle, m_colorbuffer);
  }
  else
  {
    const GLenum drawBufs[1] = {GL_NONE};
    glDrawBuffers(1, drawBufs);
    //glReadBuffer(GL_NONE);
    IsOpenGLok("createColorAttachment (no color)");
  }

  // depth-attachment point

  if (hasDepth())
  {
    int dsize = 24;
    if (!hasColor()) dsize = 32; // high precision if no color
    if (isNativeDepth()) dsize = kNativeDepthSize;
    createDepthAttachment(m_w, m_h, dsize, isMultisampled(), (m_flags & RT_DEPTH_SAMPLABLE) != 0,
                          m_depthhandle, m_depthbuffer);
  }
  else
  {
    // nothing
    IsOpenGLok("createDepthAttachment (no depth)");
  }

  // check

  bool myframebufferstatus = true;
  GLenum glstatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (glstatus != GL_FRAMEBUFFER_COMPLETE)
  {
    TRE_LOG("[Debug] Error when creating frame-buffer " << m_drawFBO);
    if      (glstatus==GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT)         { TRE_LOG("(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT returned)");         }
    else if (glstatus==GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER)        { TRE_LOG("(GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER returned)");        }
    else if (glstatus==GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT) { TRE_LOG("(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT returned)"); }
    else if (glstatus==GL_FRAMEBUFFER_UNSUPPORTED)                   { TRE_LOG("(GL_FRAMEBUFFER_UNSUPPORTED returned)");                   }
    myframebufferstatus=false;
  }

#ifdef TRE_DEBUG
  if (hasDepth())
  {
    int depthSize = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,  GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depthSize);
    TRE_LOG("FBO: Depth attachment is " << depthSize << " bits.");
  }
#endif

  glBindFramebuffer(GL_FRAMEBUFFER,0);

  // end

  TRE_LOG("FBO created " << (myframebufferstatus ? "" : "[ERROR] ") << "(ID=" << m_drawFBO << ", w=" << m_w << ", h=" << m_h << (isMultisampled() ? " [multisampled:x4]" : "" ) <<
          (hasColor() ? " [Color-0" : " [No-Color" ) << (isHDR() ? " HDR" : "" ) << ((m_flags & RT_COLOR_SAMPLABLE) ? " Sampl." : "" ) << "]" <<
          (hasDepth() ? " [Depth" : " [No-Depth" ) << ((m_flags & RT_DEPTH_SAMPLABLE) ? " Sampl." : "" ) << "])");

  myframebufferstatus &= IsOpenGLok("renderTarget::resize");

  return myframebufferstatus;
}

// ----------------------------------------------------------------------------

void renderTarget::clear()
{
  if (m_drawFBO !=0 ) glDeleteFramebuffers(1,&m_drawFBO);
  m_drawFBO = 0;

  clearAttachment(m_colorhandle, m_colorbuffer);
  clearAttachment(m_depthhandle, m_depthbuffer);
}

// ----------------------------------------------------------------------------

void renderTarget::bindForWritting() const
{
  TRE_ASSERT(m_drawFBO != 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_drawFBO);
  glViewport(0, 0, m_w, m_h);
}

// ----------------------------------------------------------------------------

void renderTarget::bindForReading() const
{
  TRE_ASSERT(m_drawFBO != 0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_drawFBO);
}

// ----------------------------------------------------------------------------

void renderTarget::resolve(const int outwidth, const int outheigth, const bool withDepth) const
{
  TRE_ASSERT(m_drawFBO != 0);
  TRE_ASSERT(!isHDR()); // glBlitFramebuffer: buffer(color & depth) must have same format.

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_drawFBO);

  const GLbitfield blitMask = (hasColor() ? GL_COLOR_BUFFER_BIT : 0) | (hasDepth() && withDepth ? GL_DEPTH_BUFFER_BIT : 0);

  glBlitFramebuffer(0, 0, m_w, m_h, 0, 0, outwidth, outheigth, blitMask, GL_NEAREST);
  IsOpenGLok("renderTarget::resolve to screen");
}

// ----------------------------------------------------------------------------

void renderTarget::resolve(renderTarget &targetFBO) const
{
  TRE_ASSERT(m_drawFBO != 0);
  TRE_ASSERT(isMultisampled() || !targetFBO.isMultisampled());
  TRE_ASSERT(isHDR() == targetFBO.isHDR()); // glBlitFramebuffer: buffer(color & depth) must have same format.

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFBO.m_drawFBO);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_drawFBO);

  const GLbitfield blitMask = (hasColor() ? GL_COLOR_BUFFER_BIT : 0) | (hasDepth() ? GL_DEPTH_BUFFER_BIT : 0);

  glBlitFramebuffer(0, 0, m_w, m_h, 0, 0, targetFBO.w(), targetFBO.h(), blitMask, GL_NEAREST);
  IsOpenGLok("renderTarget::resolve to FBO");
}

// ============================================================================

void renderTarget_ShadowMap::computeUBO_forMap(const shader::s_UBOdata_sunLight & uboLight, shader::s_UBOdata_sunShadow &uboShadow, uint shadowIndex)
{
  TRE_ASSERT(m_w>=m_h); // just a matter of implementation
  TRE_ASSERT(glm::length(uboLight.direction)>0.001f);

  // Build the view-matrix (look-at)
  glm::vec3 eZ = - glm::normalize(uboLight.direction);

  glm::vec3 eX = glm::cross(glm::vec3(0.f,0.f,-1.f), eZ);
  if (glm::length(eX)<0.001f) eX = glm::cross(glm::vec3(0.f,1.f,0.f), eZ);
  eX = glm::normalize(eX);

  glm::vec3 eY = glm::cross(eZ, eX);
  TRE_ASSERT(glm::length(eY)>=0.001f);
  eY = glm::normalize(eY);

  //TRE_LOG(" eX = " << eX.x << " " << eX.y << " " << eX.z);
  //TRE_LOG(" eY = " << eY.x << " " << eY.y << " " << eY.z);
  //TRE_LOG(" eZ = " << eZ.x << " " << eZ.y << " " << eZ.z);

  m_mView[0] = glm::vec4(eX, 0.f);
  m_mView[1] = glm::vec4(eY, 0.f);
  m_mView[2] = glm::vec4(eZ, 0.f);
  m_mView[3] = glm::vec4(0.f,0.f,0.f,1.f);
  m_mView = glm::transpose(m_mView);

  // find the best rotation (along new Z-axis "eZ") TODO
  //glm::mat4 testrot = glm::mat4(1.f);
  //testrot[0][0] = cos; testrot[0][1] =-sin;
  //testrot[1][0] = sin; testrot[1][1] = cos;
  //mView = testrot * mView;

  s_boundbox sceneProj = m_sceneBox.transform(m_mView);

#if 0
  TRE_LOG(" sceneBox  " << " x=[" << sceneBox.m_min.x  << "," << sceneBox.m_max.x  << "]"
                        << " y=[" << sceneBox.m_min.y  << "," << sceneBox.m_max.y  << "]"
                        << " z=[" << sceneBox.m_min.z  << "," << sceneBox.m_max.z  << "]");
  TRE_LOG(" sceneProj " << " x=[" << sceneProj.m_min.x << "," << sceneProj.m_max.x << "]"
                        << " y=[" << sceneProj.m_min.y << "," << sceneProj.m_max.y << "]"
                        << " z=[" << sceneProj.m_min.z << "," << sceneProj.m_max.z << "]");
#endif

  // fit the view to the clip-space

  m_mView[3] = glm::vec4(-sceneProj.center(),1.f);

  const glm::vec3 viewExtend = sceneProj.extend();
  TRE_ASSERT(viewExtend.x >= 0.f);
  TRE_ASSERT(viewExtend.y >= 0.f);
  TRE_ASSERT(viewExtend.z >= 0.f);
  const float scaleX = (viewExtend.x > 0.001f) ? 2.f/viewExtend.x : 2.f/0.001f;
  const float scaleY = (viewExtend.y > 0.001f) ? 2.f/viewExtend.y : 2.f/0.001f;
  const float scaleZ = (viewExtend.z > 0.001f) ? 2.f/viewExtend.z : 2.f/0.001f;

  glm::mat4 mscale = glm::mat4(1.f);
  mscale[0][0] = scaleX;
  mscale[1][1] = scaleY;
  mscale[2][2] = scaleZ;

  m_mView = mscale * m_mView;

  tre::compute3DOrthoProjection(m_mProj, 1.1f, 1.1f, -1.1f, 1.1f);

  uboShadow.matPV(shadowIndex) = m_mProj * m_mView;
  uboShadow.mapInvDim(shadowIndex) = 1.f / glm::vec2(float(m_w), float(m_h));
  uboShadow.mapBox(shadowIndex) = glm::vec4(viewExtend.x, viewExtend.y, sceneProj.m_min.z, sceneProj.m_max.z);
}

// ============================================================================

bool renderTarget_ShadowCubeMap::load(const int texSize)
{
  TRE_ASSERT(m_drawFBO == 0);

  m_w = texSize;
  m_h = texSize;

  // create frame buffer object
  glGenFramebuffers(1,&m_drawFBO);
  glBindFramebuffer(GL_FRAMEBUFFER,m_drawFBO);

  const GLenum drawBufs[1] = {GL_NONE};
  glDrawBuffers(1, drawBufs);
  //glReadBuffer(GL_NONE);

  glBindFramebuffer(GL_FRAMEBUFFER,0);

  // no-check

  // create a cube-map texture (depth)
  glGenTextures(1, &m_depthhandle);
  glBindTexture(GL_TEXTURE_CUBE_MAP, m_depthhandle);
  glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);

  for (uint iface=0; iface<6; ++iface)
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + iface, 0, GL_DEPTH_COMPONENT32F, m_w, m_h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

  glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

  // end

  TRE_LOG("FBO created (ID=" << m_drawFBO << ", w=" << m_w << ", h=" << m_h << " [Depth Cube])");

  IsOpenGLok("renderTarget_ShadowCubeMap::load");

  return true;
}

// ----------------------------------------------------------------------------

void renderTarget_ShadowCubeMap::clear()
{
  if (m_drawFBO!=0)     glDeleteFramebuffers(1,&m_drawFBO);
  m_drawFBO=0;
  if (m_depthhandle!=0) glDeleteTextures(1,&m_depthhandle);
  m_depthhandle=0;
}

// ----------------------------------------------------------------------------

void renderTarget_ShadowCubeMap::bindForWritting(GLenum cubeFace) const
{
  TRE_ASSERT(m_drawFBO != 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_drawFBO);
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, cubeFace, m_depthhandle, 0);
}

// ----------------------------------------------------------------------------

void renderTarget_ShadowCubeMap::bindForReading(GLenum cubeFace) const
{
  TRE_ASSERT(m_drawFBO != 0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_drawFBO);
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, cubeFace, m_depthhandle, 0);
}

// ----------------------------------------------------------------------------

void renderTarget_ShadowCubeMap::setRenderingLimits(float near, float far)
{
  compute3DFrustumProjection(m_mProj, 1.f, float(M_PI * 0.5), near, far);
  m_near = near;
  m_far = far;
}

// ----------------------------------------------------------------------------

void renderTarget_ShadowCubeMap::computeUBO_forMap(shader::s_UBOdata_ptstLight & uboLight, uint lightIndex, shader::s_UBOdata_ptsShadow &uboShadow)
{
  m_cubeCenter = uboLight.pos(lightIndex);

  uboLight.shadow(lightIndex) = 0;

  uboShadow.mapInvDimension = 1.f / glm::vec2(float(m_w), float(m_h));
  uboShadow.mapBoxUVNF = glm::vec4(m_far, m_far, m_near, m_far);

  computeMViews();
}

// ----------------------------------------------------------------------------

void renderTarget_ShadowCubeMap::computeMViews()
{
  const glm::vec4 axisX = glm::vec4(1.f, 0.f, 0.f, 0.f);
  const glm::vec4 axisY = glm::vec4(0.f, 1.f, 0.f, 0.f);
  const glm::vec4 axisZ = glm::vec4(0.f, 0.f, 1.f, 0.f);
  const glm::vec4 axisW = glm::vec4(0.f, 0.f, 0.f, 1.f);

  // Coordinate system of cube-map is left-handed. Camera towards '-Z'.
  m_mViewes[0] = glm::translate(glm::mat4( -axisZ, -axisY, -axisX, axisW), -m_cubeCenter); // +X
  m_mViewes[1] = glm::translate(glm::mat4(  axisZ, -axisY,  axisX, axisW), -m_cubeCenter); // -X
  m_mViewes[2] = glm::translate(glm::mat4(  axisX, -axisZ, -axisY, axisW), -m_cubeCenter); // +Y
  m_mViewes[3] = glm::translate(glm::mat4(  axisX,  axisZ, -axisY, axisW), -m_cubeCenter); // -Y
  m_mViewes[4] = glm::translate(glm::mat4(  axisX, -axisY, -axisZ, axisW), -m_cubeCenter); // +Z
  m_mViewes[5] = glm::translate(glm::mat4( -axisX, -axisY,  axisZ, axisW), -m_cubeCenter); // -Z
}

// ============================================================================

bool renderTarget_GBuffer::load(const int pwidth, const int pheigth)
{
  TRE_ASSERT(m_drawFBO == 0);

  // create frame buffer object
  glGenFramebuffers(1,&m_drawFBO);

  return resize(pwidth, pheigth);
}

// ----------------------------------------------------------------------------

bool renderTarget_GBuffer::resize(const int pwidth, const int pheigth)
{
  TRE_ASSERT(m_drawFBO != 0);

  if (pwidth == m_w && pheigth == m_h) return true;

  m_w = pwidth;
  m_h = pheigth;

  glBindFramebuffer(GL_FRAMEBUFFER,m_drawFBO);

  GLuint dummy = 0;

  // textures (rgba - floats)
  glGenTextures(GBUFFER_NUM,m_colorhandles);
  GLenum  drawBuffersList[GBUFFER_NUM];

  for (uint iT = 0; iT < GBUFFER_NUM; ++iT)
  {
    createColorAttachment(m_w, m_h, m_isHDR || (iT != 0), false, true, GL_COLOR_ATTACHMENT0 + iT, m_colorhandles[iT], dummy);

    drawBuffersList[iT] = GL_COLOR_ATTACHMENT0 + iT;
  }

  glDrawBuffers(GBUFFER_NUM, drawBuffersList);

  // depth (float)
  {
    createDepthAttachment(m_w, m_h, 32, false, true, m_depthhandle, dummy);
  }

  // check
  bool myframebufferstatus = true;
  GLenum glstatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (glstatus != GL_FRAMEBUFFER_COMPLETE)
  {
    TRE_LOG("[Debug] Error when creating frame-buffer " << m_drawFBO);
    if      (glstatus==GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT)         { TRE_LOG("(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT returned)");         }
    else if (glstatus==GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER)        { TRE_LOG("(GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER returned)");        }
    else if (glstatus==GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT) { TRE_LOG("(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT returned)"); }
    else if (glstatus==GL_FRAMEBUFFER_UNSUPPORTED)                   { TRE_LOG("(GL_FRAMEBUFFER_UNSUPPORTED returned)");                   }
    myframebufferstatus=false;
  }

  // end
  glBindFramebuffer(GL_FRAMEBUFFER,0);

  TRE_LOG("FBO created " << (myframebufferstatus ? "" : "[ERROR] ") << "(ID=" << m_drawFBO << ", w=" << m_w << ", h=" << m_h << " [G-BUFFER]" << (isHDR() ? " [HDR]" : "") << ")");

  return myframebufferstatus;
}

// ----------------------------------------------------------------------------

void renderTarget_GBuffer::clear()
{
  if (m_drawFBO != 0) glDeleteFramebuffers(1, &m_drawFBO);
  m_drawFBO = 0;

  glDeleteTextures(GBUFFER_NUM, m_colorhandles);
  memset(m_colorhandles, 0, sizeof(m_colorhandles));

  if (m_depthhandle!=0) glDeleteTextures(1, &m_depthhandle);
  m_depthhandle=0;
}

// ----------------------------------------------------------------------------

void renderTarget_GBuffer::bindForWritting() const
{
  TRE_ASSERT(m_drawFBO != 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_drawFBO);
}

// ----------------------------------------------------------------------------

void renderTarget_GBuffer::bindForReading() const
{
  TRE_ASSERT(m_drawFBO != 0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_drawFBO);
}

// ============================================================================

static const char * SourcePostProcess_FragMain_BlurDownPass =
"// uniColor = {alpha, offset, ., brightOnly}\n"
"// AtlasInvDim = {1/src-width, 1/src-height}\n"
"const vec3 grayFactors = vec3(0.2125f, 0.7154f, 0.0721f);\n"
"void main()\n"
"{\n"
"  if (uniColor.w != 0.f) // bright only \n"
"  {\n"
"    vec3 colorORIGIN = texture(TexDiffuse,pixelUV).xyz;\n"
"    float gray = dot(colorORIGIN, grayFactors);\n"
"    float x = max(gray - uniColor.y, 0.f);\n"
"    float xx = x * x;\n"
"    float factor = xx / (uniColor.x + xx);\n"
"    color.xyz = factor * colorORIGIN;\n"
"    color.w = 1.f;\n"
"  }\n"
"  else // downsample with Gaussian filter\n"
"  {\n"
"    float x = AtlasInvDim.x;\n"
"    float y = AtlasInvDim.y;\n"
"    vec3 a = texture(TexDiffuse, vec2(pixelUV.x - 2.f*x, pixelUV.y + 2.f*y)).rgb;\n"
"    vec3 b = texture(TexDiffuse, vec2(pixelUV.x,         pixelUV.y + 2.f*y)).rgb;\n"
"    vec3 c = texture(TexDiffuse, vec2(pixelUV.x + 2.f*x, pixelUV.y + 2.f*y)).rgb;\n"
"    vec3 d = texture(TexDiffuse, vec2(pixelUV.x - 2.f*x, pixelUV.y        )).rgb;\n"
"    vec3 e = texture(TexDiffuse, vec2(pixelUV.x,         pixelUV.y        )).rgb;\n"
"    vec3 f = texture(TexDiffuse, vec2(pixelUV.x + 2.f*x, pixelUV.y        )).rgb;\n"
"    vec3 g = texture(TexDiffuse, vec2(pixelUV.x - 2.f*x, pixelUV.y - 2.f*y)).rgb;\n"
"    vec3 h = texture(TexDiffuse, vec2(pixelUV.x,         pixelUV.y - 2.f*y)).rgb;\n"
"    vec3 i = texture(TexDiffuse, vec2(pixelUV.x + 2.f*x, pixelUV.y - 2.f*y)).rgb;\n"
"    vec3 j = texture(TexDiffuse, vec2(pixelUV.x - x    , pixelUV.y +     y)).rgb;\n"
"    vec3 k = texture(TexDiffuse, vec2(pixelUV.x + x    , pixelUV.y +     y)).rgb;\n"
"    vec3 l = texture(TexDiffuse, vec2(pixelUV.x - x    , pixelUV.y -     y)).rgb;\n"
"    vec3 m = texture(TexDiffuse, vec2(pixelUV.x + x    , pixelUV.y -     y)).rgb;\n"
"    color.xyz  = e         * 0.125;\n"
"    color.xyz += (a+c+g+i) * 0.03125;\n"
"    color.xyz += (b+d+f+h) * 0.0625;\n"
"    color.xyz += (j+k+l+m) * 0.125;\n"
"    color.w = 1.f;\n"
"  }\n"
"}\n";

static const char * SourcePostProcess_FragMain_BlurUpPass =
"// uniColor = { radiusH., ., combineStrength, withCombine}\n"
"// AtlasInvDim = {1/src-width, 1/src-height}\n"
"void main()\n"
"{\n"
"  float x = uniColor.x * AtlasInvDim.y;\n"
"  float y = uniColor.x * AtlasInvDim.y;\n"
"  // Use a 3x3 kernel around the center pixel e\n"
"  vec3 a = texture(TexDiffuse, vec2(pixelUV.x - x, pixelUV.y + y)).rgb;\n"
"  vec3 b = texture(TexDiffuse, vec2(pixelUV.x,     pixelUV.y + y)).rgb;\n"
"  vec3 c = texture(TexDiffuse, vec2(pixelUV.x + x, pixelUV.y + y)).rgb;\n"
"  vec3 d = texture(TexDiffuse, vec2(pixelUV.x - x, pixelUV.y    )).rgb;\n"
"  vec3 e = texture(TexDiffuse, vec2(pixelUV.x,     pixelUV.y    )).rgb;\n"
"  vec3 f = texture(TexDiffuse, vec2(pixelUV.x + x, pixelUV.y    )).rgb;\n"
"  vec3 g = texture(TexDiffuse, vec2(pixelUV.x - x, pixelUV.y - y)).rgb;\n"
"  vec3 h = texture(TexDiffuse, vec2(pixelUV.x,     pixelUV.y - y)).rgb;\n"
"  vec3 i = texture(TexDiffuse, vec2(pixelUV.x + x, pixelUV.y - y)).rgb;\n"
"  color.xyz  = e         * (4./16.)\n;"
"  color.xyz += (b+d+f+h) * (2./16.)\n;"
"  color.xyz += (a+c+g+i) * (1./16.)\n;"
"  color.xyz *= 0.8f; // SRC color multiplyer (half-resolution)\n"
"  color.w    = 0.3f; // DST color multiplyer (current-resolution)\n"
"  if (uniColor.w != 0.f)\n"
"  {\n"
"    color *= uniColor.z;\n"
"    color.xyz += texture(TexDiffuseB, pixelUV).rgb;\n"
"  }\n"
"}\n";

// ----------------------------------------------------------------------------

bool postFX_Blur::load(const int pwidth, const int pheigth)
{
  bool status = true;

  TRE_ASSERT(m_Npass == m_renderDownsample.size());
  TRE_ASSERT(std::pow(2,m_renderDownsample.size()) < pwidth);
  TRE_ASSERT(std::pow(2,m_renderDownsample.size()) < pheigth);
  // Load downsampling FBOs
  glm::ivec2 dim = glm::ivec2(pwidth, pheigth);
  for (uint i = 0; i < m_Npass; ++i)
  {
    status &= m_renderDownsample[i].load(dim.x, dim.y);
    dim /= 2;
  }

  // Shaders
  shader::s_layout shaderLayout(shader::PRGM_2D);
  shaderLayout.hasBUF_UV = true;
  shaderLayout.hasSMP_Diffuse = true;
  shaderLayout.hasOUT_Color0 = true;
  shaderLayout.hasUNI_uniColor = true; // to pass info data (4 floats)
  shaderLayout.hasUNI_AtlasInvDim = true; // to pass texture dimension (2 floats)
  status &= m_shaderDownPass.loadCustomShader(shaderLayout, SourcePostProcess_FragMain_BlurDownPass, "PostProcess_Blur_DownPass");

  shaderLayout.hasSMP_DiffuseB = true;
  status &= m_shaderUpPass.loadCustomShader(shaderLayout, SourcePostProcess_FragMain_BlurUpPass, "PostProcess_Blur_UpPass");

  // Model
  const glm::vec4 pos(-1.f, -1.f, 1.f, 1.f);
  const glm::vec4 uv(0.f, 0.f, 1.f, 1.f);
  const glm::vec4 color(1.f);
  const std::size_t partId = m_quadFullScreen.createPart(6);
  m_quadFullScreen.fillDataRectangle(partId, 0, pos, color, uv);
  status &= m_quadFullScreen.loadIntoGPU();

  return status && resize(pwidth, pheigth);
}

// ----------------------------------------------------------------------------

bool postFX_Blur::resize(const int pwidth, const int pheigth)
{
  TRE_ASSERT(m_Npass == m_renderDownsample.size());
  TRE_ASSERT(std::pow(2,m_renderDownsample.size()) < pwidth);
  TRE_ASSERT(std::pow(2,m_renderDownsample.size()) < pheigth);
  glm::ivec2 dim = glm::ivec2(pwidth, pheigth);
  for (uint i = 0; i < m_Npass; ++i)
  {
    if (!m_renderDownsample[i].resize(dim.x, dim.y)) return false;
    dim /= 2;
  }
  return true;
}

// ----------------------------------------------------------------------------

void postFX_Blur::clear()
{
  // FBOs
  for (renderTarget & curFBO : m_renderDownsample) curFBO.clear();
  // Shaders
  m_shaderDownPass.clearShader();
  m_shaderUpPass.clearShader();
  // Model
  m_quadFullScreen.clearGPU();
}

// ----------------------------------------------------------------------------

void postFX_Blur::processBlur(GLuint inputTextureHandle, const bool withFinalCombine)
{
  TRE_ASSERT(m_Npass > 0);

  glDisable(GL_DEPTH_TEST);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, inputTextureHandle);

  // Bright-pass
  glDisable(GL_BLEND);
  {
    m_renderDownsample[0].bindForWritting();
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_shaderDownPass.m_drawProgram);
    glUniform1i(m_shaderDownPass.getUniformLocation(tre::shader::TexDiffuse),0);
    glUniform4f(m_shaderDownPass.getUniformLocation(tre::shader::uniColor), m_brightAlpha, m_brightOffset, 0.f, 1.f);
    glUniform2f(m_shaderDownPass.getUniformLocation(tre::shader::AtlasInvDim), 0.f, 0.f); // not used in the bright-pass
    m_quadFullScreen.drawcallAll(true);
  }

  // Down-pass
  if (m_Npass > 1)
  {
    glActiveTexture(GL_TEXTURE1);

    glUniform1i(m_shaderDownPass.getUniformLocation(tre::shader::TexDiffuse),1);
    glUniform4f(m_shaderDownPass.getUniformLocation(tre::shader::uniColor), 0.f, 0.f, 0.f, 0.f);

    for (uint ipass=1;ipass < m_Npass;++ipass)
    {
      m_renderDownsample[ipass].bindForWritting();
      glBindTexture(GL_TEXTURE_2D,m_renderDownsample[ipass-1].colorHandle());
      glUniform2f(m_shaderDownPass.getUniformLocation(tre::shader::AtlasInvDim), 1.f / m_renderDownsample[ipass-1].w(), 1.f / m_renderDownsample[ipass-1].h());
      m_quadFullScreen.drawcallAll(false);
    }
  }

  // Up-pass
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_SRC_ALPHA); // Allow to pre-multiply always present color by the output alpha.
  {
    glUseProgram(m_shaderUpPass.m_drawProgram);
    glUniform1i(m_shaderUpPass.getUniformLocation(tre::shader::TexDiffuse),1);
    glUniform1i(m_shaderUpPass.getUniformLocation(tre::shader::TexDiffuseB),0);

    for (uint ipass = m_Npass; ipass-- != 1;)
    {
      m_renderDownsample[ipass - 1].bindForWritting();
      glBindTexture(GL_TEXTURE_2D,m_renderDownsample[ipass].colorHandle());

      const bool combineWithInput = (withFinalCombine && ipass == 1);

      glUniform4f(m_shaderUpPass.getUniformLocation(tre::shader::uniColor), 1.f, 0.f, m_combineStrength, combineWithInput ? 1.f : 0.f);
      glUniform2f(m_shaderDownPass.getUniformLocation(tre::shader::AtlasInvDim), 1.f / m_renderDownsample[ipass].w(), 1.f / m_renderDownsample[ipass].h());
      m_quadFullScreen.drawcallAll(false);
    }
  }

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // restore the default blending function.
}

// ============================================================================

static const char * SourcePostProcess_FragMain_ToneMapping =
"\n"
"uniform vec4 paramsToneMapping;\n"
"uniform vec4 paramsVignetting;\n"
"uniform vec3 vignetteColor;\n"
"uniform float aspectRatio;\n"
"void main(){\n"
"  vec3 colorORIGIN = texture(TexDiffuse,pixelUV).rgb;\n"
"  vec3 colorMAPPED = 1.f - exp(-colorORIGIN * paramsToneMapping.x); // tone-mapping\n"
"  float greyIntensity = 0.2126 * colorMAPPED.r + 0.7152 * colorMAPPED.g + 0.0722 * colorMAPPED.b;\n"
"  vec3 colorSAT = clamp( greyIntensity + (colorMAPPED - greyIntensity) * (1.f + paramsToneMapping.z), vec3(0.f), vec3(1.f));\n"
"  vec2 uvFactor = aspectRatio > 1.f ? vec2(1.f / aspectRatio, 1.f) : vec2(1.f, aspectRatio);\n"
"  vec2 uvNormalized = 2.f * uvFactor * (pixelUV - 0.5f);\n"
"  float normF = 2.f / max(paramsVignetting.z, 5.e-2f);\n"
"  float alpha = pow(pow(abs(uvNormalized.x),normF) + pow(abs(uvNormalized.y),normF), 1.f / normF) * (1.f - paramsVignetting.w * 0.2f);\n"
"  float cursor = min(pow(alpha, 1.f / (1.e-3f + paramsVignetting.w)), 1.f);\n"
"  float weightColor =  1.f - paramsVignetting.x * cursor;\n"
"  float weightDesat = 1.f - paramsVignetting.y * cursor;\n"
"  vec3 colorVIGN = weightColor * (weightDesat * colorSAT + (1.f - weightDesat) * greyIntensity) + (1.f - weightColor) * vignetteColor;\n"
"  if (paramsToneMapping.y != 1.f) colorVIGN = pow(colorVIGN, 1.f/paramsToneMapping.yyy); // gamma-correction\n"
"  color = vec4(colorVIGN, 1.f);\n"
"}\n";

// ----------------------------------------------------------------------------

bool postFX_ToneMapping::load()
{
  bool result = true;
  // Shaders
  shader::s_layout shaderLayout(shader::PRGM_2D);
  shaderLayout.hasBUF_UV = true;
  shaderLayout.hasSMP_Diffuse = true;
  shaderLayout.hasOUT_Color0 = true;
  result &= m_shaderToneMap.loadCustomShader(shaderLayout,SourcePostProcess_FragMain_ToneMapping,"PostProcess_ToneMapping");
  // Model
  const glm::vec4 pos(-1.f, -1.f, 1.f, 1.f);
  const glm::vec4 uv(0.f, 0.f, 1.f, 1.f);
  const glm::vec4 color(1.f);

  const std::size_t partId = m_quadFullScreen.createPart(6);
  m_quadFullScreen.fillDataRectangle(partId, 0, pos, color, uv);

  m_quadFullScreen.loadIntoGPU();

  return result;
}

// ----------------------------------------------------------------------------

void postFX_ToneMapping::clear()
{
  // Shaders
  m_shaderToneMap.clearShader();
  // Model
  m_quadFullScreen.clearGPU();
}

// ----------------------------------------------------------------------------

void postFX_ToneMapping::resolveToneMapping(GLuint inputTextureHandle, const int outwidth, const int outheigth)
{
  glDisable(GL_DEPTH_TEST);

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glViewport(0, 0, outwidth, outheigth);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, inputTextureHandle);

  glUseProgram(m_shaderToneMap.m_drawProgram);
  glUniform1i(m_shaderToneMap.getUniformLocation(tre::shader::TexDiffuse),0);
  glUniform4fv(m_shaderToneMap.getUniformLocation("paramsToneMapping"), 1, glm::value_ptr(m_params));
  glUniform4fv(m_shaderToneMap.getUniformLocation("paramsVignetting"), 1, glm::value_ptr(m_vignettingParams));
  glUniform3fv(m_shaderToneMap.getUniformLocation("vignetteColor"), 1, glm::value_ptr(m_vignetteColor));
  glUniform1f(m_shaderToneMap.getUniformLocation("aspectRatio"), float(outheigth) / float(outwidth));

  m_quadFullScreen.drawcallAll(true);
}

// ----------------------------------------------------------------------------

void postFX_ToneMapping::resolveToneMapping(GLuint inputTextureHandle, renderTarget &targetFBO)
{
  glDisable(GL_DEPTH_TEST);

  targetFBO.bindForWritting();
  glViewport(0, 0, targetFBO.w(), targetFBO.h());

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, inputTextureHandle);

  glUseProgram(m_shaderToneMap.m_drawProgram);
  glUniform1i(m_shaderToneMap.getUniformLocation(tre::shader::TexDiffuse),0);
  glUniform4fv(m_shaderToneMap.getUniformLocation("paramsToneMapping"), 1, glm::value_ptr(m_params));
  glUniform4fv(m_shaderToneMap.getUniformLocation("paramsVignetting"), 1, glm::value_ptr(m_vignettingParams));
  glUniform3fv(m_shaderToneMap.getUniformLocation("vignetteColor"), 1, glm::value_ptr(m_vignetteColor));
  glUniform1f(m_shaderToneMap.getUniformLocation("aspectRatio"), float(targetFBO.h()) / float(targetFBO.w()));

  m_quadFullScreen.drawcallAll(true);
}

// ============================================================================

} // namespace
