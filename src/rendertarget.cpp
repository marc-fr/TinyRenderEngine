#include "rendertarget.h"

#ifdef TRE_PRINTS
#include <iostream>
#endif

namespace tre {

// ============================================================================
// Helpers ...

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

static void createDepthAttachment(int w, int h, bool highPrecision, bool isMultisampled, bool isSamplable,
                                  GLuint &outTextureHandle, GLuint &outBufferHandle)
{
  TRE_ASSERT(w != 0 && h != 0);
  TRE_ASSERT(!isMultisampled || !isSamplable);
  TRE_ASSERT(outTextureHandle == 0 && outBufferHandle == 0);

  if (!isMultisampled && isSamplable)
  {
    //create
    glGenTextures(1,&outTextureHandle);
    glBindTexture(GL_TEXTURE_2D,outTextureHandle);
#ifndef TRE_OPENGL_ES
    if (highPrecision) glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32F,w,h,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
    else               glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT   ,w,h,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,nullptr); // keep the native GL_DEPTH_COMPONENT so the depth-format stay compatible when using "glBlitFramebuffer"
#else
    glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT16 ,w,h,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_SHORT,nullptr); // GL_DEPTH_COMPONENT16 is required on WebGL
#endif

    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glBindTexture(GL_TEXTURE_2D,0);
    //bind
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,outTextureHandle,0);
  }
  else
  {
    //create
    glGenRenderbuffers(1,&outBufferHandle);
    glBindRenderbuffer(GL_RENDERBUFFER,outBufferHandle);
#ifndef TRE_OPENGL_ES
    const GLenum depthFormat = highPrecision ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT; // keep the native GL_DEPTH_COMPONENT so the depth-format stay compatible when using "glBlitFramebuffer"
#else
    const GLenum depthFormat = highPrecision ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT16;
#endif
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
  TRE_ASSERT(hasValidFlags());
  TRE_ASSERT(m_drawFBO == 0);
  TRE_ASSERT(pwidth != 0 && pheigth != 0);

  if (isMultisampled())
  {
    int nmaxsamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES,&nmaxsamples);
    TRE_LOG("GL_MAX_SAMPLES: " << nmaxsamples);
    TRE_ASSERT(nmaxsamples >= 4);
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
  }

  // depth-attachment point

  if (hasDepth())
  {
    createDepthAttachment(m_w, m_h, !hasColor() /*no color, then use better precision for depth-only*/, isMultisampled(), (m_flags & RT_DEPTH_SAMPLABLE) != 0,
                          m_depthhandle, m_depthbuffer);
  }
  else
  {
    // nothing
  }

  // check

  bool myframebufferstatus = true;
  GLenum glstatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (glstatus != GL_FRAMEBUFFER_COMPLETE)
  {
#ifdef TRE_PRINTS
    std::cout << "[Debug] Error when creating frame-buffer " << m_drawFBO << std::endl;
    if      (glstatus==GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT)
      std::cout << "(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT returned)" << std::endl;
    else if (glstatus==GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER)
      std::cout << "(GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER returned)" << std::endl;
    else if (glstatus==GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)
      std::cout << "(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT returned)" << std::endl;
    else if (glstatus==GL_FRAMEBUFFER_UNSUPPORTED)
      std::cout << "(GL_FRAMEBUFFER_UNSUPPORTED returned)" << std::endl;
#endif
    myframebufferstatus=false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER,0);

  // end

  TRE_LOG("FBO created " << (myframebufferstatus ? "" : "[ERROR] ") << "(ID=" << m_drawFBO << ", w=" << m_w << ", h=" << m_h << (isMultisampled() ? " [multisampled:x4]" : "" ) <<
          (hasColor() ? " [Color-0" : " [No-Color" ) << (isHDR() ? " HDR" : "" ) << ((m_flags & RT_COLOR_SAMPLABLE) ? " Sampl." : "" ) << "]" <<
          (hasDepth() ? " [Depth" : " [No-Depth" ) << ((m_flags & RT_DEPTH_SAMPLABLE) ? " Sampl." : "" ) << "])");

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
}

// ----------------------------------------------------------------------------

void renderTarget::bindForReading() const
{
  TRE_ASSERT(m_drawFBO != 0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_drawFBO);
}

// ----------------------------------------------------------------------------

void renderTarget::resolve(const int outwidth, const int outheigth) const
{
  TRE_ASSERT(m_drawFBO != 0);
  TRE_ASSERT(!isHDR()); // glBlitFramebuffer: buffer(color & depth) must have same format.

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_drawFBO);

  const GLbitfield blitMask = (hasColor() ? GL_COLOR_BUFFER_BIT : 0) | (hasDepth() ? GL_DEPTH_BUFFER_BIT : 0);

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

