/**
 * @file PipelineGraph.cpp
 * @brief PipelineGraph实现
 */

#include "pipeline/core/PipelineGraph.h"
#include <queue>
#include <sstream>
#include <algorithm>

namespace pipeline {

PipelineGraph::PipelineGraph() = default;

PipelineGraph::~PipelineGraph() {
    clear();
}

// =============================================================================
// Entity管理
// =============================================================================

EntityId PipelineGraph::addEntity(ProcessEntityPtr entity) {
    if (!entity) {
        return InvalidEntityId;
    }
    
    std::lock_guard<std::mutex> lock(mMutex);
    
    EntityId id = entity->getId();
    
    // 检查是否已存在
    if (mEntities.find(id) != mEntities.end()) {
        return id; // 已存在，返回现有ID
    }
    
    mEntities[id] = entity;
    mOutgoingEdges[id] = {};
    mIncomingEdges[id] = {};
    
    invalidateCache();
    ++mVersion;
    
    return id;
}

bool PipelineGraph::removeEntity(EntityId entityId) {
    std::lock_guard<std::mutex> lock(mMutex);
    
    auto it = mEntities.find(entityId);
    if (it == mEntities.end()) {
        return false;
    }
    
    // 断开所有连接
    mOutgoingEdges.erase(entityId);
    mIncomingEdges.erase(entityId);
    
    // 从其他Entity的连接中移除
    for (auto& [id, edges] : mOutgoingEdges) {
        edges.erase(
            std::remove_if(edges.begin(), edges.end(),
                [entityId](const Connection& c) {
                    return c.dstEntity == entityId;
                }),
            edges.end());
    }
    
    for (auto& [id, edges] : mIncomingEdges) {
        edges.erase(
            std::remove_if(edges.begin(), edges.end(),
                [entityId](const Connection& c) {
                    return c.srcEntity == entityId;
                }),
            edges.end());
    }
    
    mEntities.erase(it);
    
    invalidateCache();
    ++mVersion;
    
    return true;
}

ProcessEntityPtr PipelineGraph::getEntity(EntityId entityId) const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    auto it = mEntities.find(entityId);
    if (it != mEntities.end()) {
        return it->second;
    }
    return nullptr;
}

bool PipelineGraph::hasEntity(EntityId entityId) const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mEntities.find(entityId) != mEntities.end();
}

std::vector<ProcessEntityPtr> PipelineGraph::getAllEntities() const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    std::vector<ProcessEntityPtr> result;
    result.reserve(mEntities.size());
    
    for (const auto& [id, entity] : mEntities) {
        result.push_back(entity);
    }
    
    return result;
}

size_t PipelineGraph::getEntityCount() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mEntities.size();
}

std::vector<ProcessEntityPtr> PipelineGraph::getEntitiesByType(EntityType type) const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    std::vector<ProcessEntityPtr> result;
    for (const auto& [id, entity] : mEntities) {
        if (entity->getType() == type) {
            result.push_back(entity);
        }
    }
    
    return result;
}

ProcessEntityPtr PipelineGraph::findEntityByName(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    for (const auto& [id, entity] : mEntities) {
        if (entity->getName() == name) {
            return entity;
        }
    }
    
    return nullptr;
}

// =============================================================================
// 连接管理
// =============================================================================

bool PipelineGraph::connect(EntityId srcId, const std::string& srcPort,
                           EntityId dstId, const std::string& dstPort) {
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 检查Entity是否存在
    auto srcIt = mEntities.find(srcId);
    auto dstIt = mEntities.find(dstId);
    
    if (srcIt == mEntities.end() || dstIt == mEntities.end()) {
        return false;
    }
    
    // 获取端口
    OutputPort* outPort = srcIt->second->getOutputPort(srcPort);
    InputPort* inPort = dstIt->second->getInputPort(dstPort);
    
    if (!outPort || !inPort) {
        return false;
    }
    
    // 检查是否已连接
    for (const auto& conn : mOutgoingEdges[srcId]) {
        if (conn.srcPort == srcPort && conn.dstEntity == dstId && conn.dstPort == dstPort) {
            return true; // 已存在
        }
    }
    
    // 创建连接
    Connection conn;
    conn.srcEntity = srcId;
    conn.srcPort = srcPort;
    conn.dstEntity = dstId;
    conn.dstPort = dstPort;
    
    mOutgoingEdges[srcId].push_back(conn);
    mIncomingEdges[dstId].push_back(conn);
    
    // 设置端口连接
    outPort->addConnection(inPort);
    inPort->setSource(srcId, srcPort);
    
    invalidateCache();
    ++mVersion;
    
    return true;
}

