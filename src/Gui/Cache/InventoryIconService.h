#ifndef INVENTORY_ICON_SERVICE_H
#define INVENTORY_ICON_SERVICE_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

class UInventoryIconService
{
public:
  struct Stats
  {
    uint64_t CacheHits{0};
    uint64_t CacheMisses{0};
    uint64_t RenderStores{0};
    uint64_t PngReadFailures{0};
    uint64_t PngWriteFailures{0};
    uint64_t ManifestVersionMismatches{0};
  };

  UInventoryIconService();
  bool Initialize();
  static std::string BuildCacheKey(const std::string &kind,
                                   const std::string &entityId,
                                   const std::string &variantId);
  static std::string HashFingerprint(const std::string &value);

  bool TryLoadIconTexture(const std::string &kind, const std::string &entityId,
                          const std::string &variantId,
                          const std::string &fingerprint, int expectedSize,
                          GLuint &outTexture);

  bool StoreIconTexture(const std::string &kind, const std::string &entityId,
                        const std::string &variantId,
                        const std::string &fingerprint, int size,
                        GLuint texture);
  void InvalidateAll();

  const Stats &GetStats() const { return Metrics; }

private:
  struct Entry
  {
    std::string Fingerprint;
    std::string File;
    int Size{0};
  };

  std::filesystem::path CacheRoot;
  std::filesystem::path ManifestPath;
  std::unordered_map<std::string, Entry> Entries;
  mutable Stats Metrics;

  static std::string SanitizeFileToken(const std::string &raw);

  bool LoadManifest();
  bool SaveManifest() const;
  bool ReadPngRgba(const std::filesystem::path &path, int expectedSize,
                   std::vector<unsigned char> &pixels, int &outSize) const;
  bool WritePngRgba(const std::filesystem::path &path, int size,
                    const std::vector<unsigned char> &pixels) const;
  bool CaptureTexturePixels(GLuint texture, int size,
                            std::vector<unsigned char> &pixels) const;
  GLuint CreateTextureFromPixels(int size,
                                 const std::vector<unsigned char> &pixels) const;
};

} // namespace cutum

#endif