// ----------------------------------------------------------------------------

bool renderTarget::hasValidFlags() const
{
  bool isOk = true;
  isOk &= (hasColor() || hasDepth());
  isOk &= (hasColor() || ((m_flags & RT_COLOR_SAMPLABLE) == 0));
  isOk &= (hasDepth() || ((m_flags & RT_DEPTH_SAMPLABLE) == 0));
  isOk &= (!isMultisampled() || ((m_flags & (RT_COLOR_SAMPLABLE | RT_DEPTH_SAMPLABLE)) == 0));
  isOk &= (hasColor() || !isHDR());
  return isOk;
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
    createDepthAttachment(m_w, m_h, true, false, true, m_depthhandle, dummy);
  }

  // check
  bool myframebufferstatus = true;
  GLenum glstatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (glstatus != GL_FRAMEBUFFER_COMPLETE)
  {
#ifdef TRE_PRINTS
    std::cout << "[Debug] Error when creating frame-buffer " << m_drawFBO << std::endl;
    if      (glstatus==GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT)
      std::cout << "(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT returned)" << std::endl;
    else if (glstatus==GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER)
      std::cout << "(GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER returned)" << std::endl;
    else if (glstatus==GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)
      std::cout << "(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT returned)" << std::endl;
    else if (glstatus==GL_FRAMEBUFFER_UNSUPPORTED)
      std::cout << "(GL_FRAMEBUFFER_UNSUPPORTED returned)" << std::endl;
#endif
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

static const char * SourcePostProcess_FragMain_BrightPass_LDR =
"\n"
"uniform vec2 paramSubMul;\n"
"void main(){\n"
"  vec3 colorORIGIN = texture(TexDiffuse,pixelUV).xyz;\n"
"  float colorORIGINmax = max(max(colorORIGIN.r,colorORIGIN.g),colorORIGIN.b);\n"
"  vec3 colorHP = max(colorORIGIN.rgb - paramSubMul.x, 0.f);\n"
"  float colorHPmax = max(max(colorHP.r, colorHP.g), colorHP.b);\n"
"  float colorHPmax2 = colorHPmax * colorHPmax;\n"
"  float factor = paramSubMul.y * colorHPmax2 / (0.25f + colorHPmax2);\n"
"  color = vec4(factor * colorORIGIN, 1.f);\n"
"}\n";

static const char * SourcePostProcess_FragMain_BrightPass_HDR =
"\n"
"uniform vec2 paramSubMul;\n"
"const vec3 grayFactors = vec3(0.2125f, 0.7154f, 0.0721f);\n"
"void main(){\n"
"  vec3 colorORIGIN = texture(TexDiffuse,pixelUV).xyz;\n"
"  vec3 colorHP = max(colorORIGIN.rgb - paramSubMul.x, 0.f);\n"
"  float grayHP = dot(colorHP, grayFactors);\n"
"  float grayHP2 = grayHP * grayHP;\n"
"  float factor = paramSubMul.y * grayHP2 / (0.33f + grayHP2);\n"
"  color = vec4(factor * colorORIGIN, 1.f);\n"
"}\n";

static const char * SourcePostProcess_FragMain_Blur =
"\n"
"//uniform vec2 AtlasInvDim; (declared from shader layout)\n"
"const float gaussFilter[49] = float[49]( // kernel=7, sigma=3 \n"
"0.011362, 0.014962, 0.017649, 0.018648, 0.017649, 0.014962, 0.011362,\n"
"0.014962, 0.019703, 0.02324 , 0.024556, 0.02324 , 0.019703, 0.014962,\n"
"0.017649, 0.02324 , 0.027413, 0.028964, 0.027413, 0.02324 , 0.017649,\n"
"0.018648, 0.024556, 0.028964, 0.030603, 0.028964, 0.024556, 0.018648,\n"
"0.017649, 0.02324 , 0.027413, 0.028964, 0.027413, 0.02324 , 0.017649,\n"
"0.014962, 0.019703, 0.02324 , 0.024556, 0.02324 , 0.019703, 0.014962,\n"
"0.011362, 0.014962, 0.017649, 0.018648, 0.017649, 0.014962, 0.011362);\n"
"\n"
"void main(){\n"
"  vec4 colorBlur = vec4(0.);\n"
"  for (int i=0;i<7;++i) {\n"
"    for (int j=0;j<7;++j) {\n"
"      vec2 offsetUV = vec2((i-3),(j-3)) * AtlasInvDim;\n"
"      colorBlur += gaussFilter[i*7+j] * texture(TexDiffuse,pixelUV+offsetUV);\n"
"  } }\n"
"  color = colorBlur;\n"
"}\n";

static const char * SourcePostProcess_FragMain_Combine =
"void main(){\n"
"  vec3 colorMain = texture(TexDiffuse,pixelUV).xyz;\n"
"  vec3 colorEff1 = texture(TexDiffuseB,pixelUV).xyz;\n"
"  color = vec4(colorMain + colorEff1, 1.f);\n"
"}\n";

// ----------------------------------------------------------------------------

static int integerPrevPowerOfTwo(int value)
{
  TRE_ASSERT(value > 0);
  int count = 0;
  while ((value = value >> 1) != 0) ++count;
  return (0x1 << count);
}

static void computeDownChainSize(std::vector<glm::ivec2> &sizes, const int startWidth, const int startHeigth)
{
  TRE_ASSERT(!sizes.empty());
  TRE_ASSERT(pow(2,sizes.size()) < startWidth);
  TRE_ASSERT(pow(2,sizes.size()) < startHeigth);

  sizes[0] = glm::ivec2(startWidth, startHeigth);

  if (sizes.size() >= 2)
  {
    sizes[1] = glm::ivec2(integerPrevPowerOfTwo(startWidth), integerPrevPowerOfTwo(startHeigth));
  }
  for (uint i = 2; i < sizes.size(); ++i)
  {
    sizes[i] = glm::max(sizes[i - 1] / 2, glm::ivec2(1));
  }
};

// ----------------------------------------------------------------------------

bool postFX_Blur::load(const int pwidth, const int pheigth)
{
  bool status = true;
  TRE_ASSERT(m_Npass == m_renderDownsample.size() && m_Npass == m_renderInvScreenSize.size());
  TRE_ASSERT( pow(2,m_renderDownsample.size()) < pwidth );
  TRE_ASSERT( pow(2,m_renderDownsample.size()) < pheigth );
  // Compute render size
  std::vector<glm::ivec2> downSizes;
  downSizes.resize(m_Npass);
  computeDownChainSize(downSizes, pwidth, pheigth);
  // Load downsampling FBOs
  for (uint i = 0; i < m_Npass; ++i)
  {
    status &= m_renderDownsample[i].load(downSizes[i].x, downSizes[i].y);
    m_renderInvScreenSize[i] = glm::vec2(1.f/float(downSizes[i].x),1.f/float(downSizes[i].y));
  }
  // Shaders
  shader::s_layout shaderLayout(shader::PRGM_2D);
  shaderLayout.hasBUF_UV = true;
  shaderLayout.hasSMP_Diffuse = true;
  shaderLayout.hasOUT_Color0 = true;
  status &= m_shaderBrightPass.loadCustomShader(shaderLayout,
                                              (m_isOutHDR) ? SourcePostProcess_FragMain_BrightPass_HDR : SourcePostProcess_FragMain_BrightPass_LDR,
                                              (m_isOutHDR) ? "PostProcess_Fragment_BrightPass_HDR" : "PostProcess_Fragment_BrightPass_LDR");
  shaderLayout.hasUNI_AtlasInvDim = true;
  status &= m_shaderDownSample.loadCustomShader(shaderLayout,
                                              SourcePostProcess_FragMain_Blur,
                                              "PostProcess_Fragment_Blur");
  shaderLayout.hasUNI_AtlasInvDim = false;
  shaderLayout.hasSMP_DiffuseB = true;
  status &= m_shaderCombine.loadCustomShader(shaderLayout,
                                           SourcePostProcess_FragMain_Combine,
                                           "PostProcess_Fragment_BlurCombine");
  // Model
  const glm::vec4 pos(-1.f, -1.f, 1.f, 1.f);
  const glm::vec4 uv(0.f, 0.f, 1.f, 1.f);
  const glm::vec4 color(1.f);

  const uint partId = m_quadFullScreen.createPart(6);
  m_quadFullScreen.fillDataRectangle(partId, 0, pos, color, uv);

  status &= m_quadFullScreen.loadIntoGPU();

  return status;
}

// ----------------------------------------------------------------------------

bool postFX_Blur::resize(const int pwidth, const int pheigth)
{
  TRE_ASSERT(m_Npass == m_renderDownsample.size() && m_Npass == m_renderInvScreenSize.size());
  TRE_ASSERT( pow(2,m_renderDownsample.size()) < pwidth );
  TRE_ASSERT( pow(2,m_renderDownsample.size()) < pheigth );

  if (m_renderDownsample[0].w() == pwidth && m_renderDownsample[0].h() == pheigth)
    return true;

  // Compute render size
  std::vector<glm::ivec2> downSizes;
  downSizes.resize(m_Npass);
  computeDownChainSize(downSizes, pwidth, pheigth);

  // Resize FBOs
  for (uint i = 0; i < m_Npass; ++i)
  {
    m_renderDownsample[i].resize(downSizes[i].x, downSizes[i].y);
    m_renderInvScreenSize[i] = glm::vec2(1.f/float(downSizes[i].x),1.f/float(downSizes[i].y));
  }

  return true;
}

// ----------------------------------------------------------------------------

void postFX_Blur::clear()
{
  // FBOs
  for (renderTarget & curFBO : m_renderDownsample)
    curFBO.clear();
  // Shaders
  m_shaderBrightPass.clearShader();
  m_shaderDownSample.clearShader();
  m_shaderCombine.clearShader();
  // Model
  m_quadFullScreen.clearGPU();
}

// ----------------------------------------------------------------------------

void postFX_Blur::resolveBlur(GLuint inputTextureHandle, const int outwidth, const int outheigth)
{
  _blur_passBright(inputTextureHandle);
  _blur_passBlur();

  // Final
  {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, outwidth, outheigth);

    glUseProgram(m_shaderCombine.m_drawProgram);
    glUniform1i(m_shaderCombine.getUniformLocation(tre::shader::TexDiffuse),0);
    glUniform1i(m_shaderCombine.getUniformLocation(tre::shader::TexDiffuseB),1);

    m_quadFullScreen.drawcallAll(false);
  }
}

