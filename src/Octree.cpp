#include "World.h"
#include "Object.h"
#include <algorithm>
#include <cmath>

namespace cutum {

OctreeNode::OctreeNode(const QVector3D& center, float size)
    : center(center), size(size) {
}

void OctreeNode::Insert(std::shared_ptr<Object> object) {
    if (IsLeaf()) {
        objects.push_back(object);
        if (objects.size() > MAX_OBJECTS_PER_NODE && size > MIN_NODE_SIZE) {
            Subdivide();
        }
    } else {
        // Найти подходящий дочерний узел
        QVector3D objectPos(object->GetPose()(0,3), object->GetPose()(1,3), object->GetPose()(2,3));
        for (auto& child : children) {
            if (child && child->Contains(objectPos)) {
                child->Insert(object);
                break;
            }
        }
    }
}

void OctreeNode::Remove(std::shared_ptr<Object> object) {
    if (IsLeaf()) {
        auto it = std::find(objects.begin(), objects.end(), object);
        if (it != objects.end()) {
            objects.erase(it);
        }
    } else {
        QVector3D objectPos(object->GetPose()(0,3), object->GetPose()(1,3), object->GetPose()(2,3));
        for (auto& child : children) {
            if (child && child->Contains(objectPos)) {
                child->Remove(object);
                break;
            }
        }
    }
}

void OctreeNode::Query(const QVector3D& position, float radius, std::vector<std::shared_ptr<Object>>& result) const {
    if (!IntersectsSphere(position, radius)) {
        return;
    }
    
    if (IsLeaf()) {
        for (const auto& obj : objects) {
            QVector3D objPos(obj->GetPose()(0,3), obj->GetPose()(1,3), obj->GetPose()(2,3));
            if ((objPos - position).length() <= radius) {
                result.push_back(obj);
            }
        }
    } else {
        for (const auto& child : children) {
            if (child) {
                child->Query(position, radius, result);
            }
        }
    }
}

void OctreeNode::QueryRay(const QVector3D& origin, const QVector3D& direction, std::vector<std::shared_ptr<Object>>& result) const {
    // Простая проверка пересечения луча с bounding box узла
    QVector3D min = center - QVector3D(size/2, size/2, size/2);
    QVector3D max = center + QVector3D(size/2, size/2, size/2);
    
    // Простая проверка пересечения луча с AABB
    float t1 = (min.x() - origin.x()) / direction.x();
    float t2 = (max.x() - origin.x()) / direction.x();
    float t3 = (min.y() - origin.y()) / direction.y();
    float t4 = (max.y() - origin.y()) / direction.y();
    float t5 = (min.z() - origin.z()) / direction.z();
    float t6 = (max.z() - origin.z()) / direction.z();
    
    float tmin = std::max(std::max(std::min(t1, t2), std::min(t3, t4)), std::min(t5, t6));
    float tmax = std::min(std::min(std::max(t1, t2), std::max(t3, t4)), std::max(t5, t6));
    
    if (tmax < 0 || tmin > tmax) {
        return;
    }
    
    if (IsLeaf()) {
        for (const auto& obj : objects) {
            result.push_back(obj);
        }
    } else {
        for (const auto& child : children) {
            if (child) {
                child->QueryRay(origin, direction, result);
            }
        }
    }
}

void OctreeNode::Clear() {
    objects.clear();
    for (auto& child : children) {
        if (child) {
            child->Clear();
        }
    }
}

bool OctreeNode::IsLeaf() const {
    return children[0] == nullptr;
}

void OctreeNode::Subdivide() {
    float halfSize = size / 2.0f;
    float quarterSize = size / 4.0f;
    
    // Создать 8 дочерних узлов
    children[0] = std::make_unique<OctreeNode>(center + QVector3D(-quarterSize, -quarterSize, -quarterSize), halfSize);
    children[1] = std::make_unique<OctreeNode>(center + QVector3D( quarterSize, -quarterSize, -quarterSize), halfSize);
    children[2] = std::make_unique<OctreeNode>(center + QVector3D(-quarterSize, -quarterSize,  quarterSize), halfSize);
    children[3] = std::make_unique<OctreeNode>(center + QVector3D( quarterSize, -quarterSize,  quarterSize), halfSize);
    children[4] = std::make_unique<OctreeNode>(center + QVector3D(-quarterSize,  quarterSize, -quarterSize), halfSize);
    children[5] = std::make_unique<OctreeNode>(center + QVector3D( quarterSize,  quarterSize, -quarterSize), halfSize);
    children[6] = std::make_unique<OctreeNode>(center + QVector3D(-quarterSize,  quarterSize,  quarterSize), halfSize);
    children[7] = std::make_unique<OctreeNode>(center + QVector3D( quarterSize,  quarterSize,  quarterSize), halfSize);
    
    // Перераспределить объекты по дочерним узлам
    for (const auto& obj : objects) {
        QVector3D objPos(obj->GetPose()(0,3), obj->GetPose()(1,3), obj->GetPose()(2,3));
        for (auto& child : children) {
            if (child->Contains(objPos)) {
                child->Insert(obj);
                break;
            }
        }
    }
    
    // Очистить список объектов в текущем узле
    objects.clear();
}

bool OctreeNode::Contains(const QVector3D& point) const {
    QVector3D min = center - QVector3D(size/2, size/2, size/2);
    QVector3D max = center + QVector3D(size/2, size/2, size/2);
    return point.x() >= min.x() && point.x() <= max.x() &&
           point.y() >= min.y() && point.y() <= max.y() &&
           point.z() >= min.z() && point.z() <= max.z();
}

bool OctreeNode::IntersectsSphere(const QVector3D& sphereCenter, float sphereRadius) const {
    QVector3D min = center - QVector3D(size/2, size/2, size/2);
    QVector3D max = center + QVector3D(size/2, size/2, size/2);
    
    float closestX = std::max(min.x(), std::min(sphereCenter.x(), max.x()));
    float closestY = std::max(min.y(), std::min(sphereCenter.y(), max.y()));
    float closestZ = std::max(min.z(), std::min(sphereCenter.z(), max.z()));
    
    QVector3D closestPoint(closestX, closestY, closestZ);
    return (closestPoint - sphereCenter).length() <= sphereRadius;
}

} // namespace cutum
