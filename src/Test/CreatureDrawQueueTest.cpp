#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Items/ItemDrawRequest.h"

#include <iostream>
#include <vector>

namespace
{

void Expect(bool condition, const char *message, int &failures)
{
  if (!condition)
  {
    std::cerr << "creature_draw_queue_test: " << message << '\n';
    ++failures;
  }
}

cutum::CreatureDrawRequest MakeRequest(cutum::CreatureDrawKind kind,
                                       unsigned texture, float marker)
{
  cutum::CreatureDrawRequest request;
  request.Kind = kind;
  request.Texture = texture;
  request.Mvp[0][0] = marker;
  return request;
}

} // namespace

int main()
{
  using namespace cutum;
  int failures = 0;

  {
    CreatureDrawQueue queue;
    Expect(queue.Size() == 0, "queue starts empty", failures);
    queue.Push(MakeRequest(CreatureDrawKind::TexturedPart, 3u, 1.f));
    queue.Push(MakeRequest(CreatureDrawKind::SkeletalMesh, 7u, 2.f));
    Expect(queue.Size() == 2, "push appends requests", failures);
    queue.Clear();
    Expect(queue.Size() == 0, "clear empties the queue", failures);
  }

  {
    // Flush ordering is exercised through the extracted sort because Flush
    // dispatches straight into the GL engine.
    std::vector<CreatureDrawRequest> requests;
    requests.push_back(MakeRequest(CreatureDrawKind::WireframeBox, 5u, 1.f));
    requests.push_back(MakeRequest(CreatureDrawKind::TexturedPart, 9u, 2.f));
    requests.push_back(MakeRequest(CreatureDrawKind::TexturedPart, 3u, 3.f));
    requests.push_back(MakeRequest(CreatureDrawKind::SkinnedMesh, 7u, 4.f));
    requests.push_back(MakeRequest(CreatureDrawKind::SkeletalMesh, 1u, 5.f));
    requests.push_back(MakeRequest(CreatureDrawKind::TexturedPart, 3u, 6.f));

    SortCreatureDrawRequests(requests);
    Expect(requests.size() == 6, "sort keeps every request", failures);
    const CreatureDrawKind expectedKinds[] = {
        CreatureDrawKind::TexturedPart, CreatureDrawKind::TexturedPart,
        CreatureDrawKind::TexturedPart, CreatureDrawKind::SkeletalMesh,
        CreatureDrawKind::SkinnedMesh,  CreatureDrawKind::WireframeBox};
    bool kindsInOrder = true;
    for (size_t i = 0; i < requests.size(); ++i)
    {
      kindsInOrder = kindsInOrder && requests[i].Kind == expectedKinds[i];
    }
    Expect(kindsInOrder, "requests sort by draw kind first", failures);

    const unsigned expectedTextures[] = {3u, 3u, 9u, 1u, 7u, 5u};
    bool texturesInOrder = true;
    for (size_t i = 0; i < requests.size(); ++i)
    {
      texturesInOrder =
          texturesInOrder && requests[i].Texture == expectedTextures[i];
    }
    Expect(texturesInOrder, "equal-kind requests sort by texture id", failures);

    Expect(requests[0].Mvp[0][0] == 3.f && requests[1].Mvp[0][0] == 6.f,
           "sort is stable for equal kind and texture", failures);
  }

  {
    Expect(!CreatureDrawRequestLess(
               MakeRequest(CreatureDrawKind::TexturedPart, 1u, 0.f),
               MakeRequest(CreatureDrawKind::TexturedPart, 1u, 0.f)),
           "comparator is irreflexive for equal requests", failures);
    Expect(CreatureDrawRequestLess(
               MakeRequest(CreatureDrawKind::TexturedPart, 1u, 0.f),
               MakeRequest(CreatureDrawKind::SkeletalMesh, 0u, 0.f)),
           "comparator orders kinds before textures", failures);
  }

  {
    struct Sink : ICreatureDrawSink
    {
      int Items{0};
      glm::vec2 Light{0};
      float Fog{0};
      const ItemDrawRequest *Received{nullptr};
      void DrawTexturedPart(const glm::mat4 &, GLuint,
                            CreaturePartMesh) override
      {
      }
      void DrawBoneSkeletonMesh(const glm::mat4 &, GLuint,
                                const BoneSkeletonCubeMeshCpu &) override
      {
      }
      void DrawSkinnedMesh(
          const glm::mat4 &, GLuint, const GltfPrimitiveCpu &,
          const std::shared_ptr<const std::vector<glm::mat4>> &) override
      {
      }
      void DrawBoxWireframe(const glm::mat4 &, const glm::vec4 &) override {}
      void DrawBillboard(const glm::mat4 &, GLuint, const glm::vec4 &) override
      {
      }
      void SetCreatureLight(const glm::vec2 &value) override { Light = value; }
      void SetCreatureFog(float value) override { Fog = value; }
      void DrawItemSurface(const ItemDrawRequest &value) override
      {
        ++Items;
        Received = &value;
      }
    } sink;
    auto payload = std::make_shared<ItemDrawRequest>();
    CreatureDrawRequest request;
    request.Kind = CreatureDrawKind::ItemSurface;
    request.ItemSurface = payload;
    CreatureDrawQueue queue;
    queue.Push(std::move(request));
    queue.StampLight(0, {.2f, .7f});
    queue.StampFog(0, .35f);
    queue.Flush(sink);
    Expect(sink.Items == 1 && sink.Received == payload.get(),
           "queue preserves the item-only payload", failures);
    Expect(sink.Light == glm::vec2(.2f, .7f) && sink.Fog == .35f,
           "item requests inherit carrier light and fog", failures);
  }

  if (failures != 0)
  {
    return 1;
  }
  std::cout << "creature_draw_queue_test: OK\n";
  return 0;
}