// ----------------------------------------------------------------------------

void postFX_Blur::resolveBlur(GLuint inputTextureHandle, renderTarget &targetFBO)
{
  _blur_passBright(inputTextureHandle);
  _blur_passBlur();

  // Final
  {
    targetFBO.bindForWritting();
    glViewport(0, 0, targetFBO.w(), targetFBO.h());

    glUseProgram(m_shaderCombine.m_drawProgram);
    glUniform1i(m_shaderCombine.getUniformLocation(tre::shader::TexDiffuse),0);
    glUniform1i(m_shaderCombine.getUniformLocation(tre::shader::TexDiffuseB),1);

    m_quadFullScreen.drawcallAll(false);
  }
}

// ----------------------------------------------------------------------------

void postFX_Blur::_blur_passBright(GLuint inputTextureHandle)
{
  TRE_ASSERT(m_Npass>0);

  glDisable(GL_DEPTH_TEST);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, inputTextureHandle);

  // Begin - separate
  {
    m_renderDownsample[0].bindForWritting();
    glViewport(0, 0, m_renderDownsample[0].w(), m_renderDownsample[0].h());

    glUseProgram(m_shaderBrightPass.m_drawProgram);
    glUniform1i(m_shaderBrightPass.getUniformLocation(tre::shader::TexDiffuse),0);
    glUniform2fv(m_shaderBrightPass.getUniformLocation("paramSubMul"),1,glm::value_ptr(m_paramBrightPass));
    m_quadFullScreen.drawcallAll(true);
  }
}

