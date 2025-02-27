#ifndef MODEL_H
#define MODEL_H

#include "tre_utils.h"

#include <vector>
#include <string>
#include <iostream>

namespace tre {

//=============================================================================

/**
 * @brief s_modelDataLayout holds semantics on mesh data
 * It holds semantics on mesh data, such as the the vertice position ...
 * It describes also the "instanced" semantics.
 * This is the main interface when manipulating meshes or when mapping mesh data to the GPU buffers.
 */
struct s_modelDataLayout
{
  /// @name Index buffer
  /// @{
  struct s_indexData
  {
    GLuint* __restrict m_data = nullptr; ///< Pointer to the buffer (does not own it)
    inline GLuint& operator[] (std::size_t iIndex) const { TRE_ASSERT(m_data != nullptr); return m_data[iIndex]; }
    inline GLuint* getPointer(std::size_t iIndex) const  { TRE_ASSERT(m_data != nullptr); return & m_data[iIndex]; }
  };
  s_indexData m_index;
  std::size_t m_indexCount = 0;
  /// @}

  /// @name Vertex buffer
  /// @{
  struct s_vertexData
  {
    GLfloat*  __restrict m_data = nullptr; ///< Offseted-pointer to the buffer (does not own it)
    std::size_t          m_size = 0; ///< Number of "float" per data (per vertex)
    std::size_t          m_stride = 0; ///< Advance (in number of "float") in the buffer
    inline GLfloat*   operator[] (std::size_t iVertex) const { TRE_ASSERT(m_data != nullptr); return & m_data[iVertex * m_stride]; }
    inline bool       isMatching(std::size_t size, std::size_t stride) const { return m_size == size && m_stride == stride; }
    inline bool       hasData() const { return m_data != nullptr; }

    template<class dtype> dtype & get(std::size_t iVertex) const
    {
      TRE_ASSERT(m_data != nullptr);
      TRE_ASSERT(sizeof(dtype) <= sizeof(GLfloat) * m_size);
      return * reinterpret_cast<dtype*>(& m_data[iVertex * m_stride]);
    }

    template<class dtype> class iterator
    {
    public:
      iterator(GLfloat *dataPtr, std::size_t dataStride) : m_dataPtr(dataPtr), m_dataStride(dataStride) {}
      inline iterator& operator++() { m_dataPtr += m_dataStride; return *this; }
      inline iterator  operator++(int ) { iterator retIt(*this); ++(*this); return retIt; }
      inline bool      operator==(const iterator &other) const { TRE_ASSERT(m_dataStride == other.m_dataStride);  return m_dataPtr == other.m_dataPtr; }
      inline bool      operator!=(const iterator &other) const { TRE_ASSERT(m_dataStride == other.m_dataStride);  return m_dataPtr != other.m_dataPtr; }
      inline dtype&    operator*() { return * reinterpret_cast<dtype*>(m_dataPtr); }
     private:
      GLfloat           *m_dataPtr;
      const std::size_t m_dataStride;
    };
    template<class dtype> iterator<dtype>  begin(std::size_t offset = 0) const
    {
      TRE_ASSERT(m_data != nullptr);
      TRE_ASSERT(sizeof(dtype) <= sizeof(GLfloat) * m_size);
      return iterator<dtype>(m_data + offset * m_stride, m_stride);
    }
  };
  s_vertexData m_positions;
  s_vertexData m_colors;
  s_vertexData m_normals;
  s_vertexData m_tangents;
  s_vertexData m_uvs;
  std::size_t  m_vertexCount = 0;
  /// @}

  /// @name Instance buffer
  /// @{
  struct s_instanceData : s_vertexData
  {
    const std::size_t m_divisor = 1; ///< nbr of data per instance
  };
  s_instanceData m_instancedPositions;
  s_instanceData m_instancedOrientations;
  s_instanceData m_instancedAtlasBlends;
  s_instanceData m_instancedColors;
  s_instanceData m_instancedRotations;
  std::size_t    m_instanceCount = 0;
  /// @}

