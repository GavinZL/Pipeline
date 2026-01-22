/**
 * @file FramePacketPool.cpp
 * @brief FramePacketPool实现
 */

#include "pipeline/pool/FramePacketPool.h"
#include <algorithm>
#include <thread>

namespace pipeline {

// =============================================================================
// FramePacketPool
// =============================================================================

FramePacketPool::FramePacketPool(const FramePacketPoolConfig& config)
    : mConfig(config)
{
}

FramePacketPool::~FramePacketPool() {
    mShutdown.store(true);
    clear();
}

FramePacketPtr FramePacketPool::acquire() {
    std::unique_lock<std::mutex> lock(mMutex);
    
    mTotalAllocations.fetch_add(1);
    
    // 尝试从池中获取
    if (!mAvailable.empty()) {
        FramePacketPtr packet = std::move(mAvailable.front());
        mAvailable.pop();
        mInUseCount.fetch_add(1);
        
        // 重置帧ID
        packet->setFrameId(mNextFrameId.fetch_add(1));
        return packet;
    }
    
    // 池为空，检查是否可以创建新的
    if (mTotalCreated.load() < mConfig.capacity) {
        mInUseCount.fetch_add(1);
        return createPacket();
    }
    
    // 池满，等待或返回空
    if (!mConfig.blockOnEmpty) {
        return nullptr;
    }
    
    mBlockCount.fetch_add(1);
    
    bool success = mCondition.wait_for(lock, 
        std::chrono::milliseconds(mConfig.blockTimeoutMs),
        [this] { return !mAvailable.empty() || mShutdown.load(); });
    
    if (!success || mShutdown.load() || mAvailable.empty()) {
        mTimeoutCount.fetch_add(1);
        return nullptr;
    }
    
    FramePacketPtr packet = std::move(mAvailable.front());
    mAvailable.pop();
    mInUseCount.fetch_add(1);
    
    packet->setFrameId(mNextFrameId.fetch_add(1));
    return packet;
}

FramePacketPtr FramePacketPool::tryAcquire() {
    std::lock_guard<std::mutex> lock(mMutex);
    
    mTotalAllocations.fetch_add(1);
    
    if (!mAvailable.empty()) {
        FramePacketPtr packet = std::move(mAvailable.front());
        mAvailable.pop();
        mInUseCount.fetch_add(1);
        
        packet->setFrameId(mNextFrameId.fetch_add(1));
        return packet;
    }
    
    // 池为空，检查是否可以创建新的
    if (mTotalCreated.load() < mConfig.capacity) {
        mInUseCount.fetch_add(1);
        return createPacket();
    }
    
    return nullptr;
}

void FramePacketPool::release(FramePacketPtr packet) {
    if (!packet) return;
    
    mTotalReleases.fetch_add(1);
    mInUseCount.fetch_sub(1);
    
    // 重置并放回池中
    resetPacket(packet);
    
    {
        std::lock_guard<std::mutex> lock(mMutex);
        
        // 检查是否超出容量
        if (mAvailable.size() < mConfig.capacity) {
            mAvailable.push(std::move(packet));
        }
        // 否则让packet自然销毁
    }
    
    mCondition.notify_one();
}

FramePacketPtr FramePacketPool::acquireAutoRelease() {
    FramePacketPtr packet = acquire();
    if (!packet) {
        return nullptr;
    }
    
    // 创建带自定义删除器的智能指针
    std::weak_ptr<FramePacketPool> weakPool = shared_from_this();
    
    return FramePacketPtr(packet.get(), 
        [weakPool, originalPacket = packet](FramePacket*) mutable {
            if (auto pool = weakPool.lock()) {
                pool->release(originalPacket);
            }
        });
}

void FramePacketPool::preallocate(uint32_t count) {
    if (count == 0) {
        count = mConfig.capacity;
    }
    
    std::lock_guard<std::mutex> lock(mMutex);
    
    while (mTotalCreated.load() < count && mTotalCreated.load() < mConfig.capacity) {
        mAvailable.push(createPacket());
    }
}

void FramePacketPool::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    
    while (!mAvailable.empty()) {
        mAvailable.pop();
    }
    
    mTotalCreated.store(0);
}

bool FramePacketPool::waitAllReleased(int64_t timeoutMs) {
    auto startTime = std::chrono::steady_clock::now();
    
    while (mInUseCount.load() > 0) {
        if (timeoutMs >= 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed >= timeoutMs) {
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    return true;
}

size_t FramePacketPool::getAvailableCount() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mAvailable.size();
}

size_t FramePacketPool::getInUseCount() const {
    return mInUseCount.load();
}

bool FramePacketPool::isEmpty() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mAvailable.empty();
}

bool FramePacketPool::isFull() const {
    return mInUseCount.load() == 0 && getAvailableCount() == mConfig.capacity;
}

void FramePacketPool::setCapacity(uint32_t capacity) {
    std::lock_guard<std::mutex> lock(mMutex);
    mConfig.capacity = capacity;
}

void FramePacketPool::setBackpressureEnabled(bool enabled) {
    mConfig.enableBackpressure = enabled;
}

void FramePacketPool::resetStats() {
    mTotalAllocations.store(0);
    mTotalReleases.store(0);
    mBlockCount.store(0);
    mTimeoutCount.store(0);
}

FramePacketPtr FramePacketPool::createPacket() {
    auto packet = std::make_shared<FramePacket>(mNextFrameId.fetch_add(1));
    packet->setPool(this);
    mTotalCreated.fetch_add(1);
    return packet;
}

void FramePacketPool::resetPacket(FramePacketPtr packet) {
    if (packet) {
        packet->reset();
    }
}

// =============================================================================
// BufferPool
// =============================================================================

BufferPool::BufferPool(size_t maxBuffers)
    : mMaxBuffers(maxBuffers)
{
}

BufferPool::~BufferPool() {
    clear();
}

std::shared_ptr<uint8_t> BufferPool::acquire(size_t size) {
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 查找匹配大小的缓冲区
    for (auto it = mBuffers.begin(); it != mBuffers.end(); ++it) {
        if (it->size >= size) {
            auto buffer = std::move(it->buffer);
            mTotalMemory.fetch_sub(it->size);
            mBuffers.erase(it);
            return buffer;
        }
    }
    
    // 创建新缓冲区（使用 delete[] 删除器）
    uint8_t* rawBuffer = new uint8_t[size];
    return std::shared_ptr<uint8_t>(rawBuffer, [](uint8_t* p) { delete[] p; });
}

void BufferPool::release(std::shared_ptr<uint8_t> buffer, size_t size) {
    if (!buffer || size == 0) return;
    
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 检查是否超出限制
    if (mBuffers.size() >= mMaxBuffers) {
        // 移除最旧的缓冲区
        if (!mBuffers.empty()) {
            mTotalMemory.fetch_sub(mBuffers.front().size);
            mBuffers.erase(mBuffers.begin());
        }
    }
    
    BufferEntry entry;
    entry.buffer = std::move(buffer);
    entry.size = size;
    entry.lastUsed = std::chrono::steady_clock::now();
    
    mBuffers.push_back(std::move(entry));
    mTotalMemory.fetch_add(size);
}

void BufferPool::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    mBuffers.clear();
    mTotalMemory.store(0);
}

size_t BufferPool::getMemoryUsage() const {
    return mTotalMemory.load();
}

} // namespace pipeline
