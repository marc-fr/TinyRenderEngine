#include "tre_ui.h"

#include "tre_shader.h"
#include "tre_font.h"

namespace tre {

// Size =======================================================================

const ui::s_size ui::s_size::ONE_PIXEL = ui::s_size(1.f, ui::SIZE_PIXEL);

// Color ======================================================================

glm::vec4 ui::transformColor(const glm::vec4 &baseColor, ui::e_colorThemeMode mode, float cursor)
{
  if (cursor == 0.f)
    return baseColor;

  switch (mode)
  {
    case ui::COLORTHEME_LIGHTNESS:
    {
      const float maxV = glm::max(baseColor.r, glm::max(baseColor.g, baseColor.b));
      const float minV = glm::min(baseColor.r, glm::min(baseColor.g, baseColor.b));
      const float lightness = 0.5f * (maxV + minV);
      const float lightnessNew = glm::clamp(lightness + cursor, 0.f, 1.f);
      const float sum = 2 * lightnessNew; // = maxVnew + minVnew
      const float diff = (maxV - minV) * (1.f - fabsf(2.f * lightnessNew - 1.f)) / (1.f - fabsf(2.f * lightness - 1.f) + 1.e-5f); // = maxVnew - minVnew
      const float maxVnew = 0.5f * (sum + diff);
      const float minVnew = 0.5f * (sum - diff);
      const float vMul = (maxV - minV < 1.e-3f) ? 0.f : (maxVnew - minVnew) / (maxV - minV);
      const float vAdd = minVnew - minV * vMul;
      return glm::vec4(vMul, vMul, vMul, 1.f) * baseColor + glm::vec4(vAdd, vAdd, vAdd, 0.f);
    }
    case ui::COLORTHEME_SATURATION:
    {
      const float maxV = glm::max(baseColor.r, glm::max(baseColor.g, baseColor.b));
      const float minV = glm::min(baseColor.r, glm::min(baseColor.g, baseColor.b));
      const float lightness = 0.5f * (maxV + minV);
      const float saturation = glm::min((maxV - minV) / (1.f - fabsf(2.f * lightness - 1.f) + 1.e-5f), 1.f);
      const float saturationNew = glm::clamp(saturation + cursor, 0.f, 1.f);
      const float sum = 2 * lightness; // = maxVnew + minVnew
      const float diff = saturationNew * (1.f - fabsf(2.f * lightness - 1.f)); // = maxVnew - minVnew
      const float maxVnew = 0.5f * (sum + diff);
      const float minVnew = 0.5f * (sum - diff);
      const float vMul = (maxV - minV < 1.e-3f) ? 0.f : (maxVnew - minVnew) / (maxV - minV);
      const float vAdd = minVnew - minV * vMul;
      return glm::vec4(vMul, vMul, vMul, 1.f) * baseColor + glm::vec4(vAdd, vAdd, vAdd, 0.f);
    }
    case ui::COLORTHEME_HUE:
    {
      const float maxV = glm::max(baseColor.r, glm::max(baseColor.g, baseColor.b));
      const float minV = glm::min(baseColor.r, glm::min(baseColor.g, baseColor.b));
      const float c = maxV - minV;
      const float invc = (c < 1.e-5f) ? 0.f : 1.f / c;
      const float hue = (baseColor.r >= maxV) ? (baseColor.g - baseColor.b) * invc :
                        ((baseColor.g >= maxV) ? (baseColor.b - baseColor.r) * invc + 2.f :
                                                 (baseColor.r - baseColor.g) * invc + 4.f );
      const float hueNew = hue + cursor * 6.f;
      const float hueNew_06 = fmodf(hueNew + 60.f, 6.f);
      glm::vec3 rgb = glm::vec3(2.f - fabsf(hueNew_06 - 3.f), fabsf(hueNew_06 - 2.f) - 1.f, fabsf(hueNew_06 - 4.f) - 1.f);
      rgb = maxV - c * glm::clamp(rgb, 0.f, 1.f);
      return glm::vec4(rgb, baseColor.a);
    }
  }
  return baseColor;
}

glm::vec4 ui::inverseColor(const glm::vec4 &baseColor, ui::e_colorThemeMode mode)
{
  switch (mode)
  {
    case ui::COLORTHEME_LIGHTNESS:
    {
      const float maxV = glm::max(baseColor.r, glm::max(baseColor.g, baseColor.b));
      const float minV = glm::min(baseColor.r, glm::min(baseColor.g, baseColor.b));
      const float lightness = 0.5f * (maxV + minV);
      const float lightnessNew = lightness < 0.45f ? lightness + 0.5f : lightness * 0.5f - 0.1f;
      const float sum = 2 * lightnessNew; // = maxVnew + minVnew
      const float diff = (maxV - minV) * (1.f - fabsf(2.f * lightnessNew - 1.f)) / (1.f - fabsf(2.f * lightness - 1.f) + 1.e-5f); // = maxVnew - minVnew
      const float maxVnew = 0.5f * (sum + diff);
      const float minVnew = 0.5f * (sum - diff);
      const float vMul = (maxV - minV < 1.e-3f) ? 0.f : (maxVnew - minVnew) / (maxV - minV);
      const float vAdd = minVnew - minV * vMul;
      return glm::vec4(vMul, vMul, vMul, 1.f) * baseColor + glm::vec4(vAdd, vAdd, vAdd, 0.f);
    }
    case ui::COLORTHEME_SATURATION:
    {
      const float maxV = glm::max(baseColor.r, glm::max(baseColor.g, baseColor.b));
      const float minV = glm::min(baseColor.r, glm::min(baseColor.g, baseColor.b));
      const float lightness = 0.5f * (maxV + minV);
      const float saturation = glm::min((maxV - minV) / (1.f - fabsf(2.f * lightness - 1.f) + 1.e-5f), 1.f);
      const float saturationNew = saturation < 0.5f ? glm::min(saturation + 0.8f, 1.f) : glm::max(saturation - 0.8f, 0.f);
      const float sum = 2 * lightness; // = maxVnew + minVnew
      const float diff = saturationNew * (1.f - fabsf(2.f * lightness - 1.f)); // = maxVnew - minVnew
      const float maxVnew = 0.5f * (sum + diff);
      const float minVnew = 0.5f * (sum - diff);
      const float vMul = (maxV - minV < 1.e-3f) ? 0.f : (maxVnew - minVnew) / (maxV - minV);
      const float vAdd = minVnew - minV * vMul;
      return glm::vec4(vMul, vMul, vMul, 1.f) * baseColor + glm::vec4(vAdd, vAdd, vAdd, 0.f);
    }
    case ui::COLORTHEME_HUE:
    {
      const float maxV = glm::max(baseColor.r, glm::max(baseColor.g, baseColor.b));
      const float minV = glm::min(baseColor.r, glm::min(baseColor.g, baseColor.b));
      const float minPmax = maxV + minV;
      return glm::vec4(-1.f, -1.f, -1.f, 1.f) * baseColor + glm::vec4(minPmax, minPmax, minPmax, 0.f);
    }
  }
  return baseColor;
}

glm::vec4 ui::blendColor(const glm::vec4 &frontColor, const glm::vec4 &backColor, float cursor)
{
  const float cursorRGB = cursor * glm::min(backColor.w / (1.e-5f + frontColor.w), 1.f);
  return frontColor + (backColor - frontColor) * glm::vec4(cursorRGB, cursorRGB, cursorRGB, cursor);
}

// baseUI methods : global settings ===========================================

void baseUI::animate(float dt)
{
  for (auto win : windowsList)
  {
    if (win != nullptr) win->animate(dt);
  }
}

void baseUI::clear()
{
  for (auto win : windowsList) delete(win);
  windowsList.clear();
  m_model.clearParts();
  m_textures.fill(nullptr);
}

// baseUI methods : builtin language support ==================================

void baseUI::set_language(std::size_t lid)
{
  TRE_ASSERT(lid < TRE_UI_NLANGUAGES);
  if (lid == m_language) return;
  m_language = lid;
  for (auto win : windowsList) win->m_isUpdateNeededAdress = win->m_isUpdateNeededLayout = win->m_isUpdateNeededData = true;
}

// baseUI methods : GPU interface =============================================

bool baseUI::loadIntoGPU()
{
  TRE_ASSERT(!m_model.isLoadedGPU());

  if (!m_textureWhite.loadWhite()) return false;

  createData();
  updateData();
  return m_model.loadIntoGPU();
}

void baseUI::updateIntoGPU()
{
  TRE_ASSERT(m_model.isLoadedGPU());
  updateData();
  m_model.updateIntoGPU();
}

void baseUI::clearGPU()
{
  m_model.clearGPU();
  m_textureWhite.clear();
}

std::size_t baseUI::addTexture(const texture *t)
{
  for (std::size_t i = 0; i < m_textures.size(); ++i)
  {
    if (m_textures[i] == t) return i;
  }
  for (std::size_t i = 0; i < m_textures.size(); ++i)
  {
    if (m_textures[i] == nullptr)
    {
      m_textures[i] = t;
      return i;
    }
  }
  return std::size_t(-1);
}

std::size_t baseUI::getTextureSlot(const texture *t) const
{
  for (std::size_t i = 0; i < m_textures.size(); ++i)
  {
    if (m_textures[i] == t) return i;
  }
  return std::size_t(-1);
}

ui::window *  baseUI::create_window()
{
  TRE_ASSERT(!m_model.isLoadedGPU());
  ui::window * newwin = new ui::window(this);
  if (newwin == nullptr) return nullptr; // out of memory ?!
  newwin->m_isUpdateNeededAdress = true;
  windowsList.push_back(newwin);
  return newwin;
}

void baseUI::clearShader()
{
  if (m_shaderOwner && m_shader != nullptr)
  {
    m_shader->clearShader();
    delete m_shader;
  }
  m_shader = nullptr;
  m_shaderOwner = true;
}

void baseUI::createData()
{
  m_model.clearParts();

  for (ui::window * curwin : windowsList)
  {
    curwin->m_adSolid.part = m_model.createPart(0u); // solid
    curwin->m_adrLine.part = m_model.createPart(0u); // line
    curwin->m_adrPict.part = m_model.createPart(0u); // one for each texture
    for (std::size_t i = 1; i < m_textures.size(); ++i) m_model.createPart(0u); // one for each texture (note: assume that the parts are created in order)
    curwin->m_adrText.part = m_model.createPart(0u); // text
  }

  TRE_ASSERT(m_model.partCount() == (3 + m_textures.size()) * windowsList.size())
}

void baseUI::updateData()
{
  for (ui::window * curwin : windowsList)
  {
    curwin->compute_adressPlage();
    curwin->compute_layout();
    curwin->compute_data();
  }
}

// baseUI2D implementation ========================================

void baseUI2D::draw() const
{
  TRE_ASSERT(m_shader != nullptr);
  TRE_ASSERT(m_textureWhite.m_handle != 0);
  TRE_ASSERT(glIsEnabled(GL_BLEND)==GL_TRUE);
  TRE_ASSERT(glIsEnabled(GL_DEPTH_TEST)==GL_FALSE); // needed ??

  bool isModelBound = false;
  std::array<bool, s_textureSlotsCount> isTextureBound;
  isTextureBound.fill(false);
  bool isFontBound = false;

  glUseProgram(m_shader->m_drawProgram);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_textureWhite.m_handle);

