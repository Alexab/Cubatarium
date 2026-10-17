from pathlib import Path


def find_matching_brace(text: str, open_idx: int) -> int:
    depth = 0
    i = open_idx
    while i < len(text):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise RuntimeError("no matching brace")


p = Path(r"e:/Work/Home/Cubatarium/src/Render/Engine/GreedyGpuPublication.cpp")
text = p.read_text(encoding="utf-8")

sig = "bool UGreedyGpuBackend::PublishPassInputs("
i0 = text.find(sig)
assert i0 > 0
brace0 = text.find("{", i0)
end0 = find_matching_brace(text, brace0)
old_body = text[brace0 + 1 : end0]

apply_sig = "bool UGreedyGpuBackend::ApplyPublicationDelta("
i1 = text.find(apply_sig)
assert i1 > end0
brace1 = text.find("{", i1)
end1 = find_matching_brace(text, brace1)
old_apply_body = text[brace1 + 1 : end1]

adapter = """
  PublicationDelta delta;
  delta.kind = PublicationDeltaKind::Replace;
  delta.targetBackend = 0;
  delta.sourceMeshRevision = mesh_revision;
  delta.sourceCullRevision = cull_revision;
  delta.sourceSortRevision = sort_revision;
  delta.replaceInputs = &inputs;
  delta.replaceDirty = &dirty;
  delta.meshCache = mesh_cache;
  delta.cachePassIndicesOverride = cache_pass_indices_override;
  // Production Remove: authoritative empty single-coord replace.
  if (inputs.empty() && dirty.size() == 1 && cache_pass_indices_override &&
      cache_pass_indices_override->empty())
  {
    delta.kind = PublicationDeltaKind::Remove;
    delta.coord = *dirty.begin();
  }
  return ApplyPublicationDelta(cache, delta);
"""

# Patch replace body returns to set LastAppliedDeltaKind
replace_body = old_body.replace(
    "return true;",
    "LastAppliedDeltaKind_ = PublicationDeltaKind::Replace;\n  return true;",
)

reject = """  if (delta.kind != PublicationDeltaKind::Remove &&
      delta.kind != PublicationDeltaKind::RepresentationSwitch)
  {
    // Non-empty Replace stays on PublishPassInputs (complete pass payload).
    return false;
  }
"""
if reject not in old_apply_body:
    raise SystemExit("reject not found in ApplyPublicationDelta")

replace_branch = f"""  if (delta.kind == PublicationDeltaKind::Replace)
  {{
    if (!delta.replaceInputs || !delta.replaceDirty)
      return false;
    const std::vector<GreedyGpuUploadInput> &inputs = *delta.replaceInputs;
    const std::unordered_set<glm::ivec3, IVec3Hash> &dirty = *delta.replaceDirty;
    const uint64_t mesh_revision = delta.sourceMeshRevision;
    const uint64_t cull_revision = delta.sourceCullRevision;
    const uint64_t sort_revision = delta.sourceSortRevision;
    const UChunkMeshCache *mesh_cache = delta.meshCache;
    const std::unordered_set<uint16_t> *cache_pass_indices_override =
        delta.cachePassIndicesOverride;
{replace_body}
  }}
  if (delta.kind != PublicationDeltaKind::Remove &&
      delta.kind != PublicationDeltaKind::RepresentationSwitch)
  {{
    return false;
  }}
"""

new_apply_body = old_apply_body.replace(reject, replace_branch, 1)

new_text = (
    text[: brace0 + 1]
    + adapter
    + text[end0 : brace1 + 1]
    + new_apply_body
    + text[end1:]
)
p.write_text(new_text, encoding="utf-8")
print("refactored OK")
print("PublishPassInputs bytes", len(adapter))
print("ApplyPublicationDelta bytes", len(new_apply_body))
