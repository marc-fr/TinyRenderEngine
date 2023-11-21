
#include "tre_model_importer.h"

#include <map>
#include <fstream>

namespace tre {

// == OBJ =====================================================================

bool modelImporter::addFromWavefront(modelIndexed &outModel, const std::string &objfile, const std::string &mtlfile)
{
  // Get files handle
  std::ifstream rawOBJ;
  rawOBJ.open(objfile.c_str());
  if (!rawOBJ)
  {
    TRE_LOG("Failed to load wavefront file " << objfile);
    return false;
  }
  std::ifstream rawMTL;
  if (!mtlfile.empty())
  {
    rawMTL.open((mtlfile.c_str()));
    if (!rawMTL)
    {
      TRE_LOG("Warning: ignore material because it failed to load wavefront file " << mtlfile);
    }
  }
  std::string line;

  // Read colors
  std::map<std::string, glm::vec4> colorMap;
  if (rawMTL)
  {
    std::string curMat;
    while (std::getline(rawMTL, line))
    {
      if      (line.substr(0,7) == "newmtl ")
      {
        curMat = line.substr(7);
      }
      else if (line.substr(0,3) == "Kd ")
      {
        float cx, cy, cz;
        sscanf(line.substr(3).data(),"%f %f %f",&cx,&cy,&cz);
        colorMap[curMat] = glm::vec4(cx, cy, cz, 1.f);
      }
    }
  }
  rawMTL.close();

  // First pass: count

  struct s_partRead
  {
    std::size_t m_nvertices = 0;
    std::size_t m_nnormals = 0;
    std::size_t m_nuvs = 0;
    std::size_t m_ntri = 0;
    std::string m_name;
    glm::vec4   m_matColor = glm::vec4(0.f);

    std::size_t m_partId = std::size_t(-1);
    std::size_t m_vertexOffset = std::size_t(-1);
  };
  std::vector<s_partRead> partRead;

  while (std::getline(rawOBJ, line))
  {
    if      (line.substr(0,2) == "o " || line.substr(0,2) == "g ")
    {
      partRead.emplace_back();
      partRead.back().m_name = line.substr(2);
    }
    else if (line.substr(0,2) == "v " ) ++ partRead.back().m_nvertices;
    else if (line.substr(0,3) == "vn ") ++ partRead.back().m_nnormals;
    else if (line.substr(0,3) == "vt ") ++ partRead.back().m_nuvs;
    else if (line.substr(0,2) == "f " ) ++ partRead.back().m_ntri;
    else if (!colorMap.empty() && line.substr(0,7) == "usemtl ")
    {
      partRead.back().m_matColor = colorMap[line.substr(7)];
    }
  }
  rawOBJ.clear();
  rawOBJ.seekg(0,std::ios::beg);

  // Allocations and checks

  std::size_t totalVertexIn = 0;
  std::size_t totalNormalIn = 0;
  std::size_t totalUVIn = 0;
  std::size_t totalTriIn = 0;

  for (s_partRead &p : partRead)
  {
    totalVertexIn += p.m_nvertices;
    totalNormalIn += p.m_nnormals;
    totalUVIn += p.m_nuvs;
    totalTriIn += p.m_ntri;
  }

  if (partRead.empty() || totalTriIn == 0 || totalVertexIn == 0)
  {
    TRE_LOG("No part parsed in wavefront file " << objfile);
    rawOBJ.close();
    return false;
  }

  const s_modelDataLayout &outLayout = outModel.layout();

  // Second pass: read data and write index/vertex data
  std::vector<std::size_t> vertexKey;
  std::vector<glm::vec3> positionsRead, normalsRead;
  std::vector<glm::vec2> uvsRead;
  vertexKey.reserve(totalVertexIn);
  positionsRead.reserve(totalVertexIn);
  normalsRead.reserve(totalNormalIn);
  uvsRead.reserve(totalUVIn);
  outModel.reserveVertex(totalVertexIn + 2 * totalTriIn * (totalNormalIn != 0) /* pre-allocate to handle sharp edges */);
  std::size_t iPartRead = std::size_t(-1);
  std::size_t iIndex = 0;
  std::size_t vertexOffsetIn = 0;
  std::size_t vertexAddPart = 0;
  while (std::getline(rawOBJ,line))
  {
    if (line.substr(0,2) == "o " || line.substr(0,2) == "g ")
    {
      ++iPartRead;

      s_partRead &p = partRead[iPartRead];
      p.m_partId = outModel.createPart(p.m_ntri * 3, p.m_nvertices, p.m_vertexOffset);
      outModel.renamePart(p.m_partId, p.m_name);

      iIndex = 0;
      vertexOffsetIn = positionsRead.size();
      vertexAddPart = 0;
    }
    else if (line.substr(0,2) == "v ")
    {
      float x,y,z;
      sscanf(line.substr(2).data(),"%f %f %f",&x,&y,&z);
      positionsRead.emplace_back(x, y, z);
      vertexKey.push_back(0u);
    }
    else if (totalNormalIn != 0 && line.substr(0,3) == "vn ")
    {
      float x,y,z;
      sscanf(line.substr(3).data(),"%f %f %f",&x,&y,&z);
      normalsRead.emplace_back(x, y, z);
    }
    else if (totalUVIn != 0 && line.substr(0,3) == "vt ")
    {
      float x,y;
      sscanf(line.substr(3).data(),"%f %f",&x,&y);
      uvsRead.emplace_back(x, 1.f - y); // OpenGL uv coordinate-system.
    }
    else if (line.substr(0,2) == "f ")
    {
      std::array<int, 9> readBuffer;
      uint readCount = 0;
      for (std::size_t is = 2, iprev = 2, stop = line.size(); is < stop; ++is)
      {
        if (line[is] == '/' || line[is] == ' ' || is + 1 == stop)
        {
          if (is + 1 == stop) is = stop;
          TRE_ASSERT(readCount < 9);
          readBuffer[readCount++] = (iprev != is) ? std::stoi(line.substr(iprev, is - iprev)) : 0;
          iprev = is + 1;
        }
      }
      TRE_ASSERT(readCount % 3 == 0);
      //
      int pi = 0, pj = 0, pk = 0;
      int ni = 0, nj = 0, nk = 0;
      int ti = 0, tj = 0, tk = 0;
      if (readCount == 3)
      {
        pi = readBuffer[0];
        pj = readBuffer[1];
        pk = readBuffer[2];
      }
      else if (readCount == 6)
      {
        pi = readBuffer[0]; ti = readBuffer[1];
        pj = readBuffer[2]; tj = readBuffer[3];
        pk = readBuffer[4]; tk = readBuffer[5];
      }
      else // (readCount == 9)
      {
        pi = readBuffer[0]; ti = readBuffer[1]; ni = readBuffer[2];
        pj = readBuffer[3]; tj = readBuffer[4]; nj = readBuffer[5];
        pk = readBuffer[6]; tk = readBuffer[7]; nk = readBuffer[8];
      }
      TRE_ASSERT(pi !=0 && pj != 0 && pk != 0);

      // A same vertex "position" could be used multiple times. So we need to duplicate it if the "normal" or "uv" differ.

      const uint ki = (pi & 0xFFFF) | ((ni & 0xFFF) << 16) | ((ti & 0xF) << 28);
      const uint kj = (pj & 0xFFFF) | ((nj & 0xFFF) << 16) | ((tj & 0xF) << 28);
      const uint kk = (pk & 0xFFFF) | ((nk & 0xFFF) << 16) | ((tk & 0xF) << 28);

      pi = (pi < 0) ? uint(totalVertexIn) + pi : pi - 1;
      pj = (pj < 0) ? uint(totalVertexIn) + pj : pj - 1;
      pk = (pk < 0) ? uint(totalVertexIn) + pk : pk - 1;

      ni = (ni < 0) ? uint(totalNormalIn) + ni  : ni - 1;
      nj = (nj < 0) ? uint(totalNormalIn) + nj  : nj - 1;
      nk = (nk < 0) ? uint(totalNormalIn) + nk  : nk - 1;

      ti = (ti < 0) ? uint(totalUVIn) + ti      : ti - 1;
      tj = (tj < 0) ? uint(totalUVIn) + tj      : tj - 1;
      tk = (tk < 0) ? uint(totalUVIn) + tk      : tk - 1;

      const s_partRead &curPartRead = partRead[iPartRead];
      const s_partInfo &curPartInfo = outModel.partInfo(curPartRead.m_partId);

      uint i = curPartRead.m_vertexOffset + (pi - vertexOffsetIn);
      uint j = curPartRead.m_vertexOffset + (pj - vertexOffsetIn);
      uint k = curPartRead.m_vertexOffset + (pk - vertexOffsetIn);

      if (vertexKey[pi] != ki)
      {
        if (vertexKey[pi] != 0) // duplicate
        {
          i = curPartRead.m_vertexOffset + curPartRead.m_nvertices + vertexAddPart;
          ++vertexAddPart;
          outModel.reserveVertex(((i + 1) / 256 + 1) * 256);
        }
        else
        {
          vertexKey[pi] = ki;
        }
        outLayout.m_positions.get<glm::vec3>(i) = positionsRead[pi];
        if (outLayout.m_normals.m_size == 3) outLayout.m_normals.get<glm::vec3>(i) = (totalNormalIn != 0 && ni != -1) ? normalsRead[ni] : glm::vec3(0.f);
        if (outLayout.m_uvs.m_size == 2) outLayout.m_uvs.get<glm::vec2>(i) = (totalUVIn != 0 && ti != -1) ? uvsRead[ti] : glm::vec2(0.f);

        const_cast<s_partInfo&>(curPartInfo).m_bbox.addPointInBox(positionsRead[pi]);
      }

      if (vertexKey[pj] != kj)
      {
        if (vertexKey[pj] != 0) // duplicate
        {
          j = curPartRead.m_vertexOffset + curPartRead.m_nvertices + vertexAddPart;
          ++vertexAddPart;
          outModel.reserveVertex(((j + 1) / 256 + 1) * 256);
        }
        else
        {
          vertexKey[pj] = kj;
        }
        outLayout.m_positions.get<glm::vec3>(j) = positionsRead[pj];
        if (outLayout.m_normals.m_size == 3) outLayout.m_normals.get<glm::vec3>(j) = (totalNormalIn != 0 && nj != -1) ? normalsRead[nj] : glm::vec3(0.f);
        if (outLayout.m_uvs.m_size == 2) outLayout.m_uvs.get<glm::vec2>(j) = (totalUVIn != 0 && tj != -1) ? uvsRead[tj] : glm::vec2(0.f);

        const_cast<s_partInfo&>(curPartInfo).m_bbox.addPointInBox(positionsRead[pj]);
      }

      if (vertexKey[pk] != kk)
      {
        if (vertexKey[pk] != 0) // duplicate
        {
          k = curPartRead.m_vertexOffset + curPartRead.m_nvertices + vertexAddPart;
          ++vertexAddPart;
          outModel.reserveVertex(((k + 1) / 256 + 1) * 256);
        }
        else
        {
          vertexKey[pk] = kk;
        }
        outLayout.m_positions.get<glm::vec3>(k) = positionsRead[pk];
        if (outLayout.m_normals.m_size == 3) outLayout.m_normals.get<glm::vec3>(k) = (totalNormalIn != 0 && nk != -1) ? normalsRead[nk] : glm::vec3(0.f);
        if (outLayout.m_uvs.m_size == 2) outLayout.m_uvs.get<glm::vec2>(k) = (totalUVIn != 0 && tk != -1) ? uvsRead[tk] : glm::vec2(0.f);

        const_cast<s_partInfo&>(curPartInfo).m_bbox.addPointInBox(positionsRead[pk]);
      }

      TRE_ASSERT(iIndex + 2 < curPartInfo.m_size);

      outLayout.m_index[curPartInfo.m_offset + iIndex + 0] = i;
      outLayout.m_index[curPartInfo.m_offset + iIndex + 1] = j;
      outLayout.m_index[curPartInfo.m_offset + iIndex + 2] = k;

      iIndex += 3;
    }
  }
  rawOBJ.close();

  // Print some checks
  TRE_ASSERT(outLayout.m_positions.m_size == 3); // only 3D mesh
  const bool hasMatColor = !colorMap.empty() && (outLayout.m_colors.m_size == 4);
  if (outLayout.m_normals.m_size == 3 && totalNormalIn == 0)
  {
    TRE_LOG("Warning in model::loadfromWavefront: the model asks for normals, but the file does not contain normal data. The reader will fill normals with zeros.");
  }
  if (outLayout.m_uvs.m_size == 2 && totalUVIn == 0)
  {
    TRE_LOG("Warning in model::loadfromWavefront: the model asks for uvs, but the file does not contain uv data. The reader will fill uvs with zeros.");
  }
  if (outLayout.m_colors.m_size == 4 && !hasMatColor)
  {
    TRE_LOG("Warning in model::loadfromWavefront: the model asks for colors, but the file does not contain color data. The reader will fill colors with zeros.");
  }

  // Apply color
  if (hasMatColor)
  {
    for (s_partRead &p : partRead)
      outModel.colorizePart(p.m_partId, p.m_matColor);
  }

  // End
  TRE_LOG("Load " << objfile << " (new parts=" << partRead.size() <<
          ", vertices+=" << positionsRead.size() << ", triangles+=" << totalTriIn << ")" <<
          ", model total (vertices=" << outLayout.m_vertexCount << ")");
  return true;
}

// == GLTF ====================================================================

namespace json {

#define _JSON_DEBUG 0
#if (_JSON_DEBUG == 1)
#define _JSON_LOG(_msg) TRE_LOG("JSON: " << _msg);
#else
#define _JSON_LOG(_msg)
#endif

static bool isNumber(char c)
{
  return (c >= '0' && c <= '9') || (c == '-') || (c == '.');
}

static bool readString(std::ifstream &ins, std::string &out)
{
  while (true)
  {
    char cread;
    ins.read(&cread, 1);
    if (ins.fail()) return false;
    if (cread == '"') return true;
    out.push_back(cread);
  }
}

static bool readNumberStr(std::ifstream &ins, std::string &out)
{
  while (true)
  {
    char cread;
    ins.read(&cread, 1);
    if (ins.fail()) return false;
    if (cread == ']') return false;
    if (cread == ' ' || cread == ',' || cread == '\n') return true;
    if (cread == '}') { ins.seekg(-1, std::ios_base::cur); return true; }
    out.push_back(cread);
  }
}

static bool readNumberList(std::ifstream &ins, std::string &out)
{
  char clast = 0;
  while (true)
  {
    char cread = 0;
    ins.read(&cread, 1);
    if (ins.fail()) return false;
    if (cread == ']') return true;
    if (cread < '+' || cread == ',' || cread > 'z') cread = ' ';
    if (!(cread == ' ' && clast == ' ')) out.push_back(cread);
    clast = cread;
  }
}

static bool readStringList(std::ifstream &ins, std::string &out)
{
  while (true)
  {
    char cread = 0;
    ins.read(&cread, 1);
    if (ins.fail()) return false;
    if (cread == ']') return true;
    if (cread == '"')
    {
      out.push_back('"');
      if (!readString(ins, out)) return false;
      out.push_back('"');
    }
  }
}

struct s_node;
bool readFileBlock(std::ifstream &ins, std::vector<s_node> &out);

struct s_node
{
  std::string         m_key;
  std::string         m_valueStr;
  std::vector<s_node> m_list;