  for (ui::window * curwin : windowsList)
  {
    if (!curwin->wvisible) continue;

    const glm::mat3 & matWin = curwin->get_mat3();
    m_shader->setUniformMatrix(m_PV * matWin);

    glUniform1i(m_shader->getUniformLocation(shader::TexDiffuse),0);

    m_model.drawcall(curwin->m_adSolid.part, 1, !isModelBound);
    isModelBound = true;

    m_model.drawcall(curwin->m_adrLine.part, 1, false, GL_LINES);

    for (std::size_t tslot = 0; tslot < s_textureSlotsCount; ++tslot)
    {
      if (m_model.partInfo(curwin->m_adrPict.part + tslot).m_size > 0)
      {
        if (m_textures[tslot] == nullptr)
        {
          glUniform1i(m_shader->getUniformLocation(shader::TexDiffuse),0);
        }
        else
        {
          if (!isTextureBound[tslot])
          {
            glActiveTexture(GL_TEXTURE1 + tslot);
            glBindTexture(GL_TEXTURE_2D, m_textures[tslot]->m_handle);
            isTextureBound[tslot] = true;
          }
          glUniform1i(m_shader->getUniformLocation(shader::TexDiffuse),1 + tslot);
        }
        m_model.drawcall(curwin->m_adrPict.part + tslot, 1, false);
      }
    }

    if (m_model.partInfo(curwin->m_adrText.part).m_size > 0)
    {
      if (m_defaultFont == nullptr)
      {
        glUniform1i(m_shader->getUniformLocation(shader::TexDiffuse), 0);
      }
      else
      {
        if (!isFontBound)
        {
          glActiveTexture(GL_TEXTURE7);
          glBindTexture(GL_TEXTURE_2D, m_defaultFont->get_texture().m_handle);
        }
        glUniform1i(m_shader->getUniformLocation(shader::TexDiffuse), 7);
      }
      m_model.drawcall(curwin->m_adrText.part, 1, false);
    }
  }
}

