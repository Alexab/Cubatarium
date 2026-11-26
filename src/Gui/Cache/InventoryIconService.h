#ifndef INVENTORY_ICON_SERVICE_H
#define INVENTORY_ICON_SERVICE_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

struct InventoryIconStorage
{
  size_t serviceBytes{}, serviceRequest{}, readbackStateBytes{}, pathBytes{}, manifestBytes{}, jobBytes{}, cpuBytes{};
  size_t entries{}, jobs{}, packBufferBytes{}, aliases{};
  bool present{}, initialized{}, dirty{}, factoryOwner{}, bindingsMatch{}, supported{};
};

enum class InventoryIconIoPhase { ManifestRead, ManifestWrite, ManifestFile, PngDecode, PngEncode, Count };

struct InventoryIconIoStorage
{
  InventoryIconIoPhase phase;
  size_t ordinaryBytes{}, codecBytes{}, parserUpper{}, payloadBytes{};
  bool supported{};
};

struct InventoryIconIoProbe
{
  void *context;
  void (*sample)(void *, const InventoryIconIoStorage &) noexcept;
};

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
  ~UInventoryIconService();
  static std::shared_ptr<UInventoryIconService> Create();
  InventoryIconStorage InspectOwnedStorage() const noexcept;
  bool Initialize(const std::filesystem::path &cacheRoot = {}, const InventoryIconIoProbe *probe = nullptr);
  static std::string BuildCacheKey(const std::string &kind,
                                   const std::string &entityId,
                                   const std::string &variantId);
  static std::string HashFingerprint(const std::string &value);

  bool TryLoadIconTexture(const std::string &kind, const std::string &entityId,
                          const std::string &variantId,
                          const std::string &fingerprint, int expectedSize,
                          GLuint &outTexture, const InventoryIconIoProbe *probe = nullptr);

  /// Item stores enqueue GPU readback; success means accepted for persistence.
  bool StoreIconTexture(const std::string &kind, const std::string &entityId,
                        const std::string &variantId,
                        const std::string &fingerprint, int size,
                        GLuint texture, const InventoryIconIoProbe *probe = nullptr);
  /// UX R2: manifest writes are batched - StoreIconTexture marks the
  /// manifest dirty; FlushManifest() serializes once per batch/frame
  /// instead of a full rewrite per icon.
  void FlushManifest(const InventoryIconIoProbe *probe = nullptr);
  void InvalidateAll();
  void InvalidateKind(const std::string &kind);

  const Stats &GetStats() const { return Metrics; }
  struct ReadbackStorage
  {
    size_t jobs{}, buffer_bytes{};
    GLuint first_buffer{};
    bool supported{};
  };
  ReadbackStorage InspectReadbackStorage() const noexcept;
  void CancelPendingItemReadbacks() noexcept;

private:
  struct ReadbackState;
  std::unique_ptr<ReadbackState> Readbacks;
  void DrainReadbacks(const InventoryIconIoProbe *probe);

  struct Entry
  {
    std::string Fingerprint;
    std::string File;
    int Size{0};
  };
  using ManifestEntries = std::unordered_map<std::string, Entry>;

  std::filesystem::path CacheRoot;
  std::filesystem::path ManifestPath;
  ManifestEntries Entries;
  mutable Stats Metrics;

  static std::string SanitizeFileToken(const std::string &raw);

  static bool LoadManifest(const std::filesystem::path &path, ManifestEntries &entries,
                           bool &versionMismatch, const InventoryIconIoProbe *probe);
  bool SaveManifest(const InventoryIconIoProbe *probe = nullptr) const;
  bool ManifestDirty{false};
  bool FactoryOwner{false};
  bool WritePngRgba(const std::filesystem::path &path, int size,
                    const std::vector<unsigned char> &pixels, const InventoryIconIoProbe *probe) const;
  bool CaptureTexturePixels(GLuint texture, int size,
                            std::vector<unsigned char> &pixels) const;
  GLuint CreateTextureFromPixels(int size, const unsigned char *pixels) const;
};

} // namespace cutum

#endif