  void     colorize(std::size_t ifirst, std::size_t icount, const glm::vec4 &unicolor) const; ///< fill uniform color
  void     colorize(const glm::vec4 &unicolor) const; ///< fill uniform color for the whole model

  void     transform(std::size_t ifirst, std::size_t icount, const glm::mat4 &tr) const; ///< transform by the matrix
  void     transform(const glm::mat4 &tr) const; ///< transform the whole model by the matrix

  void     copyIndex(std::size_t ifirst, std::size_t icount, std::size_t dstfirst) const; ///< copy of index-data of "i" to "dst". Warning: src and dst must not alias.
  void     copyVertex(std::size_t ivfirst, std::size_t ivcount, std::size_t dstfirst) const; ///< deep copy of vertex-data of "i" to "dst". Warning: src and dst must not alias.

  std::size_t rayIntersect(std::size_t ifirst, std::size_t icount, const glm::vec3 & pos, const glm::vec3 & vecN) const;

  void     clear();
};

//=============================================================================

/**
 * @brief s_partInfo defines a sub-region in a model
 * ...
 */
struct s_partInfo
{
  s_partInfo(const std::string & pname = "no-name") : m_name(pname) {}

  bool read(std::istream & inbuffer);
  bool write(std::ostream & outbuffer) const;

  std::string m_name;       ///< name of the part
  std::size_t m_size = 0;   ///< vertex-count (glDrawArray...) or index-count (glDrawElement...). As indice-value (not in bytes)
  std::size_t m_offset = 0; ///< first-vertex (glDrawArray...) or offset in the index-buffer (glDrawElement...). As indice-value (not in bytes)
  s_boundbox  m_bbox;       ///< bounding-box
};

//=============================================================================

/**
 * @brief model is an interface for the mesh operations and the mesh drawing.
 * It holds the mesh semantics (called layout) and the set of mesh-partition (called part).
 * It also describes how the mesh will be rendered.
 */
class model
{
public:

  model() {}
  model(const model &) = delete;
  virtual ~model() { TRE_ASSERT(m_VAO==0); }

  model & operator=(const model &) = delete; // no-copy

  /// @name Part(ition) of model
  /// @{
public:
  std::size_t partCount() const { return m_partInfo.size(); }
  std::size_t getPartWithName(const std::string &matchname) const;
  bool        reorganizeParts(const std::vector<std::string> & matchnames); ///< Re-order parts by matching part-names (only m_partInfo is modified)
  void        movePart(std::size_t ipart, std::size_t dstIndex); ///< Move the part into the desired index (only m_partInfo is modified)
  void        mergeParts(std::size_t ipart, std::size_t jpart, const bool keepEmpty_jpart = false); ///< Merge part-j into part-i, and remove or clear part-j
  void        mergeAllParts(); ///< Merge all parts into one part
  void        defragmentParts(); ///< Re-order and compact part allocated spaces [m_partOffset, m_partOffset + m_partSize - 1]
  void        clearParts() { m_partInfo.clear(); } ///< Clear all parts. All buffers are kept allocated.

  virtual std::size_t copyPart(std::size_t ipart, std::size_t pcount = 1); ///< Deep-copy of a part. Return the copied part id. This method must be overrided for indexed-mesh.
  virtual void        resizePart(std::size_t ipart, std::size_t count); ///< Resize a part. This method must be overrided for indexed-mesh
  void        renamePart(std::size_t ipart, const std::string &newname) { TRE_ASSERT(ipart<m_partInfo.size()); m_partInfo[ipart].m_name = newname; }
  void        colorizePart(std::size_t ipart, const glm::vec4 & unicolor) { TRE_ASSERT(ipart<m_partInfo.size()); m_layout.colorize(m_partInfo[ipart].m_offset, m_partInfo[ipart].m_size, unicolor); }
  void        transformPart(std::size_t ipart, const glm::mat4 &transform);
  void        computeBBoxPart(std::size_t ipart); ///< Re-compute the bound-box. Only needed if positions are modified through the "layout.m_position"
  void        clearPart(std::size_t ipart) { m_partInfo[ipart] = s_partInfo(); }