void baseUI2D::updateCameraInfo(const glm::mat3 &mProjView, const glm::ivec2 &screenSize)
{
  const bool changed = (m_PV != mProjView) || (m_viewport != glm::vec2(screenSize));
  if (!changed)
    return;

  m_PV = mProjView;
  m_viewport = screenSize;
  m_PVinv = glm::inverse(m_PV);

  for (ui::window *w : windowsList)
    w->m_isUpdateNeededLayout = true;
}

bool baseUI2D::acceptEvent(const SDL_Event &event)
{
  eventState.event = event;

  switch(event.type)
  {
  case SDL_MOUSEBUTTONDOWN:
    if      (event.button.button == SDL_BUTTON_LEFT ) eventState.mouseLeftPressed = true;
    else if (event.button.button == SDL_BUTTON_RIGHT) eventState.mouseRightPressed = true;
    eventState.mousePos.x = float(event.button.x);
    eventState.mousePos.y = float(event.button.y);
    eventState.mousePos.z = 0.f;
    eventState.mousePosPrev = eventState.mousePos;
    break;
  case SDL_MOUSEBUTTONUP:
    if      (event.button.button == SDL_BUTTON_LEFT ) eventState.mouseLeftPressed = false;
    else if (event.button.button == SDL_BUTTON_RIGHT) eventState.mouseRightPressed = false;
    eventState.mousePos.x = float(event.button.x);
    eventState.mousePos.y = float(event.button.y);
    eventState.mousePos.z = 0.f;
    break;
  case SDL_MOUSEMOTION:
    eventState.mousePos.x = float(event.button.x);
    eventState.mousePos.y = float(event.button.y);
    eventState.mousePos.z = 0.f;
    break;
  }

  bool accepted = false;

  for (ui::window * curw : windowsList)
  {
    // visible ?
    if (!curw->wvisible) continue;
    // process
    ui::s_eventIntern eventIntern = eventState;
    eventIntern.mousePosPrev = glm::vec3(projectWindowPointFromScreen(eventState.mousePosPrev, curw->get_mat3()), 0.f);
    eventIntern.mousePos     = glm::vec3(projectWindowPointFromScreen(eventState.mousePos, curw->get_mat3()), 0.f);
    eventIntern.accepted     = accepted;

    curw->acceptEvent(eventIntern);

    accepted |= eventIntern.accepted;
  }

  return accepted;
}

