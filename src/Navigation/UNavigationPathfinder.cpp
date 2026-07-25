#include "Navigation/UNavigationPathfinder.h"
#include "Navigation/NavigationPathBudget.h"
#include "World/Math/GridMath.h"
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace cutum
{

namespace
{

struct NodeKey
{
  int x{0};
  int ground_y{0};
  int z{0};

  bool operator==(const NodeKey &other) const
  {
    return x == other.x && ground_y == other.ground_y && z == other.z;
  }

  bool operator!=(const NodeKey &other) const { return !(*this == other); }
};

struct NodeKeyHash
{
  size_t operator()(const NodeKey &key) const
  {
    const size_t hx = std::hash<int>()(key.x);
    const size_t hy = std::hash<int>()(key.ground_y);
    const size_t hz = std::hash<int>()(key.z);
    return hx ^ (hy << 1) ^ (hz << 2);
  }
};

NodeKey ToKey(const NavigationStandNode &node)
{
  NodeKey key;
  key.x = node.x;
  key.ground_y = node.ground_y;
  key.z = node.z;
  return key;
}

NavigationStandNode ToNode(const NodeKey &key)
{
  NavigationStandNode node;
  node.x = key.x;
  node.ground_y = key.ground_y;
  node.z = key.z;
  return node;
}

NavigationStandNode StandNodeFromBody(const glm::vec3 &body_origin)
{
  NavigationStandNode node;
  node.x = WorldCoordToBlockIndex(body_origin.x);
  node.z = WorldCoordToBlockIndex(body_origin.z);
  node.ground_y = WorldCoordToBlockIndex(body_origin.y - 0.05f);
  return node;
}

glm::vec3 BodyOriginFromNode(const NavigationStandNode &node)
{
  return glm::vec3(static_cast<float>(node.x), BlockTopY(node.ground_y),
                   static_cast<float>(node.z));
}

NavigationStandNode SnapStandNode(const IUWorldNavigation &navigation,
                                  NavigationStandNode node, float body_height,
                                  const glm::vec3 &body_origin,
                                  float max_snap_blocks)
{
  const float max_delta = std::max(0.5f, max_snap_blocks) + 0.05f;
  auto accept = [&](const NavigationStandNode &trial) -> bool
  {
    if (!navigation.IsTerrestrialStandNode(trial, body_height))
    {
      return false;
    }
    // Reject snaps that would "fix" an eye-height goal down to feet.
    return std::abs(BlockTopY(trial.ground_y) - body_origin.y) <= max_delta;
  };

  if (accept(node))
  {
    return node;
  }
  const int base_y = node.ground_y;
  const int base_x = node.x;
  const int base_z = node.z;
  // Vertical first (common float / half-block mismatch).
  for (int dy = 1; dy <= 2; ++dy)
  {
    for (const int sign : {-1, 1})
    {
      NavigationStandNode trial = node;
      trial.ground_y = base_y + sign * dy;
      if (accept(trial))
      {
        return trial;
      }
    }
  }
  // XZ neighbors: bodies near cell edges where integer-center clearance fails.
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      if (dx == 0 && dz == 0)
      {
        continue;
      }
      for (int dy = -2; dy <= 2; ++dy)
      {
        NavigationStandNode trial;
        trial.x = base_x + dx;
        trial.z = base_z + dz;
        trial.ground_y = base_y + dy;
        if (accept(trial))
        {
          return trial;
        }
      }
    }
  }
  return node;
}

float Heuristic(const NodeKey &a, const NodeKey &b)
{
  const float dx = static_cast<float>(a.x - b.x);
  const float dy = static_cast<float>(a.ground_y - b.ground_y);
  const float dz = static_cast<float>(a.z - b.z);
  return std::abs(dx) + std::abs(dz) + std::abs(dy) * 1.5f;
}

NavigationPath ReconstructPath(
    const NodeKey &start_key, const NodeKey &end_key,
    const std::unordered_map<NodeKey, NodeKey, NodeKeyHash> &came_from,
    int max_waypoints, bool partial)
{
  NavigationPath result;
  std::vector<NodeKey> reversed;
  NodeKey walk = end_key;
  reversed.push_back(walk);
  while (walk != start_key)
  {
    const auto it = came_from.find(walk);
    if (it == came_from.end())
    {
      return result;
    }
    walk = it->second;
    reversed.push_back(walk);
  }
  result.valid = true;
  result.partial = partial;
  if (partial)
  {
    result.failReason = "partial";
  }
  for (auto it = reversed.rbegin(); it != reversed.rend(); ++it)
  {
    if (result.waypoints.size() >= static_cast<size_t>(max_waypoints))
    {
      break;
    }
    result.waypoints.push_back({BodyOriginFromNode(ToNode(*it))});
  }
  return result;
}

} // namespace

