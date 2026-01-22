/**
 * @file TexturePool.h
 * @brief 纹理池 - 管理纹理资源的复用
 */

#pragma once

#include "pipeline/data/EntityTypes.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <chrono>

// 前向声明
namespace lrengine {
namespace render {
class LRRenderContext;
class LRTexture;
} // namespace render
} // namespace lrengine

namespace pipeline {

/**
 * @brief 纹理规格
 */
struct TextureSpec {
    uint32_t width = 0;
    uint32_t height = 0;
    PixelFormat format = PixelFormat::RGBA8;
    
    bool operator==(const TextureSpec& other) const {
        return width == other.width && 
               height == other.height && 
               format == other.format;
    }
};

/**
 * @brief TextureSpec的哈希函数
 */
struct TextureSpecHash {
    size_t operator()(const TextureSpec& spec) const {
        size_t h1 = std::hash<uint32_t>{}(spec.width);
        size_t h2 = std::hash<uint32_t>{}(spec.height);
        size_t h3 = std::hash<uint8_t>{}(static_cast<uint8_t>(spec.format));
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

/**
 * @brief 纹理池配置
 */
struct TexturePoolConfig {
    uint32_t maxTexturesPerBucket = 4;    // 每个桶的最大纹理数
    uint32_t maxTotalTextures = 32;       // 最大总纹理数
    uint32_t idleTimeoutMs = 5000;        // 空闲超时（毫秒）
    bool enableLRU = true;                 // 启用LRU淘汰
};

/**
 * @brief 纹理池
 * 
 * 管理GPU纹理资源的复用，特点：
 * - 按尺寸和格式分桶管理
 * - LRU淘汰策略
 * - 支持预热（预分配常用尺寸）
 * - 自动释放长时间未使用的纹理
 */
class TexturePool {
public:
    /**
     * @brief 构造函数
     * @param renderContext 渲染上下文
     * @param config 配置
     */
    explicit TexturePool(lrengine::render::LRRenderContext* renderContext,
                        const TexturePoolConfig& config = TexturePoolConfig());
    
    ~TexturePool();
    
    // 禁止拷贝
    TexturePool(const TexturePool&) = delete;
    TexturePool& operator=(const TexturePool&) = delete;
    
    // ==========================================================================
    // 纹理获取和释放
    // ==========================================================================
    
    /**
     * @brief 获取纹理
     * 
     * 从池中获取指定规格的纹理，如果池中没有则创建新纹理。
     * @param width 宽度
     * @param height 高度
     * @param format 像素格式
     * @return 纹理智能指针
     */
    std::shared_ptr<lrengine::render::LRTexture> acquire(
        uint32_t width, 
        uint32_t height, 
        PixelFormat format = PixelFormat::RGBA8);
    
    /**
     * @brief 获取纹理（使用TextureSpec）
     */
    std::shared_ptr<lrengine::render::LRTexture> acquire(const TextureSpec& spec);
    
    /**
     * @brief 释放纹理回池
     * 
     * 将纹理归还到池中以供复用。
     * @param texture 纹理智能指针
     */
    void release(std::shared_ptr<lrengine::render::LRTexture> texture);
    
    /**
     * @brief 创建自动释放的纹理
     * 
     * 返回的纹理在智能指针销毁时自动归还到池中。
     * @return 带自定义删除器的纹理智能指针
     */
    std::shared_ptr<lrengine::render::LRTexture> acquireAutoRelease(
        uint32_t width, 
        uint32_t height, 
        PixelFormat format = PixelFormat::RGBA8);
    
    // ==========================================================================
    // 池管理
    // ==========================================================================
    
    /**
     * @brief 预热池
     * 
     * 预分配指定规格的纹理。
     * @param specs 纹理规格列表
     */
    void warmup(const std::vector<TextureSpec>& specs);
    
    /**
     * @brief 预热指定数量的纹理
     */
    void warmup(uint32_t width, uint32_t height, 
               PixelFormat format, uint32_t count);
    
    /**
     * @brief 清理空闲纹理
     * 
     * 释放超过空闲超时时间未使用的纹理。
     */
    void cleanup();
    
    /**
     * @brief 清空池
     * 
     * 释放所有纹理（包括正在使用的）。
     */
    void clear();
    
    /**
     * @brief 收缩池
     * 
     * 释放超出限制的纹理。
     */
    void shrink();
    
    // ==========================================================================
    // 状态查询
    // ==========================================================================
    
    /**
     * @brief 获取可用纹理数量
     */
    size_t getAvailableCount() const;
    
    /**
     * @brief 获取指定规格的可用纹理数量
     */
    size_t getAvailableCount(const TextureSpec& spec) const;
    
    /**
     * @brief 获取正在使用的纹理数量
     */
    size_t getInUseCount() const;
    
    /**
     * @brief 获取总纹理数量
     */
    size_t getTotalCount() const;
    
    /**
     * @brief 获取池的内存使用量（字节）
     */
    size_t getMemoryUsage() const;
    
    /**
     * @brief 获取命中率
     */
    float getHitRate() const;
    
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
    const TexturePoolConfig& getConfig() const { return mConfig; }
    
    /**
     * @brief 设置配置
     */
    void setConfig(const TexturePoolConfig& config);
    
private:
    /**
     * @brief 纹理项
     */
    struct TextureEntry {
        std::shared_ptr<lrengine::render::LRTexture> texture;
        TextureSpec spec;
        std::chrono::steady_clock::time_point lastUsed;
        bool inUse = false;
    };
    
    /**
     * @brief 桶
     */
    struct Bucket {
        std::vector<TextureEntry> entries;
    };
    
    // 渲染上下文
    lrengine::render::LRRenderContext* mRenderContext;
    
    // 配置
    TexturePoolConfig mConfig;
    
    // 纹理存储（按规格分桶）
    mutable std::mutex mMutex;
    std::unordered_map<TextureSpec, Bucket, TextureSpecHash> mBuckets;
    
    // 统计
    std::atomic<uint64_t> mHitCount{0};
    std::atomic<uint64_t> mMissCount{0};
    std::atomic<uint64_t> mTotalAllocated{0};
    std::atomic<uint64_t> mTotalReleased{0};
    
    // ==========================================================================
    // 内部方法
    // ==========================================================================
    
    /**
     * @brief 创建新纹理
     */
    std::shared_ptr<lrengine::render::LRTexture> createTexture(const TextureSpec& spec);
    
    /**
     * @brief 从桶中获取纹理
     */
    std::shared_ptr<lrengine::render::LRTexture> acquireFromBucket(Bucket& bucket);
    
    /**
     * @brief 计算纹理内存大小
     */
    size_t calculateTextureSize(const TextureSpec& spec) const;
    
    /**
     * @brief 转换像素格式
     */
    uint32_t convertPixelFormat(PixelFormat format) const;
};

} // namespace pipeline
