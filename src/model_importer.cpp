
#include "tre_model_importer.h"

#include "tre_model_tools.h"

#include <map>
#include <fstream>
#include <charconv>
#include <cstdlib>
#include <cstring>

namespace tre {

// == Helpers =================================================================

/// Fast but stricter numeric parsing (no leading space, no "+" sign, ...)
template <typename _T>
void _readNumber(const std::string &str, _T &ret)
{
  std::from_chars(str.data(), str.c_str() + str.length(), ret);
}

// ----------------------------------------------------------------------------

static void _numberList_to_Vector(const std::string &str, std::vector<std::size_t> &outV)
{
  if (str.empty()) return;
  std::size_t sep = str.find(' ');
  std::size_t b = 0u;
  while (sep != std::string::npos)
  {
    std::size_t vread = 0u;
    std::from_chars(str.data() + b, str.c_str() + sep, vread);
    outV.push_back(vread);
    b = sep + 1;
    sep = str.find(' ', b);
  }
  if (b < str.size())
  {
    std::size_t vread = 0u;
    std::from_chars(str.data() + b, str.c_str() + str.size(), vread);
    outV.push_back(vread);
  }
}

// ----------------------------------------------------------------------------

/// Read up to "maxCount" floats, separated by blanks. Returns the number of floats read.
static unsigned _readFloats(const char *str, float *out, unsigned maxCount)
{
  unsigned count = 0;
  for (; count < maxCount; ++count)
  {
    char *next = nullptr;
    out[count] = std::strtof(str, &next);
    if (next == str) break;
    str = next;
  }
  return count;
}

// ----------------------------------------------------------------------------

template <typename _Container>
static bool _readFile(const std::string &filename, _Container &out)
{
  std::ifstream ins(filename, std::ios_base::binary | std::ios_base::ate);
  if (!ins) return false;
  const std::streamoff size = ins.tellg();
  if (size < 0) return false;
  out.resize(std::size_t(size));
  ins.seekg(0);
  return bool(ins.read(reinterpret_cast<char*>(out.data()), std::streamsize(size)));
}

// == OBJ =====================================================================

/// Split a line into its keyword and its arguments
static bool _objSplitLine(std::string &line, std::string &key, const char *&args)
{
  const std::size_t cut = line.find_first_of("#\r"); // comment, or the CR of CRLF
  if (cut != std::string::npos) line.resize(cut);
  const std::size_t begin = line.find_first_not_of(" \t");
  if (begin == std::string::npos) return false;
  key.assign(line, begin, line.find_first_of(" \t", begin) - begin); // ok with npos
  args = line.c_str() + begin + key.size();
  return true;
}

// ----------------------------------------------------------------------------

static std::string _objReadName(const char *args)
{
  const std::string name(args);
  const std::size_t b = name.find_first_not_of(" \t");
  if (b == std::string::npos) return std::string();
  return name.substr(b, name.find_last_not_of(" \t") + 1 - b);
}

// ----------------------------------------------------------------------------

/// Read an index (1-based, or negative: relative to the end of the list read so far), and convert it into a 0-based index.
static bool _objReadIndex(const char *&cur, std::size_t listSize, int &out)
{
  char       *next = nullptr;
  const long raw = std::strtol(cur, &next, 10);
  if (next == cur) return false;
  cur = next;
  const long ind = (raw < 0) ? long(listSize) + raw : raw - 1;
  if (ind < 0 || ind >= long(listSize)) return false;
  out = int(ind);
  return true;
}

// ----------------------------------------------------------------------------

static bool _objLoadMTL(const std::string &filename, std::vector<glm::vec4> &matColors, std::map<std::string, std::size_t> &matIds)
{
  std::ifstream rawMTL(filename);
  if (!rawMTL) return false;
  std::string line, key;
  const char  *args = nullptr;
  while (std::getline(rawMTL, line))
  {
    if (!_objSplitLine(line, key, args)) continue;
    if (key == "newmtl")
    {
      matIds[_objReadName(args)] = matColors.size();
      matColors.emplace_back(1.f);
    }
    else if (matColors.empty())
    {
      continue;
    }
    else if (key == "Kd")
    {
      _readFloats(args, glm::value_ptr(matColors.back()), 3);
    }
    else if (key == "d")
    {
      _readFloats(args, &matColors.back().a, 1);
    }
  }
  return true;
}

// ----------------------------------------------------------------------------

bool modelImporter::addFromWavefront(modelIndexed &outModel, const std::string &objfile, const std::string &mtlfile)
{
  std::ifstream rawOBJ(objfile);
  if (!rawOBJ)
  {
    TRE_LOG("Failed to load wavefront file " << objfile);
    return false;
  }

  outModel.reserveVertex(outModel.layout().m_vertexCount);
  const s_modelDataLayout &outLayout = outModel.layout();
  if (outLayout.m_positions.m_size != 3)
  {
    TRE_LOG("Error in addFromWavefront: the model must have 3D positions.");
    return false;
  }
  const bool needNormals = (outLayout.m_normals.m_size == 3);
  const bool needUVs     = (outLayout.m_uvs.m_size == 2);
  const bool needColors  = (outLayout.m_colors.m_size == 4);

  struct s_corner
  {
    int m_pos = -1; // 0-based indices in the read lists (-1: none)
    int m_uv  = -1;
    int m_nor = -1;
    int m_mat = -1;
    bool operator<(const s_corner &other) const { return std::memcmp(this, &other, sizeof(s_corner)) < 0; } // any strict order is fine for the de-duplication
  };
  struct s_partRead
  {
    std::string           m_name;
    std::vector<s_corner> m_corners; // 3 corners per triangle (read data)
    std::vector<s_corner> m_vertices; // post-treated data
    std::vector<GLuint>   m_indices;  // post-treated data
    bool                  m_hasAllNormals = true;
  };

  std::vector<glm::vec3>             positionsRead, normalsRead;
  std::vector<glm::vec2>             uvsRead;
  std::vector<s_partRead>            partRead;
  std::vector<glm::vec4>             matColors;
  std::map<std::string, std::size_t> matIds;

  // Read the materials

  const bool loadMTLFromOBJ = needColors && mtlfile.empty(); // else, "mtllib" is ignored
  if (needColors && !mtlfile.empty() && !_objLoadMTL(mtlfile, matColors, matIds))
  {
    TRE_LOG("Warning: ignore materials because it failed to load the material file " << mtlfile);
  }

  // Read the OBJ file

  std::string           line, key;
  const char            *args = nullptr;
  std::size_t           lineNumber = 0;
  int                   currentMat = -1;
  std::vector<s_corner> polygon;

  while (std::getline(rawOBJ, line))
  {
    ++lineNumber;
    if (!_objSplitLine(line, key, args)) continue;

    if (key == "v")
    {
      glm::vec3 pos;
      if (_readFloats(args, glm::value_ptr(pos), 3) != 3)
      {
        TRE_LOG("Error in addFromWavefront: invalid vertex at " << objfile << ":" << lineNumber);
        return false;
      }
      positionsRead.push_back(pos);
    }
    else if (key == "vn")
    {
      glm::vec3 nor;
      if (_readFloats(args, glm::value_ptr(nor), 3) != 3)
      {
        TRE_LOG("Error in addFromWavefront: invalid normal at " << objfile << ":" << lineNumber);
        return false;
      }
      normalsRead.push_back(nor);
    }
    else if (key == "vt")
    {
      glm::vec2 uv = glm::vec2(0.f);
      if (_readFloats(args, glm::value_ptr(uv), 2) == 0)
      {
        TRE_LOG("Error in addFromWavefront: invalid uv at " << objfile << ":" << lineNumber);
        return false;
      }
      uvsRead.emplace_back(uv.x, 1.f - uv.y); // OpenGL uv coordinate-system.
    }
    else if (key == "f")
    {
      // corners: "v", "v/vt", "v//vn" or "v/vt/vn"
      polygon.clear();
      const char *cur = args;
      while (*(cur + std::strspn(cur, " \t")) != 0)
      {
        s_corner c;
        c.m_mat = currentMat;
        bool     valid = _objReadIndex(cur, positionsRead.size(), c.m_pos);
        if (valid && *cur == '/')
        {
          ++cur;
          if (*cur != '/') valid = _objReadIndex(cur, uvsRead.size(), c.m_uv);
          if (valid && *cur == '/') { ++cur; valid = _objReadIndex(cur, normalsRead.size(), c.m_nor); }
        }
        if (!valid)
        {
          TRE_LOG("Error in addFromWavefront: invalid face at " << objfile << ":" << lineNumber);
          return false;
        }
        polygon.push_back(c);
      }
      if (polygon.size() < 3)
      {
        TRE_LOG("Error in addFromWavefront: face with less than 3 vertices at " << objfile << ":" << lineNumber);
        return false;
      }
      if (partRead.empty()) // faces before any "o" or "g"
      {
        partRead.emplace_back();
        partRead.back().m_name = "default";
      }
      std::vector<s_corner> &corners = partRead.back().m_corners;
      for (std::size_t k = 1; k + 1 < polygon.size(); ++k) // fan
      {
        corners.push_back(polygon[0]);
        corners.push_back(polygon[k]);
        corners.push_back(polygon[k + 1]);
      }
    }
    else if (key == "o" || key == "g")
    {
      if (partRead.empty() || !partRead.back().m_corners.empty()) partRead.emplace_back();
      partRead.back().m_name = _objReadName(args);
    }
    else if (key == "usemtl")
    {
      const auto it = matIds.find(_objReadName(args));
      currentMat = (it != matIds.end()) ? int(it->second) : -1;
    }
    else if (key == "mtllib" && loadMTLFromOBJ)
    {
      const std::size_t lastSlash = objfile.find_last_of("/\\");
      const std::string mtlpath = (lastSlash != std::string::npos ? objfile.substr(0, lastSlash + 1) : std::string()) + _objReadName(args);
      if (!_objLoadMTL(mtlpath, matColors, matIds)) TRE_LOG("Warning: ignore materials because it failed to load the material file " << mtlpath);
    }
  }
  rawOBJ.close();

  // Build the parts: a vertex is a unique (position, uv, normal, material), keeping only the attributes the model has.

  std::size_t totalVertices  = 0;
  std::size_t totalTriangles = 0;
  std::size_t partCount      = 0;
  for (s_partRead &p : partRead)
  {
    if (p.m_corners.empty()) continue;
    std::map<s_corner, GLuint> vertexMap;
    p.m_indices.reserve(p.m_corners.size());
    for (const s_corner &c : p.m_corners)
    {
      s_corner vkey = c;
      if (!needUVs) vkey.m_uv = -1;
      if (!needNormals) vkey.m_nor = -1;
      if (!needColors) vkey.m_mat = -1;
      p.m_hasAllNormals &= (c.m_nor >= 0);
      const auto it = vertexMap.emplace(vkey, GLuint(p.m_vertices.size())).first;
      if (it->second == p.m_vertices.size()) p.m_vertices.push_back(vkey); // new vertex
      p.m_indices.push_back(it->second);
    }
    totalVertices  += p.m_vertices.size();
    totalTriangles += p.m_indices.size() / 3;
    ++partCount;
  }

  if (partCount == 0)
  {
    TRE_LOG("No face parsed in wavefront file " << objfile);
    return false;
  }

  // Write into the model

  if (outModel.partCount() == 0) outModel.reserveVertex(totalVertices); // single allocation

  bool computedNormals = false;
  for (const s_partRead &p : partRead)
  {
    if (p.m_corners.empty()) continue;

    std::size_t       firstVertex = 0;
    const std::size_t partId = outModel.createPart(p.m_indices.size(), p.m_vertices.size(), firstVertex);
    outModel.renamePart(partId, p.m_name);

    for (std::size_t iv = 0; iv < p.m_vertices.size(); ++iv)
    {
      const s_corner    &v = p.m_vertices[iv];
      const std::size_t dst = firstVertex + iv;
      outLayout.m_positions.get<glm::vec3>(dst) = positionsRead[v.m_pos];
      if (needNormals) outLayout.m_normals.get<glm::vec3>(dst) = (v.m_nor >= 0) ? normalsRead[v.m_nor] : glm::vec3(0.f);
      if (needUVs)     outLayout.m_uvs.get<glm::vec2>(dst) = (v.m_uv >= 0) ? uvsRead[v.m_uv] : glm::vec2(0.f);
      if (needColors)  outLayout.m_colors.get<glm::vec4>(dst) = (v.m_mat >= 0) ? matColors[v.m_mat] : glm::vec4(1.f);
    }

    const s_partInfo &pInfo = outModel.partInfo(partId);
    for (std::size_t ii = 0; ii < p.m_indices.size(); ++ii)
      outLayout.m_index[pInfo.m_offset + ii] = GLuint(firstVertex + p.m_indices[ii]);

    outModel.computeBBoxPart(partId);

    if (needNormals && !p.m_hasAllNormals)
    {
      modelTools::computeOutNormal(outLayout, outModel.partInfo(partId), true); // smooth, counter-clockwise faces
      computedNormals = true;
    }
  }

  // Report

  if (computedNormals)
    TRE_LOG("Info: the wavefront file " << objfile << " has faces without normals, they are computed.");
  if (needUVs && uvsRead.empty())
    TRE_LOG("Warning: the model asks for uvs, but the wavefront file " << objfile << " has none. The uvs are filled with zeros.");
  if (needColors && matColors.empty())
    TRE_LOG("Warning: the model asks for colors, but the wavefront file " << objfile << " has no material. The colors are filled with white.");

  TRE_LOG("Load " << objfile << " (new parts=" << partCount <<
          ", vertices+=" << totalVertices << ", triangles+=" << totalTriangles << ")" <<
          ", model total (vertices=" << outLayout.m_vertexCount << ")");
  return true;
}

// == GLTF ====================================================================

namespace json {

struct s_node
{
  std::string         m_key;
  std::string         m_valueStr;
  std::vector<s_node> m_list;

