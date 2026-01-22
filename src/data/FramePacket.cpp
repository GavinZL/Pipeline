/**
 * @file FramePacket.cpp
 * @brief FramePacket实现
 */

#include "pipeline/data/FramePacket.h"
#include "pipeline/pool/FramePacketPool.h"

namespace pipeline {

FramePacket::FramePacket(uint64_t frameId)
    : mFrameId(frameId)
    , mTimestamp(0)
    , mSequenceNumber(0)
{
}

FramePacket::~FramePacket() = default;

// =============================================================================
// 图像数据
// =============================================================================

void FramePacket::setTexture(std::shared_ptr<lrengine::render::LRTexture> texture) {
    mTexture = std::move(texture);
    // 清除CPU缓冲，因为纹理已更新
    mCpuBuffer.reset();
    mCpuBufferSize = 0;
}

const uint8_t* FramePacket::getCpuBuffer() {
    if (!mCpuBuffer && mTexture) {
        loadCpuBufferFromTexture();
    }
    return mCpuBuffer.get();
}

void FramePacket::setCpuBuffer(const uint8_t* data, size_t size, bool takeOwnership) {
    if (takeOwnership && data) {
        // 接管所有权：使用自定义删除器确保使用 delete[]
        mCpuBuffer = std::shared_ptr<uint8_t>(
            const_cast<uint8_t*>(data), 
            [](uint8_t* p) { delete[] p; }
        );
        mCpuBufferSize = size;
    } else if (data && size > 0) {
        // 复制数据
        uint8_t* buffer = new uint8_t[size];
        std::memcpy(buffer, data, size);
        mCpuBuffer = std::shared_ptr<uint8_t>(buffer, [](uint8_t* p) { delete[] p; });
        mCpuBufferSize = size;
    }
}

void FramePacket::clearCpuBuffer() {
    mCpuBuffer.reset();
    mCpuBufferSize = 0;
}

void FramePacket::setSize(uint32_t width, uint32_t height) {
    mWidth = width;
    mHeight = height;
    if (mStride == 0) {
        mStride = width * getPixelFormatBytesPerPixel(mFormat);
    }
}

// =============================================================================
// 元数据
// =============================================================================

bool FramePacket::hasMetadata(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mMetadataMutex);
    return mMetadata.find(key) != mMetadata.end();
}

void FramePacket::removeMetadata(const std::string& key) {
    std::lock_guard<std::mutex> lock(mMetadataMutex);
    mMetadata.erase(key);
}

void FramePacket::clearMetadata() {
    std::lock_guard<std::mutex> lock(mMetadataMutex);
    mMetadata.clear();
}

// =============================================================================
// GPU同步
// =============================================================================

void FramePacket::setGpuFence(std::shared_ptr<lrengine::render::LRFence> fence) {
    mGpuFence = std::move(fence);
}

bool FramePacket::waitGpu(int64_t timeoutMs) {
    if (!mGpuFence) {
        return true;
    }
    
    // 这里需要调用LRFence的等待方法
    // 具体实现取决于LREngine的Fence接口
    // mGpuFence->Wait(timeoutMs);
    return true;
}

void FramePacket::signalGpu() {
    if (mGpuFence) {
        // mGpuFence->Signal();
    }
}

// =============================================================================
// 引用计数
// =============================================================================

void FramePacket::retain() {
    mRefCount.fetch_add(1, std::memory_order_relaxed);
}

int32_t FramePacket::release() {
    int32_t count = mRefCount.fetch_sub(1, std::memory_order_acq_rel);
    if (count == 1) {
        // 引用计数降为0，归还到池
        if (mPool) {
            reset();
            // 注意：这里需要通过shared_from_this获取智能指针
            // 但由于我们在析构路径上，这可能有问题
            // 实际使用时应该通过FramePacketPool的自动释放机制处理
        }
    }
    return count - 1;
}

// =============================================================================
// 生命周期
// =============================================================================

void FramePacket::reset() {
    mFrameId = 0;
    mTimestamp = 0;
    mSequenceNumber = 0;
    
    // 保留纹理引用但清除CPU缓冲
    mCpuBuffer.reset();
    mCpuBufferSize = 0;
    
    // 清除元数据
    clearMetadata();
    
    // 清除同步对象
    mGpuFence.reset();
    
    // 重置引用计数
    mRefCount.store(1, std::memory_order_relaxed);
}

FramePacketPtr FramePacket::clone() const {
    auto packet = std::make_shared<FramePacket>(mFrameId);
    
    packet->mTimestamp = mTimestamp;
    packet->mSequenceNumber = mSequenceNumber;
    
    // 浅拷贝纹理（共享同一个纹理）
    packet->mTexture = mTexture;
    
    packet->mWidth = mWidth;
    packet->mHeight = mHeight;
    packet->mStride = mStride;
    packet->mFormat = mFormat;
    
    // 复制元数据
    {
        std::lock_guard<std::mutex> lock(mMetadataMutex);
        packet->mMetadata = mMetadata;
    }
    
    // GPU Fence不复制（每个packet有自己的同步）
    
    return packet;
}

// =============================================================================
// 内部方法
// =============================================================================

void FramePacket::loadCpuBufferFromTexture() {
    if (!mTexture || mWidth == 0 || mHeight == 0) {
        return;
    }
    
    // 计算缓冲区大小
    size_t bytesPerPixel = getPixelFormatBytesPerPixel(mFormat);
    if (bytesPerPixel == 0) {
        bytesPerPixel = 4; // 默认RGBA
    }
    
    size_t bufferSize = mWidth * mHeight * bytesPerPixel;
    
    // 分配缓冲区（使用 delete[] 删除器）
    uint8_t* buffer = new uint8_t[bufferSize];
    mCpuBuffer = std::shared_ptr<uint8_t>(buffer, [](uint8_t* p) { delete[] p; });
    mCpuBufferSize = bufferSize;
    
    // 从纹理读取数据
    // 这里需要调用LRTexture的读取方法
    // 具体实现取决于LREngine的纹理接口
    // mTexture->ReadPixels(buffer, bufferSize);
}

} // namespace pipeline
