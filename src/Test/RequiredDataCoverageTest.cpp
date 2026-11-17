#include "World/Streaming/RequiredDataCoverage.h"
#include "World/Streaming/RelightCaptureQueue.h"
#include "World/Streaming/RequiredPresentationCoverage.h"
#include "World/Streaming/RequiredSectionScan.h"
#include "World/Streaming/RequiredWorldDomain.h"
#include <cstdlib>
#include <iostream>
#include <set>
#include <tuple>

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
  {
    cutum::RequiredSectionScan scan;
    std::set<std::tuple<int, int, int>> visited;
    int calls = 0;
    for (int tick = 0; tick < 28; ++tick)
    {
      const glm::ivec3 focus(-tick, 100 + tick, tick - 20);
      int this_tick = 0;
      Expect(scan.Drain(focus, 8, -4, 19, 256,
                        [&](glm::ivec3 c)
                        {
                          ++calls;
                          ++this_tick;
                          Expect(c.y >= -4 && c.y <= 19,
                                 "section scan keeps signed absolute height");
                          visited.emplace(c.x - focus.x, c.y, c.z - focus.z);
                        }) == 256 &&
                 this_tick == 256,
             "maintenance visits are bounded despite changing focus");
    }
    Expect(
        visited.size() == 17 * 17 * 24 && calls == 28 * 256,
        "every section in a radius-eight full profile receives fair service");
    Expect(scan.Drain({0, 0, 0}, 8, -4, 19, 0, [&](glm::ivec3) { ++calls; }) ==
                   0 &&
               calls == 28 * 256,
           "zero budget performs no repair work");
    std::vector<glm::ivec3> first;
    scan.Drain({-20, -100, 30}, 1, -7, -5, 1,
               [&](glm::ivec3 c) { first.push_back(c); });
    Expect(first.size() == 1 && first[0] == glm::ivec3(-21, -7, 29),
           "domain/profile change starts the new section range");
    scan.Reset();
    scan.Drain({-20, 200, 30}, 1, -7, -5, 100,
               [&](glm::ivec3 c) { first.push_back(c); });
    Expect(first.size() == 28 && first[1] == first[0],
           "new session resets cursor and oversized budget visits one cycle");
  }
  {
    std::deque<int> priority{-7, 5}, ordinary{-12, 9};
    std::set<int> running{-7, 5, -12};
    bool from_priority = true;
    int probes = 0;
    const auto ready = [&](int key)
    {
      ++probes;
      return running.count(key) == 0;
    };
    Expect(cutum::PopReadyRelightColumn(priority, ordinary, ready,
                                        from_priority) == 9 &&
               !from_priority && probes == 4,
           "busy priority owners cannot starve executable ordinary work");
    Expect(priority == std::deque<int>({-7, 5}) &&
               ordinary == std::deque<int>({-12}),
           "skipped owners retain exact queue obligations and order");
    probes = 0;
    Expect(!cutum::PopReadyRelightColumn(priority, ordinary, ready,
                                         from_priority) &&
               probes == 3,
           "all-busy lanes are probed once, not repeatedly recaptured");
    running.erase(-7);
    Expect(cutum::PopReadyRelightColumn(priority, ordinary, ready,
                                        from_priority) == -7 &&
               from_priority,
           "completed priority owner becomes executable on next drain");
    priority = {-7, 5, 8};
    running.insert(-7);
    ordinary = {9};
    probes = 0;
    Expect(cutum::PopReadyRelightColumn(priority, ordinary, ready,
                                        from_priority, 2) == 9 &&
               probes == 3,
           "scan cap bounds each lane without starving ordinary work");
    Expect(cutum::PopReadyRelightColumn(priority, ordinary, ready,
                                        from_priority, 2) == 8 &&
               from_priority,
           "rotation makes an unprobed priority tail executable next time");
  }
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
  const auto domain = cutum::RequiredWorldDomain::ForRenderDistance(4, true);
  const auto larger = cutum::RequiredWorldDomain::ForRenderDistance(8, true);
  const auto flat = cutum::RequiredWorldDomain::ForRenderDistance(1, false);
  Expect(domain.presentation == 4 && domain.approach == 5 &&
             domain.light == 6 && domain.data == 7 && larger.data == 11 &&
             flat.data == 3,
         "consumer closure follows the actual render radius and light mode");
  cutum::RequiredDataLease lease{{-7, 0, -9}, domain.data};
  Expect(lease.Contains({-14, -99, -9}) && !lease.Contains({1, 0, -9}),
         "required full-column data survives an unrelated vertical keep band");
  Expect(lease.RetentionRadiusAround({-6, 0, -9}, 1) == 8 &&
             lease.RetentionRadiusAround({-6, 0, -9}, 12) == 12,
         "request cancellation includes an offset consumer lease without "
         "shrinking ordinary retention");
  lease.center.x -= 20;
  Expect(!lease.Contains({-7, 0, -9}) && lease.Contains({-27, 3, -9}),
         "moving demand releases the old lease instead of pinning the initial "
         "ring");
  cutum::RequiredLightScan fair_scan;
  std::set<std::pair<int, int>> relative_visits;
  for (int frame = 0; frame < 6; ++frame)
  {
    const glm::ivec3 moving_center(-frame, -3, frame);
    Expect(fair_scan.Drain(
               moving_center, domain.light, 32,
               [&](glm::ivec3 c)
               {
                 Expect(
                     c.y == 0,
                     "negative camera Y cannot leak into ground-column work");
                 relative_visits.emplace(c.x - moving_center.x,
                                         c.z - moving_center.z);
               }) == 32,
           "light discovery is bounded independently of camera changes");
  }
  Expect(relative_visits.size() == 169,
         "changing focus every frame cannot starve the outer light dependency "
         "ring");

  // A data-only halo must move, survive unload/cancel decisions and eventually
  // close beyond the original preload, even when ordinary keep radius is zero.
  std::set<std::pair<int, int>> rolling_data;
  for (const auto &c : cutum::RequiredDataColumns({0, 0, -2}, domain.data))
    rolling_data.emplace(c.x, c.z);
  cutum::RequiredLightScan rolling_scan;
  std::set<std::pair<int, int>> calculated_light;
  for (const auto &c : cutum::RequiredDataColumns({0, 0, -2}, domain.light))
    calculated_light.emplace(c.x, c.z);
  struct PendingData
  {
    glm::ivec3 coord;
    int ready_frame;
  };
  std::vector<PendingData> pending_data;
  int frame = 0;
  for (int step = 1; step <= 22; ++step)
  {
    const glm::ivec3 center(-step, -4, -2);
    lease = {center, domain.data};
    for (int local_frame = 0; local_frame < 8; ++local_frame, ++frame)
    {
      for (auto it = pending_data.begin(); it != pending_data.end();)
        if (it->ready_frame <= frame)
        {
          rolling_data.emplace(it->coord.x, it->coord.z);
          it = pending_data.erase(it);
        }
        else
          ++it;
      int retired = 0;
      for (auto it = rolling_data.begin();
           it != rolling_data.end() && retired < 2;)
        if (!lease.Contains({it->first, 0, it->second}))
        {
          it = rolling_data.erase(it);
          ++retired;
        }
        else
          ++it;
      const auto wanted = cutum::RequiredDataColumns(center, domain.data);
      cutum::AdmitRequiredDataColumns(
          wanted, 4,
          [&](glm::ivec3 c) { return rolling_data.count({c.x, c.z}) != 0; },
          [&](glm::ivec3 c)
          {
            return std::any_of(pending_data.begin(), pending_data.end(),
                               [&](const PendingData &p)
                               { return p.coord == c; });
          },
          [&](glm::ivec3 c) { pending_data.push_back({c, frame + 2}); });
      rolling_scan.Drain(center, domain.light, 32,
                         [&](glm::ivec3 c)
                         {
                           bool known = true;
                           for (int dz = -1; dz <= 1; ++dz)
                             for (int dx = -1; dx <= 1; ++dx)
                               known &= rolling_data.count(
                                            {c.x + dx, c.z + dz}) != 0;
                           if (known)
                             calculated_light.emplace(c.x, c.z);
                         });
      Expect(rolling_data.size() <= 260 && pending_data.size() <= 8,
             "consumer leases move with bounded residency and pending data");
    }
    for (const auto &c :
         cutum::RequiredDataColumns(center, domain.presentation + 1))
      Expect(calculated_light.count({c.x, c.z}) != 0,
             "every presentation light neighbor converges beyond the initial "
             "ring without teleport");
  }

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
