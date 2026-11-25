#pragma once

#include "WorldGen/Core/CatalogStorageCensus.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/RelightContentCertificate.h"
#include "Render/Lighting/SectionRenderLightBinding.h"
#include "Render/Mesh/AsyncMeshBuilder.h"
#include "Render/Mesh/GreedyMesher.h"

namespace cutum
{
// Catalog payloads reached from actual retained publications, not storage of
// fields/meshes/bindings/results themselves. Those are separate E12 terms.
// All supplied objects must remain stable during this synchronous traversal.
class CatalogPublicationRootInspector
{
public:
  enum class Node {Field,Relight,Certificate,Binding,Geometry,Light,Mesh,Snapshot,DrawBatch,CrossBatch,NoSurface,Count};
  struct RootCounts {
    size_t catalog_handles{0},field_handles{0},result_handles{0},certificate_handles{0},binding_handles{0};
    std::array<size_t,static_cast<size_t>(Node::Count)> nodes{};
    bool overflow{false};
    size_t Nodes(Node kind)const{return nodes[static_cast<size_t>(kind)];}
  } Counts;
  CatalogStorageCensus Catalogs()const{return Storage.Result;}
  void BlockCatalog(const std::shared_ptr<const BlockDefinitionCatalog> &catalog)
  {if(catalog){Add(Counts.catalog_handles);Storage.Blocks(catalog);}}
  void LightField(const std::shared_ptr<const SectionLightField> &field)
  {if(field){Add(Counts.field_handles);Field(*field);}}
  void Field(const SectionLightField &field)
  {if(Enter(Node::Field,&field))field.InspectCatalogRoots(*this);}
  void RelightResult(const std::shared_ptr<const RelightComputeResult> &result)
  {if(result){Add(Counts.result_handles);RelightResult(*result);}}
  void RelightResult(const RelightComputeResult &result)
  {
    if(!Enter(Node::Relight,&result))return;
    BlockCatalog(result.input_catalog);Certificate(result.content_certificate);
  }
  void Certificate(const std::shared_ptr<const RelightContentCertificate> &certificate)
  {
    if(!certificate)return;Add(Counts.certificate_handles);
    if(Enter(Node::Certificate,certificate.get()))certificate->InspectCatalogRoots(*this);
  }
  void Geometry(const SectionGeometryRenderInputs &geometry)
  {if(Enter(Node::Geometry,&geometry))BlockCatalog(geometry.catalog);}
  void LightSnapshot(const SectionRenderLightSnapshot &light)
  {if(Enter(Node::Light,&light))Geometry(light.GeometryInputs());}
  void Binding(const std::shared_ptr<SectionRenderLightBinding> &binding)
  {
    if(!binding)return;Add(Counts.binding_handles);
    if(!Enter(Node::Binding,binding.get()))return;
    Geometry(binding->GeometryInputs());LightSnapshot(binding->LightSnapshot());
  }
  void MeshSnapshot(const ChunkMeshSnapshot &snapshot)
  {if(Enter(Node::Snapshot,&snapshot) && snapshot.renderLight)LightSnapshot(*snapshot.renderLight);}
  void MeshResult(const MeshBuildResult &result)
  {
    if(!Enter(Node::Mesh,&result))return;
    BlockCatalog(result.InputCatalog);Geometry(result.GeometryInputs);
    if(result.RenderLight)LightSnapshot(*result.RenderLight);
    if(result.PendingSnapshot)MeshSnapshot(*result.PendingSnapshot);
    for(const auto &batch:result.batches)DrawBatch(batch);
  }
  // CPU GreedyMeshBatch, packed references and backend GreedyGpuBatch all
  // carry this lease. Include retained invalid leases as owning objects.
  template<class Batch> void DrawBatch(const Batch &batch)
  {if(Enter(Node::DrawBatch,&batch))Binding(batch.renderLightBinding);}
  template<class Batch> void CrossBatch(const Batch &batch)
  {if(Enter(Node::CrossBatch,&batch))for(const auto &range:batch.lightRanges)Binding(range.binding);}
  void NoSurface(const std::shared_ptr<const SectionNoSurfaceProof> &proof)
  {if(proof && Enter(Node::NoSurface,proof.get()))proof->InspectCatalogRoots(*this);}
private:
  struct SeenNode {Node kind;const void *address;};
  std::vector<SeenNode> Seen;
  detail::CatalogStorageInspector Storage;
  void Add(size_t &count)
  {if(count==std::numeric_limits<size_t>::max())Counts.overflow=true;else ++count;}
  bool Enter(Node kind,const void *address)
  {
    for(const auto &node:Seen)if(node.kind==kind && node.address==address)return false;
    Seen.push_back({kind,address});Add(Counts.nodes[static_cast<size_t>(kind)]);return true;
  }
};
} // namespace cutum