  void        transform(const glm::mat4 &tr);
  void        colorize(const glm::vec4 & unicolor) { m_layout.colorize(unicolor); }

  inline const s_partInfo & partInfo(std::size_t ipart) const { TRE_ASSERT(ipart<m_partInfo.size()); return m_partInfo[ipart]; }
  inline const s_modelDataLayout & layout() const { return m_layout; }
protected:
  std::vector<s_partInfo> m_partInfo; ///< info for each partition
  s_modelDataLayout       m_layout;   ///< info for data semantic

  std::size_t getFreeSpaceAtBegin() const; ///< [intern] Returns the free spaces after at the begin (offset = 0)
  std::size_t getFreeSpaceAfterPart(std::size_t ipart) const; ///< [intern] Returns the free spaces after the part "ipart"
  /// @}

  /// @name I/O
  /// @{
public:
  bool readBase(std::istream & inbuffer);
  bool writeBase(std::ostream & outbuffer) const;
  virtual bool read(std::istream & inbuffer) = 0;
  virtual bool write(std::ostream & outbuffer) const = 0;
  inline void  reserveVertex(std::size_t count) { if (count >= m_layout.m_vertexCount) resizeVertex(count); } ///< only grow. Use it carefully.
  inline void  reserveIndex(std::size_t count) { if (count >= m_layout.m_indexCount) resizeIndex(count); } ///< only grow. Use it carefully.
protected:
  virtual void resizeVertex(std::size_t count) = 0;
  virtual void resizeIndex(std::size_t count) = 0;
  /// @}

  /// @name on GPU side
  /// @{
public:
  virtual bool loadIntoGPU() = 0; ///< create GPU handes and load geometric data into the GPU memory
  virtual void updateIntoGPU() = 0; ///< update GPU data
  virtual void clearGPU() = 0; ///< clean GPU data
  virtual void drawcall(std::size_t partfirst, std::size_t partcount, const bool bindVAO = true, GLenum mode = GL_TRIANGLES) const = 0;
  virtual void drawcallAll(const bool bindVAO = true, GLenum mode = GL_TRIANGLES) const;
  bool         isLoadedGPU() const { return m_VAO != 0; }
protected:
  GLuint m_VAO = 0; ///< OpenGL handle (VAO)
  /// @}
};

//=============================================================================

/**
 * @brief The modelIndexed class
 * Common class for indexed-models.
 */
class modelIndexed : public model
{
public:
  modelIndexed() {}
  virtual ~modelIndexed() { TRE_ASSERT(m_IBufferHandle == 0); }

  virtual std::size_t copyPart(std::size_t ipart, std::size_t pcount = 1) override;

  std::size_t createPart(std::size_t indiceCount, std::size_t vertexCount, std::size_t &firstVertex); ///< create a uninitialized part. Reserve buffers only. Returns the vertex-offset.
  std::size_t createPartFromIndexes(tre::span<GLuint> indices, const GLfloat *pvert = nullptr);

  std::size_t createRawPart(std::size_t count); ///< create a part with 1:1 relationship between indice and vertice.
  void        resizeRawPart(std::size_t ipart, std::size_t count); ///< resize the raw-part (may recompute index-buffer)

  void        defragmentVertices(const bool makeVerticesUnique); ///< Re-order and compact the vertices space for each part. "makeVerticesUnique = true" will duplicates vertex that are shared between multiple parts.

  // 3D-primitive generator

