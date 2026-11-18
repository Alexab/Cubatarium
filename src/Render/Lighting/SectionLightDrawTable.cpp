#include "Render/Lighting/SectionLightDrawTable.h"
#include "Render/GlIncludes.h"

namespace cutum
{
SectionLightDrawTable::~SectionLightDrawTable() { Reset(); }

bool SectionLightDrawTable::Prepare(const std::vector<Words> &rows)
{
  if (rows.empty() || rows.size() > MaxRows)
    return false;
  if (Live >= 0 && Stores[Live].rows == rows)
    return true;
  auto &next = Stores[Live == 0 ? 1 : 0];
  if (next.unfenced)
    return false;
  if (next.fence)
  {
    const auto state = glClientWaitSync(static_cast<GLsync>(next.fence), 0, 0);
    if (state != GL_ALREADY_SIGNALED && state != GL_CONDITION_SATISFIED)
      return false;
    glDeleteSync(static_cast<GLsync>(next.fence));
    next.fence = nullptr;
  }
  if (glGetError() != GL_NO_ERROR)
    return false;
  if (!next.buffer)
    glGenBuffers(1, &next.buffer);
  glBindBuffer(GL_ARRAY_BUFFER, next.buffer);
  if (BeforeTransfer)
    BeforeTransfer(TransferContext);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(rows.size() * sizeof(Words)),
               rows.data(), GL_DYNAMIC_DRAW);
  if (glGetError() != GL_NO_ERROR)
  {
    next.rows.clear();
    return false;
  }
  next.rows = rows;
  Live = Live == 0 ? 1 : 0;
  ++Uploads;
  return true;
}

bool SectionLightDrawTable::BindAttribute(unsigned location) const
{
  if (Live < 0)
    return false;
  glBindBuffer(GL_ARRAY_BUFFER, Stores[Live].buffer);
  glVertexAttribIPointer(location, 4, GL_UNSIGNED_INT, sizeof(Words), nullptr);
  glVertexAttribDivisor(location, 1);
  glEnableVertexAttribArray(location);
  return glGetError() == GL_NO_ERROR;
}

void SectionLightDrawTable::DisableAttribute(unsigned location)
{
  glDisableVertexAttribArray(location);
  glVertexAttribDivisor(location, 0);
  glVertexAttribI4ui(location, 0, 0, 0, 0);
}

void SectionLightDrawTable::SignalDrawComplete()
{
  if (Live < 0)
    return;
  auto &store = Stores[Live];
  const auto fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  if (!fence)
  {
    store.unfenced = true;
    return;
  }
  if (store.fence)
    glDeleteSync(static_cast<GLsync>(store.fence));
  store.fence = fence;
}

void SectionLightDrawTable::Reset()
{
  for (auto &store : Stores)
  {
    if (store.fence)
      glDeleteSync(static_cast<GLsync>(store.fence));
    if (store.buffer)
      glDeleteBuffers(1, &store.buffer);
    store = {};
  }
  Live = -1;
  Uploads = 0;
}

unsigned SectionLightDrawTable::Buffer() const
{
  return Live < 0 ? 0 : Stores[Live].buffer;
}
} // namespace cutum
