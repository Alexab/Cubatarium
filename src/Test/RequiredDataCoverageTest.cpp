#include "World/Streaming/RequiredDataCoverage.h"
#include "World/Streaming/RequiredPresentationCoverage.h"
#include <cstdlib>
#include <iostream>
#include <set>

static void Expect(bool value, const char *message)
{
  if (!value)
  {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

int main()
{
  std::vector<glm::ivec3> columns = {{-1, 0, 0}, {-2, 0, 0}, {-3, 1, 0},
                                     {-3, 0, 0}, {-3, 0, 0}, {-4, 0, 0},
                                     {-5, 0, 0}};
  std::set<int> complete{-1}, pending{-2};
  std::vector<int> requests;
  auto admit = [&](int budget)
  {
    return cutum::AdmitRequiredDataColumns(
        columns, budget, [&](glm::ivec3 c) { return complete.count(c.x) != 0; },
        [&](glm::ivec3 c) { return pending.count(c.x) != 0; },
        [&](glm::ivec3 c)
        {
          requests.push_back(c.x);
          pending.insert(c.x);
        });
  };
  Expect(admit(0) == 0 && requests.empty(),
         "zero budget cannot admit data work");
  Expect(admit(2) == 2 && requests == std::vector<int>({-3, -4}),
         "known, pending, duplicate and non-ground columns do not steal "
         "admission slots");
  Expect(complete.size() == 1,
         "queued data is not optimistically declared complete");
  Expect(admit(2) == 1 && requests.back() == -5,
         "an indefinitely pending earlier column cannot starve later unknown "
         "data");
  Expect(admit(2) == 0,
         "repeated halo demand preserves existing asynchronous owners");
  complete.insert(pending.begin(), pending.end());
  pending.clear();
  Expect(admit(4) == 0,
         "committed coverage needs no more requests, regardless of lighting");
  complete.erase(-4);
  Expect(admit(1) == 1 && requests.back() == -4,
         "explicitly retired data is requested again when a consumer still "
         "needs it");
  // The saved preload center can differ by one chunk from the restored eye.
  // Prove closure of geometry -> neighboring light -> neighboring geometry,
  // rather than assuming the old square happens to cover the new view.
  const glm::ivec3 restored_eye_chunk(1, -3, 3);
  const auto required = cutum::RequiredDataColumns(restored_eye_chunk, 6);
  std::set<std::pair<int, int>> data;
  for (int z = -3; z <= 7; ++z)
    for (int x = -5; x <= 5; ++x)
      data.emplace(x, z); // Old center (0,2), radius 5.
  auto closure = [&]
  {
    for (int vz = -4; vz <= 4; ++vz)
      for (int vx = -4; vx <= 4; ++vx)
        for (int lz = -1; lz <= 1; ++lz)
          for (int lx = -1; lx <= 1; ++lx)
            for (int dz = -1; dz <= 1; ++dz)
              for (int dx = -1; dx <= 1; ++dx)
                if (!data.count({1 + vx + lx + dx, 3 + vz + lz + dz}))
                  return false;
    return true;
  };
  Expect(
      !closure(),
      "the old preload ring does not cover the restored view's light inputs");
  for (int frame = 0; frame < 64 && !closure(); ++frame)
  {
    std::vector<glm::ivec3> issued;
    const int n = cutum::AdmitRequiredDataColumns(
        required, 4, [&](glm::ivec3 c) { return data.count({c.x, c.z}) != 0; },
        [](glm::ivec3) { return false; },
        [&](glm::ivec3 c) { issued.push_back(c); });
    Expect(n <= 4 && !closure(),
           "bounded admission alone does not complete a data halo");
    for (const auto &c : issued)
    {
      Expect(
          c.y == 0,
          "negative restored eye height must not become a non-ground request");
      data.emplace(c.x, c.z); // Completion is a separate producer event.
    }
  }
  Expect(closure() && required.size() == 169,
         "bounded requests eventually supply every transitive light/mesh input "
         "at the new center");
  cutum::RequiredPresentationCoverage proof;
  Expect(!proof.Ready(), "an empty census cannot authorize the first frame");
  proof.expected_columns = proof.columns = 1;
  proof.expected_sections = 3;
  proof.ObserveAbsent(true);
  proof.ObserveResident(true, true, false);
  Expect(!proof.Ready(),
         "omitting a required section cannot shrink the census");
  proof.ObserveResident(false, true, true);
  Expect(proof.Ready(),
         "known AIR needs no mesh; resident fields and surfaces do");
  ++proof.publication_unready_sections;
  Expect(!proof.Ready(),
         "a stale or uncommitted publication blocks first frame");
  --proof.publication_unready_sections;
  ++proof.light_unready_sections;
  Expect(!proof.Ready(),
         "an unsettled field blocks even a dark or AIR section");
  --proof.light_unready_sections;
  ++proof.missing_data_columns;
  Expect(!proof.Ready(), "a queued data load is not a complete column");
  --proof.missing_data_columns;
  --proof.known_air_sections;
  ++proof.unknown_sections;
  Expect(!proof.Ready(),
         "unknown absence cannot stand in for a known AIR receipt");
  std::cout << "required_data_coverage_test: OK\n";
}