bool baseUI2D::acceptEvent(glm::ivec2 mousePosition, bool mouseLEFT, bool mouseRIGHT)
{
  eventState.mousePos = glm::vec3(mousePosition, 0.f);

  eventState.event.type = SDL_MOUSEMOTION;
  eventState.event.button.x = mousePosition.x;
  eventState.event.button.y = mousePosition.y;

  if (mouseLEFT && !eventState.mouseLeftPressed)
  {
    eventState.event.type = SDL_MOUSEBUTTONDOWN;
    eventState.event.button.button = SDL_BUTTON_LEFT;
    eventState.mousePosPrev = eventState.mousePos;
  }
  else if (!mouseLEFT && eventState.mouseLeftPressed)
  {
    eventState.event.type = SDL_MOUSEBUTTONUP;
    eventState.event.button.button = SDL_BUTTON_LEFT;
  }
  else if (mouseRIGHT && !eventState.mouseRightPressed)
  {
    eventState.event.type = SDL_MOUSEBUTTONDOWN;
    eventState.event.button.button = SDL_BUTTON_RIGHT;
    eventState.mousePosPrev = eventState.mousePos;
  }
  else if (!mouseRIGHT && eventState.mouseRightPressed)
  {
    eventState.event.type = SDL_MOUSEBUTTONUP;
    eventState.event.button.button = SDL_BUTTON_RIGHT;
  }

  eventState.mouseLeftPressed = mouseLEFT;
  eventState.mouseRightPressed = mouseRIGHT;

  bool accepted = false;

  for (ui::window * curw : windowsList)
  {
    // visible ?
    if (!curw->wvisible) continue;
    // process
    ui::s_eventIntern eventIntern = eventState;
    eventIntern.mousePosPrev = glm::vec3(projectWindowPointFromScreen(eventState.mousePosPrev, curw->get_mat3()), 0.f);
    eventIntern.mousePos     = glm::vec3(projectWindowPointFromScreen(eventState.mousePos, curw->get_mat3()), 0.f);
    eventIntern.accepted     = accepted;

    curw->acceptEvent(eventIntern);

    accepted |= eventIntern.accepted;
  }

  return accepted;
}

