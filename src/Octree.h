#ifndef OCTREE_H
#define OCTREE_H

//#include <QVector3D>
#include <vector>
#include <memory>
#include <array>
#include <glm/glm.hpp>
#include "Object.h"

namespace cutum {

// Spatial partitioning for search optimization
class OctreeNode {
public:
    OctreeNode(const glm::vec3& center, float size);
    
    void Insert(std::shared_ptr<Object> object);
    void Remove(std::shared_ptr<Object> object);
    void Query(const glm::vec3& position, float radius, std::vector<std::shared_ptr<Object>>& result) const;
    void QueryRay(const glm::vec3& origin, const glm::vec3& direction, std::vector<std::shared_ptr<Object>>& result) const;
    void Clear();
    
private:
    bool IsLeaf() const;
    void Subdivide();
    bool Contains(const glm::vec3& point) const;
    bool IntersectsSphere(const glm::vec3& center, float radius) const;
    
    glm::vec3 center;
    float size;
    std::vector<std::shared_ptr<Object>> objects;
    std::array<std::unique_ptr<OctreeNode>, 8> children;
    static constexpr size_t MAX_OBJECTS_PER_NODE = 8;
    static constexpr float MIN_NODE_SIZE = 1.0f;
};

}

#endif // OCTREE_H