  bool readNodeValue(std::ifstream &ins)
  {
    // get the type: either a list, a singleton, a string, a number, a boolean
    int valueType = 0;
    while (true)
    {
      char cread = 0;
      ins.read(&cread, 1);
      if (ins.fail()) return false;
      if (cread == ' ' || cread == '\n') continue;
      if      (cread == '[') { valueType = 1; break; }
      else if (cread == '{') { valueType = 2; ins.seekg(-1, std::ios_base::cur); break; }
      else if (cread == '"') { valueType = 3; break; }
      else if (isNumber(cread)) { valueType = 4; ins.seekg(-1, std::ios_base::cur); break; }
      else if (cread == 't' || cread == 'T' || cread == 'f' || cread == 'F') { valueType = 5; ins.seekg(-1, std::ios_base::cur); break; }
      else if (cread == ',' || cread == '}' || cread == '{' || cread == ':' || cread == ']') // invalid
      {
        _JSON_LOG("invalid value of node \"" << m_key << "\"");
        return false;
      }
    }

    switch (valueType)
    {
      case 1: // open a list
      {
        char cread = 0;
        while (true)
        {
          ins.read(&cread, 1);
          if (ins.fail()) return false;
          if (cread == '{' || cread == '"' || isNumber(cread)) break;
        }
        ins.seekg(-1, std::ios_base::cur);
        if (cread == '{')
        {
          _JSON_LOG("open a list of nodes");
          while (true) // list of scopes
          {
            m_list.emplace_back();
            m_list.back().m_key = "list-element";
            if (!readFileBlock(ins, m_list.back().m_list)) return false;
            bool closeList = false;
            while (true)
            {
              ins.read(&cread, 1);
              if (ins.fail()) return false;
              if (cread == ']') { closeList = true; break; }
              if (cread == ',') { break; }
            }
            if (closeList)
            {
              _JSON_LOG("close a list of nodes");
              return true;
            }
          }
        }
        else if (cread == '"')
        {
          _JSON_LOG("open a list of strings");
          return readStringList(ins, m_valueStr);
        }
        else
        {
          _JSON_LOG("open a list of numbers");
          return readNumberList(ins, m_valueStr);
        }
      }
      break;
      case 2: // open a singleton -> the list is flatten
      {
        _JSON_LOG("open a singleton");
        return readFileBlock(ins, m_list); // it will check for the scope.
      }
      break;
      case 3: // read a string
      {
        _JSON_LOG("will read a value string");
        return readString(ins, m_valueStr);
      }
      break;
      case 4: // read a number
      {
        _JSON_LOG("will read a number");
        return readNumberStr(ins, m_valueStr);
      }
      break;
      case 5:
      {
        _JSON_LOG("will read a boolean");
        char cread = 0;
        ins.read(&cread, 1);
        if (ins.fail()) return false;
        const bool isTrue = cread == 't' || cread == 'T';
        ins.read(&cread, 1); // r a
        ins.read(&cread, 1); // u l
        ins.read(&cread, 1); // e s
        if (!isTrue) ins.read(&cread, 1); // . e
        if (cread != 'e' && cread != 'E') return false;
        m_valueStr = isTrue ? "true" : "false";
        return true;
      }
      break;
      default:
        TRE_FATAL("not reached");
        return false;
    }
  }
};

bool readFileBlock(std::ifstream &ins, std::vector<s_node> &out)
{
  // the block must start with a scope opening '{'
  while (true)
  {
    char cread;
    ins.read(&cread, 1);
    if (ins.fail()) return false;
    if (cread == '{') break;
    if (cread >= 'a') { _JSON_LOG("bad opening of a block (" << cread << ")"); return false; }
  }
  _JSON_LOG("block opened");
  out.reserve(8); // a bit less of tiny allocations
  // get the next token
  while (true)
  {
    char cread;
    ins.read(&cread, 1);
    if (ins.fail()) return false;
    if (cread == '"') // new node
    {
      out.emplace_back();
      readString(ins, out.back().m_key);
      _JSON_LOG("read new node \"" << out.back().m_key << "\"");
      while (true)
      {
        ins.read(&cread, 1);
        if (ins.fail()) return false;
        if (cread == ':') break;
      }
      if (!out.back().readNodeValue(ins)) return false;
    }
    else if (cread == '}') // closing the scope
    {
      _JSON_LOG("block closed");
      return true;
    }
  }
}

} // json

// ----------------------------------------------------------------------------

static void _numberList_to_Vector(const std::string &str, std::vector<std::size_t> &outV)
{
  if (str.empty()) return;
  std::size_t sep = str.find(' ');
  std::size_t b = 0u;
  while (sep != std::string::npos)
  {
    std::size_t vread = 0u;
    sscanf(str.substr(b, sep - b).data(),"%lu",&vread);
    outV.push_back(vread);
    b = sep + 1;
    sep = str.find(' ', b);
  }
  if (b < str.size())
  {
    std::size_t vread = 0u;
    sscanf(str.substr(b).data(),"%lu",&vread);
    outV.push_back(vread);
  }
}

// ----------------------------------------------------------------------------

static void _GLTF_readNode(const json::s_node &nns, std::size_t &meshId, glm::vec3 &tr, glm::quat &rot, std::vector<std::size_t> &children)
{
  TRE_ASSERT(nns.m_key.compare("list-element") == 0);
  tr = glm::vec3(0.f);
  rot = glm::quat(1.f, 0.f, 0.f, 0.f);
  for (const json::s_node &nn : nns.m_list)
  {
    if      (nn.m_key.compare("mesh") == 0) sscanf(nn.m_valueStr.data(),"%lu",&meshId);
    else if (nn.m_key.compare("translation") == 0) sscanf(nn.m_valueStr.data(),"%f %f %f",&tr.x, &tr.y, &tr.z);
    else if (nn.m_key.compare("rotation") == 0) sscanf(nn.m_valueStr.data(),"%f %f %f %f",&rot.x, &rot.y, &rot.z,&rot.w);
    else if (nn.m_key.compare("children") == 0) _numberList_to_Vector(nn.m_valueStr, children);
  }
}

static void _GLTF_readAccessor(const json::s_node &na, std::size_t &bufferViewId, std::size_t &count, bool &isHalfPrecision, const char *expectedType)
{
  TRE_ASSERT(na.m_key.compare("list-element") == 0);
  for (const json::s_node &nn : na.m_list)
  {
    if      (nn.m_key.compare("bufferView") == 0) sscanf(nn.m_valueStr.data(),"%lu",&bufferViewId);
    else if (nn.m_key.compare("count") == 0) sscanf(nn.m_valueStr.data(),"%lu",&count);
    else if (nn.m_key.compare("type") == 0)
    {
      if (nn.m_valueStr.compare(expectedType) != 0) { TRE_LOG("model::loadfromGLTF: un-expected type \"" << nn.m_valueStr << "\" of a " << expectedType << " buffer"); }
    }
    else if (nn.m_key.compare("componentType") == 0)
    {
      std::size_t ctype = 0;
      sscanf(nn.m_valueStr.data(),"%lu",&ctype);
      isHalfPrecision = (ctype == 5123);
    }
  }
}

static void _GLTF_readBufferView(const json::s_node &nbf, std::size_t &bufferId, std::size_t &byteOffset, const std::size_t &expectedByteLength)
{
  TRE_ASSERT(nbf.m_key.compare("list-element") == 0);
  for (const json::s_node &nn : nbf.m_list)
  {
    if      (nn.m_key.compare("buffer") == 0) sscanf(nn.m_valueStr.data(),"%lu",&bufferId);
    else if (nn.m_key.compare("byteOffset") == 0) sscanf(nn.m_valueStr.data(),"%lu",&byteOffset);
    else if (nn.m_key.compare("byteLength") == 0)
    {
      std::size_t rbl = 0;
      sscanf(nn.m_valueStr.data(),"%lu",&rbl);
      if (rbl != expectedByteLength) { TRE_LOG("model::loadfromGLTF: un-expected byte-length (" << rbl << ") for a buffer (expected: " << expectedByteLength << ")"); } }
  }
}

static uint8_t _decode_base64_char(const char c)
{
  if (c >= 97 /*a*/) return c - 97 + 26;
  if (c >= 65 /*A*/) return c - 65;
  if (c >= 48 /*0*/) return c - 48 + 52;
  if (c == 43 /*+*/) return 62;
  TRE_ASSERT(c == 47 /*/*/);
  return 63;
}

static void _decode_base64(std::vector<uint8_t> &out, const std::string &inEncoded, std::size_t inOffset = 0)
{
  const std::size_t cStop = inEncoded.size();
  const std::size_t cStop3 = (cStop > 4) ? cStop - 4 : 0;
  std::size_t       cp = inOffset;
  uint8_t           * __restrict dstBuffer = out.data();
  const uint8_t     * __restrict dstStop = out.data() + out.size();
  for (; cp < cStop3; cp += 4)
  {
    const uint8_t c0 = _decode_base64_char(inEncoded[cp]    );
    const uint8_t c1 = _decode_base64_char(inEncoded[cp + 1]);
    const uint8_t c2 = _decode_base64_char(inEncoded[cp + 2]);
    const uint8_t c3 = _decode_base64_char(inEncoded[cp + 3]);
    TRE_ASSERT(c0 < 64 && c1 < 64 && c2 < 64);
    const uint8_t d0 = (c0       ) << 2 | (c1 & 0x30) >> 4;
    const uint8_t d1 = (c1 & 0x0F) << 4 | (c2 & 0x3C) >> 2;
    const uint8_t d2 = (c2       ) << 6 | (c3       )     ;
    *dstBuffer++ = d0;
    *dstBuffer++ = d1;
    *dstBuffer++ = d2;
    TRE_ASSERT(dstBuffer <= dstStop);
  }
  if (cp < cStop)
  {
    const uint8_t c0 =                _decode_base64_char(inEncoded[cp++]) ;
    const uint8_t c1 = (cp < cStop) ? _decode_base64_char(inEncoded[cp++]) : 0;
    const uint8_t c2 = (cp < cStop) ? _decode_base64_char(inEncoded[cp++]) : 0;
    const uint8_t c3 = (cp < cStop) ? _decode_base64_char(inEncoded[cp++]) : 0;
    TRE_ASSERT(c0 < 64 && c1 < 64 && c2 < 64);
    const uint8_t d0 = (c0       ) << 2 | (c1 & 0x30) >> 4;
    const uint8_t d1 = (c1 & 0x0F) << 4 | (c2 & 0x3C) >> 2;
    const uint8_t d2 = (c2       ) << 6 | (c3       )     ;
    *dstBuffer++ = d0;
    *dstBuffer++ = d1;
    *dstBuffer++ = d2;
    TRE_ASSERT(dstBuffer <= dstStop);
  }
  for (; dstBuffer < dstStop; ++dstBuffer) *dstBuffer = 0;
}

bool modelImporter::addFromGLTF(modelIndexed &outModel, s_modelHierarchy &outHierarchy, const std::string &gltffile, const bool isBinary)
{
  // dirname
  const std::size_t lastSlash = gltffile.rfind('/');
  const std::string dirname = (lastSlash != std::string::npos) ? gltffile.substr(0, lastSlash) + "/" : "";

  // Get files handle
  std::ifstream readerGLTF;
  readerGLTF.open(gltffile.c_str(), std::ios_base::binary);
  if (!readerGLTF)
  {
    TRE_LOG("Failed to load GLTF file " << gltffile);
    return false;
  }

  std::size_t glbNextBlock = 0;
  if (isBinary)
  {
    // read header.
    uint32_t hType = 0, version = 0, dummy = 0, jbSize = 0, jbCode = 0;
    readerGLTF.read(reinterpret_cast<char*>(&hType), sizeof(uint32_t));
    readerGLTF.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
    readerGLTF.read(reinterpret_cast<char*>(&dummy), sizeof(uint32_t));
    readerGLTF.read(reinterpret_cast<char*>(&jbSize), sizeof(uint32_t));
    readerGLTF.read(reinterpret_cast<char*>(&jbCode), sizeof(uint32_t));
    if (hType != 0x46546C67 /* glTF */ || version != 2 || jbCode != 0x4E4F534A /* JSON */)
    {
      TRE_LOG("Invalid header of a binary GLTF file " << gltffile);
      readerGLTF.close();
      return false;
    }
    glbNextBlock = 12 + 8 + jbSize;
  }

  std::vector<json::s_node> readJsonNodes;
  if (!json::readFileBlock(readerGLTF, readJsonNodes))
  {
    readerGLTF.close();
    TRE_LOG("Failed to parse GLTF file (json format)");
    return false;
  }

  json::s_node *readScenes = nullptr;
  json::s_node *readNodes = nullptr;
  json::s_node *readMeshes = nullptr;
  json::s_node *readAccessors = nullptr;
  json::s_node *readBufferViews = nullptr;
  json::s_node *readBuffers = nullptr;

  for (json::s_node &n : readJsonNodes)
  {
    if      (n.m_key.compare("scenes")      == 0) readScenes = &n;
    else if (n.m_key.compare("nodes")       == 0) readNodes = &n;
    else if (n.m_key.compare("meshes")      == 0) readMeshes = &n;
    else if (n.m_key.compare("accessors")   == 0) readAccessors = &n;
    else if (n.m_key.compare("bufferViews") == 0) readBufferViews = &n;
    else if (n.m_key.compare("buffers")     == 0) readBuffers = &n;
  }

  if (readMeshes == nullptr || readAccessors == nullptr || readBufferViews == nullptr || readBuffers == nullptr)
  {
    TRE_LOG("model::loadfromGLTF: missing at least one of the mandatory lists (meshes, accessors, bufferViews, buffers)");
    return false;
  }

  // read and check parts
  struct s_partRead
  {
    std::size_t m_partId = std::size_t(-1);
    std::size_t m_indexCount = std::size_t(-1);
    std::size_t m_vertexCount = std::size_t(-1);
    std::size_t m_bufferViewId_pos = std::size_t(-1);
    std::size_t m_bufferViewId_normal = std::size_t(-1);
    std::size_t m_bufferViewId_uv = std::size_t(-1);
    std::size_t m_bufferViewId_index = std::size_t(-1);
    std::string m_name;
    bool        m_indiceHalf = false; // U16:true, U32:false
  };
  std::vector<s_partRead> partRead;
  for (const json::s_node &n : readMeshes->m_list)
  {
    TRE_ASSERT(n.m_key.compare("list-element") == 0);
    partRead.emplace_back();
    s_partRead &curPartRead = partRead.back();
    std::size_t partVertex = 0;
    std::size_t partIndex = 0;
    // read the mesh
    for (const json::s_node &nn : n.m_list)
    {
      if (nn.m_key.compare("name") == 0)
      {
        curPartRead.m_name = nn.m_valueStr;
      }
      else if (nn.m_key.compare("primitives") == 0)
      {
        TRE_ASSERT(nn.m_list.size() == 1); // for now, only support mesh with only one "primitive" def
        TRE_ASSERT(nn.m_list[0].m_key.compare("list-element") == 0);
        for (const json::s_node &np : nn.m_list[0].m_list)
        {
          if (np.m_key.compare("attributes") == 0)
          {
            for (const json::s_node &na : np.m_list)
            {
              if      (na.m_key.compare("POSITION") == 0) sscanf(na.m_valueStr.data(),"%lu",&curPartRead.m_bufferViewId_pos); // for now, it stores the accessor id.
              else if (na.m_key.compare("NORMAL") == 0) sscanf(na.m_valueStr.data(),"%lu",&curPartRead.m_bufferViewId_normal); // for now, it stores the accessor id.
              else if (na.m_key.compare("TEXCOORD_0") == 0) sscanf(na.m_valueStr.data(),"%lu",&curPartRead.m_bufferViewId_uv); // for now, it stores the accessor id.
              else    { TRE_LOG("model::loadfromGLTF: unkown attribute \"" << na.m_key << "\""); }
            }
          }
          else if (np.m_key.compare("indices") == 0)
          {
            sscanf(np.m_valueStr.data(),"%lu",&curPartRead.m_bufferViewId_index); // for now, it stores the accessor id.
          }
          else if (np.m_key.compare("modes") == 0)
          {
            std::size_t m = 0;
            sscanf(np.m_valueStr.data(),"%lu",&m);
            if (m != 4) // GL_TRIANGLES
            {
              TRE_LOG("model::loadfromGLTF: the primitives mode is not TRIANGLES(4): " <<  m << ". This part may not be rendered correctly.");
            }
          }
          else
          {
            TRE_LOG("model::loadfromGLTF: unkown primitives \"" <<  np.m_key << "\"");
          }
        }
      }
      else
      {
        TRE_LOG("model::loadfromGLTF: unkown mesh property \"" <<  nn.m_key << "\"");
      }
    }
    // get buffersView from their accessors
    // -> position
    if (curPartRead.m_bufferViewId_pos < readAccessors->m_list.size())
    {
      const json::s_node &na = readAccessors->m_list[curPartRead.m_bufferViewId_pos];
      curPartRead.m_bufferViewId_pos = std::size_t(-1); // reset
      bool dummy = false;
      _GLTF_readAccessor(na, curPartRead.m_bufferViewId_pos, partVertex, dummy, "VEC3");
    }
    // -> normal
    if (curPartRead.m_bufferViewId_normal < readAccessors->m_list.size())
    {
      const json::s_node &na = readAccessors->m_list[curPartRead.m_bufferViewId_normal];
      curPartRead.m_bufferViewId_normal = std::size_t(-1); // reset
      std::size_t c = 0;
      bool dummy = false;
      _GLTF_readAccessor(na, curPartRead.m_bufferViewId_normal, c, dummy, "VEC3");
    }
    // -> uv
    if (curPartRead.m_bufferViewId_uv < readAccessors->m_list.size())
    {
      const json::s_node &na = readAccessors->m_list[curPartRead.m_bufferViewId_uv];
      curPartRead.m_bufferViewId_uv = std::size_t(-1); // reset
      std::size_t c = 0;
      bool dummy = false;
      _GLTF_readAccessor(na, curPartRead.m_bufferViewId_uv, c, dummy, "VEC2");
    }
    // -> index
    if (curPartRead.m_bufferViewId_index < readAccessors->m_list.size())
    {
      const json::s_node &na = readAccessors->m_list[curPartRead.m_bufferViewId_index];
      curPartRead.m_bufferViewId_index = std::size_t(-1); // reset
      _GLTF_readAccessor(na, curPartRead.m_bufferViewId_index, partIndex, curPartRead.m_indiceHalf, "SCALAR");
    }

    if (partIndex == 0 || partVertex == 0)
    {
      TRE_LOG("model::loadfromGLTF: invalid mesh " << partRead.size()-1  << ":\"" << curPartRead.m_name << "\". Abort.");
      return false;
    }

    curPartRead.m_indexCount = partIndex;
    curPartRead.m_vertexCount = partVertex;
  }

  // read and check buffers
  struct s_buffer
  {
    std::vector<uint8_t> m_rawData;
  };
  std::vector<s_buffer> readBuffersResolved;
  for (const json::s_node &n : readBuffers->m_list)
  {
    TRE_ASSERT(n.m_key.compare("list-element") == 0);
    readBuffersResolved.emplace_back();
    s_buffer &currentBuffer = readBuffersResolved.back();
    bool hasURIData = false;
    // read the buffer
    for (const json::s_node &nn : n.m_list)
    {
      if (nn.m_key.compare("byteLength") == 0)
      {
        std::size_t blen = 0;
        sscanf(nn.m_valueStr.data(),"%lu",&blen);
        currentBuffer.m_rawData.resize(blen + 4); // encoding can introduce padding.
      }
      else if (nn.m_key.compare("uri") == 0)
      {
        hasURIData = true;
        const std::size_t sepPos = nn.m_valueStr.find(",");
        if (sepPos != std::string::npos && (nn.m_valueStr.substr(0, 16).compare("data:application") == 0))
        {
          _decode_base64(currentBuffer.m_rawData, nn.m_valueStr, sepPos + 1u);
        }
        else
        {
          const std::string fullpath = dirname + nn.m_valueStr;
          std::ifstream readerBuffer;
          readerBuffer.open(fullpath.c_str(), std::ios::basic_ios::binary);
          if (readerBuffer)
          {
            readerBuffer.read(reinterpret_cast<char*>(currentBuffer.m_rawData.data()), currentBuffer.m_rawData.size());
            readerBuffer.close();
          }
          else
          {
            currentBuffer.m_rawData.clear();
            TRE_LOG("Failed to load GLTF buffer file " << fullpath);
          }
        }
      }
      else
      {
        TRE_LOG("model::loadfromGLTF: unkown buffer property \"" <<  nn.m_key << "\"");
      }
    }
    if (isBinary && !hasURIData)
    {
      readerGLTF.seekg(glbNextBlock, std::ios_base::beg);
      // read BIN chunk ...
      uint32_t jbSize = 0, jbCode = 0;
      readerGLTF.read(reinterpret_cast<char*>(&jbSize), sizeof(uint32_t));
      readerGLTF.read(reinterpret_cast<char*>(&jbCode), sizeof(uint32_t));
      if (jbCode != 0x004E4942 /* BIN */ || jbSize == 0)
      {
        TRE_LOG("Invalid chunck for the buffer " <<  readBuffersResolved.size()-1 << " of a binary GLTF file " << gltffile);
        currentBuffer.m_rawData.clear();
      }
      else
      {
        glbNextBlock += 8 + jbSize;
        readerGLTF.read(reinterpret_cast<char*>(currentBuffer.m_rawData.data()), std::min(currentBuffer.m_rawData.size(), std::size_t(jbSize)));
      }
    }
  }

  readerGLTF.close();

  // fill data ...

  const s_modelDataLayout &outLayout = outModel.layout();

  for (s_partRead &curPartRead : partRead)
  {
    std::size_t vertexOffset = 0;
    curPartRead.m_partId = outModel.createPart(curPartRead.m_indexCount, curPartRead.m_vertexCount, vertexOffset);
    outModel.renamePart(curPartRead.m_partId, curPartRead.m_name);

    const s_partInfo &curPartInfo = outModel.partInfo(curPartRead.m_partId);

    const glm::vec3 *bufferPos = nullptr;
    const glm::vec3 *bufferNor = nullptr;
    const glm::vec2 *bufferUV = nullptr;
    const uint32_t  *bufferInd32 = nullptr;
    const uint16_t  *bufferInd16 = nullptr;

    if (curPartRead.m_bufferViewId_pos < readBufferViews->m_list.size())
    {
      std::size_t bufIdx = 0, bufOffset = 0;
      _GLTF_readBufferView(readBufferViews->m_list[curPartRead.m_bufferViewId_pos], bufIdx, bufOffset, curPartRead.m_vertexCount * 3 * sizeof(float));
      if (bufOffset < readBuffersResolved[bufIdx].m_rawData.size())
        bufferPos =  reinterpret_cast<const glm::vec3*>(& readBuffersResolved[bufIdx].m_rawData[bufOffset]);
    }
    if (bufferPos == nullptr)
    {
      TRE_LOG("model::loadfromGLTF: no position-buffer found for mesh \"" << curPartInfo.m_name << "\". Abort.");
      return false;
    }

    if (curPartRead.m_bufferViewId_normal < readBufferViews->m_list.size())
    {
      std::size_t bufIdx = 0, bufOffset = 0;
      _GLTF_readBufferView(readBufferViews->m_list[curPartRead.m_bufferViewId_normal], bufIdx, bufOffset, curPartRead.m_vertexCount * 3 * sizeof(float));
      if (bufOffset < readBuffersResolved[bufIdx].m_rawData.size())
        bufferNor =  reinterpret_cast<const glm::vec3*>(& readBuffersResolved[bufIdx].m_rawData[bufOffset]);
    }
    if (bufferNor == nullptr && outLayout.m_normals.hasData())
    {
      TRE_LOG("model::loadfromGLTF: no normal-buffer found for mesh \"" << curPartInfo.m_name << "\" but the model has normal-data. Zeros will be written.");
    }

    if (curPartRead.m_bufferViewId_uv < readBufferViews->m_list.size())
    {
      std::size_t bufIdx = 0, bufOffset = 0;
      _GLTF_readBufferView(readBufferViews->m_list[curPartRead.m_bufferViewId_uv], bufIdx, bufOffset, curPartRead.m_vertexCount * 2 * sizeof(float));
      if (bufOffset < readBuffersResolved[bufIdx].m_rawData.size())
        bufferUV =  reinterpret_cast<const glm::vec2*>(& readBuffersResolved[bufIdx].m_rawData[bufOffset]);
    }
    if (bufferUV == nullptr && outLayout.m_uvs.hasData())
    {
      TRE_LOG("model::loadfromGLTF: no uv-buffer found for mesh \"" << curPartInfo.m_name << "\" but the model has uv-data. Zeros will be written.");
    }

    if (curPartRead.m_bufferViewId_index < readBufferViews->m_list.size())
    {
      std::size_t bufIdx = 0, bufOffset = 0;
      _GLTF_readBufferView(readBufferViews->m_list[curPartRead.m_bufferViewId_index], bufIdx, bufOffset, curPartInfo.m_size * (curPartRead.m_indiceHalf ? 2 : 4));
      if (bufOffset < readBuffersResolved[bufIdx].m_rawData.size())
      {
        if (curPartRead.m_indiceHalf) bufferInd16 =  reinterpret_cast<const uint16_t*>(& readBuffersResolved[bufIdx].m_rawData[bufOffset]);
        else                          bufferInd32 =  reinterpret_cast<const uint32_t*>(& readBuffersResolved[bufIdx].m_rawData[bufOffset]);
      }
    }
    if (bufferInd32 == nullptr && bufferInd16 == nullptr)
    {
      TRE_LOG("model::loadfromGLTF: no index-buffer found for mesh \"" << curPartInfo.m_name << "\". Abort.");
      return false;
    }

    const bool fillNor = (bufferNor != nullptr && outLayout.m_normals.hasData());
    const bool fillNor_zero = (bufferNor == nullptr && outLayout.m_normals.hasData());
    const bool fillUV = (bufferUV != nullptr && outLayout.m_uvs.hasData());
    const bool fillUV_zero = (bufferUV == nullptr && outLayout.m_uvs.hasData());

    // fill vertex-data
    for (std::size_t iv = 0; iv < curPartRead.m_vertexCount; ++iv)
    {
      const glm::vec3 pos = *bufferPos++;
      outLayout.m_positions.get<glm::vec3>(vertexOffset + iv) = pos;

      const_cast<s_partInfo&>(curPartInfo).m_bbox.addPointInBox(pos);

      if (fillNor) outLayout.m_normals.get<glm::vec3>(vertexOffset + iv) = *bufferNor++;
      else if (fillNor_zero) outLayout.m_normals.get<glm::vec3>(vertexOffset + iv) = glm::vec3(0.f);

      if (fillUV) outLayout.m_uvs.get<glm::vec2>(vertexOffset + iv) = *bufferUV++;
      else if (fillUV_zero) outLayout.m_uvs.get<glm::vec2>(vertexOffset + iv) = glm::vec2(0.f);
    }

    // fill index-data
    if (bufferInd32 != nullptr)
    {
      for (std::size_t ii = 0; ii < curPartInfo.m_size; ++ii)
        outLayout.m_index[curPartInfo.m_offset + ii] = vertexOffset + *bufferInd32++;
    }
    else // if (bufferInd16 != nullptr)
    {
      for (std::size_t ii = 0; ii < curPartInfo.m_size; ++ii)
        outLayout.m_index[curPartInfo.m_offset + ii] = vertexOffset + (uint32_t(*bufferInd16++) & 0x0000FFFF);
    }
  }

  // read model hierarchy
  if (readScenes != nullptr && readNodes != nullptr)
  {
    struct s_nodeRead
    {
      std::size_t              m_meshId;
      glm::mat4                m_tr;
      std::vector<std::size_t> m_children;

      static void appendNodeToHierarchy(const std::vector<s_nodeRead> &nr, const std::vector<s_partRead> &pr, std::size_t nid, s_modelHierarchy &out) // recursive
      {
        const s_nodeRead &nri = nr[nid];
        out.m_partId = pr[nri.m_meshId].m_partId;
        out.m_transform = nri.m_tr;
        for (std::size_t cid : nri.m_children)
        {
          TRE_ASSERT(cid < nr.size());
          out.m_childs.emplace_back();
          appendNodeToHierarchy(nr, pr, cid, out.m_childs.back());
        }
      }
    };
    std::vector<s_nodeRead> nodeRead;

    for (const json::s_node &nn : readNodes->m_list)
    {
      nodeRead.emplace_back();
      glm::vec3 trs;
      glm::quat rot;
      _GLTF_readNode(nn, nodeRead.back().m_meshId, trs, rot, nodeRead.back().m_children);
      nodeRead.back().m_tr = glm::mat4_cast(rot);
      nodeRead.back().m_tr[3] = glm::vec4(trs, 1.f);
      TRE_ASSERT(nodeRead.back().m_meshId < partRead.size());
    }

    for (const json::s_node &n : readScenes->m_list)
    {
      TRE_ASSERT(n.m_key.compare("list-element") == 0);

      // read the scene
      std::vector<std::size_t> sceneRootNodes;
      for (const json::s_node &nn : n.m_list)
      {
        if (nn.m_key.compare("nodes") == 0) _numberList_to_Vector(nn.m_valueStr, sceneRootNodes);
      }

      for (std::size_t nid : sceneRootNodes)
      {
        TRE_ASSERT(nid < nodeRead.size());
        outHierarchy.m_childs.emplace_back();
        s_nodeRead::appendNodeToHierarchy(nodeRead, partRead, nid, outHierarchy.m_childs.back());
      }
    }
  }

  outHierarchy.m_partId = std::size_t(-1);   // the root has no part-id,
  outHierarchy.m_transform = glm::mat4(1.f); // neither transform

  // End
  TRE_LOG("Load " << gltffile << " (new parts=" << partRead.size() << ")" <<
          ", model total (vertices=" << outLayout.m_vertexCount << ", triangles=" << (outLayout.m_indexCount) / 3 << ")");

  return true;
}

}