glm::vec2 baseUI2D::projectWindowPointFromScreen(const glm::vec2 &pixelCoords, const glm::mat3 &mModelWindow, bool isPosition) const
{
  glm::vec3 unproj;
  if (isPosition)
    unproj = glm::vec3(pixelCoords.x / m_viewport.x * 2.f - 1.f,
                       pixelCoords.y / m_viewport.y * -2.f + 1.f,
                       1.f);
  else
    unproj = glm::vec3(pixelCoords.x / m_viewport.x * 2.f,
                       pixelCoords.y / m_viewport.y * 2.f,
                       0.f);

  unproj = glm::inverse(mModelWindow) * (m_PVinv * unproj);
  return unproj;
}

glm::vec2 baseUI2D::projectWindowPointToScreen(const glm::vec2 &windowCoords, const glm::mat3 &mModelWindow, bool isPosition) const
{
  if (isPosition)
  {
    const glm::vec3 projv3 = glm::vec3(windowCoords, 1.f);
    const glm::vec3 projWorld = m_PV * (mModelWindow * projv3);
    const glm::vec2 projClip = glm::vec2(projWorld.x, -projWorld.y);
    const glm::vec2 projScreen = (0.5f + projClip * 0.5f) * m_viewport;
    return projScreen;
  }
  else
  {
    const glm::vec3 projv3 = glm::vec3(windowCoords, 0.f);
    const glm::vec3 projWorld = m_PV * (mModelWindow * projv3);
    const glm::vec2 projScreen = glm::vec2(projWorld) * 0.5f * m_viewport;
    return projScreen;
  }
}

bool baseUI2D::loadShader(shader *shaderToUse)
{
  if (shaderToUse != nullptr)
  {
    m_shader = shaderToUse;
    m_shaderOwner = false;
  }
  else
  {
    m_shader = new shader;
    m_shaderOwner = true;
    return m_shader->loadShader(shader::PRGM_2D, shader::PRGM_COLOR | shader::PRGM_TEXTURED, "ui-2D");
  }
  return true;
}

// baseUI3D implementation ========================================