  static constexpr std::size_t fillDataBox_ISize() { return 36; }
  static constexpr std::size_t fillDataBox_VSize() { return 24; }
  void fillDataBox(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float edgeLength, const glm::vec4 & color);
  std::size_t createPartFromPrimitive_box(const glm::mat4 &transform, float edgeLength, const glm::vec4 & color = glm::vec4(1.f))
  {
    std::size_t offsetV;
    std::size_t pid = createPart(fillDataBox_ISize(), fillDataBox_VSize(), offsetV);
    fillDataBox(pid, 0, offsetV, transform, edgeLength, color);
    return pid;
  }

  static constexpr std::size_t fillDataBoxWireframe_ISize() { return 48; }
  static constexpr std::size_t fillDataBoxWireframe_VSize() { return 8; }
  void fillDataBoxWireframe(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float edgeLength, const glm::vec4 & color);
  std::size_t createPartFromPrimitive_box_wireframe(const glm::mat4 &transform, float edgeLength, const glm::vec4 & color = glm::vec4(1.f))
  {
    std::size_t offsetV;
    std::size_t pid = createPart(fillDataBoxWireframe_ISize(), fillDataBoxWireframe_VSize(), offsetV);
    fillDataBoxWireframe(pid, 0, offsetV, transform, edgeLength, color);
    return pid;
  }

  static constexpr std::size_t fillDataCone_ISize(uint subdiv) { return subdiv * 6; }
  static constexpr std::size_t fillDataCone_VSize(uint subdiv) { return 2 + subdiv; }
  void fillDataCone(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float radius, float heigth, uint subdiv, const glm::vec4 & color);
  std::size_t createPartFromPrimitive_cone(const glm::mat4 &transform, float radius, float heigth, uint subdiv, const glm::vec4 & color = glm::vec4(1.f))
  {
    std::size_t offsetV;
    std::size_t pid = createPart(fillDataCone_ISize(subdiv), fillDataCone_VSize(subdiv), offsetV);
    fillDataCone(pid, 0, offsetV, transform, radius, heigth, subdiv, color);
    return pid;
  }

  static constexpr std::size_t fillDataDisk_ISize(float radiusIn, uint subdiv) { return radiusIn == 0.f ? subdiv * 3 : subdiv * 6;   }
  static constexpr std::size_t fillDataDisk_VSize(float radiusIn, uint subdiv) { return radiusIn == 0.f ? 1 + subdiv : 2 * subdiv; }
  void fillDataDisk(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float radiusOut, float radiusIn, uint subdiv, const glm::vec4 & color);
  std::size_t createPartFromPrimitive_disk(const glm::mat4 &transform, float radiusOut, float radiusIn, uint subdiv, const glm::vec4 & color = glm::vec4(1.f))
  {
    std::size_t offsetV;
    std::size_t pid = createPart(fillDataDisk_ISize(radiusIn, subdiv), fillDataDisk_VSize(radiusIn, subdiv), offsetV);
    fillDataDisk(pid, 0, offsetV, transform, radiusOut, radiusIn, subdiv, color);
    return pid;
  }

  static constexpr std::size_t fillDataTorus_ISize(uint subdiv_main, uint subdiv_in) { return subdiv_main * subdiv_in * 6; }
  static constexpr std::size_t fillDataTorus_VSize(uint subdiv_main, uint subdiv_in) { return subdiv_main * subdiv_in;     }
  void fillDataTorus(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float radiusMain, float radiusIn, uint subdiv_main, uint subdiv_in, const glm::vec4 & color);
  std::size_t createPartFromPrimitive_halftorus(const glm::mat4 &transform, float radiusMain, float radiusIn, uint subdiv_main, uint subdiv_in, const glm::vec4 & color = glm::vec4(1.f))
  {
    std::size_t offsetV;
    std::size_t pid = createPart(fillDataTorus_ISize(subdiv_main, subdiv_in), fillDataTorus_VSize(subdiv_main, subdiv_in), offsetV);
    fillDataTorus(pid, 0, offsetV, transform, radiusMain, radiusIn, subdiv_main, subdiv_in, color);
    return pid;
  }

