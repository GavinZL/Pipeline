/**
 * @file PipelineGraph.h
 * @brief 管线拓扑图 - 管理Entity之间的连接关系
 */

#pragma once

#include "pipeline/data/EntityTypes.h"
#include "pipeline/entity/ProcessEntity.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <string>

namespace pipeline {

/**
 * @brief 图验证结果
 */
struct ValidationResult {
    bool valid = true;
    std::string errorMessage;
    std::vector<EntityId> problematicEntities;
};

/**
 * @brief 管线拓扑图
 * 
 * 管理所有Entity的拓扑关系，提供：
 * - 节点和边的CRUD操作
 * - DAG验证（循环检测）
 * - 拓扑排序
 * - 分层执行（同层可并行）
 * - 动态编辑支持
 */
class PipelineGraph {
public:
    PipelineGraph();
    ~PipelineGraph();
    
    // 禁止拷贝
    PipelineGraph(const PipelineGraph&) = delete;
    PipelineGraph& operator=(const PipelineGraph&) = delete;
    
    // ==========================================================================
    // Entity管理
    // ==========================================================================
    
    /**
     * @brief 添加Entity
     * @param entity Entity智能指针
     * @return Entity ID
     */
    EntityId addEntity(ProcessEntityPtr entity);
    
    /**
     * @brief 移除Entity
     * @param entityId Entity ID
     * @return 是否成功移除
     */
    bool removeEntity(EntityId entityId);
    
    /**
     * @brief 获取Entity
     * @param entityId Entity ID
     * @return Entity指针，不存在返回nullptr
     */
    ProcessEntityPtr getEntity(EntityId entityId) const;
    
    /**
     * @brief 检查Entity是否存在
     */
    bool hasEntity(EntityId entityId) const;
    
    /**
     * @brief 获取所有Entity
     */
    std::vector<ProcessEntityPtr> getAllEntities() const;
    
    /**
     * @brief 获取Entity数量
     */
    size_t getEntityCount() const;
    
    /**
     * @brief 按类型获取Entity
     */
    std::vector<ProcessEntityPtr> getEntitiesByType(EntityType type) const;
    
    /**
     * @brief 按名称查找Entity
     */
    ProcessEntityPtr findEntityByName(const std::string& name) const;
    
    // ==========================================================================
    // 连接管理
    // ==========================================================================
    
    /**
     * @brief 连接两个Entity
     * @param srcId 源Entity ID
     * @param srcPort 源输出端口名称
     * @param dstId 目标Entity ID
     * @param dstPort 目标输入端口名称
     * @return 是否成功连接
     */
    bool connect(EntityId srcId, const std::string& srcPort,
                 EntityId dstId, const std::string& dstPort);
    
    /**
     * @brief 连接两个Entity（使用默认端口）
     * @param srcId 源Entity ID
     * @param dstId 目标Entity ID
     * @return 是否成功连接
     */
    bool connect(EntityId srcId, EntityId dstId);
    
    /**
     * @brief 断开连接
     * @param srcId 源Entity ID
     * @param srcPort 源输出端口名称
     * @param dstId 目标Entity ID
     * @param dstPort 目标输入端口名称
     * @return 是否成功断开
     */
    bool disconnect(EntityId srcId, const std::string& srcPort,
                    EntityId dstId, const std::string& dstPort);
    
    /**
     * @brief 断开两个Entity之间的所有连接
     */
    bool disconnectAll(EntityId srcId, EntityId dstId);
    
    /**
     * @brief 断开Entity的所有连接
     */
    void disconnectEntity(EntityId entityId);
    
    /**
     * @brief 获取Entity的所有入边（前置依赖）
     */
    std::vector<Connection> getIncomingConnections(EntityId entityId) const;
    
    /**
     * @brief 获取Entity的所有出边（后置依赖）
     */
    std::vector<Connection> getOutgoingConnections(EntityId entityId) const;
    
    /**
     * @brief 获取所有连接
     */
    std::vector<Connection> getAllConnections() const;
    