void baseUI3D::draw() const
{
  TRE_ASSERT(m_shader != nullptr);
  TRE_ASSERT(m_textureWhite.m_handle != 0);
  TRE_ASSERT(glIsEnabled(GL_BLEND)==GL_TRUE);

  bool isModelBound = false;
  std::array<bool, s_textureSlotsCount> isTextureBound;
  isTextureBound.fill(false);
  bool isFontBound = false;

  glUseProgram(m_shader->m_drawProgram);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_textureWhite.m_handle);

  for (ui::window * curwin : windowsList)
  {
    if (!curwin->wvisible) continue;

    const glm::mat4 & matWin = curwin->get_mat4();
    m_shader->setUniformMatrix(m_PV * matWin, matWin);

    glUniform1i(m_shader->getUniformLocation(shader::TexDiffuse),0);

    m_model.drawcall(curwin->m_adSolid.part, 1, !isModelBound);
    isModelBound = true;

    m_model.drawcall(curwin->m_adrLine.part, 1, false, GL_LINES);

    for (std::size_t tslot = 0; tslot < s_textureSlotsCount; ++tslot)
    {
      if (m_model.partInfo(curwin->m_adrPict.part + tslot).m_size > 0)
      {
        if (m_textures[tslot] == nullptr)
        {
          glUniform1i(m_shader->getUniformLocation(shader::TexDiffuse),0);
        }
        else
        {
          if (!isTextureBound[tslot])
          {
            glActiveTexture(GL_TEXTURE1 + tslot);
            glBindTexture(GL_TEXTURE_2D, m_textures[tslot]->m_handle);
            isTextureBound[tslot] = true;
          }
          glUniform1i(m_shader->getUniformLocation(shader::TexDiffuse),1 + tslot);
        }
        m_model.drawcall(curwin->m_adrPict.part + tslot, 1, false);
      }
    }

    if (m_model.partInfo(curwin->m_adrText.part).m_size > 0)
    {
      if (m_defaultFont == nullptr)
      {
        glUniform1i(m_shader->getUniformLocation(shader::TexDiffuse), 0);
      }
      else
      {
        if (!isFontBound)
        {
          glActiveTexture(GL_TEXTURE7);
          glBindTexture(GL_TEXTURE_2D, m_defaultFont->get_texture().m_handle);
        }
        glUniform1i(m_shader->getUniformLocation(shader::TexDiffuse), 7);
      }
      m_model.drawcall(curwin->m_adrText.part, 1, false);
    }
  }
}

void baseUI3D::updateCameraInfo(const glm::mat4 &mProj, const glm::mat4 &mView, const glm::ivec2 &screenSize)
{
  const glm::mat4 pv = mProj * mView;
  const glm::vec2 ss = screenSize;

  const bool changed = (m_PV != pv) || (m_viewport != ss);
  if (!changed)
    return;

  m_PV = pv;
  m_viewport = ss;
  m_cameraPosition = glm::inverse(mView) * glm::vec4(-glm::vec3(mView[3]), 0.f);
  m_PVinv = glm::inverse(pv);

  for (ui::window *w : windowsList)
    w->m_isUpdateNeededLayout = true;
}

bool baseUI3D::acceptEvent(const SDL_Event &event)
{
  eventState.event = event;

  switch(event.type)
  {
  case SDL_MOUSEBUTTONDOWN:
    if      (event.button.button == SDL_BUTTON_LEFT ) eventState.mouseLeftPressed = true;
    else if (event.button.button == SDL_BUTTON_RIGHT) eventState.mouseRightPressed = true;
    eventState.mousePos.x = float(event.button.x);
    eventState.mousePos.y = float(event.button.y);
    eventState.mousePos.z = 0.f;
    eventState.mousePosPrev = eventState.mousePos;
    break;
  case SDL_MOUSEBUTTONUP:
    if      (event.button.button == SDL_BUTTON_LEFT ) eventState.mouseLeftPressed = false;
    else if (event.button.button == SDL_BUTTON_RIGHT) eventState.mouseRightPressed = false;
    eventState.mousePos.x = float(event.button.x);
    eventState.mousePos.y = float(event.button.y);
    eventState.mousePos.z = 0.f;
    break;
  case SDL_MOUSEMOTION:
    eventState.mousePos.x = float(event.button.x);
    eventState.mousePos.y = float(event.button.y);
    eventState.mousePos.z = 0.f;
    break;
  default:
    return false;
    break;
  }

  bool accepted = false;

  for (ui::window * curw : windowsList)
  {
    // visible ?
    if (!curw->wvisible) continue;
    // process
    ui::s_eventIntern eventIntern = eventState;
    eventIntern.mousePosPrev = projectWindowPointFromScreen(glm::vec2(eventState.mousePosPrev), curw->get_mat4());
    eventIntern.mousePos     = projectWindowPointFromScreen(glm::vec2(eventState.mousePos), curw->get_mat4());
    eventIntern.accepted     = accepted;

    curw->acceptEvent(eventIntern);

    accepted |= eventIntern.accepted;
  }

  return accepted;
}