bool PipelineGraph::connect(EntityId srcId, EntityId dstId) {
    // 使用默认端口（第一个端口或名为"output"/"input"的端口）
    auto srcEntity = getEntity(srcId);
    auto dstEntity = getEntity(dstId);
    
    if (!srcEntity || !dstEntity) {
        return false;
    }
    
    std::string srcPort = "output";
    std::string dstPort = "input";
    
    // 查找默认端口
    if (srcEntity->getOutputPortCount() > 0) {
        auto port = srcEntity->getOutputPort("output");
        if (!port) {
            port = srcEntity->getOutputPort(size_t(0));
        }
        if (port) {
            srcPort = port->getName();
        }
    }
    
    if (dstEntity->getInputPortCount() > 0) {
        auto port = dstEntity->getInputPort("input");
        if (!port) {
            port = dstEntity->getInputPort(size_t(0));
        }
        if (port) {
            dstPort = port->getName();
        }
    }
    
    return connect(srcId, srcPort, dstId, dstPort);
}

bool PipelineGraph::disconnect(EntityId srcId, const std::string& srcPort,
                              EntityId dstId, const std::string& dstPort) {
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 从出边中移除
    auto& outEdges = mOutgoingEdges[srcId];
    auto outIt = std::find_if(outEdges.begin(), outEdges.end(),
        [&](const Connection& c) {
            return c.srcPort == srcPort && c.dstEntity == dstId && c.dstPort == dstPort;
        });
    
    if (outIt == outEdges.end()) {
        return false;
    }
    
    outEdges.erase(outIt);
    
    // 从入边中移除
    auto& inEdges = mIncomingEdges[dstId];
    inEdges.erase(
        std::remove_if(inEdges.begin(), inEdges.end(),
            [&](const Connection& c) {
                return c.srcEntity == srcId && c.srcPort == srcPort && c.dstPort == dstPort;
            }),
        inEdges.end());
    
    // 更新端口连接
    auto srcEntity = mEntities[srcId];
    auto dstEntity = mEntities[dstId];
    
    if (srcEntity && dstEntity) {
        OutputPort* outPort = srcEntity->getOutputPort(srcPort);
        InputPort* inPort = dstEntity->getInputPort(dstPort);
        
        if (outPort && inPort) {
            outPort->removeConnection(inPort);
            inPort->disconnect();
        }
    }
    
    invalidateCache();
    ++mVersion;
    
    return true;
}

bool PipelineGraph::disconnectAll(EntityId srcId, EntityId dstId) {
    std::lock_guard<std::mutex> lock(mMutex);
    
    bool removed = false;
    
    // 收集要移除的连接
    std::vector<Connection> toRemove;
    for (const auto& conn : mOutgoingEdges[srcId]) {
        if (conn.dstEntity == dstId) {
            toRemove.push_back(conn);
        }
    }
    
    // 移除连接
    for (const auto& conn : toRemove) {
        // 这里不能调用disconnect因为已经持有锁
        auto& outEdges = mOutgoingEdges[srcId];
        outEdges.erase(
            std::remove_if(outEdges.begin(), outEdges.end(),
                [&](const Connection& c) {
                    return c.srcPort == conn.srcPort && 
                           c.dstEntity == conn.dstEntity && 
                           c.dstPort == conn.dstPort;
                }),
            outEdges.end());
        
        auto& inEdges = mIncomingEdges[dstId];
        inEdges.erase(
            std::remove_if(inEdges.begin(), inEdges.end(),
                [&](const Connection& c) {
                    return c.srcEntity == conn.srcEntity && 
                           c.srcPort == conn.srcPort && 
                           c.dstPort == conn.dstPort;
                }),
            inEdges.end());
        
        removed = true;
    }
    
    if (removed) {
        invalidateCache();
        ++mVersion;
    }
    
    return removed;
}

void PipelineGraph::disconnectEntity(EntityId entityId) {
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 清除出边
    mOutgoingEdges[entityId].clear();
    
    // 清除入边
    mIncomingEdges[entityId].clear();
    
    // 从其他Entity的连接中移除
    for (auto& [id, edges] : mOutgoingEdges) {
        edges.erase(
            std::remove_if(edges.begin(), edges.end(),
                [entityId](const Connection& c) {
                    return c.dstEntity == entityId;
                }),
            edges.end());
    }
    
    for (auto& [id, edges] : mIncomingEdges) {
        edges.erase(
            std::remove_if(edges.begin(), edges.end(),
                [entityId](const Connection& c) {
                    return c.srcEntity == entityId;
                }),
            edges.end());
    }
    
    invalidateCache();
    ++mVersion;
}

std::vector<Connection> PipelineGraph::getIncomingConnections(EntityId entityId) const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    auto it = mIncomingEdges.find(entityId);
    if (it != mIncomingEdges.end()) {
        return it->second;
    }
    return {};
}