  const s_node *find(const char *key) const
  {
    for (const s_node &n : m_list) { if (n.m_key == key) return &n; }
    return nullptr;
  }

  /// Integer value of a member, or "defaultValue" if the member does not exist.
  template <typename _T> _T get(const char *key, _T defaultValue) const
  {
    const s_node *n = find(key);
    if (n != nullptr) _readNumber(n->m_valueStr, defaultValue);
    return defaultValue;
  }
};

// ----------------------------------------------------------------------------

struct s_parser
{
  const char *m_cur;
  const char *m_end;
  unsigned   m_depth = 0;

  static const unsigned s_maxDepth = 64;

  static bool isBlank(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

  bool next(char c)
  {
    while (m_cur < m_end && isBlank(*m_cur)) ++m_cur; // skip the blanks
    if (m_cur == m_end || *m_cur != c) return false;
    ++m_cur;
    return true;
  }

  bool readString(std::string &out)
  {
    while (m_cur < m_end)
    {
      const char *b = m_cur;
      while (m_cur < m_end && *m_cur != '"' && *m_cur != '\\') ++m_cur;
      out.append(b, m_cur);
      if (m_cur == m_end) return false;
      if (*m_cur++ == '"') return true;
      if (m_cur == m_end) return false;
      const char c = *m_cur++; // escaped character
      if      (c == 'n') out.push_back('\n');
      else if (c == 't') out.push_back('\t');
      else if (c == 'r') out.push_back('\r');
      else if (c == 'b') out.push_back('\b');
      else if (c == 'f') out.push_back('\f');
      else if (c == 'u') { if (m_end - m_cur < 4) return false; m_cur += 4; out.push_back('?'); } // not decoded
      else               out.push_back(c); // '"', '\\', '/'
    }
    return false;
  }

  bool readValue(s_node &node, bool &isScalar)
  {
    isScalar = true;
    if (next('"')) return readString(node.m_valueStr);
    isScalar = false;
    if (m_cur < m_end && *m_cur == '{') return readObject(node.m_list);
    if (next('['))
    {
      if (++m_depth > s_maxDepth) return false;
      if (!next(']'))
      {
        do
        {
          s_node elem;
          bool   elemIsScalar = false;
          if (!readValue(elem, elemIsScalar)) return false;
          if (elemIsScalar)
          {
            if (!node.m_valueStr.empty()) node.m_valueStr.push_back(' ');
            node.m_valueStr += elem.m_valueStr;
          }
          else
          {
            elem.m_key = "list-element";
            node.m_list.push_back(std::move(elem));
          }
        }
        while (next(','));
        if (!next(']')) return false;
      }
      --m_depth;
      return true;
    }
    // number, true, false, null
    isScalar = true;
    const char *b = m_cur;
    while (m_cur < m_end && !isBlank(*m_cur) && *m_cur != ',' && *m_cur != ']' && *m_cur != '}') ++m_cur;
    node.m_valueStr.assign(b, m_cur);
    return m_cur != b;
  }

  bool readObject(std::vector<s_node> &out)
  {
    if (!next('{') || ++m_depth > s_maxDepth) return false;
    if (!next('}'))
    {
      do
      {
        out.emplace_back();
        bool isScalar = false;
        if (!next('"') || !readString(out.back().m_key) || !next(':') || !readValue(out.back(), isScalar)) return false;
      }
      while (next(','));
      if (!next('}')) return false;
    }
    --m_depth;
    return true;
  }
};

} // json

// ----------------------------------------------------------------------------

/// Decode base64 data
static bool _decodeBase64(const char *in, const char *end, std::vector<uint8_t> &out)
{
  out.clear();
  out.reserve(std::size_t(end - in) / 4 * 3 + 3);
  uint32_t acc  = 0;
  unsigned bits = 0;
  for (; in < end && *in != '='; ++in)
  {
    const char c = *in;
    uint32_t   v = 0;
    if      (c >= 'A' && c <= 'Z') v = uint32_t(c - 'A');
    else if (c >= 'a' && c <= 'z') v = uint32_t(c - 'a' + 26);
    else if (c >= '0' && c <= '9') v = uint32_t(c - '0' + 52);
    else if (c == '+')             v = 62;
    else if (c == '/')             v = 63;
    else                           return false;
    acc = (acc << 6) | v;
    bits += 6;
    if (bits >= 8)
    {
      bits -= 8;
      out.push_back(uint8_t(acc >> bits));
    }
  }
  return true;
}

// ----------------------------------------------------------------------------

/// Decode the percent-encoded characters of an URI
static std::string _decodeURI(const std::string &uri)
{
  std::string out;
  for (std::size_t i = 0; i < uri.size(); ++i)
  {
    unsigned hex = 0;
    if (uri[i] == '%' && i + 2 < uri.size() && std::from_chars(uri.data() + i + 1, uri.data() + i + 3, hex, 16).ptr == uri.data() + i + 3)
    {
      out.push_back(char(hex));
      i += 2;
    }
    else
    {
      out.push_back(uri[i]);
    }
  }
  return out;
}

// ----------------------------------------------------------------------------

static uint32_t _readU32(const char *data)
{
  uint32_t v;
  std::memcpy(&v, data, sizeof(v));
  return v;
}

// ----------------------------------------------------------------------------

static bool _GLTF_appendNode(const json::s_node &nodes, const std::vector<std::vector<std::size_t>> &meshParts, std::size_t nodeId,
                             std::size_t &remainingNodes, modelImporter::s_modelHierarchy &out)
{
  if (nodeId >= nodes.m_list.size() || remainingNodes == 0) return false;
  --remainingNodes;
  const json::s_node &node = nodes.m_list[nodeId];

  // transform
  const json::s_node *matrix = node.find("matrix");
  if (matrix != nullptr)
  {
    if (_readFloats(matrix->m_valueStr.c_str(), glm::value_ptr(out.m_transform), 16) != 16) return false; // column-major, as glm.
  }
  else
  {
    glm::vec3 t = glm::vec3(0.f);
    glm::vec3 s = glm::vec3(1.f);
    float     r[4] = { 0.f, 0.f, 0.f, 1.f }; // x, y, z, w
    const json::s_node *nt = node.find("translation");
    const json::s_node *nr = node.find("rotation");
    const json::s_node *ns = node.find("scale");
    if (nt != nullptr && _readFloats(nt->m_valueStr.c_str(), glm::value_ptr(t), 3) != 3) return false;
    if (nr != nullptr && _readFloats(nr->m_valueStr.c_str(), r, 4) != 4) return false;
    if (ns != nullptr && _readFloats(ns->m_valueStr.c_str(), glm::value_ptr(s), 3) != 3) return false;
    glm::quat q;
    q.x = r[0]; q.y = r[1]; q.z = r[2]; q.w = r[3];
    out.m_transform = glm::mat4_cast(q); // T * R * S
    out.m_transform[0] *= s.x;
    out.m_transform[1] *= s.y;
    out.m_transform[2] *= s.z;
    out.m_transform[3] = glm::vec4(t, 1.f);
  }

  // parts: the first primitive of the mesh is on the node, the next ones are children (with identity transform)
  out.m_partId = std::size_t(-1);
  const std::size_t meshId = node.get<std::size_t>("mesh", std::size_t(-1));
  if (meshId != std::size_t(-1))
  {
    if (meshId >= meshParts.size()) return false;
    for (std::size_t partId : meshParts[meshId])
    {
      if (out.m_partId == std::size_t(-1)) { out.m_partId = partId; continue; }
      out.m_childs.emplace_back();
      out.m_childs.back().m_transform = glm::mat4(1.f);
      out.m_childs.back().m_partId = partId;
    }
  }

  // children
  const json::s_node      *children = node.find("children");
  std::vector<std::size_t> childIds;
  if (children != nullptr) _numberList_to_Vector(children->m_valueStr, childIds);
  for (std::size_t cid : childIds)
  {
    out.m_childs.emplace_back();
    if (!_GLTF_appendNode(nodes, meshParts, cid, remainingNodes, out.m_childs.back())) return false;
  }
  return true;
}

// ----------------------------------------------------------------------------

bool modelImporter::addFromGLTF(modelIndexed &outModel, s_modelHierarchy &outHierarchy, const std::string &gltffile, const bool isBinary)
{
  outModel.reserveVertex(outModel.layout().m_vertexCount);
  const s_modelDataLayout &outLayout = outModel.layout();
  if (outLayout.m_positions.m_size != 3)
  {
    TRE_LOG("Error in addFromGLTF: the model must have 3D positions.");
    return false;
  }
  const bool needNormals = (outLayout.m_normals.m_size == 3);
  const bool needUVs     = (outLayout.m_uvs.m_size == 2);
  const bool needColors  = (outLayout.m_colors.m_size == 4);
  const bool needSkins   = (outLayout.m_skins.m_size == 2);

  struct s_view
  {
    const uint8_t *m_data           = nullptr; ///< nullptr: no data
    std::size_t    m_count          = 0;
    std::size_t    m_stride         = 0; ///< in bytes
    unsigned       m_componentType  = 0; ///< 5120: i8, 5121: u8, 5122: i16, 5123: u16, 5125: u32, 5126: float
    unsigned       m_componentCount = 0; ///< 1: SCALAR, 2: VEC2, 3: VEC3, 4: VEC4
    bool           m_normalized     = false;

    bool hasCount(std::size_t count) const { return m_data == nullptr || m_count == count; }

    /// Read (up to "maxCount") components of an element, as floats. Normalized integers are converted into [0,1] or [-1,1].
    void readFloats(std::size_t i, float *out, unsigned maxCount) const
    {
      if (m_data == nullptr) return;
      const uint8_t *src = m_data + i * m_stride;
      for (unsigned c = 0; c < m_componentCount && c < maxCount; ++c)
      {
        switch (m_componentType)
        {
          case 5120: { int8_t   v; std::memcpy(&v, src + c,     sizeof(v)); out[c] = m_normalized ? glm::max(v / 127.f, -1.f)   : float(v); } break;
          case 5121: { uint8_t  v; std::memcpy(&v, src + c,     sizeof(v)); out[c] = m_normalized ? v / 255.f                   : float(v); } break;
          case 5122: { int16_t  v; std::memcpy(&v, src + 2 * c, sizeof(v)); out[c] = m_normalized ? glm::max(v / 32767.f, -1.f) : float(v); } break;
          case 5123: { uint16_t v; std::memcpy(&v, src + 2 * c, sizeof(v)); out[c] = m_normalized ? v / 65535.f                 : float(v); } break;
          case 5125: { uint32_t v; std::memcpy(&v, src + 4 * c, sizeof(v)); out[c] = float(v); } break;
          default:   std::memcpy(out + c, src + 4 * c, sizeof(float)); break; // 5126
        }
      }
    }

    /// Read an unsigned integer scalar (indices).
    uint32_t readUInt(std::size_t i) const
    {
      const uint8_t *src = m_data + i * m_stride;
      if (m_componentType == 5121) return *src;
      if (m_componentType == 5123) return *reinterpret_cast<const uint16_t*>(src);
      return *reinterpret_cast<const uint16_t*>(src);
    }
  };

  /// The GLTF content
  struct s_file
  {
    json::s_node                      m_root;
    const json::s_node                *m_accessors   = nullptr;
    const json::s_node                *m_bufferViews = nullptr;
    std::vector<std::vector<uint8_t>> m_buffers;

    bool getView(std::size_t accessorId, s_view &out) const
    {
      if (accessorId >= m_accessors->m_list.size()) return false;
      const json::s_node &acc  = m_accessors->m_list[accessorId];
      const json::s_node *type = acc.find("type");
      if (type == nullptr || acc.find("sparse") != nullptr) return false;
      if      (type->m_valueStr == "SCALAR") out.m_componentCount = 1;
      else if (type->m_valueStr == "VEC2")   out.m_componentCount = 2;
      else if (type->m_valueStr == "VEC3")   out.m_componentCount = 3;
      else if (type->m_valueStr == "VEC4")   out.m_componentCount = 4;
      else                                   return false;
      out.m_componentType = acc.get<unsigned>("componentType", 0);
      std::size_t componentSize = 0;
      if      (out.m_componentType == 5120 || out.m_componentType == 5121) componentSize = 1;
      else if (out.m_componentType == 5122 || out.m_componentType == 5123) componentSize = 2;
      else if (out.m_componentType == 5125 || out.m_componentType == 5126) componentSize = 4;
      else                                                                 return false;
      const json::s_node *normalized = acc.find("normalized");
      out.m_normalized = (normalized != nullptr && normalized->m_valueStr == "true");
      out.m_count = acc.get<std::size_t>("count", 0);
      const std::size_t accOffset = acc.get<std::size_t>("byteOffset", 0);

      const std::size_t viewId = acc.get<std::size_t>("bufferView", std::size_t(-1));
      if (viewId >= m_bufferViews->m_list.size()) return false;
      const json::s_node &view       = m_bufferViews->m_list[viewId];
      const std::size_t  bufferId    = view.get<std::size_t>("buffer", std::size_t(-1));
      const std::size_t  viewOffset  = view.get<std::size_t>("byteOffset", 0);
      const std::size_t  viewLength  = view.get<std::size_t>("byteLength", 0);
      const std::size_t  elementSize = componentSize * out.m_componentCount;
      out.m_stride = view.get<std::size_t>("byteStride", elementSize);

      // check the bounds (written to avoid overflows)
      if (bufferId >= m_buffers.size()) return false;
      const std::size_t bufferSize = m_buffers[bufferId].size();
      if (viewLength > bufferSize || viewOffset > bufferSize - viewLength) return false;
      if (out.m_count == 0 || out.m_count > viewLength || out.m_stride < elementSize || out.m_stride > 256 || accOffset > viewLength) return false;
      if ((out.m_count - 1) * out.m_stride + elementSize > viewLength - accOffset) return false;

      out.m_data = m_buffers[bufferId].data() + viewOffset + accOffset;
      return true;
    }

    /// Get the data of an optional attribute. Returns false if the attribute exists but it is invalid.
    bool getAttribute(const json::s_node &attributes, const char *name, unsigned componentsMin, unsigned componentsMax, s_view &out) const
    {
      if (attributes.find(name) == nullptr) return true;
      return getView(attributes.get<std::size_t>(name, std::size_t(-1)), out) && out.m_componentCount >= componentsMin && out.m_componentCount <= componentsMax;
    }
  };

  /// A primitive, that becomes a part
  struct s_primitive
  {
    std::string m_name;
    s_view      m_pos, m_normal, m_uv, m_color, m_index;
    glm::vec4   m_baseColor   = glm::vec4(1.f);
    std::size_t m_vertexCount = 0;
    std::size_t m_indexCount  = 0;
    bool        m_flatNormals = false; ///< normals to compute: the vertices are not shared between triangles
  };

  // Read the file, and the chunks of a binary file

  std::string fileData;
  if (!_readFile(gltffile, fileData))
  {
    TRE_LOG("Failed to load GLTF file " << gltffile);
    return false;
  }

  std::size_t jsonBegin = 0, jsonEnd = fileData.size();
  std::size_t binBegin = 0, binEnd = 0; // GLB-stored buffer (binEnd = 0: none)
  if (isBinary)
  {
    const char *d = fileData.data();
    if (fileData.size() < 20 || _readU32(d) != 0x46546C67 /* glTF */ || _readU32(d + 4) != 2 ||
        _readU32(d + 16) != 0x4E4F534A /* JSON */ || _readU32(d + 12) > fileData.size() - 20)
    {
      TRE_LOG("Invalid header of a binary GLTF file " << gltffile);
      return false;
    }
    jsonBegin = 20;
    jsonEnd   = 20 + _readU32(d + 12);
    if (jsonEnd + 8 <= fileData.size() && _readU32(d + jsonEnd + 4) == 0x004E4942 /* BIN */)
    {
      binBegin = jsonEnd + 8;
      binEnd   = binBegin + _readU32(d + jsonEnd);
      if (binEnd > fileData.size())
      {
        TRE_LOG("Invalid binary chunk of a binary GLTF file " << gltffile);
        return false;
      }
    }
  }

  s_file         file;
  json::s_parser parser = { fileData.data() + jsonBegin, fileData.data() + jsonEnd };
  if (!parser.readObject(file.m_root.m_list))
  {
    TRE_LOG("Failed to parse GLTF file (json format) " << gltffile);
    return false;
  }

  const json::s_node *readScenes    = file.m_root.find("scenes");
  const json::s_node *readNodes     = file.m_root.find("nodes");
  const json::s_node *readMeshes    = file.m_root.find("meshes");
  const json::s_node *readMaterials = file.m_root.find("materials");
  const json::s_node *readBuffers   = file.m_root.find("buffers");
  file.m_accessors                  = file.m_root.find("accessors");
  file.m_bufferViews                = file.m_root.find("bufferViews");

  if (readMeshes == nullptr || file.m_accessors == nullptr || file.m_bufferViews == nullptr || readBuffers == nullptr)
  {
    TRE_LOG("model::loadfromGLTF: missing at least one of the mandatory lists (meshes, accessors, bufferViews, buffers) in " << gltffile);
    return false;
  }

  // Load the buffers

  const std::size_t lastSlash = gltffile.find_last_of("/\\");
  const std::string dirname   = (lastSlash != std::string::npos) ? gltffile.substr(0, lastSlash + 1) : std::string();

  for (const json::s_node &nb : readBuffers->m_list)
  {
    file.m_buffers.emplace_back();
    std::vector<uint8_t> &data   = file.m_buffers.back();
    const json::s_node   *uri    = nb.find("uri");
    bool                 loaded = false;
    if (uri == nullptr) // GLB-stored buffer (only the first buffer)
    {
      loaded = (binEnd != 0 && file.m_buffers.size() == 1);
      if (loaded) data.assign(fileData.begin() + binBegin, fileData.begin() + binEnd);
    }
    else if (uri->m_valueStr.compare(0, 5, "data:") == 0) // embedded
    {
      const std::size_t sep = uri->m_valueStr.find(";base64,");
      loaded = (sep != std::string::npos) && _decodeBase64(uri->m_valueStr.data() + sep + 8, uri->m_valueStr.data() + uri->m_valueStr.size(), data);
    }
    else // external file
    {
      loaded = _readFile(dirname + _decodeURI(uri->m_valueStr), data);
    }
    if (!loaded || data.size() < nb.get<std::size_t>("byteLength", 0))
    {
      TRE_LOG("model::loadfromGLTF: failed to load the buffer " << file.m_buffers.size() - 1 << " of " << gltffile);
      return false;
    }
  }

  // Read and check the primitives. The parts will be created in this order, so their ids are known.

  const std::size_t                     firstPartId = outModel.partCount();
  std::vector<s_primitive>              primitives;
  std::vector<std::vector<std::size_t>> meshParts(readMeshes->m_list.size()); // part-ids per mesh
  std::size_t                           totalVertices = 0, totalTriangles = 0;
  bool                                  missingUVs = false;

  for (std::size_t im = 0; im < readMeshes->m_list.size(); ++im)
  {
    const json::s_node &mesh      = readMeshes->m_list[im];
    const json::s_node *meshName  = mesh.find("name");
    const json::s_node *meshPrims = mesh.find("primitives");
    if (meshPrims == nullptr || meshPrims->m_list.empty())
    {
      TRE_LOG("model::loadfromGLTF: the mesh " << im << " has no primitive in " << gltffile);
      return false;
    }

    for (std::size_t ip = 0; ip < meshPrims->m_list.size(); ++ip)
    {
      const json::s_node &prim = meshPrims->m_list[ip];
      s_primitive        p;
      p.m_name = (meshName != nullptr && !meshName->m_valueStr.empty()) ? meshName->m_valueStr : "mesh-" + std::to_string(im);
      if (meshPrims->m_list.size() > 1) p.m_name += "-" + std::to_string(ip);

      if (prim.get<unsigned>("mode", 4) != 4) // TRIANGLES
      {
        TRE_LOG("model::loadfromGLTF: the primitive \"" << p.m_name << "\" is not made of triangles. Skipped.");
        continue;
      }

      // attributes
      const json::s_node *attributes = prim.find("attributes");
      bool valid = attributes != nullptr &&
                   file.getAttribute(*attributes, "POSITION",   3, 3, p.m_pos) && p.m_pos.m_data != nullptr &&
                   file.getAttribute(*attributes, "NORMAL",     3, 3, p.m_normal) &&
                   file.getAttribute(*attributes, "TEXCOORD_0", 2, 2, p.m_uv) &&
                   file.getAttribute(*attributes, "COLOR_0",    3, 4, p.m_color);
      const std::size_t posCount = p.m_pos.m_count;
      valid = valid && p.m_normal.hasCount(posCount) && p.m_uv.hasCount(posCount) && p.m_color.hasCount(posCount);

      // indices
      if (valid && prim.find("indices") != nullptr)
      {
        valid = file.getView(prim.get<std::size_t>("indices", std::size_t(-1)), p.m_index) && p.m_index.m_componentCount == 1;
        valid = valid && (p.m_index.m_componentType == 5121 || p.m_index.m_componentType == 5123 || p.m_index.m_componentType == 5125);
        for (std::size_t i = 0; valid && i < p.m_index.m_count; ++i) valid = (p.m_index.readUInt(i) < posCount);
      }
      p.m_indexCount = (p.m_index.m_data != nullptr) ? p.m_index.m_count : posCount;
      valid = valid && (p.m_indexCount % 3 == 0);

      // material
      const std::size_t matId = prim.get<std::size_t>("material", std::size_t(-1));
      if (valid && matId != std::size_t(-1))
      {
        valid = (readMaterials != nullptr && matId < readMaterials->m_list.size());
        const json::s_node *pbr    = valid ? readMaterials->m_list[matId].find("pbrMetallicRoughness") : nullptr;
        const json::s_node *factor = (pbr != nullptr) ? pbr->find("baseColorFactor") : nullptr;
        if (factor != nullptr) valid = (_readFloats(factor->m_valueStr.c_str(), glm::value_ptr(p.m_baseColor), 4) == 4);
      }

      if (!valid)
      {
        TRE_LOG("model::loadfromGLTF: invalid or unsupported primitive \"" << p.m_name << "\" in " << gltffile);
        return false;
      }

      p.m_flatNormals = needNormals && p.m_normal.m_data == nullptr && p.m_index.m_data != nullptr; // un-share the vertices
      p.m_vertexCount = p.m_flatNormals ? p.m_indexCount : posCount;
      missingUVs |= (needUVs && p.m_uv.m_data == nullptr);
      totalVertices  += p.m_vertexCount;
      totalTriangles += p.m_indexCount / 3;

      meshParts[im].push_back(firstPartId + primitives.size());
      primitives.push_back(p);
    }
  }

  if (primitives.empty())
  {
    TRE_LOG("model::loadfromGLTF: no triangle primitive in " << gltffile);
    return false;
  }

  // Read and check the hierarchy of the default scene

  std::vector<s_modelHierarchy> rootNodes;
  if (readScenes != nullptr && !readScenes->m_list.empty())
  {
    const std::size_t        sceneId = file.m_root.get<std::size_t>("scene", 0);
    const json::s_node       noNodes;
    const json::s_node       &nodes = (readNodes != nullptr) ? *readNodes : noNodes;
    std::size_t              remainingNodes = nodes.m_list.size();
    std::vector<std::size_t> rootIds;
    const json::s_node       *sceneNodes = (sceneId < readScenes->m_list.size()) ? readScenes->m_list[sceneId].find("nodes") : nullptr;
    if (sceneNodes != nullptr) _numberList_to_Vector(sceneNodes->m_valueStr, rootIds);
    bool valid = (sceneId < readScenes->m_list.size());
    for (std::size_t nid : rootIds)
    {
      rootNodes.emplace_back();
      valid = valid && _GLTF_appendNode(nodes, meshParts, nid, remainingNodes, rootNodes.back());
    }
    if (!valid)
    {
      TRE_LOG("model::loadfromGLTF: invalid scene or nodes in " << gltffile);
      return false;
    }
  }

  // Write into the model

  if (outModel.partCount() == 0) outModel.reserveVertex(totalVertices); // single allocation

  for (std::size_t ip = 0; ip < primitives.size(); ++ip)
  {
    const s_primitive &p = primitives[ip];

    std::size_t       firstVertex = 0;
    const std::size_t partId = outModel.createPart(p.m_indexCount, p.m_vertexCount, firstVertex);
    TRE_ASSERT(partId == firstPartId + ip);
    outModel.renamePart(partId, p.m_name);

    for (std::size_t iv = 0; iv < p.m_vertexCount; ++iv)
    {
      const std::size_t src = p.m_flatNormals ? p.m_index.readUInt(iv) : iv;
      const std::size_t dst = firstVertex + iv;
      p.m_pos.readFloats(src, glm::value_ptr(outLayout.m_positions.get<glm::vec3>(dst)), 3);
      if (needNormals)
      {
        glm::vec3 &n = outLayout.m_normals.get<glm::vec3>(dst);
        n = glm::vec3(0.f);
        p.m_normal.readFloats(src, glm::value_ptr(n), 3);
      }
      if (needUVs)
      {
        glm::vec2 &uv = outLayout.m_uvs.get<glm::vec2>(dst);
        uv = glm::vec2(0.f);
        p.m_uv.readFloats(src, glm::value_ptr(uv), 2);
      }
      if (needColors)
      {
        glm::vec4 c = glm::vec4(1.f);
        p.m_color.readFloats(src, glm::value_ptr(c), 4);
        outLayout.m_colors.get<glm::vec4>(dst) = c * p.m_baseColor;
      }
      if (needSkins)
        outLayout.m_skins.get<glm::vec2>(dst) = glm::vec2(0.f); // TODO
    }

    const s_partInfo &pInfo = outModel.partInfo(partId);
    for (std::size_t ii = 0; ii < p.m_indexCount; ++ii)
    {
      const std::size_t ind = (p.m_index.m_data != nullptr && !p.m_flatNormals) ? p.m_index.readUInt(ii) : ii;
      outLayout.m_index[pInfo.m_offset + ii] = GLuint(firstVertex + ind);
    }

    outModel.computeBBoxPart(partId);

    if (needNormals && p.m_normal.m_data == nullptr)
      modelTools::computeOutNormal(outLayout, outModel.partInfo(partId), true); // flat (vertices are not shared), counter-clockwise faces
  }

  for (s_modelHierarchy &h : rootNodes) outHierarchy.m_childs.push_back(std::move(h));
  outHierarchy.m_partId    = std::size_t(-1); // the root has no part-id,
  outHierarchy.m_transform = glm::mat4(1.f);  // neither transform

  // Report

  if (missingUVs)
    TRE_LOG("Warning: the model asks for uvs, but some primitives of the GLTF file " << gltffile << " have none. The uvs are filled with zeros.");

  TRE_LOG("Load " << gltffile << " (new parts=" << primitives.size() <<
          ", vertices+=" << totalVertices << ", triangles+=" << totalTriangles << ")" <<
          ", model total (vertices=" << outLayout.m_vertexCount << ")");
  return true;
}

}