  //static constexpr std::size_t fillDataIcosphere_ISize(uint ) { return 0; }
  //static constexpr std::size_t fillDataIcosphere_VSize(uint ) { return 0; }
  //void fillDataIcosphere(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float radius, uint subdiv, const glm::vec4 & color);

  static constexpr std::size_t fillDataSquare_ISize() { return 6; }
  static constexpr std::size_t fillDataSquare_VSize() { return 4; }
  void fillDataSquare(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float edgeLength, const glm::vec4 & color);
  std::size_t createPartFromPrimitive_square(const glm::mat4 &transform, float edgeLength, const glm::vec4 & color = glm::vec4(1.f))
  {
    std::size_t offsetV;
    std::size_t pid = createPart(fillDataSquare_ISize(), fillDataSquare_VSize(), offsetV);
    fillDataSquare(pid, 0, offsetV, transform, edgeLength, color);
    return pid;
  }

  static constexpr std::size_t fillDataSquareWireframe_ISize() { return 8; }
  static constexpr std::size_t fillDataSquareWireframe_VSize() { return 4; }
  void fillDataSquareWireframe(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float edgeLength, const glm::vec4 & color);
  std::size_t createPartFromPrimitive_square_wireframe(const glm::mat4 &transform, float edgeLength, const glm::vec4 & color = glm::vec4(1.f))
  {
    std::size_t offsetV;
    std::size_t pid = createPart(fillDataSquareWireframe_ISize(), fillDataSquareWireframe_VSize(), offsetV);
    fillDataSquareWireframe(pid, 0, offsetV, transform, edgeLength, color);
    return pid;
  }

  static constexpr std::size_t fillDataTube_ISize(bool       , uint subdiv_r, uint subdiv_h) { return subdiv_r * subdiv_h * 6;                       }
  static constexpr std::size_t fillDataTube_VSize(bool closed, uint subdiv_r, uint subdiv_h) { return subdiv_r * (subdiv_h + 1) + (closed ? 2 : 0);  }
  void fillDataTube(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float radius, float heigth, bool closed, uint subdiv_r, uint subdiv_h, const glm::vec4 & color);
  std::size_t createPartFromPrimitive_tube(const glm::mat4 &transform, float radius, float heigth, bool closed, uint subdiv_u, uint subdiv_v, const glm::vec4 & color = glm::vec4(1.f))
  {
    std::size_t offsetV;
    std::size_t pid = createPart(fillDataTube_ISize(closed, subdiv_u, subdiv_v), fillDataTube_VSize(closed, subdiv_u, subdiv_v), offsetV);
    fillDataTube(pid, 0, offsetV, transform, radius, heigth, closed, subdiv_u, subdiv_v, color);
    return pid;
  }

  static constexpr std::size_t fillDataUvtrisphere_ISize(uint subdiv_u, uint subdiv_v) { return (subdiv_u & ~0x1) * ((subdiv_v * 2 - 1) * 3 + 3 + 3); }
  static constexpr std::size_t fillDataUvtrisphere_VSize(uint subdiv_u, uint subdiv_v) { return 2 + (subdiv_v * 2 + 1) * (subdiv_u / 2);              }
  void fillDataUvtrisphere(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float radius, uint subdiv_u, uint subdiv_v, const glm::vec4 & color);
  std::size_t createPartFromPrimitive_uvtrisphere(const glm::mat4 &transform, float radius, uint subdiv_u, uint subdiv_v, const glm::vec4 & color = glm::vec4(1.f))
  {
    std::size_t offsetV;
    std::size_t pid = createPart(fillDataUvtrisphere_ISize(subdiv_u, subdiv_v), fillDataUvtrisphere_VSize(subdiv_u, subdiv_v), offsetV);
    fillDataUvtrisphere(pid, 0, offsetV, transform, radius, subdiv_u, subdiv_v, color);
    return pid;
  }

protected:
  virtual void resizePart(std::size_t ipart, std::size_t count) override; // use it carefully : does not resize the vertex data.
  virtual void resizeIndex(std::size_t count) override;

