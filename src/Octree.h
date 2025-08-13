#ifndef OCTREE_H
#define OCTREE_H

#include <QVector3D>
#include <vector>
#include <memory>
#include <array>
#include "Object.h"

namespace cutum {

// Пространственное разбиение для оптимизации поиска
class OctreeNode {
public:
    OctreeNode(const QVector3D& center, float size);
    
    void Insert(std::shared_ptr<Object> object);
    void Remove(std::shared_ptr<Object> object);
    void Query(const QVector3D& position, float radius, std::vector<std::shared_ptr<Object>>& result) const;
    void QueryRay(const QVector3D& origin, const QVector3D& direction, std::vector<std::shared_ptr<Object>>& result) const;
    void Clear();
    
private:
    bool IsLeaf() const;
    void Subdivide();
    bool Contains(const QVector3D& point) const;
    bool IntersectsSphere(const QVector3D& center, float radius) const;
    
    QVector3D center;
    float size;
    std::vector<std::shared_ptr<Object>> objects;
    std::array<std::unique_ptr<OctreeNode>, 8> children;
    static constexpr size_t MAX_OBJECTS_PER_NODE = 8;
    static constexpr float MIN_NODE_SIZE = 1.0f;
};

}

#endif // OCTREE_H