std::vector<Connection> PipelineGraph::getOutgoingConnections(EntityId entityId) const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    auto it = mOutgoingEdges.find(entityId);
    if (it != mOutgoingEdges.end()) {
        return it->second;
    }
    return {};
}

std::vector<Connection> PipelineGraph::getAllConnections() const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    std::vector<Connection> result;
    for (const auto& [id, edges] : mOutgoingEdges) {
        result.insert(result.end(), edges.begin(), edges.end());
    }
    return result;
}

// =============================================================================
// 拓扑分析
// =============================================================================

ValidationResult PipelineGraph::validate() const {
    ValidationResult result;
    
    // 检查是否为DAG
    if (hasCycle()) {
        result.valid = false;
        result.errorMessage = "Graph contains a cycle";
        return result;
    }
    
    // 检查所有端口连接是否有效
    std::lock_guard<std::mutex> lock(mMutex);
    
    for (const auto& [id, entity] : mEntities) {
        // 检查输入端口
        for (size_t i = 0; i < entity->getInputPortCount(); ++i) {
            auto port = entity->getInputPort(i);
            // 输入端口可以不连接（对于InputEntity）
        }
        
        // 检查输出端口
        for (size_t i = 0; i < entity->getOutputPortCount(); ++i) {
            auto port = entity->getOutputPort(i);
            // 输出端口可以不连接（对于OutputEntity）
        }
    }
    
    return result;
}

bool PipelineGraph::hasCycle() const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    std::unordered_set<EntityId> visited;
    std::unordered_set<EntityId> recursionStack;
    
    for (const auto& [id, entity] : mEntities) {
        if (hasCycleDFS(id, visited, recursionStack)) {
            return true;
        }
    }
    
    return false;
}

bool PipelineGraph::hasCycleDFS(EntityId node,
                                std::unordered_set<EntityId>& visited,
                                std::unordered_set<EntityId>& recursionStack) const {
    if (recursionStack.find(node) != recursionStack.end()) {
        return true; // 发现环
    }
    
    if (visited.find(node) != visited.end()) {
        return false; // 已访问过
    }
    
    visited.insert(node);
    recursionStack.insert(node);
    
    auto it = mOutgoingEdges.find(node);
    if (it != mOutgoingEdges.end()) {
        for (const auto& conn : it->second) {
            if (hasCycleDFS(conn.dstEntity, visited, recursionStack)) {
                return true;
            }
        }
    }
    
    recursionStack.erase(node);
    return false;
}

std::vector<EntityId> PipelineGraph::getTopologicalOrder() const {
    updateTopologyCache();
    return mTopologicalOrderCache;
}

std::vector<std::vector<EntityId>> PipelineGraph::getExecutionLevels() const {
    updateTopologyCache();
    return mExecutionLevelsCache;
}

std::vector<EntityId> PipelineGraph::getSourceEntities() const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    std::vector<EntityId> result;
    for (const auto& [id, edges] : mIncomingEdges) {
        if (edges.empty()) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<EntityId> PipelineGraph::getSinkEntities() const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    std::vector<EntityId> result;
    for (const auto& [id, edges] : mOutgoingEdges) {
        if (edges.empty()) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<EntityId> PipelineGraph::getPredecessors(EntityId entityId) const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    std::vector<EntityId> result;
    auto it = mIncomingEdges.find(entityId);
    if (it != mIncomingEdges.end()) {
        for (const auto& conn : it->second) {
            result.push_back(conn.srcEntity);
        }
    }
    
    // 去重
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    
    return result;
}

std::vector<EntityId> PipelineGraph::getSuccessors(EntityId entityId) const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    std::vector<EntityId> result;
    auto it = mOutgoingEdges.find(entityId);
    if (it != mOutgoingEdges.end()) {
        for (const auto& conn : it->second) {
            result.push_back(conn.dstEntity);
        }
    }
    
    // 去重
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    
    return result;
}

size_t PipelineGraph::getInDegree(EntityId entityId) const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    auto it = mIncomingEdges.find(entityId);
    if (it != mIncomingEdges.end()) {
        return it->second.size();
    }
    return 0;
}

size_t PipelineGraph::getOutDegree(EntityId entityId) const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    auto it = mOutgoingEdges.find(entityId);
    if (it != mOutgoingEdges.end()) {
        return it->second.size();
    }
    return 0;
}

// =============================================================================
// 图操作
// =============================================================================

void PipelineGraph::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    
    mEntities.clear();
    mOutgoingEdges.clear();
    mIncomingEdges.clear();
    
    invalidateCache();
    ++mVersion;
}