  bool read_IndexBuffer(std::istream & inbuffer);
  bool write_IndexBuffer(std::ostream & outbuffer) const;

  bool loadIntoGPU_IndexBuffer(const bool clearCPUbuffer = false);
  void updateIntoGPU_IndexBuffer();
  void clearGPU_IndexBuffer(const bool restoreCPUbuffer = false); ///< [intern] Warning: do not restore the data, just reset the buffer allocation with the layout's data-pointers.

  std::size_t get_NextAvailable_vertex() const;
  std::size_t get_NextAvailable_index() const;

protected:
  std::vector<GLuint> m_IBuffer;
  GLuint              m_IBufferHandle = 0;
};

//=============================================================================

/**
 * @brief The modelRaw2D class
 * Simple model, that can draw triangles and lines
 * The layout is:
 * - 8-floats vertex Buffer: [0-1]:position [2-3]:UV [4-7]:color
 */
class modelRaw2D : public model
{
public:
  modelRaw2D() {}
  virtual ~modelRaw2D() { TRE_ASSERT(m_VBufferHandle == 0); }

protected:
  virtual void resizeVertex(std::size_t count) override;
  virtual void resizeIndex(std::size_t) override { TRE_FATAL("should not be called"); }

public:
  std::size_t createPart(std::size_t partSize);
  void fillDataFromRAM(std::size_t ipart, std::size_t offset, std::size_t count, GLfloat* __restrict vbuffer);
  void fillDataRectangle(std::size_t ipart, std::size_t offset, const glm::vec4 & AxAyBxBy, const glm::vec4 & color, const glm::vec4 & AuAvBuBv);
  void fillDataLine(std::size_t ipart, std::size_t offset, const glm::vec2 & ptA, const glm::vec2 & ptB, const glm::vec4 & color);
  void fillDataLine(std::size_t ipart, std::size_t offset, float Ax, float Ay, float Bx, float By, const glm::vec4 & color);

public:
  virtual bool read(std::istream & inbuffer) override;
  virtual bool write(std::ostream & outbuffer) const override;
  virtual bool loadIntoGPU() override;
  virtual void updateIntoGPU() override;
  virtual void clearGPU() override;
  virtual void drawcall(std::size_t partfirst, std::size_t partcount, const bool bindVAO = true, GLenum mode = GL_TRIANGLES) const override;

protected:
  void loadIntoGPU_VertexBuffer(const bool clearCPUbuffer = false); ///< [intern] only bind the buffer and set the attribute pointer

protected:
  std::vector<GLfloat> m_VBuffer; ///< per-Vertex Buffer
  GLuint m_VBufferHandle = 0;
};

//=============================================================================

/**
 * @brief The modelStaticIndexed3D class
 * Big model that is loaded once and never updated in the GPU.
 * It can handle multiple layouts, but with only one vertex-buffer.
 */
class modelStaticIndexed3D : public modelIndexed
{
public:
  static const int VB_POSITION = 0x0010;
  static const int VB_UV       = 0x0001;
  static const int VB_NORMAL   = 0x0002;
  static const int VB_TANGENT  = 0x0004;
  static const int VB_COLOR    = 0x0008;

  modelStaticIndexed3D() = default;
  modelStaticIndexed3D(int flags) : m_flags(flags | VB_POSITION) {}
  virtual ~modelStaticIndexed3D() { TRE_ASSERT(m_VBufferHandle == 0); }