// ----------------------------------------------------------------------------

void postFX_Blur::_blur_passBlur()
{
  // Intermediate - blur it
  if (m_Npass > 1)
  {
    glActiveTexture(GL_TEXTURE1);

    glUseProgram(m_shaderDownSample.m_drawProgram);
    glUniform1i(m_shaderDownSample.getUniformLocation(tre::shader::TexDiffuse),1);

    for (uint ipass=1;ipass < m_Npass;++ipass)
    {
      m_renderDownsample[ipass].bindForWritting();
      glViewport(0, 0, m_renderDownsample[ipass].w(), m_renderDownsample[ipass].h());

      glUniform2fv(m_shaderDownSample.getUniformLocation(tre::shader::AtlasInvDim),1,glm::value_ptr(m_renderInvScreenSize[ipass]));

      glBindTexture(GL_TEXTURE_2D,m_renderDownsample[ipass-1].colorHandle());
      m_quadFullScreen.drawcallAll(false);
    }
  }

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D,m_renderDownsample[m_Npass-1].colorHandle());
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
"  vec3 colorMAPPED = 1.f - exp(-colorORIGIN * paramsToneMapping.x);\n"
"  //vec3 colorGAMMA = pow(colorMAPPED, 1.f/paramsToneMapping.y);\n"
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
  result &= m_shaderToneMap.loadCustomShader(shaderLayout,SourcePostProcess_FragMain_ToneMapping,"PostProcess_Fragment_ToneMapping");
  // Model
  const glm::vec4 pos(-1.f, -1.f, 1.f, 1.f);
  const glm::vec4 uv(0.f, 0.f, 1.f, 1.f);
  const glm::vec4 color(1.f);

  const uint partId = m_quadFullScreen.createPart(6);
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
