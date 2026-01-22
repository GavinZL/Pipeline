/**
 * @file FramePacket.h
 * @brief 帧数据包 - 在管线中流转的数据载体
 */

#pragma once

#include "EntityTypes.h"
#include <atomic>
#include <any>
#include <unordered_map>
#include <chrono>
#include <mutex>

// 前向声明LREngine类型
namespace lrengine {
namespace render {
class LRTexture;
class LRFence;
} // namespace render
} // namespace lrengine

namespace pipeline {

/**
 * @brief 帧数据包
 * 
 * 封装GPU纹理、CPU缓冲和元数据，在管线Entity之间流转。
 * 支持引用计数和自动回收到池。
 */
class FramePacket : public std::enable_shared_from_this<FramePacket> {
public:
    /**
     * @brief 构造函数
     * @param frameId 帧ID
     */
    explicit FramePacket(uint64_t frameId = 0);
    
    /**
     * @brief 析构函数
     */
    ~FramePacket();
    
    // 禁止拷贝
    FramePacket(const FramePacket&) = delete;
    FramePacket& operator=(const FramePacket&) = delete;
    
    // ==========================================================================
    // 标识信息
    // ==========================================================================
    
    /**
     * @brief 获取帧ID
     */
    uint64_t getFrameId() const { return mFrameId; }
    
    /**
     * @brief 设置帧ID
     */
    void setFrameId(uint64_t id) { mFrameId = id; }
    
    /**
     * @brief 获取时间戳（毫秒）
     */
    uint64_t getTimestamp() const { return mTimestamp; }
    
    /**
     * @brief 设置时间戳
     */
    void setTimestamp(uint64_t ts) { mTimestamp = ts; }
    
    /**
     * @brief 获取序列号
     */
    uint64_t getSequenceNumber() const { return mSequenceNumber; }
    
    /**
     * @brief 设置序列号
     */
    void setSequenceNumber(uint64_t seq) { mSequenceNumber = seq; }
    
    // ==========================================================================
    // 图像数据
    // ==========================================================================
    
    /**
     * @brief 获取GPU纹理
     */
    std::shared_ptr<lrengine::render::LRTexture> getTexture() const { return mTexture; }
    
    /**
     * @brief 设置GPU纹理
     */
    void setTexture(std::shared_ptr<lrengine::render::LRTexture> texture);
    
    /**
     * @brief 获取CPU缓冲数据（懒加载）
     * 
     * 如果CPU缓冲为空且存在GPU纹理，将从GPU读取数据。
     * @return CPU缓冲指针，可能为nullptr
     */
    const uint8_t* getCpuBuffer();
    
    /**
     * @brief 获取CPU缓冲数据（不触发加载）
     */
    const uint8_t* getCpuBufferNoLoad() const { 
        return mCpuBuffer.get(); 
    }
    
    /**
     * @brief 设置CPU缓冲数据
     * @param data 数据指针
     * @param size 数据大小
     * @param takeOwnership 是否接管数据所有权
     */
    void setCpuBuffer(const uint8_t* data, size_t size, bool takeOwnership = true);
    
    /**
     * @brief 清除CPU缓冲
     */
    void clearCpuBuffer();
    
    /**
     * @brief 获取图像宽度
     */
    uint32_t getWidth() const { return mWidth; }
    
    /**
     * @brief 获取图像高度
     */
    uint32_t getHeight() const { return mHeight; }
    
    /**
     * @brief 设置图像尺寸
     */
    void setSize(uint32_t width, uint32_t height);
    
    /**
     * @brief 获取像素格式
     */
    PixelFormat getFormat() const { return mFormat; }
    
    /**
     * @brief 设置像素格式
     */
    void setFormat(PixelFormat format) { mFormat = format; }
    
    /**
     * @brief 获取行步长（字节）
     */
    uint32_t getStride() const { return mStride; }
    
    /**
     * @brief 设置行步长
     */
    void setStride(uint32_t stride) { mStride = stride; }
    
    // ==========================================================================
    // 元数据
    // ==========================================================================
    
    /**
     * @brief 设置元数据
     * @param key 键
     * @param value 值
     */
    template<typename T>
    void setMetadata(const std::string& key, T&& value) {
        std::lock_guard<std::mutex> lock(mMetadataMutex);
        mMetadata[key] = std::forward<T>(value);
    }
    
    /**
     * @brief 获取元数据
     * @param key 键
     * @return 值，如果不存在返回空optional
     */
    template<typename T>
    std::optional<T> getMetadata(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mMetadataMutex);
        auto it = mMetadata.find(key);
        if (it != mMetadata.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (const std::bad_any_cast&) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }
    
    /**
     * @brief 检查是否存在元数据
     */
    bool hasMetadata(const std::string& key) const;
    
    /**
     * @brief 移除元数据
     */
    void removeMetadata(const std::string& key);
    
    /**
     * @brief 清除所有元数据
     */
    void clearMetadata();
    
    // ==========================================================================
    // GPU同步
    // ==========================================================================
    
    /**
     * @brief 获取GPU栅栏
     */
    std::shared_ptr<lrengine::render::LRFence> getGpuFence() const { return mGpuFence; }
    
    /**
     * @brief 设置GPU栅栏
     */
    void setGpuFence(std::shared_ptr<lrengine::render::LRFence> fence);
    
    /**
     * @brief 等待GPU操作完成
     * @param timeoutMs 超时时间（毫秒），-1表示无限等待
     * @return 是否成功等待
     */
    bool waitGpu(int64_t timeoutMs = -1);
    
    /**
     * @brief 标记GPU操作完成
     */
    void signalGpu();
    
    // ==========================================================================
    // 引用计数
    // ==========================================================================
    
    /**
     * @brief 增加引用计数
     */
    void retain();
    
    /**
     * @brief 减少引用计数
     * @return 减少后的引用计数
     */
    int32_t release();
    
    /**
     * @brief 获取当前引用计数
     */
    int32_t getRefCount() const { return mRefCount.load(); }
    
    // ==========================================================================
    // 生命周期
    // ==========================================================================
    
    /**
     * @brief 重置帧数据（用于池复用）
     */
    void reset();
    
    /**
     * @brief 克隆帧数据（浅拷贝纹理）
     */
    FramePacketPtr clone() const;
    
    /**
     * @brief 设置所属池
     */
    void setPool(FramePacketPool* pool) { mPool = pool; }
    
    /**
     * @brief 获取所属池
     */
    FramePacketPool* getPool() const { return mPool; }
    
private:
    // 标识信息
    uint64_t mFrameId = 0;
    uint64_t mTimestamp = 0;
    uint64_t mSequenceNumber = 0;
    
    // 图像数据
    std::shared_ptr<lrengine::render::LRTexture> mTexture;
    std::shared_ptr<uint8_t> mCpuBuffer;  // 使用 uint8_t 而非 uint8_t[] 以简化操作
    size_t mCpuBufferSize = 0;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    uint32_t mStride = 0;
    PixelFormat mFormat = PixelFormat::Unknown;
    
    // 元数据
    mutable std::mutex mMetadataMutex;
    std::unordered_map<std::string, std::any> mMetadata;
    
    // GPU同步
    std::shared_ptr<lrengine::render::LRFence> mGpuFence;
    
    // 引用计数和池
    std::atomic<int32_t> mRefCount{1};
    FramePacketPool* mPool = nullptr;
    
    // 内部方法
    void loadCpuBufferFromTexture();
};

} // namespace pipeline
