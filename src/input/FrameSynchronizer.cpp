/**
 * @file FrameSynchronizer.cpp
 * @brief FrameSynchronizer 实现
 */

#include "pipeline/input/FrameSynchronizer.h"
#include "pipeline/data/FramePacket.h"

namespace pipeline {
namespace input {

// =============================================================================
// 构造与析构
// =============================================================================

FrameSynchronizer::FrameSynchronizer() = default;

FrameSynchronizer::~FrameSynchronizer() {
    clear();
}

// =============================================================================
// 配置
// =============================================================================

void FrameSynchronizer::configure(const FrameSyncConfig& config) {
    std::lock_guard<std::mutex> lock(mMutex);
    mConfig = config;
}

void FrameSynchronizer::setCallback(SyncCallback callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    mCallback = std::move(callback);
}

// =============================================================================
// 数据输入
// =============================================================================

void FrameSynchronizer::pushGPUFrame(FramePacketPtr frame, int64_t timestamp) {
    if (!frame || !mConfig.enableGPU) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mMutex);
    
    ++mTotalGPUFrames;
    
    // 查找或创建匹配的时间戳记录
    int64_t matchedTs = findMatchingTimestamp(timestamp);
    if (matchedTs == 0) {
        matchedTs = timestamp;
    }
    
    auto& pending = mPendingFrames[matchedTs];
    pending.gpuFrame = frame;
    pending.hasGPU = true;
    pending.timestamp = matchedTs;
    if (pending.arrivalTime.time_since_epoch().count() == 0) {
        pending.arrivalTime = std::chrono::steady_clock::now();
    }
    
    // 清理旧帧
    cleanupOldFrames();
    
    // 尝试完成同步
    tryComplete(matchedTs);
}

void FrameSynchronizer::pushCPUFrame(FramePacketPtr frame, int64_t timestamp) {
    if (!frame || !mConfig.enableCPU) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mMutex);
    
    ++mTotalCPUFrames;
    
    // 查找或创建匹配的时间戳记录
    int64_t matchedTs = findMatchingTimestamp(timestamp);
    if (matchedTs == 0) {
        matchedTs = timestamp;
    }
    
    auto& pending = mPendingFrames[matchedTs];
    pending.cpuFrame = frame;
    pending.hasCPU = true;
    pending.timestamp = matchedTs;
    if (pending.arrivalTime.time_since_epoch().count() == 0) {
        pending.arrivalTime = std::chrono::steady_clock::now();
    }
    
    // 清理旧帧
    cleanupOldFrames();
    
    // 尝试完成同步
    tryComplete(matchedTs);
}

// =============================================================================
// 同步获取
// =============================================================================

SyncedFramePtr FrameSynchronizer::tryGetSyncedFrame() {
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 先检查超时
    checkTimeouts();
    
    if (mSyncedFrames.empty()) {
        return nullptr;
    }
    
    auto frame = mSyncedFrames.front();
    mSyncedFrames.pop();
    return frame;
}

SyncedFramePtr FrameSynchronizer::waitSyncedFrame(int64_t timeoutMs) {
    std::unique_lock<std::mutex> lock(mMutex);
    
    // 先检查是否已有可用帧
    checkTimeouts();
    if (!mSyncedFrames.empty()) {
        auto frame = mSyncedFrames.front();
        mSyncedFrames.pop();
        return frame;
    }
    
    // 等待新帧
    if (timeoutMs < 0) {
        mSyncedCond.wait(lock, [this] { return !mSyncedFrames.empty(); });
    } else {
        if (!mSyncedCond.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                   [this] { return !mSyncedFrames.empty(); })) {
            // 超时，检查是否可以强制输出
            checkTimeouts();
            if (mSyncedFrames.empty()) {
                return nullptr;
            }
        }
    }
    
    if (mSyncedFrames.empty()) {
        return nullptr;
    }
    
    auto frame = mSyncedFrames.front();
    mSyncedFrames.pop();
    return frame;
}

// =============================================================================
// 状态查询
// =============================================================================

size_t FrameSynchronizer::getPendingGPUCount() const {
    std::lock_guard<std::mutex> lock(mMutex);
    size_t count = 0;
    for (const auto& [ts, frame] : mPendingFrames) {
        if (frame.hasGPU && !frame.hasCPU) {
            ++count;
        }
    }
    return count;
}

size_t FrameSynchronizer::getPendingCPUCount() const {
    std::lock_guard<std::mutex> lock(mMutex);
    size_t count = 0;
    for (const auto& [ts, frame] : mPendingFrames) {
        if (frame.hasCPU && !frame.hasGPU) {
            ++count;
        }
    }
    return count;
}

size_t FrameSynchronizer::getSyncedCount() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mSyncedFrames.size();
}

