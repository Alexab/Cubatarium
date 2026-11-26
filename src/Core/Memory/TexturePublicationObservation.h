#pragma once
namespace cutum
{
#if defined(CUBA_TEXTURE_PUBLICATION_TEST)
// Test-only notifications around real GL calls;never supplies GL dispatch.
void ObserveTextureCreated(unsigned int name) noexcept;
void ObserveTextureMutation(unsigned int name) noexcept;
void ObserveTextureUploaded(unsigned int name) noexcept;
#else
inline void ObserveTextureCreated(unsigned int) noexcept {}
inline void ObserveTextureMutation(unsigned int) noexcept {}
inline void ObserveTextureUploaded(unsigned int) noexcept {}
#endif
}