  void setFlags(int flags) { TRE_ASSERT(m_VBufferHandle == 0 && m_VBuffer.empty()); m_flags = flags | VB_POSITION; }
  int flags() const { return m_flags; }

protected:
  virtual void resizeVertex(std::size_t count) override;

public:
  virtual bool read(std::istream & inbuffer) override;
  virtual bool write(std::ostream & outbuffer) const override;
  virtual bool loadIntoGPU() override;
  virtual void updateIntoGPU() override { TRE_FATAL("Should never be called"); }
  virtual void clearGPU() override;
  virtual void drawcall(std::size_t partfirst, std::size_t partcount, const bool bindVAO = true, GLenum mode = GL_TRIANGLES) const override;

protected:
  void loadIntoGPU_VertexBuffer(); ///< [intern] only bind the buffer and set the attribute pointer

protected:
  std::vector<GLfloat> m_VBuffer; ///< per-Vertex Buffer
  int    m_flags = VB_POSITION;
  GLuint m_VBufferHandle = 0;
};

//=============================================================================

/**
 * @brief The modelSemiDynamic3D class
 * Semi-dynamic model, that contains a vertex-buffer that can be updated.
 * It can handle multiple layouts, but with 2 vertex-buffers.
 */
class modelSemiDynamic3D : public modelStaticIndexed3D
{
public:
  modelSemiDynamic3D() = default;
  modelSemiDynamic3D(int flagsStatic, int flagDynamic) { setFlags(flagsStatic, flagDynamic); }
  virtual ~modelSemiDynamic3D() { TRE_ASSERT(m_VBufferHandleDyn == 0); }

  void setFlags(int flagsStatic, int flagDynamic)
  {
    TRE_ASSERT(m_VBufferHandleDyn == 0 && m_VBufferDyn.empty());
    TRE_ASSERT((flagsStatic & flagDynamic) == 0); // enforce unique flags
    TRE_ASSERT(((flagsStatic | flagDynamic) & VB_POSITION) != 0); // enforce position
    m_flags = flagsStatic;
    m_flagsDynamic = flagDynamic;
  }
  int flagsDynamic() const { return m_flagsDynamic; }

protected:
  virtual void resizeVertex(std::size_t count) override;

public:
  virtual bool read(std::istream & inbuffer) override;
  virtual bool write(std::ostream & outbuffer) const override;
  virtual bool loadIntoGPU() override;
  virtual void updateIntoGPU() override;
  virtual void clearGPU() override;

protected:
  void loadIntoGPU_VertexBuffer(const bool clearCPUbuffer = false); ///< [intern] only bind the buffer and set the attribute pointer

protected:
  std::vector<GLfloat> m_VBufferDyn; ///< per-Vertex Buffer (dynamic)
  int m_flagsDynamic = 0; ///< Store which data is dynamic
  GLuint m_VBufferHandleDyn = 0;
};

//=============================================================================

/**
 * @brief The modelInstanced class
 * Common class of instanced draw.
 */
class modelInstanced
{
public:
  static const int VI_POSITION      = 0x1000; // note: position(x,y,z) + size(w)
  static const int VI_ORIENTATION   = 0x0100; // note: the 3 firsts columns of the orientation matrix // TODO : use quaternion
  static const int VI_ATLAS         = 0x0200;
  static const int VI_BLEND         = 0x0400;
  static const int VI_COLOR         = 0x0800;
  static const int VI_ROTATION      = 0x2000; // note: scalar

  modelInstanced() = default;
  modelInstanced(int flags) : m_flagsInstanced(flags | VI_POSITION) {}
  virtual ~modelInstanced() { TRE_ASSERT(m_InstBufferHandle == 0); }

  void setFlagsInstanced(int flagsInstanced)
  {
    TRE_ASSERT(m_InstBufferHandle == 0 && m_InstBuffer.empty());
    m_flagsInstanced = flagsInstanced | VI_POSITION;
  }
  int flagsInstanced() const { return m_flagsInstanced; }

  std::size_t sizeInstance() const { return const_cast<modelInstanced*>(this)->_layout().m_instanceCount; }
  void        resizeInstance(std::size_t newsize);
  void        reserveInstance(std::size_t newsize) { if (sizeInstance() < newsize) resizeInstance(newsize); }

