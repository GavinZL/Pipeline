/**
 * @file FrameSynchronizer.h
 * @brief 帧同步器 - 同步双路处理结果
 * 
 * FrameSynchronizer 负责同步 GPU 路径和 CPU 路径的处理结果，
 * 确保两路数据基于相同的时间戳进行匹配。
 */

#pragma once

#include "pipeline/data/EntityTypes.h"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <functional>
#include <chrono>

namespace pipeline {
namespace input {

/**
 * @brief 同步帧数据
 */
struct SyncedFrame {
    FramePacketPtr gpuFrame;    ///< GPU 路径处理结果
    FramePacketPtr cpuFrame;    ///< CPU 路径处理结果
    int64_t timestamp = 0;      ///< 时间戳
    bool hasGPU = false;        ///< 是否有 GPU 数据
    bool hasCPU = false;        ///< 是否有 CPU 数据
    
    bool isComplete() const { return hasGPU && hasCPU; }
    bool isEmpty() const { return !hasGPU && !hasCPU; }
};

using SyncedFramePtr = std::shared_ptr<SyncedFrame>;
using SyncCallback = std::function<void(SyncedFramePtr)>;

/**
 * @brief 同步策略
 */
enum class SyncPolicy : uint8_t {
    WaitBoth,       ///< 等待双路都完成
    GPUFirst,       ///< GPU 优先，CPU 可选
    CPUFirst,       ///< CPU 优先，GPU 可选
    DropOld         ///< 丢弃旧帧，只保留最新
};

/**
 * @brief 帧同步器配置
 */
struct FrameSyncConfig {
    SyncPolicy policy = SyncPolicy::WaitBoth;
    int64_t maxWaitTimeMs = 33;         ///< 最大等待时间（毫秒）
    int64_t timestampToleranceUs = 1000; ///< 时间戳容差（微秒）
    size_t maxPendingFrames = 3;        ///< 最大待同步帧数
    bool enableGPU = true;              ///< 是否启用 GPU 路径
    bool enableCPU = true;              ///< 是否启用 CPU 路径
};

/**
 * @brief 帧同步器
 * 
 * 基于时间戳同步 GPU 和 CPU 两路数据：
 * 
 * @code
 * FrameSynchronizer sync;
 * sync.configure(FrameSyncConfig{
 *     .policy = SyncPolicy::WaitBoth,
 *     .maxWaitTimeMs = 33
 * });
 * 
 * sync.setCallback([](SyncedFramePtr frame) {
 *     // 处理同步后的帧
 *     if (frame->hasGPU) { ... }
 *     if (frame->hasCPU) { ... }
 * });
 * 
 * // GPU 路径完成时
 * sync.pushGPUFrame(gpuPacket, timestamp);
 * 
 * // CPU 路径完成时
 * sync.pushCPUFrame(cpuPacket, timestamp);
 * @endcode
 */
class FrameSynchronizer {
public:
    FrameSynchronizer();
    ~FrameSynchronizer();
    
    // 禁止拷贝
    FrameSynchronizer(const FrameSynchronizer&) = delete;
    FrameSynchronizer& operator=(const FrameSynchronizer&) = delete;
    
    // ==========================================================================
    // 配置
    // ==========================================================================
    
    /**
     * @brief 配置同步器
     */
    void configure(const FrameSyncConfig& config);
    
    /**
     * @brief 获取当前配置
     */
    const FrameSyncConfig& getConfig() const { return mConfig; }
    
    /**
     * @brief 设置同步完成回调
     */
    void setCallback(SyncCallback callback);
    
    // ==========================================================================
    // 数据输入
    // ==========================================================================
    
    /**
     * @brief 推送 GPU 帧
     * @param frame GPU 处理结果
     * @param timestamp 时间戳（微秒）
     */
    void pushGPUFrame(FramePacketPtr frame, int64_t timestamp);
    
    /**
     * @brief 推送 CPU 帧
     * @param frame CPU 处理结果
     * @param timestamp 时间戳（微秒）
     */
    void pushCPUFrame(FramePacketPtr frame, int64_t timestamp);
    
    // ==========================================================================
    // 同步获取
    // ==========================================================================
    
    /**
     * @brief 尝试获取同步帧（非阻塞）
     * @return 同步帧，可能为空
     */
    SyncedFramePtr tryGetSyncedFrame();
    
    /**
     * @brief 等待获取同步帧（阻塞）
     * @param timeoutMs 超时时间（毫秒），-1 表示无限等待
     * @return 同步帧，超时返回空
     */
    SyncedFramePtr waitSyncedFrame(int64_t timeoutMs = -1);
    
    // ==========================================================================
    // 状态查询
    // ==========================================================================
    
    /**
     * @brief 获取待同步 GPU 帧数量
     */
    size_t getPendingGPUCount() const;
    
    /**
     * @brief 获取待同步 CPU 帧数量
     */
    size_t getPendingCPUCount() const;
    
    /**
     * @brief 获取已同步帧数量
     */
    size_t getSyncedCount() const;
    
    /**
     * @brief 检查是否有可用的同步帧
     */
    bool hasSyncedFrame() const;
    
    // ==========================================================================
    // 控制
    // ==========================================================================
    
    /**
     * @brief 清空所有待同步帧
     */
    void clear();
    
    /**
     * @brief 重置同步器状态
     */
    void reset();
    
    /**
     * @brief 强制同步当前帧（即使不完整）
     */
    void flush();
    
private:
    // 内部帧记录
    struct PendingFrame {
        FramePacketPtr gpuFrame;
        FramePacketPtr cpuFrame;
        int64_t timestamp = 0;
        std::chrono::steady_clock::time_point arrivalTime;
        bool hasGPU = false;
        bool hasCPU = false;
    };
    
    // 查找匹配的时间戳
    int64_t findMatchingTimestamp(int64_t timestamp);
    
    // 尝试完成同步
    void tryComplete(int64_t timestamp);
    
    // 检查超时
    void checkTimeouts();
    
    // 清理旧帧
    void cleanupOldFrames();
    
    // 发出同步帧
    void emitSyncedFrame(const PendingFrame& frame);
    
private:
    FrameSyncConfig mConfig;
    SyncCallback mCallback;
    
    // 待同步帧（按时间戳索引）
    mutable std::mutex mMutex;
    std::unordered_map<int64_t, PendingFrame> mPendingFrames;
    
    // 已完成的同步帧队列
    std::queue<SyncedFramePtr> mSyncedFrames;
    std::condition_variable mSyncedCond;
    
    // 统计
    uint64_t mTotalGPUFrames = 0;
    uint64_t mTotalCPUFrames = 0;
    uint64_t mTotalSyncedFrames = 0;
    uint64_t mDroppedFrames = 0;
};

using FrameSynchronizerPtr = std::shared_ptr<FrameSynchronizer>;

} // namespace input
} // namespace pipeline
