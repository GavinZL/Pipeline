/**
 * @file PipelineExecutor.h
 * @brief 管线执行调度器 - 集成TaskQueue进行任务调度
 */

#pragma once

#include "pipeline/data/EntityTypes.h"
#include "PipelineGraph.h"
#include <memory>
#include <atomic>
#include <functional>

// 前向声明TaskQueue类型
namespace task {
class TaskQueue;
class TaskGroup;
class Consumable;
} // namespace task

namespace pipeline {

// 前向声明
class PipelineContext;
class TexturePool;
class FramePacketPool;

/**
 * @brief 执行器配置
 */
struct ExecutorConfig {
    std::string gpuQueueLabel = "Pipeline.GPU";
    std::string cpuQueueLabel = "Pipeline.CPU";
    std::string ioQueueLabel = "Pipeline.IO";
    
    uint32_t maxConcurrentFrames = 3;     // 最大并发帧数
    uint32_t cpuThreadCount = 0;           // CPU线程数（0表示自动）
    bool enableParallelExecution = true;   // 是否启用并行执行
    bool enableFrameSkipping = true;       // 是否启用跳帧
    uint32_t maxPendingFrames = 5;         // 最大待处理帧数（超过则跳帧）
};

/**
 * @brief 执行统计信息
 */
struct ExecutionStats {
    uint64_t totalFrames = 0;           // 总处理帧数
    uint64_t droppedFrames = 0;         // 丢弃帧数
    uint64_t averageFrameTime = 0;      // 平均帧处理时间（微秒）
    uint64_t peakFrameTime = 0;         // 峰值帧处理时间
    uint64_t lastFrameTime = 0;         // 最后一帧处理时间
    
    // 各队列统计
    uint64_t gpuQueueTime = 0;
    uint64_t cpuQueueTime = 0;
    uint64_t ioQueueTime = 0;
};

/**
 * @brief 管线执行调度器
 * 
 * 负责根据拓扑顺序调度Entity执行，特点：
 * - 集成TaskQueue进行异步调度
 * - 按执行队列类型分配任务（GPU/CPU/IO）
 * - 支持层次并行（同层Entity并行执行）
 * - 使用Consumable管理依赖链
 * - 支持跳帧和背压控制
 */
class PipelineExecutor {
public:
    /**
     * @brief 构造函数
     * @param graph 管线拓扑图
     * @param config 执行器配置
     */
    explicit PipelineExecutor(PipelineGraph* graph, 
                             const ExecutorConfig& config = ExecutorConfig());
    
    ~PipelineExecutor();
    
    // 禁止拷贝
    PipelineExecutor(const PipelineExecutor&) = delete;
    PipelineExecutor& operator=(const PipelineExecutor&) = delete;
    
    // ==========================================================================
    // 生命周期
    // ==========================================================================
    
    /**
     * @brief 初始化执行器
     * @return 是否成功
     */
    bool initialize();
    
    /**
     * @brief 关闭执行器
     */
    void shutdown();
    
    /**
     * @brief 检查是否已初始化
     */
    bool isInitialized() const { return mInitialized.load(); }
    
    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const { return mRunning.load(); }
    
    // ==========================================================================
    // 资源池
    // ==========================================================================
    
    /**
     * @brief 设置纹理池
     */
    void setTexturePool(std::shared_ptr<TexturePool> pool);
    
    /**
     * @brief 设置帧包池
     */
    void setFramePacketPool(std::shared_ptr<FramePacketPool> pool);
    
    /**
     * @brief 获取管线上下文
     */
    std::shared_ptr<PipelineContext> getContext() const { return mContext; }
    
    // ==========================================================================
    // 执行控制
    // ==========================================================================
    
    /**
     * @brief 处理一帧
     * 
     * 将输入数据包注入管线并执行所有Entity。
     * @param input 输入数据包
     * @return 是否成功提交（可能因背压被拒绝）
     */
    bool processFrame(FramePacketPtr input);
    
    /**
     * @brief 异步处理一帧
     * 
     * 立即返回，处理完成后调用回调。
     * @param input 输入数据包
     * @param callback 完成回调
     * @return 是否成功提交
     */
    bool processFrameAsync(FramePacketPtr input, 
                          std::function<void(FramePacketPtr)> callback);
    