  float    *bufferInstanced() { return m_InstBuffer.data(); } ///< Fill directly the buffer, without any checks.

public:
  virtual bool read(std::istream & inbuffer);
  virtual bool write(std::ostream & outbuffer) const;

protected:
  void loadIntoGPU_InstancedBuffer(const bool clearCPUbuffer = false); ///< [intern] only bind the buffer and set the attribute pointer
  void updateIntoGPU_InstancedBuffer();
  void clearGPU_InstancedBuffer(const bool restoreCPUbuffer = false); ///< [intern] Warning: do not restore the data, just reset the buffer allocation with the layout's data-pointers.

protected:
  virtual s_modelDataLayout &_layout() = 0; // scaffolding
  std::vector<GLfloat> m_InstBuffer; ///< per-Instance Buffer
  int    m_flagsInstanced = VI_POSITION;
  GLuint m_InstBufferHandle = 0;
};

//=============================================================================

/**
 * @brief The modelInstancedBillboard class
 */
class modelInstancedBillboard : public modelRaw2D, public modelInstanced
{
public:
  modelInstancedBillboard() = default;
  modelInstancedBillboard(int flagsInstanced) : modelInstanced(flagsInstanced) {}

  std::size_t createBillboard(const glm::vec4 & AxAyBxBy = glm::vec4(-1.f,-1.f,1.f,1.f), const glm::vec4 & AuAvBuBv = glm::vec4(0.f,1.f,1.f,0.f), const glm::vec4 & color = glm::vec4(1.f)); ///< returns the part where the billboard is created.
  std::size_t createBillboard_wireframe(const glm::vec4 & AxAyBxBy = glm::vec4(-1.f,-1.f,1.f,1.f), const glm::vec4 & color = glm::vec4(1.f)); ///< returns the part where the billboard is created.

public:
  virtual bool read(std::istream & inbuffer) override { return modelRaw2D::read(inbuffer) && modelInstanced::read(inbuffer); }
  virtual bool write(std::ostream & outbuffer) const override { return modelRaw2D::write(outbuffer) && modelInstanced::write(outbuffer); }
  virtual bool loadIntoGPU() override;
  virtual void updateIntoGPU() override { updateIntoGPU_InstancedBuffer(); }
  virtual void clearGPU() override { modelRaw2D::clearGPU(); clearGPU_InstancedBuffer(); }

  void drawInstanced(std::size_t ipart, std::size_t instancedOffset, std::size_t instancedCount, const bool bindVAO = true, GLenum mode = GL_TRIANGLES) const;

protected:
  virtual s_modelDataLayout &_layout() override { return m_layout; }
};

/**
 * @brief The modelInstancedMesh class
 */
class modelInstancedMesh : public modelStaticIndexed3D, public modelInstanced
{
public:
  modelInstancedMesh() = default;
  modelInstancedMesh(int flags, int flagsInstanced) : modelStaticIndexed3D(flags), modelInstanced(flagsInstanced) {}

public:
  virtual bool read(std::istream & inbuffer) override { return modelStaticIndexed3D::read(inbuffer) && modelInstanced::read(inbuffer); }
  virtual bool write(std::ostream & outbuffer) const override { return modelStaticIndexed3D::write(outbuffer) && modelInstanced::write(outbuffer); }
  virtual bool loadIntoGPU() override;
  virtual void updateIntoGPU() override { updateIntoGPU_InstancedBuffer(); }
  virtual void clearGPU() override { modelStaticIndexed3D::clearGPU(); clearGPU_InstancedBuffer(); }

  void drawInstanced(std::size_t ipart, std::size_t instancedOffset, std::size_t instancedCount, const bool bindVAO = true, GLenum mode = GL_TRIANGLES) const;

protected:
  virtual s_modelDataLayout &_layout() override { return m_layout; }
};

} // namespace

#endif // MODEL_H