NavigationPath UNavigationPathfinder::FindTerrestrialPath(
    const IUWorldNavigation &navigation, const glm::vec3 &start_body,
    const glm::vec3 &goal_body, const NavigationQuery &query)
{
  NavigationPath result;
  if (!UNavigationPathBudget::HasRemainingBudget())
  {
    result.failReason = "budget_exhausted";
    return result;
  }

  NavigationStandNode start =
      SnapStandNode(navigation, StandNodeFromBody(start_body),
                    query.body_height, start_body, query.max_jump);
  NavigationStandNode goal =
      SnapStandNode(navigation, StandNodeFromBody(goal_body),
                    query.body_height, goal_body, query.max_jump);
  if (!navigation.IsTerrestrialStandNode(start, query.body_height))
  {
    result.failReason = "start_invalid";
    return result;
  }
  if (!navigation.IsTerrestrialStandNode(goal, query.body_height))
  {
    result.failReason = "goal_invalid";
    return result;
  }

  const NodeKey start_key = ToKey(start);
  const NodeKey goal_key = ToKey(goal);
  if (start_key == goal_key)
  {
    result.valid = true;
    result.waypoints.push_back({BodyOriginFromNode(goal)});
    return result;
  }

  struct OpenEntry
  {
    float f{0.0f};
    NodeKey key;
  };
  struct OpenCompare
  {
    bool operator()(const OpenEntry &a, const OpenEntry &b) const
    {
      return a.f > b.f;
    }
  };

  std::priority_queue<OpenEntry, std::vector<OpenEntry>, OpenCompare> open;
  std::unordered_map<NodeKey, NodeKey, NodeKeyHash> came_from;
  std::unordered_map<NodeKey, float, NodeKeyHash> g_score;
  std::unordered_set<NodeKey, NodeKeyHash> closed;

  const auto push_open = [&](const NodeKey &key)
  {
    const float g = g_score[key];
    open.push({g + Heuristic(key, goal_key), key});
  };

  g_score[start_key] = 0.0f;
  push_open(start_key);

  constexpr int kMaxWaypoints = 256;
  constexpr int kDefaultMaxExpands = kMaxWaypoints * 32;
  const int local_max =
      query.max_expands > 0 ? query.max_expands : kDefaultMaxExpands;
  int expanded = 0;
  bool budget_cut = false;

  NodeKey best_key = start_key;
  float best_h = Heuristic(start_key, goal_key);

  while (!open.empty() && expanded < local_max)
  {
    const NodeKey current = open.top().key;
    open.pop();
    if (closed.count(current) != 0)
    {
      continue;
    }
    if (!UNavigationPathBudget::TryConsumeExpand())
    {
      budget_cut = true;
      break;
    }
    closed.insert(current);
    ++expanded;

    const float h = Heuristic(current, goal_key);
    if (h < best_h - 1e-4f ||
        (std::abs(h - best_h) <= 1e-4f &&
         g_score[current] < g_score[best_key]))
    {
      best_h = h;
      best_key = current;
    }

    if (current == goal_key)
    {
      return ReconstructPath(start_key, current, came_from, kMaxWaypoints,
                             false);
    }

    const NavigationStandNode from = ToNode(current);
    const int search = query.search_distance;
    for (int dx = -1; dx <= 1; ++dx)
    {
      for (int dz = -1; dz <= 1; ++dz)
      {
        if (std::abs(dx) + std::abs(dz) != 1)
        {
          continue;
        }
        for (int dy = -static_cast<int>(query.max_drop);
             dy <= static_cast<int>(std::ceil(query.max_jump)); ++dy)
        {
          NavigationStandNode to{from.x + dx, from.ground_y + dy, from.z + dz};
          if (std::abs(to.x - start.x) > search ||
              std::abs(to.z - start.z) > search)
          {
            continue;
          }
          if (!navigation.CanStepTerrestrial(from, to, query.max_jump,
                                             query.max_drop, query.body_height))
          {
            continue;
          }
          const NodeKey neighbor = ToKey(to);
          if (closed.count(neighbor) != 0)
          {
            continue;
          }
          const float step_cost =
              1.0f + std::abs(static_cast<float>(dy)) * 0.35f;
          const float tentative = g_score[current] + step_cost;
          if (g_score.find(neighbor) == g_score.end() ||
              tentative < g_score[neighbor])
          {
            came_from[neighbor] = current;
            g_score[neighbor] = tentative;
            push_open(neighbor);
          }
        }
      }
    }
  }

  // Partial / closest path (MC-style): usable corridor toward goal.
  if (best_key != start_key)
  {
    NavigationPath partial =
        ReconstructPath(start_key, best_key, came_from, kMaxWaypoints, true);
    if (partial.valid && partial.waypoints.size() >= 2)
    {
      return partial;
    }
  }

  result.failReason = budget_cut ? "budget_exhausted" : "search_exhausted";
  return result;
}

} // namespace cutum