    /**
     * @brief 等待所有帧处理完成
     * @param timeoutMs 超时时间（毫秒），-1表示无限等待
     * @return 是否成功等待
     */
    bool flush(int64_t timeoutMs = -1);
    
    /**
     * @brief 取消所有待处理帧
     */
    void cancelAll();
    
    // ==========================================================================
    // 状态查询
    // ==========================================================================
    
    /**
     * @brief 获取待处理帧数
     */
    uint32_t getPendingFrameCount() const { return mPendingFrames.load(); }
    
    /**
     * @brief 获取执行统计
     */
    ExecutionStats getStats() const;
    
    /**
     * @brief 重置统计数据
     */
    void resetStats();
    
    // ==========================================================================
    // 配置
    // ==========================================================================
    
    /**
     * @brief 获取配置
     */
    const ExecutorConfig& getConfig() const { return mConfig; }
    
    /**
     * @brief 设置是否启用并行执行
     */
    void setParallelExecutionEnabled(bool enabled);
    
    /**
     * @brief 设置是否启用跳帧
     */
    void setFrameSkippingEnabled(bool enabled);
    
    /**
     * @brief 设置回调
     */
    void setFrameCompleteCallback(std::function<void(FramePacketPtr)> callback);
    void setFrameDroppedCallback(std::function<void(FramePacketPtr)> callback);
    void setErrorCallback(std::function<void(EntityId, const std::string&)> callback);
    
private:
    // 配置
    ExecutorConfig mConfig;
    PipelineGraph* mGraph;
    
    // 状态
    std::atomic<bool> mInitialized{false};
    std::atomic<bool> mRunning{false};
    std::atomic<uint32_t> mPendingFrames{0};
    
    // 任务队列
    std::shared_ptr<task::TaskQueue> mGPUQueue;
    std::shared_ptr<task::TaskQueue> mCPUQueue;
    std::shared_ptr<task::TaskQueue> mIOQueue;
    
    // 资源
    std::shared_ptr<PipelineContext> mContext;
    std::shared_ptr<TexturePool> mTexturePool;
    std::shared_ptr<FramePacketPool> mFramePacketPool;
    
    // 统计
    mutable std::mutex mStatsMutex;
    ExecutionStats mStats;
    
    // 回调
    std::function<void(FramePacketPtr)> mFrameCompleteCallback;
    std::function<void(FramePacketPtr)> mFrameDroppedCallback;
    std::function<void(EntityId, const std::string&)> mErrorCallback;
    
    // 图版本（用于检测变化）
    uint64_t mLastGraphVersion = 0;
    std::vector<std::vector<EntityId>> mExecutionLevels;
    
    // ==========================================================================
    // 内部方法
    // ==========================================================================
    
    /**
     * @brief 创建任务队列
     */
    bool createTaskQueues();
    
    /**
     * @brief 更新执行计划
     */
    void updateExecutionPlan();
    
    /**
     * @brief 执行单个Entity
     */
    void executeEntity(EntityId entityId, FramePacketPtr frameContext);
    
    /**
     * @brief 执行一个层级
     */
    void executeLevel(const std::vector<EntityId>& level, 
                     FramePacketPtr frameContext,
                     std::shared_ptr<task::TaskGroup> group);
    
    /**
     * @brief 获取Entity对应的任务队列
     */
    std::shared_ptr<task::TaskQueue> getQueueForEntity(EntityId entityId) const;
    
    /**
     * @brief 处理Entity执行完成
     */
    void onEntityComplete(EntityId entityId, FramePacketPtr frameContext);
    
    /**
     * @brief 处理Entity执行错误
     */
    void onEntityError(EntityId entityId, const std::string& error);
    
    /**
     * @brief 处理帧完成
     */
    void onFrameComplete(FramePacketPtr frameContext);
    
    /**
     * @brief 更新统计信息
     */
    void updateStats(uint64_t frameTime);
    
    /**
     * @brief 检查是否应该跳帧
     */
    bool shouldSkipFrame() const;
};

} // namespace pipeline
