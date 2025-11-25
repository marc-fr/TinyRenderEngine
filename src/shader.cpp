#include "tre_shader.h"

#ifdef TRE_DEBUG
#include <fstream>
#endif

namespace tre {

bool shader::loadShader(e_category cat, int flags, const char *pname)
{
  TRE_ASSERT(m_drawProgram == 0);

  //-- Layout

  if (flags & PRGM_SHADOW_SUN) flags |= PRGM_LIGHT_SUN;
  if (flags & PRGM_SHADOW_PTS) flags |= PRGM_LIGHT_PTS;

  m_layout = s_layout(cat, flags);

  //-- Create the shader source
  std::string sourceVert,sourceFrag;
  sourceVert.reserve(4096);
  sourceFrag.reserve(4096);

  createShaderSource_Layout(sourceVert, sourceFrag);

  createShaderSource_VertexMain(sourceVert);
  createShaderSource_FragmentMain(flags, sourceFrag);

  //-- compile and load it

  compute_name(cat, flags, pname);

  return linkProgram(sourceVert.data(), sourceFrag.data());
}

// ----------------------------------------------------------------------------

bool shader::loadShaderWireframe(e_category cat, int flags, const char *pname)
{
  TRE_ASSERT(m_drawProgram == 0);

  //-- Layout

  m_layout = s_layout(cat, flags);
  m_layout.hasPIP_Geom = true;

  //-- Create the shader source
  std::string sourceVert,sourceGeom, sourceFrag;
  sourceVert.reserve(4096);
  sourceGeom.reserve(4096);
  sourceFrag.reserve(4096);

  createShaderSource_Layout(sourceVert, sourceFrag);

  createShaderSource_VertexMain(sourceVert);
  createShaderSource_GeomWireframe(sourceGeom);
  createShaderSource_FragmentMain(flags, sourceFrag);

  //-- compile and load it

  compute_name(cat, flags, pname);
  if (pname == nullptr) m_name += "_Wireframe";

  return linkProgram(sourceVert.data(), sourceGeom.data(), sourceFrag.data());
}

// ----------------------------------------------------------------------------

bool shader::loadCustomShader(const s_layout & shaderLayout , const char * sourceMainFrag, const char * pname)
{
  TRE_ASSERT(m_drawProgram == 0);

  //-- Layout

  m_layout = shaderLayout;

  //-- Do some corrections

  m_layout.hasBUF_Normal |= m_layout.hasBUF_TangentU;
  m_layout.hasUNI_MModel |= m_layout.hasPIX_Position || m_layout.hasBUF_Normal;
  m_layout.hasUNI_MView  |= m_layout.hasPIX_Normal || m_layout.hasUBO_ptslight || m_layout.hasUBO_sunlight;

  //-- Create the shader source

  std::string sourceVert,sourceFrag;
  sourceVert.reserve(4096);
  sourceFrag.reserve(4096);

  createShaderSource_Layout(sourceVert, sourceFrag);
  createShaderSource_VertexMain(sourceVert);
  TRE_ASSERT(sourceMainFrag != nullptr);
  sourceFrag += sourceMainFrag;

  //-- compile and load it

  compute_name(m_layout.category, 0, pname);

  return linkProgram(sourceVert.data(), sourceFrag.data());
}

// ----------------------------------------------------------------------------

bool shader::loadCustomShaderVF(const s_layout &shaderLayout, const char *sourceMainVert, const char *sourceMainFrag, const char *pname)
{
  TRE_ASSERT(m_drawProgram == 0);

  //-- Layout

  m_layout = shaderLayout;

  //-- Do some corrections

  m_layout.hasBUF_Normal |= m_layout.hasBUF_TangentU;
  m_layout.hasUNI_MModel |= m_layout.hasPIX_Position || m_layout.hasBUF_Normal;
  m_layout.hasUNI_MView  |= m_layout.hasPIX_Normal;

  //-- Create the shader source

  std::string sourceVert,sourceFrag;
  sourceVert.reserve(4096);
  sourceFrag.reserve(4096);

  createShaderSource_Layout(sourceVert, sourceFrag);
  TRE_ASSERT(sourceMainVert != nullptr);
  sourceVert += sourceMainVert;
  TRE_ASSERT(sourceMainFrag != nullptr);
  sourceFrag += sourceMainFrag;

  //-- compile and load it

  compute_name(m_layout.category, 0, pname);

  return linkProgram(sourceVert.data(), sourceFrag.data());
}

// ----------------------------------------------------------------------------

bool shader::loadCustomShaderGF(const s_layout & shaderLayout , const char * sourceFullGeom, const char * sourceFullFrag, const char * pname)
{
  TRE_ASSERT(m_drawProgram == 0);

  //-- Layout

  m_layout = shaderLayout;
  m_layout.hasPIP_Geom = true;

  //-- Do some corrections

  m_layout.hasBUF_Normal |= m_layout.hasBUF_TangentU;
  m_layout.hasUNI_MModel |= m_layout.hasPIX_Position || m_layout.hasBUF_Normal;
  m_layout.hasUNI_MView  |= m_layout.hasPIX_Normal;

  // Create the shader source

  std::string sourceVert, sourceGeom, sourceFrag;
  sourceVert.reserve(4096);
  sourceGeom.reserve(4096);
  sourceFrag.reserve(4096);

  createShaderSource_Layout(sourceVert, sourceFrag);
  createShaderSource_VertexMain(sourceVert);

  TRE_ASSERT(sourceFullGeom != nullptr);
  sourceGeom = sourceFullGeom;
  TRE_ASSERT(sourceFullFrag != nullptr);
  sourceFrag = sourceFullFrag;

  //-- compile and load it

  compute_name(m_layout.category, 0, pname);

#ifdef TRE_OPENGL_ES
  TRE_LOG("shader::loadCustomShaderGF : OpenGL ES does not have GEOMETRY_SHADER. Skip shader " << m_name);
  return false;
#endif

  return linkProgram(sourceVert.data(), sourceGeom.data(), sourceFrag.data());
}

// ----------------------------------------------------------------------------

bool shader::loadCustomShaderVGF(const s_layout &shaderLayout, const char *sourceMainVert, const char *sourceFullGeom, const char *sourceFullFrag, const char *pname)
{
  //-- Layout

  m_layout = shaderLayout;
  m_layout.hasPIP_Geom = true;

  //-- Do some corrections

  m_layout.hasBUF_Normal |= m_layout.hasBUF_TangentU;
  m_layout.hasUNI_MModel |= m_layout.hasPIX_Position || m_layout.hasBUF_Normal;
  m_layout.hasUNI_MView  |= m_layout.hasPIX_Normal;

  // Create the shader source

  std::string sourceVert, sourceGeom, sourceFrag;
  sourceVert.reserve(4096);
  sourceGeom.reserve(4096);
  sourceFrag.reserve(4096);

  createShaderSource_Layout(sourceVert, sourceFrag);
  sourceFrag.clear();
  TRE_ASSERT(sourceMainVert != nullptr);
  sourceVert += sourceMainVert;
  TRE_ASSERT(sourceFullGeom != nullptr);
  sourceGeom += sourceFullGeom;
  TRE_ASSERT(sourceFullFrag != nullptr);
  sourceFrag += sourceFullFrag;

  //-- compile and load it

  compute_name(m_layout.category, 0, pname);

#ifdef TRE_OPENGL_ES
  TRE_LOG("shader::loadCustomShaderVGF : OpenGL ES does not have GEOMETRY_SHADER. Skip shader " << m_name);
  return false;
#endif

  return linkProgram(sourceVert.data(), sourceGeom.data(), sourceFrag.data());
}

// ----------------------------------------------------------------------------

void shader::clearShader()
{
  if (m_drawProgram!=0) glDeleteProgram(m_drawProgram);
  m_drawProgram = 0;
}

// ----------------------------------------------------------------------------

GLint shader::getUniformLocation(const uniformname utype) const
{
  const int itype = (const int)utype;
  TRE_ASSERT(itype>=0 && itype<NCOMUNIFORMVAR);

  GLint * puloc = const_cast<GLint*>(& m_uniformVars[itype]);

  TRE_ASSERT(!(utype==TexShadowSun0 && m_shadowSun_count < 1));
  TRE_ASSERT(!(utype==TexShadowSun1 && m_shadowSun_count < 2));
  TRE_ASSERT(!(utype==TexShadowSun2 && m_shadowSun_count < 3));
  TRE_ASSERT(!(utype==TexShadowSun3 && m_shadowSun_count < 4));
  TRE_ASSERT(!(utype==TexShadowPts0 && m_shadowPts_count < 1));

  if ( *puloc != -2 ) return *puloc;

  if      (utype==MPVM)          *puloc = glGetUniformLocation(m_drawProgram, "MPVM");
  else if (utype==MView)         *puloc = glGetUniformLocation(m_drawProgram, "MView");
  else if (utype==MModel)        *puloc = glGetUniformLocation(m_drawProgram, "MModel");
  else if (utype==MOrientation)  *puloc = glGetUniformLocation(m_drawProgram, "MOrientation");
  else if (utype==uniBlend)      *puloc = glGetUniformLocation(m_drawProgram, "uniBlend");
  else if (utype==uniColor)      *puloc = glGetUniformLocation(m_drawProgram, "uniColor");
  else if (utype==uniMat)        *puloc = glGetUniformLocation(m_drawProgram, "uniMat");
  else if (utype==AtlasInvDim)   *puloc = glGetUniformLocation(m_drawProgram, "AtlasInvDim");
  else if (utype==SoftDistance)  *puloc = glGetUniformLocation(m_drawProgram, "SoftDistance");
  else if (utype==TexDiffuse)    *puloc = glGetUniformLocation(m_drawProgram, "TexDiffuse");
  else if (utype==TexDiffuseB)   *puloc = glGetUniformLocation(m_drawProgram, "TexDiffuseB");
  else if (utype==TexCube)       *puloc = glGetUniformLocation(m_drawProgram, "TexCube");
  else if (utype==TexCubeB)      *puloc = glGetUniformLocation(m_drawProgram, "TexCubeB");
  else if (utype==TexNormal)     *puloc = glGetUniformLocation(m_drawProgram, "TexNormal");
  else if (utype==TexMat)        *puloc = glGetUniformLocation(m_drawProgram, "TexMat");
  else if (utype==TexShadowSun0) *puloc = glGetUniformLocation(m_drawProgram, "TexShadowSun0");
  else if (utype==TexShadowSun1) *puloc = glGetUniformLocation(m_drawProgram, "TexShadowSun1");
  else if (utype==TexShadowSun2) *puloc = glGetUniformLocation(m_drawProgram, "TexShadowSun2");
  else if (utype==TexShadowSun3) *puloc = glGetUniformLocation(m_drawProgram, "TexShadowSun3");
  else if (utype==TexShadowPts0) *puloc = glGetUniformLocation(m_drawProgram, "TexShadowPts0");
  else if (utype==TexDepth)      *puloc = glGetUniformLocation(m_drawProgram, "TexDepth");
  else if (utype==TexAO)         *puloc = glGetUniformLocation(m_drawProgram, "TexAO");
  else
  {
    TRE_FATAL("not reached");
  }

  return *puloc;
}

// ----------------------------------------------------------------------------

GLint shader::getUniformLocation(const char * uname) const
{
  return glGetUniformLocation(m_drawProgram, uname);
}

// ----------------------------------------------------------------------------

void shader::setUniformMatrix(const glm::mat3 &MPVM, const glm::mat3 &MModel) const
{
  TRE_ASSERT(m_layout.category == PRGM_2D);
  if (m_layout.hasUNI_MPVM)
    glUniformMatrix3fv(getUniformLocation(shader::MPVM), 1, GL_FALSE, glm::value_ptr(MPVM));
  if (m_layout.hasUNI_MModel)
    glUniformMatrix3fv(getUniformLocation(shader::MModel), 1, GL_FALSE, glm::value_ptr(MModel));
  TRE_ASSERT(!m_layout.hasUNI_MView);
}

// ----------------------------------------------------------------------------

void shader::setUniformMatrix(const glm::mat4 &MPVM, const glm::mat4 &MModel, const glm::mat4 &MView) const
{
  TRE_ASSERT(m_layout.category == PRGM_3D || m_layout.category == PRGM_3D_DEPTH || m_layout.category == PRGM_2Dto3D);
  if (m_layout.hasUNI_MPVM)
    glUniformMatrix4fv(getUniformLocation(shader::MPVM), 1, GL_FALSE, glm::value_ptr(MPVM));
  if (m_layout.hasUNI_MModel)
    glUniformMatrix4fv(getUniformLocation(shader::MModel), 1, GL_FALSE, glm::value_ptr(MModel));
  if (m_layout.hasUNI_MView)
    glUniformMatrix4fv(getUniformLocation(shader::MView), 1, GL_FALSE, glm::value_ptr(MView));
}

// ----------------------------------------------------------------------------

void shader::setShadowSunSamplerCount(uint count)
{
  TRE_ASSERT(m_drawProgram == 0); // cannot be changed dynamically after compilation.
  TRE_ASSERT(count <= SHADOW_SUN_MAX);
  m_shadowSun_count = count;
}

// ----------------------------------------------------------------------------

void shader::setShadowPtsSamplerCount(uint count)
{
  TRE_ASSERT(m_drawProgram == 0); // cannot be changed dynamically after compilation.
  TRE_ASSERT(count <= 1);
  m_shadowPts_count = count;
}

// ----------------------------------------------------------------------------

bool shader::activeUBO_sunLight()
{
  TRE_ASSERT(m_drawProgram!=0);

  UBOhandle_sunLight.create(sizeof(s_UBOdata_sunLight)); // create if needed

  const GLuint index = glGetUniformBlockIndex(m_drawProgram, "s_sunlight");
  glUniformBlockBinding(m_drawProgram, index, UBOhandle_sunLight.m_bindpoint);
  return IsOpenGLok("shader::activeUBO_sunLight");
}

// ----------------------------------------------------------------------------

bool shader::activeUBO_sunShadow()
{
  TRE_ASSERT(m_drawProgram!=0);

  UBOhandle_sunShadow.create(sizeof(s_UBOdata_sunShadow)); // create if needed

  const GLuint index = glGetUniformBlockIndex(m_drawProgram, "s_sunshadow");
  glUniformBlockBinding(m_drawProgram, index, UBOhandle_sunShadow.m_bindpoint);
  return IsOpenGLok("shader::activeUBO_sunShadow");
}

// ----------------------------------------------------------------------------

bool shader::activeUBO_ptsLight()
{
  TRE_ASSERT(m_drawProgram!=0);

  UBOhandle_ptsLight.create(sizeof(s_UBOdata_ptstLight)); // create if needed

  const GLuint index = glGetUniformBlockIndex(m_drawProgram, "s_ptslight");
  glUniformBlockBinding(m_drawProgram, index, UBOhandle_ptsLight.m_bindpoint);
  return IsOpenGLok("shader::activeUBO_ptsLight");
}

// ----------------------------------------------------------------------------

bool shader::activeUBO_ptsShadow()
{
  TRE_ASSERT(m_drawProgram!=0);

  UBOhandle_ptsShadow.create(sizeof(s_UBOdata_ptsShadow)); // create if needed

  const GLuint index = glGetUniformBlockIndex(m_drawProgram, "s_ptsshadow");
  glUniformBlockBinding(m_drawProgram, index, UBOhandle_ptsShadow.m_bindpoint);
  return IsOpenGLok("shader::activeUBO_ptsShadow");
}

// ----------------------------------------------------------------------------

void shader::clearUBO()
{
  UBOhandle_sunLight.clear();
  UBOhandle_sunShadow.clear();
  UBOhandle_ptsLight.clear();
  UBOhandle_ptsShadow.clear();
}

// ----------------------------------------------------------------------------

bool shader::activeUBO_custom(const s_UBOhandle &ubo, const char *name)
{
  TRE_ASSERT(m_drawProgram!=0);
  TRE_ASSERT(ubo.m_bindpoint != GLuint(-1) && ubo.m_buffersize != 0);

  const GLuint index = glGetUniformBlockIndex(m_drawProgram, name);
  glUniformBlockBinding(m_drawProgram, index, ubo.m_bindpoint);
  return IsOpenGLok("shader::activeUBO_custom");
}

// ----------------------------------------------------------------------------

bool shader::linkProgram(const char *sourceVert, const char *sourceFrag)
{
#ifdef TRE_DEBUG
  const std::string outfilename = std::string("ShaderSource_") + m_name + std::string(".txt");
  std::ofstream outfile(outfilename);
  outfile << "====== Vertex Shader: --------------" << std::endl << sourceVert << std::endl;
  outfile << "====== Fragment Shader: --------------" << std::endl << sourceFrag << std::endl;
  outfile.close();
#endif

  // Create the shaders
  GLuint shaderV = glCreateShader(GL_VERTEX_SHADER);
  GLuint shaderF = glCreateShader(GL_FRAGMENT_SHADER);

  bool compil = true;
  compil &= compileShader(shaderV,GL_VERTEX_SHADER,sourceVert);
  compil &= compileShader(shaderF,GL_FRAGMENT_SHADER,sourceFrag);
  if (!compil) return false;

  // Link program
  {
    m_drawProgram = glCreateProgram();
    glAttachShader(m_drawProgram, shaderV);
    glAttachShader(m_drawProgram, shaderF);
    glLinkProgram(m_drawProgram);

    GLint Result = GL_FALSE;
    glGetProgramiv(m_drawProgram, GL_LINK_STATUS, &Result);
    if (Result == GL_FALSE)
    {
#ifdef TRE_PRINTS
      int infoLogLength;
      glGetProgramiv(m_drawProgram, GL_INFO_LOG_LENGTH, &infoLogLength);
      if (infoLogLength > 0)
      {
        char *programErrorMessage = new char[infoLogLength + 1];
        programErrorMessage[infoLogLength] = 0x00;
        glGetProgramInfoLog(m_drawProgram, infoLogLength, nullptr, programErrorMessage);
        TRE_LOG(programErrorMessage);
        delete[] programErrorMessage;
      }
      TRE_LOG("Link shaders program failed (ID="<<m_drawProgram<<", name="<<m_name<<")");
#endif
      return false;
    }
  }

  TRE_LOG("Link shaders program done (ID="<<m_drawProgram<<", name="<<m_name<<")");

  // end
  glDetachShader(m_drawProgram, shaderV);
  glDetachShader(m_drawProgram, shaderF);

  glDeleteShader(shaderV);
  glDeleteShader(shaderF);

  // UBOs
  bool status = true;
  if (m_layout.hasUBO_sunlight) status &= activeUBO_sunLight();
  if (m_layout.hasUBO_sunshadow) status &= activeUBO_sunShadow();
  if (m_layout.hasUBO_ptslight) status &= activeUBO_ptsLight();
  if (m_layout.hasUBO_ptsshadow) status &= activeUBO_ptsShadow();

  return status;
}

// ----------------------------------------------------------------------------

bool shader::linkProgram(const char *sourceVert, const char *sourceGeom, const char *sourceFrag)
{
#ifdef TRE_DEBUG
  const std::string outfilename = std::string("ShaderSource_") + m_name + std::string(".txt");
  std::ofstream outfile(outfilename);
  outfile << "====== Vertex Shader: --------------" << std::endl << sourceVert << std::endl;
  outfile << "====== Geometry Shader: --------------" << std::endl << sourceGeom << std::endl;
  outfile << "====== Fragment Shader: --------------" << std::endl << sourceFrag << std::endl;
  outfile.close();
#endif

  // Create the shaders
  GLuint shaderV = glCreateShader(GL_VERTEX_SHADER);
  GLuint shaderG = glCreateShader(GL_GEOMETRY_SHADER);
  GLuint shaderF = glCreateShader(GL_FRAGMENT_SHADER);

  bool compil = true;
  compil &= compileShader(shaderV,GL_VERTEX_SHADER,sourceVert);
  compil &= compileShader(shaderG,GL_GEOMETRY_SHADER,sourceGeom);
  compil &= compileShader(shaderF,GL_FRAGMENT_SHADER,sourceFrag);
  if (!compil) return false;

  // Link program
  {
    m_drawProgram = glCreateProgram();
    glAttachShader(m_drawProgram, shaderV);
    glAttachShader(m_drawProgram, shaderG);
    glAttachShader(m_drawProgram, shaderF);
    glLinkProgram(m_drawProgram);

    GLint Result = GL_FALSE;
    glGetProgramiv(m_drawProgram, GL_LINK_STATUS, &Result);
    if (Result == GL_FALSE)
    {
#ifdef TRE_PRINTS
      int infoLogLength;
      glGetProgramiv(m_drawProgram, GL_INFO_LOG_LENGTH, &infoLogLength);
      if (infoLogLength > 0)
      {
        char *programErrorMessage = new char[infoLogLength + 1];
        programErrorMessage[infoLogLength] = 0x00;
        glGetProgramInfoLog(m_drawProgram, infoLogLength, nullptr, programErrorMessage);
        TRE_LOG(programErrorMessage);
        delete[] programErrorMessage;
      }
      TRE_LOG("Link shaders program failed (ID="<<m_drawProgram<<", name="<<m_name<<")");
#endif
      return false;
    }
  }

  TRE_LOG("Link shaders program done (ID="<<m_drawProgram<<", name="<<m_name<<")");

  // end
  glDetachShader(m_drawProgram, shaderV);
  glDetachShader(m_drawProgram, shaderG);
  glDetachShader(m_drawProgram, shaderF);

  glDeleteShader(shaderV);
  glDeleteShader(shaderG);
  glDeleteShader(shaderF);

  // UBOs
  bool status = true;
  if (m_layout.hasUBO_sunlight) status &= activeUBO_sunLight();
  if (m_layout.hasUBO_sunshadow) status &= activeUBO_sunShadow();
  if (m_layout.hasUBO_ptslight) status &= activeUBO_ptsLight();
  if (m_layout.hasUBO_ptsshadow) status &= activeUBO_ptsShadow();

  return true;
}

// ----------------------------------------------------------------------------

bool shader::compileShader(GLuint & shaderHandle, const GLenum shaderType, const char *shaderSource)
{
  glShaderSource(shaderHandle, 1, & shaderSource , nullptr);
  glCompileShader(shaderHandle);

  GLint Result = GL_FALSE;
  glGetShaderiv(shaderHandle, GL_COMPILE_STATUS, &Result);
  if (Result == GL_FALSE)
  {
#ifdef TRE_PRINTS
    int infoLogLength;
    glGetShaderiv(shaderHandle, GL_INFO_LOG_LENGTH, &infoLogLength);
    if (infoLogLength > 0)
    {
      char *programErrorMessage = new char[infoLogLength + 1];
      programErrorMessage[infoLogLength] = 0x00;
      glGetShaderInfoLog(shaderHandle, infoLogLength, nullptr, programErrorMessage);
      TRE_LOG(programErrorMessage);
      delete[] programErrorMessage;
    }
    if      (shaderType==GL_VERTEX_SHADER  ) { TRE_LOG("Vertex-shader building failed (part of "  << m_name << ")"); }
    else if (shaderType==GL_GEOMETRY_SHADER) { TRE_LOG("Geometry-shader building failed (part of "<< m_name << ")"); }
    else if (shaderType==GL_FRAGMENT_SHADER) { TRE_LOG("Fragment-shader building failed (part of "<< m_name << ")"); }
    else                                     { TRE_LOG("Generic-shader building failed (part of " << m_name << ")"); }
#endif
    return false;
  }
  return true;
}

// ----------------------------------------------------------------------------

void shader::compute_name(e_category cat, int flags, const char * pname)
{
  if (pname != nullptr)
  {
    m_name = pname;
    return;
  }

  m_name.clear();
  m_name.reserve(64);

  if (cat == PRGM_2D) m_name = "2d";
  if (cat == PRGM_3D) m_name = "3d";
  if (cat == PRGM_2Dto3D) m_name = "2dto3d";
  if (cat == PRGM_3D_DEPTH) { m_name = "3ddepth"; return; }

  m_name += "_COLOR";
  if (flags & PRGM_UNICOLOR) m_name += "u";
  if (flags & PRGM_COLOR) m_name += "c";
  if (flags & PRGM_TEXTURED) m_name += "t";
  if (flags & PRGM_CUBEMAPED) m_name += "3";
  if (flags & PRGM_ATLAS) m_name += "a";
  if (flags & PRGM_BLEND) m_name += "b";

  if (flags & PRGM_MASK_LIGHT) m_name += "_LIT";
  if (flags & PRGM_MODELPHONG) m_name += "phong";
  else                         m_name += "ggx";
  if (flags & PRGM_MAPMAT) m_name += "m";
  if (flags & PRGM_MAPNORMAL) m_name += "n";

  std::string pre_light;
  if (flags & PRGM_LIGHT_SUN) pre_light += "s";
  if (flags & PRGM_LIGHT_PTS) pre_light += "p";
  if (flags & PRGM_AO) pre_light += "o";
  if (!pre_light.empty()) pre_light = "_LIGHT" + pre_light;
  m_name += pre_light;

  std::string pre_shadow;
  if (flags & PRGM_SHADOW_SUN) pre_shadow += "s";
  if (flags & PRGM_SHADOW_PTS) pre_shadow += "p";
  if (!pre_shadow.empty())  pre_shadow = "_SHADOW" + pre_shadow;
  m_name += pre_shadow;

  if (flags & PRGM_INSTANCED)
  {
    m_name += "_INSTp";
    if (flags & PRGM_ORIENTATION) m_name += "o";
    if (flags & PRGM_ROTATION) m_name += "r";
    if (flags & PRGM_INSTCOLOR) m_name += "c";
  }

  if (flags & PRGM_SOFT)
    m_name += "_Soft";
}

// ----------------------------------------------------------------------------

void shader::s_UBOhandle::create(uint sizeofdata)
{
  if (m_handle!=0) { TRE_ASSERT(m_buffersize==sizeofdata);  return; }
  m_buffersize = sizeofdata;

  glGenBuffers(1, &m_handle);
  glBindBuffer(GL_UNIFORM_BUFFER, m_handle);
  glBufferData(GL_UNIFORM_BUFFER, m_buffersize, nullptr, GL_STATIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  IsOpenGLok("shader::UBOhandle::create buffer");

  if (m_bindpoint==GLuint(-1)) m_bindpoint = UBObindpoint_incr++;
  glBindBufferBase(GL_UNIFORM_BUFFER, m_bindpoint, m_handle);
  //glBindBufferRange(GL_UNIFORM_BUFFER, m_bindpoint, m_handle, 0, m_buffersize); // note: in fact, the buffer can contain more than 1 UBO-block ...
  IsOpenGLok("shader::UBOhandle::create bind-point");
}

// ----------------------------------------------------------------------------

void shader::s_UBOhandle::update(const void *data)
{
  TRE_ASSERT(m_handle!=0 && m_bindpoint!=GLuint(-1));
  glBindBuffer(GL_UNIFORM_BUFFER, m_handle);
  glBufferData(GL_UNIFORM_BUFFER, m_buffersize, data, GL_STATIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  IsOpenGLok("shader::UBOhandle::update");
}

// ----------------------------------------------------------------------------

void shader::s_UBOhandle::clear()
{
  if (m_handle!=0) glDeleteBuffers(1, &m_handle);
  m_handle = 0;
  m_buffersize = 0;
}

// ----------------------------------------------------------------------------

GLuint shader::UBObindpoint_incr = 0;

shader::s_UBOhandle shader::UBOhandle_sunLight = shader::s_UBOhandle();

shader::s_UBOhandle shader::UBOhandle_sunShadow = shader::s_UBOhandle();

shader::s_UBOhandle shader::UBOhandle_ptsLight = shader::s_UBOhandle();

shader::s_UBOhandle shader::UBOhandle_ptsShadow = shader::s_UBOhandle();

} // namespace
