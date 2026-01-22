/**
 * @file FramePacketPool.h
 * @brief 帧包池 - 管理FramePacket的复用
 */

#pragma once

#include "pipeline/data/FramePacket.h"
#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace pipeline {

/**
 * @brief 帧包池配置
 */
struct FramePacketPoolConfig {
    uint32_t capacity = 5;           // 池容量
    bool blockOnEmpty = true;        // 池空时是否阻塞
    uint32_t blockTimeoutMs = 100;   // 阻塞超时（毫秒）
    bool enableBackpressure = true;  // 启用背压机制
};

/**
 * @brief 帧包池
 * 
 * 管理FramePacket的复用，特点：
 * - 固定容量（根据管线深度配置）
 * - 支持背压机制（池空时阻塞生产者）
 * - 快速重置（清空metadata但保留纹理引用）
 * - 引用计数自动回收
 */
class FramePacketPool : public std::enable_shared_from_this<FramePacketPool> {
public:
    /**
     * @brief 构造函数
     * @param config 配置
     */
    explicit FramePacketPool(const FramePacketPoolConfig& config = FramePacketPoolConfig());
    
    ~FramePacketPool();
    
    // 禁止拷贝
    FramePacketPool(const FramePacketPool&) = delete;
    FramePacketPool& operator=(const FramePacketPool&) = delete;
    
    // ==========================================================================
    // 帧包获取和释放
    // ==========================================================================
    
    /**
     * @brief 获取帧包
     * 
     * 从池中获取一个空闲的FramePacket。
     * 如果池空且启用了阻塞，则等待直到有可用的帧包或超时。
     * @return FramePacket智能指针，如果超时返回nullptr
     */
    FramePacketPtr acquire();
    
    /**
     * @brief 尝试获取帧包（非阻塞）
     * 
     * @return FramePacket智能指针，如果池空返回nullptr
     */
    FramePacketPtr tryAcquire();
    
    /**
     * @brief 释放帧包回池
     * 
     * @param packet 帧包智能指针
     */
    void release(FramePacketPtr packet);
    
    /**
     * @brief 创建自动释放的帧包
     * 
     * 返回的帧包在引用计数为0时自动归还到池中。
     * @return FramePacket智能指针
     */
    FramePacketPtr acquireAutoRelease();
    
    // ==========================================================================
    // 池管理
    // ==========================================================================
    
    /**
     * @brief 预分配帧包
     * 
     * 预先创建指定数量的FramePacket。
     * @param count 数量（不超过容量）
     */
    void preallocate(uint32_t count = 0);
    
    /**
     * @brief 清空池
     * 
     * 释放所有帧包。正在使用的帧包会在释放时被丢弃。
     */
    void clear();
    
    /**
     * @brief 等待所有帧包归还
     * @param timeoutMs 超时时间（毫秒），-1表示无限等待
     * @return 是否所有帧包都已归还
     */
    bool waitAllReleased(int64_t timeoutMs = -1);
    
    // ==========================================================================
    // 状态查询
    // ==========================================================================
    
    /**
     * @brief 获取可用帧包数量
     */
    size_t getAvailableCount() const;
    
    /**
     * @brief 获取正在使用的帧包数量
     */
    size_t getInUseCount() const;
    
    /**
     * @brief 获取池容量
     */
    size_t getCapacity() const { return mConfig.capacity; }
    
    /**
     * @brief 检查池是否为空
     */
    bool isEmpty() const;
    
    /**
     * @brief 检查池是否已满
     */
    bool isFull() const;
    
    // ==========================================================================
    // 配置
    // ==========================================================================
    
    /**
     * @brief 获取配置
     */
    const FramePacketPoolConfig& getConfig() const { return mConfig; }
    
    /**
     * @brief 设置容量
     * 
     * 注意：如果新容量小于当前已创建的帧包数量，多余的帧包会在释放时被丢弃。
     */
    void setCapacity(uint32_t capacity);
    
    /**
     * @brief 设置是否启用背压
     */
    void setBackpressureEnabled(bool enabled);
    
    // ==========================================================================
    // 统计
    // ==========================================================================
    
    /**
     * @brief 获取总分配次数
     */
    uint64_t getTotalAllocations() const { return mTotalAllocations.load(); }
    
    /**
     * @brief 获取总释放次数
     */
    uint64_t getTotalReleases() const { return mTotalReleases.load(); }
    
    /**
     * @brief 获取阻塞等待次数
     */
    uint64_t getBlockCount() const { return mBlockCount.load(); }
    
    /**
     * @brief 获取超时次数
     */
    uint64_t getTimeoutCount() const { return mTimeoutCount.load(); }
    
    /**
     * @brief 重置统计
     */
    void resetStats();
    
private:
    // 配置
    FramePacketPoolConfig mConfig;
    
    // 帧包存储
    mutable std::mutex mMutex;
    std::condition_variable mCondition;
    std::queue<FramePacketPtr> mAvailable;
    std::atomic<uint32_t> mInUseCount{0};
    std::atomic<uint32_t> mTotalCreated{0};
    
    // 状态
    std::atomic<bool> mShutdown{false};
    std::atomic<uint64_t> mNextFrameId{1};
    
    // 统计
    std::atomic<uint64_t> mTotalAllocations{0};
    std::atomic<uint64_t> mTotalReleases{0};
    std::atomic<uint64_t> mBlockCount{0};
    std::atomic<uint64_t> mTimeoutCount{0};
    
    // ==========================================================================
    // 内部方法
    // ==========================================================================
    
    /**
     * @brief 创建新帧包
     */
    FramePacketPtr createPacket();
    
    /**
     * @brief 重置帧包状态
     */
    void resetPacket(FramePacketPtr packet);
    
    /**
     * @brief 自动释放删除器
     */
    static void autoReleaseDeleter(FramePacket* packet, 
                                   std::weak_ptr<FramePacketPool> weakPool);
};

/**
 * @brief 缓冲池
 * 
 * 管理CPU缓冲区的复用。
 */
class BufferPool {
public:
    /**
     * @brief 构造函数
     * @param maxBuffers 最大缓冲区数量
     */
    explicit BufferPool(size_t maxBuffers = 16);
    
    ~BufferPool();
    
    /**
     * @brief 获取缓冲区
     * @param size 所需大小
     * @return 缓冲区智能指针
     */
    std::shared_ptr<uint8_t> acquire(size_t size);
    
    /**
     * @brief 释放缓冲区
     */
    void release(std::shared_ptr<uint8_t> buffer, size_t size);
    
    /**
     * @brief 清空池
     */
    void clear();
    
    /**
     * @brief 获取内存使用量
     */
    size_t getMemoryUsage() const;
    
private:
    struct BufferEntry {
        std::shared_ptr<uint8_t> buffer;
        size_t size;
        std::chrono::steady_clock::time_point lastUsed;
    };
    
    mutable std::mutex mMutex;
    std::vector<BufferEntry> mBuffers;
    size_t mMaxBuffers;
    std::atomic<size_t> mTotalMemory{0};
};

} // namespace pipeline