bool FrameSynchronizer::hasSyncedFrame() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return !mSyncedFrames.empty();
}

// =============================================================================
// 控制
// =============================================================================

void FrameSynchronizer::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    mPendingFrames.clear();
    while (!mSyncedFrames.empty()) {
        mSyncedFrames.pop();
    }
}

void FrameSynchronizer::reset() {
    std::lock_guard<std::mutex> lock(mMutex);
    mPendingFrames.clear();
    while (!mSyncedFrames.empty()) {
        mSyncedFrames.pop();
    }
    mTotalGPUFrames = 0;
    mTotalCPUFrames = 0;
    mTotalSyncedFrames = 0;
    mDroppedFrames = 0;
}

void FrameSynchronizer::flush() {
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 强制输出所有待同步帧
    for (auto& [ts, frame] : mPendingFrames) {
        emitSyncedFrame(frame);
    }
    mPendingFrames.clear();
}

// =============================================================================
// 内部方法
// =============================================================================

int64_t FrameSynchronizer::findMatchingTimestamp(int64_t timestamp) {
    // 在容差范围内查找匹配的时间戳
    for (const auto& [ts, frame] : mPendingFrames) {
        int64_t diff = std::abs(ts - timestamp);
        if (diff <= mConfig.timestampToleranceUs) {
            return ts;
        }
    }
    return 0; // 未找到匹配
}

void FrameSynchronizer::tryComplete(int64_t timestamp) {
    auto it = mPendingFrames.find(timestamp);
    if (it == mPendingFrames.end()) {
        return;
    }
    
    const auto& frame = it->second;
    bool canComplete = false;
    
    switch (mConfig.policy) {
        case SyncPolicy::WaitBoth:
            // 需要两路都完成
            if (mConfig.enableGPU && mConfig.enableCPU) {
                canComplete = frame.hasGPU && frame.hasCPU;
            } else if (mConfig.enableGPU) {
                canComplete = frame.hasGPU;
            } else if (mConfig.enableCPU) {
                canComplete = frame.hasCPU;
            }
            break;
            
        case SyncPolicy::GPUFirst:
            // GPU 到达即可完成（CPU 可选）
            canComplete = frame.hasGPU;
            break;
            
        case SyncPolicy::CPUFirst:
            // CPU 到达即可完成（GPU 可选）
            canComplete = frame.hasCPU;
            break;
            
        case SyncPolicy::DropOld:
            // 只保留最新，立即完成
            canComplete = frame.hasGPU || frame.hasCPU;
            break;
    }
    
    if (canComplete) {
        emitSyncedFrame(frame);
        mPendingFrames.erase(it);
    }
}

void FrameSynchronizer::checkTimeouts() {
    auto now = std::chrono::steady_clock::now();
    auto maxWait = std::chrono::milliseconds(mConfig.maxWaitTimeMs);
    
    std::vector<int64_t> timedOutTs;
    
    for (auto& [ts, frame] : mPendingFrames) {
        auto elapsed = now - frame.arrivalTime;
        if (elapsed >= maxWait) {
            timedOutTs.push_back(ts);
        }
    }
    
    // 处理超时帧
    for (int64_t ts : timedOutTs) {
        auto it = mPendingFrames.find(ts);
        if (it != mPendingFrames.end()) {
            // 超时后强制输出（即使不完整）
            emitSyncedFrame(it->second);
            mPendingFrames.erase(it);
        }
    }
}

void FrameSynchronizer::cleanupOldFrames() {
    // 如果待同步帧过多，丢弃最旧的
    while (mPendingFrames.size() > mConfig.maxPendingFrames) {
        // 找到最旧的帧
        int64_t oldestTs = INT64_MAX;
        for (const auto& [ts, frame] : mPendingFrames) {
            if (ts < oldestTs) {
                oldestTs = ts;
            }
        }
        
        if (oldestTs != INT64_MAX) {
            auto it = mPendingFrames.find(oldestTs);
            if (it != mPendingFrames.end()) {
                ++mDroppedFrames;
                mPendingFrames.erase(it);
            }
        }
    }
}

void FrameSynchronizer::emitSyncedFrame(const PendingFrame& frame) {
    auto synced = std::make_shared<SyncedFrame>();
    synced->gpuFrame = frame.gpuFrame;
    synced->cpuFrame = frame.cpuFrame;
    synced->timestamp = frame.timestamp;
    synced->hasGPU = frame.hasGPU;
    synced->hasCPU = frame.hasCPU;
    
    ++mTotalSyncedFrames;
    
    // 添加到队列
    mSyncedFrames.push(synced);
    mSyncedCond.notify_one();
    
    // 触发回调
    if (mCallback) {
        mCallback(synced);
    }
}

} // namespace input
} // namespace pipeline