bool baseUI3D::acceptEvent(glm::ivec2 mousePosition, bool mouseLEFT, bool mouseRIGHT)
{
  eventState.mousePos = glm::vec3(mousePosition, 0.f);

  eventState.event.type = SDL_MOUSEMOTION;
  eventState.event.button.x = mousePosition.x;
  eventState.event.button.y = mousePosition.y;

  if (mouseLEFT && !eventState.mouseLeftPressed)
  {
    eventState.event.type = SDL_MOUSEBUTTONDOWN;
    eventState.event.button.button = SDL_BUTTON_LEFT;
    eventState.mousePosPrev = eventState.mousePos;
  }
  else if (!mouseLEFT && eventState.mouseLeftPressed)
  {
    eventState.event.type = SDL_MOUSEBUTTONUP;
    eventState.event.button.button = SDL_BUTTON_LEFT;
  }
  else if (mouseRIGHT && !eventState.mouseRightPressed)
  {
    eventState.event.type = SDL_MOUSEBUTTONDOWN;
    eventState.event.button.button = SDL_BUTTON_RIGHT;
    eventState.mousePosPrev = eventState.mousePos;
  }
  else if (!mouseRIGHT && eventState.mouseRightPressed)
  {
    eventState.event.type = SDL_MOUSEBUTTONUP;
    eventState.event.button.button = SDL_BUTTON_RIGHT;
  }

  eventState.mouseLeftPressed = mouseLEFT;
  eventState.mouseRightPressed = mouseRIGHT;

  bool accepted = false;

  for (ui::window * curw : windowsList)
  {
    // visible ?
    if (!curw->wvisible) continue;
    // process
    ui::s_eventIntern eventIntern = eventState;
    eventIntern.mousePosPrev = projectWindowPointFromScreen(glm::vec2(eventState.mousePosPrev), curw->get_mat4());
    eventIntern.mousePos     = projectWindowPointFromScreen(glm::vec2(eventState.mousePos), curw->get_mat4());
    eventIntern.accepted     = accepted;

    curw->acceptEvent(eventIntern);

    accepted |= eventIntern.accepted;
  }

  return accepted;
}

glm::vec3 baseUI3D::projectWindowPointFromScreen(const glm::vec2 &pixelCoords, const glm::mat4 &mModelWindow) const
{
  glm::vec4 unproj(pixelCoords.x / m_viewport.x * 2.f - 1.f,
                   pixelCoords.y / m_viewport.y * -2.f + 1.f,
                   1.f,
                   1.f);

  unproj = m_PVinv * unproj;
  unproj /= unproj.w;

  const glm::vec3 rayWorldDirection = glm::normalize(glm::vec3(unproj) - m_cameraPosition);
  const glm::vec3 camToplanePoint = glm::vec3(mModelWindow[3]) - m_cameraPosition;
  const glm::vec3 planeNormal = mModelWindow[2];

  const float dist = glm::dot(planeNormal, camToplanePoint) / glm::dot(planeNormal, rayWorldDirection);
  const glm::vec3 projectedPointWorld = m_cameraPosition + rayWorldDirection * dist;

  const glm::vec3 pointWindowSpace = glm::inverse(mModelWindow) * glm::vec4(projectedPointWorld, 1.f);

  return pointWindowSpace;
}

glm::vec2 baseUI3D::projectWindowPointToScreen(const glm::vec3 &windowCoords, const glm::mat4 &mModelWindow) const
{
  const glm::vec4 projv4 = glm::vec4(windowCoords, 1.f);
  const glm::vec4 projClip = m_PV * (mModelWindow * projv4);
  const glm::vec2 projViewport = glm::vec2(projClip.x, -projClip.y) / projClip.w;
  const glm::vec2 projScreen = (0.5f + projViewport * 0.5f) * m_viewport;
  return projScreen;
}

bool baseUI3D::loadShader(shader *shaderToUse)
{
  if (shaderToUse != nullptr)
  {
    m_shader = shaderToUse;
    m_shaderOwner = false;
  }
  else
  {
    m_shader = new shader;
    m_shaderOwner = true;
    return m_shader->loadShader(shader::PRGM_2Dto3D, shader::PRGM_COLOR | shader::PRGM_TEXTURED, "ui-3D");
  }
  return true;
}

// End ========================================================================

} // namespace
