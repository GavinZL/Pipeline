/**
 * @file MergeEntity.h
 * @brief 合并实体 - 合并双路处理结果
 * 
 * MergeEntity 接收 GPU 和 CPU 两路处理结果，
 * 基于时间戳进行同步合并后输出。
 */

#pragma once

#include "pipeline/entity/ProcessEntity.h"
#include "pipeline/input/FrameSynchronizer.h"
#include <memory>

namespace pipeline {

// =============================================================================
// 常量定义
// =============================================================================

/// GPU 输入端口名称
static constexpr const char* MERGE_GPU_INPUT_PORT = "gpu_in";

/// CPU 输入端口名称
static constexpr const char* MERGE_CPU_INPUT_PORT = "cpu_in";

/// 合并后输出端口名称
static constexpr const char* MERGE_OUTPUT_PORT = "merged_out";

// =============================================================================
// 合并策略
// =============================================================================

/**
 * @brief 合并策略
 */
enum class MergeStrategy : uint8_t {
    WaitBoth,       ///< 等待双路都完成
    GPUPriority,    ///< GPU 优先，CPU 可选
    CPUPriority,    ///< CPU 优先，GPU 可选
    Latest          ///< 使用最新到达的数据
};

/**
 * @brief 合并配置
 */
struct MergeConfig {
    MergeStrategy strategy = MergeStrategy::WaitBoth;
    int64_t maxWaitTimeMs = 33;         ///< 最大等待时间（毫秒）
    int64_t timestampToleranceUs = 1000; ///< 时间戳容差（微秒）
    bool copyGPUData = true;            ///< 是否拷贝 GPU 数据
    bool copyCPUData = true;            ///< 是否拷贝 CPU 数据
};

// =============================================================================
// 合并结果
// =============================================================================

/**
 * @brief 合并后的帧数据
 */
struct MergedFrame {
    FramePacketPtr gpuResult;   ///< GPU 处理结果
    FramePacketPtr cpuResult;   ///< CPU 处理结果
    int64_t timestamp = 0;      ///< 时间戳
    bool hasGPU = false;        ///< 是否有 GPU 数据
    bool hasCPU = false;        ///< 是否有 CPU 数据
};

using MergedFramePtr = std::shared_ptr<MergedFrame>;

// =============================================================================
// MergeEntity
// =============================================================================

/**
 * @brief 合并实体类
 * 
 * 用于合并 GPU 和 CPU 两路处理结果：
 * 
 * @code
 * auto mergeEntity = std::make_shared<MergeEntity>("merge");
 * mergeEntity->configure(MergeConfig{
 *     .strategy = MergeStrategy::WaitBoth,
 *     .maxWaitTimeMs = 33
 * });
 * 
 * // 连接到管线
 * pipeline->connect(gpuEntity, "output", mergeEntity, MERGE_GPU_INPUT_PORT);
 * pipeline->connect(cpuEntity, "output", mergeEntity, MERGE_CPU_INPUT_PORT);
 * pipeline->connect(mergeEntity, MERGE_OUTPUT_PORT, outputEntity, "input");
 * @endcode
 */
class MergeEntity : public ProcessEntity {
public:
    /**
     * @brief 构造函数
     * @param name Entity 名称
     */
    explicit MergeEntity(const std::string& name = "MergeEntity");
    
    ~MergeEntity() override;
    
    // ==========================================================================
    // ProcessEntity 接口实现
    // ==========================================================================
    
    EntityType getType() const override { return EntityType::Composite; }
    
    ExecutionQueue getExecutionQueue() const override { return ExecutionQueue::GPU; }
    
    // ==========================================================================
    // 配置
    // ==========================================================================
    
    /**
     * @brief 配置合并参数
     * @param config 合并配置
     */
    void configure(const MergeConfig& config);
    
    /**
     * @brief 获取当前配置
     */
    const MergeConfig& getMergeConfig() const { return mConfig; }
    
    /**
     * @brief 设置合并回调
     * @param callback 合并完成回调
     */
    using MergeCallback = std::function<void(MergedFramePtr)>;
    void setMergeCallback(MergeCallback callback);
    
    // ==========================================================================
    // 状态查询
    // ==========================================================================
    
    /**
     * @brief 获取已合并帧数
     */
    uint64_t getMergedFrameCount() const { return mMergedFrameCount; }
        
    /**
     * @brief 获取GPU帧数
     */
    uint64_t getGPUFrameCount() const { return mGPUFrameCount; }
        
    /**
     * @brief 获取CPU帧数
     */
    uint64_t getCPUFrameCount() const { return mCPUFrameCount; }
        
    /**
     * @brief 获取丢弃帧数
     */
    uint64_t getDroppedFrameCount() const { return mDroppedFrameCount; }
        
    // ==========================================================================
    // 异步任务链接口 (新增)
    // ==========================================================================
        
    /**
     * @brief 设置PipelineExecutor引用
     */
    void setExecutor(class PipelineExecutor* executor) { mExecutor = executor; }
        
    /**
     * @brief 获取Synchronizer
     * 
     * 用于上游Entity直接推送数据到同步器。
     */
    input::FrameSynchronizer* getSynchronizer() { return mSynchronizer.get(); }
    
protected:
    // ==========================================================================
    // ProcessEntity 生命周期
    // ==========================================================================
    
    bool prepare(PipelineContext& context) override;
    
    bool process(const std::vector<FramePacketPtr>& inputs,
                std::vector<FramePacketPtr>& outputs,
                PipelineContext& context) override;
    
    void finalize(PipelineContext& context) override;
    
    void resetForNextFrame() override;
    
private:
    // 初始化端口
    void initializePorts();
    
    // 初始化同步器
    void initializeSynchronizer();
    
    // 处理输入
    void processGPUInput(FramePacketPtr packet);
    void processCPUInput(FramePacketPtr packet);
    
    // 创建合并后的输出包
    FramePacketPtr createMergedPacket(const MergedFrame& frame);
    
private:
    // 配置
    MergeConfig mConfig;
    
    // 帧同步器
    input::FrameSynchronizerPtr mSynchronizer;
    
    // 回调
    MergeCallback mMergeCallback;
    
    // 统计
    uint64_t mMergedFrameCount = 0;
    uint64_t mGPUFrameCount = 0;
    uint64_t mCPUFrameCount = 0;
    uint64_t mDroppedFrameCount = 0;
    
    // 线程安全
    mutable std::mutex mMergeMutex;
    
    // ==========================================================================
    // 异步任务链数据 (新增)
    // ==========================================================================
    
    // PipelineExecutor 引用 (用于投递下游任务)
    class PipelineExecutor* mExecutor = nullptr;
};

using MergeEntityPtr = std::shared_ptr<MergeEntity>;

} // namespace pipeline