    // ==========================================================================
    // 拓扑分析
    // ==========================================================================
    
    /**
     * @brief 验证图结构
     * 
     * 检查：
     * - 是否为DAG（无环）
     * - 端口连接是否有效
     * - 是否有孤立节点
     * @return 验证结果
     */
    ValidationResult validate() const;
    
    /**
     * @brief 检测是否存在环
     * @return 如果存在环返回true
     */
    bool hasCycle() const;
    
    /**
     * @brief 获取拓扑排序结果
     * @return 按拓扑顺序排列的Entity ID列表
     */
    std::vector<EntityId> getTopologicalOrder() const;
    
    /**
     * @brief 获取分层执行顺序
     * 
     * 将Entity按依赖关系分层，同层Entity可并行执行。
     * @return 分层的Entity ID列表
     */
    std::vector<std::vector<EntityId>> getExecutionLevels() const;
    
    /**
     * @brief 获取入口Entity（无输入依赖）
     */
    std::vector<EntityId> getSourceEntities() const;
    
    /**
     * @brief 获取出口Entity（无输出依赖）
     */
    std::vector<EntityId> getSinkEntities() const;
    
    /**
     * @brief 获取Entity的前置依赖
     */
    std::vector<EntityId> getPredecessors(EntityId entityId) const;
    
    /**
     * @brief 获取Entity的后置依赖
     */
    std::vector<EntityId> getSuccessors(EntityId entityId) const;
    
    /**
     * @brief 计算Entity的入度
     */
    size_t getInDegree(EntityId entityId) const;
    
    /**
     * @brief 计算Entity的出度
     */
    size_t getOutDegree(EntityId entityId) const;
    
    // ==========================================================================
    // 图操作
    // ==========================================================================
    
    /**
     * @brief 清空图
     */
    void clear();
    
    /**
     * @brief 克隆图结构
     * @return 克隆的图（Entity共享引用）
     */
    std::unique_ptr<PipelineGraph> clone() const;
    
    /**
     * @brief 导出为DOT格式（用于Graphviz可视化）
     */
    std::string exportToDot() const;
    
    /**
     * @brief 导出为JSON格式
     */
    std::string exportToJson() const;
    
    // ==========================================================================
    // 版本控制
    // ==========================================================================
    
    /**
     * @brief 获取图版本号
     * 
     * 每次修改图结构时版本号递增。
     */
    uint64_t getVersion() const { return mVersion; }
    
    /**
     * @brief 标记图已修改
     */
    void markDirty();
    
    /**
     * @brief 检查拓扑缓存是否有效
     */
    bool isTopologyCacheValid() const { return mTopologyCacheValid; }
    
private:
    // Entity存储
    mutable std::mutex mMutex;
    std::unordered_map<EntityId, ProcessEntityPtr> mEntities;
    
    // 邻接表（出边）
    std::unordered_map<EntityId, std::vector<Connection>> mOutgoingEdges;
    
    // 邻接表（入边）
    std::unordered_map<EntityId, std::vector<Connection>> mIncomingEdges;
    
    // 版本控制
    uint64_t mVersion = 0;
    
    // 拓扑缓存
    mutable bool mTopologyCacheValid = false;
    mutable std::vector<EntityId> mTopologicalOrderCache;
    mutable std::vector<std::vector<EntityId>> mExecutionLevelsCache;
    
    // ==========================================================================
    // 内部方法
    // ==========================================================================
    
    /**
     * @brief 执行拓扑排序（Kahn算法）
     */
    bool topologicalSort(std::vector<EntityId>& result) const;
    
    /**
     * @brief 计算执行层级
     */
    void computeExecutionLevels() const;
    
    /**
     * @brief DFS检测环
     */
    bool hasCycleDFS(EntityId node, 
                     std::unordered_set<EntityId>& visited,
                     std::unordered_set<EntityId>& recursionStack) const;
    
    /**
     * @brief 使缓存失效
     */
    void invalidateCache();
    
    /**
     * @brief 更新拓扑缓存
     */
    void updateTopologyCache() const;
};

} // namespace pipeline