std::string PipelineGraph::exportToDot() const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    std::ostringstream oss;
    oss << "digraph Pipeline {\n";
    oss << "  rankdir=LR;\n";
    oss << "  node [shape=box];\n\n";
    
    // 节点
    for (const auto& [id, entity] : mEntities) {
        oss << "  " << id << " [label=\"" << entity->getName() 
            << "\\n(" << entityTypeToString(entity->getType()) << ")\"];\n";
    }
    
    oss << "\n";
    
    // 边
    for (const auto& [srcId, edges] : mOutgoingEdges) {
        for (const auto& conn : edges) {
            oss << "  " << srcId << " -> " << conn.dstEntity
                << " [label=\"" << conn.srcPort << " -> " << conn.dstPort << "\"];\n";
        }
    }
    
    oss << "}\n";
    
    return oss.str();
}

std::string PipelineGraph::exportToJson() const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"entities\": [\n";
    
    bool first = true;
    for (const auto& [id, entity] : mEntities) {
        if (!first) oss << ",\n";
        first = false;
        oss << "    {\"id\": " << id 
            << ", \"name\": \"" << entity->getName() << "\""
            << ", \"type\": \"" << entityTypeToString(entity->getType()) << "\"}";
    }
    
    oss << "\n  ],\n";
    oss << "  \"connections\": [\n";
    
    first = true;
    for (const auto& [srcId, edges] : mOutgoingEdges) {
        for (const auto& conn : edges) {
            if (!first) oss << ",\n";
            first = false;
            oss << "    {\"src\": " << conn.srcEntity 
                << ", \"srcPort\": \"" << conn.srcPort << "\""
                << ", \"dst\": " << conn.dstEntity
                << ", \"dstPort\": \"" << conn.dstPort << "\"}";
        }
    }
    
    oss << "\n  ]\n";
    oss << "}\n";
    
    return oss.str();
}

// =============================================================================
// 内部方法
// =============================================================================

void PipelineGraph::invalidateCache() {
    mTopologyCacheValid = false;
}

void PipelineGraph::updateTopologyCache() const {
    if (mTopologyCacheValid) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mMutex);
    
    // Kahn算法进行拓扑排序
    topologicalSort(mTopologicalOrderCache);
    
    // 计算执行层级
    computeExecutionLevels();
    
    mTopologyCacheValid = true;
}

bool PipelineGraph::topologicalSort(std::vector<EntityId>& result) const {
    result.clear();
    
    // 计算入度
    std::unordered_map<EntityId, size_t> inDegree;
    for (const auto& [id, entity] : mEntities) {
        inDegree[id] = 0;
    }
    
    for (const auto& [srcId, edges] : mOutgoingEdges) {
        for (const auto& conn : edges) {
            inDegree[conn.dstEntity]++;
        }
    }
    
    // 将入度为0的节点加入队列
    std::queue<EntityId> queue;
    for (const auto& [id, degree] : inDegree) {
        if (degree == 0) {
            queue.push(id);
        }
    }
    
    // BFS
    while (!queue.empty()) {
        EntityId node = queue.front();
        queue.pop();
        result.push_back(node);
        
        auto it = mOutgoingEdges.find(node);
        if (it != mOutgoingEdges.end()) {
            for (const auto& conn : it->second) {
                if (--inDegree[conn.dstEntity] == 0) {
                    queue.push(conn.dstEntity);
                }
            }
        }
    }
    
    return result.size() == mEntities.size();
}

void PipelineGraph::computeExecutionLevels() const {
    mExecutionLevelsCache.clear();
    
    if (mEntities.empty()) {
        return;
    }
    
    // 计算每个节点的层级（最长路径长度）
    std::unordered_map<EntityId, int> levels;
    
    // 初始化入度为0的节点层级为0
    for (const auto& [id, entity] : mEntities) {
        auto inEdges = mIncomingEdges.find(id);
        if (inEdges == mIncomingEdges.end() || inEdges->second.empty()) {
            levels[id] = 0;
        } else {
            levels[id] = -1; // 未计算
        }
    }
    
    // 按拓扑顺序计算层级
    for (EntityId id : mTopologicalOrderCache) {
        if (levels[id] == -1) {
            int maxPredLevel = -1;
            auto inEdges = mIncomingEdges.find(id);
            if (inEdges != mIncomingEdges.end()) {
                for (const auto& conn : inEdges->second) {
                    maxPredLevel = std::max(maxPredLevel, levels[conn.srcEntity]);
                }
            }
            levels[id] = maxPredLevel + 1;
        }
    }
    
    // 按层级分组
    int maxLevel = 0;
    for (const auto& [id, level] : levels) {
        maxLevel = std::max(maxLevel, level);
    }
    
    mExecutionLevelsCache.resize(maxLevel + 1);
    for (const auto& [id, level] : levels) {
        mExecutionLevelsCache[level].push_back(id);
    }
}

void PipelineGraph::markDirty() {
    invalidateCache();
}

} // namespace pipeline
