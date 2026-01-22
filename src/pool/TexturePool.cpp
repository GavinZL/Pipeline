/**
 * @file TexturePool.cpp
 * @brief TexturePool实现 - 纹理资源池
 */

#include "pipeline/pool/TexturePool.h"
#include <algorithm>

// LREngine头文件
// #include "lrengine/core/LRRenderContext.h"
// #include "lrengine/core/LRTexture.h"

namespace pipeline {

TexturePool::TexturePool(lrengine::render::LRRenderContext* renderContext,
                         const TexturePoolConfig& config)
    : mRenderContext(renderContext)
    , mConfig(config)
{
}

TexturePool::~TexturePool() {
    clear();
}

// =============================================================================
// 纹理获取和释放
// =============================================================================

std::shared_ptr<lrengine::render::LRTexture> TexturePool::acquire(
    uint32_t width, uint32_t height, PixelFormat format) {
    
    TextureSpec spec{width, height, format};
    return acquire(spec);
}

std::shared_ptr<lrengine::render::LRTexture> TexturePool::acquire(const TextureSpec& spec) {
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 查找对应的桶
    auto it = mBuckets.find(spec);
    if (it != mBuckets.end()) {
        auto texture = acquireFromBucket(it->second);
        if (texture) {
            mHitCount.fetch_add(1);
            return texture;
        }
    }
    
    // 桶中没有可用纹理，创建新的
    mMissCount.fetch_add(1);
    return createTexture(spec);
}

std::shared_ptr<lrengine::render::LRTexture> TexturePool::acquireFromBucket(Bucket& bucket) {
    for (auto& entry : bucket.entries) {
        if (!entry.inUse) {
            entry.inUse = true;
            entry.lastUsed = std::chrono::steady_clock::now();
            return entry.texture;
        }
    }
    return nullptr;
}

void TexturePool::release(std::shared_ptr<lrengine::render::LRTexture> texture) {
    if (!texture) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 查找纹理所在的桶
    for (auto& [spec, bucket] : mBuckets) {
        for (auto& entry : bucket.entries) {
            if (entry.texture == texture) {
                entry.inUse = false;
                entry.lastUsed = std::chrono::steady_clock::now();
                mTotalReleased.fetch_add(1);
                return;
            }
        }
    }
    
    // 纹理不在池中，忽略（让其自然销毁）
}

std::shared_ptr<lrengine::render::LRTexture> TexturePool::acquireAutoRelease(
    uint32_t width, uint32_t height, PixelFormat format) {
    
    auto texture = acquire(width, height, format);
    if (!texture) {
        return nullptr;
    }
    
    // 创建自定义删除器，释放时归还到池
    auto* pool = this;
    return std::shared_ptr<lrengine::render::LRTexture>(
        texture.get(),
        [pool, originalTexture = texture](lrengine::render::LRTexture*) mutable {
            pool->release(originalTexture);
        });
}

// =============================================================================
// 池管理
// =============================================================================

void TexturePool::warmup(const std::vector<TextureSpec>& specs) {
    for (const auto& spec : specs) {
        warmup(spec.width, spec.height, spec.format, 2);
    }
}

void TexturePool::warmup(uint32_t width, uint32_t height, 
                         PixelFormat format, uint32_t count) {
    std::lock_guard<std::mutex> lock(mMutex);
    
    TextureSpec spec{width, height, format};
    auto& bucket = mBuckets[spec];
    
    for (uint32_t i = 0; i < count && bucket.entries.size() < mConfig.maxTexturesPerBucket; ++i) {
        auto texture = createTexture(spec);
        if (texture) {
            TextureEntry entry;
            entry.texture = texture;
            entry.spec = spec;
            entry.lastUsed = std::chrono::steady_clock::now();
            entry.inUse = false;
            bucket.entries.push_back(std::move(entry));
        }
    }
}

void TexturePool::cleanup() {
    std::lock_guard<std::mutex> lock(mMutex);
    
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(mConfig.idleTimeoutMs);
    
    for (auto& [spec, bucket] : mBuckets) {
        bucket.entries.erase(
            std::remove_if(bucket.entries.begin(), bucket.entries.end(),
                [&](const TextureEntry& entry) {
                    if (entry.inUse) {
                        return false;
                    }
                    auto idle = now - entry.lastUsed;
                    return idle > timeout;
                }),
            bucket.entries.end());
    }
}

void TexturePool::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    mBuckets.clear();
}

void TexturePool::shrink() {
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 移除超出限制的纹理（保留最近使用的）
    for (auto& [spec, bucket] : mBuckets) {
        if (bucket.entries.size() > mConfig.maxTexturesPerBucket) {
            // 按最后使用时间排序
            std::sort(bucket.entries.begin(), bucket.entries.end(),
                [](const TextureEntry& a, const TextureEntry& b) {
                    return a.lastUsed > b.lastUsed;
                });
            
            // 移除超出限制的（不在使用中的）
            while (bucket.entries.size() > mConfig.maxTexturesPerBucket) {
                auto it = std::find_if(bucket.entries.rbegin(), bucket.entries.rend(),
                    [](const TextureEntry& e) { return !e.inUse; });
                
                if (it != bucket.entries.rend()) {
                    bucket.entries.erase(std::next(it).base());
                } else {
                    break;
                }
            }
        }
    }
}

// =============================================================================
// 状态查询
// =============================================================================

size_t TexturePool::getAvailableCount() const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    size_t count = 0;
    for (const auto& [spec, bucket] : mBuckets) {
        for (const auto& entry : bucket.entries) {
            if (!entry.inUse) {
                ++count;
            }
        }
    }
    return count;
}

size_t TexturePool::getAvailableCount(const TextureSpec& spec) const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    auto it = mBuckets.find(spec);
    if (it == mBuckets.end()) {
        return 0;
    }
    
    size_t count = 0;
    for (const auto& entry : it->second.entries) {
        if (!entry.inUse) {
            ++count;
        }
    }
    return count;
}

size_t TexturePool::getInUseCount() const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    size_t count = 0;
    for (const auto& [spec, bucket] : mBuckets) {
        for (const auto& entry : bucket.entries) {
            if (entry.inUse) {
                ++count;
            }
        }
    }
    return count;
}

size_t TexturePool::getTotalCount() const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    size_t count = 0;
    for (const auto& [spec, bucket] : mBuckets) {
        count += bucket.entries.size();
    }
    return count;
}

size_t TexturePool::getMemoryUsage() const {
    std::lock_guard<std::mutex> lock(mMutex);
    
    size_t total = 0;
    for (const auto& [spec, bucket] : mBuckets) {
        size_t textureSize = calculateTextureSize(spec);
        total += textureSize * bucket.entries.size();
    }
    return total;
}

float TexturePool::getHitRate() const {
    uint64_t hits = mHitCount.load();
    uint64_t misses = mMissCount.load();
    uint64_t total = hits + misses;
    
    if (total == 0) {
        return 0.0f;
    }
    
    return static_cast<float>(hits) / static_cast<float>(total);
}

void TexturePool::resetStats() {
    mHitCount.store(0);
    mMissCount.store(0);
    mTotalAllocated.store(0);
    mTotalReleased.store(0);
}

// =============================================================================
// 配置
// =============================================================================

void TexturePool::setConfig(const TexturePoolConfig& config) {
    std::lock_guard<std::mutex> lock(mMutex);
    mConfig = config;
    shrink();
}

// =============================================================================
// 内部方法
// =============================================================================

std::shared_ptr<lrengine::render::LRTexture> TexturePool::createTexture(const TextureSpec& spec) {
    if (!mRenderContext) {
        return nullptr;
    }
    
    mTotalAllocated.fetch_add(1);
    
    // 使用LREngine创建纹理
    // 这里需要根据LREngine的实际API来实现
    /*
    lrengine::render::TextureDescriptor desc;
    desc.width = spec.width;
    desc.height = spec.height;
    desc.format = convertPixelFormat(spec.format);
    desc.mipLevels = 1;
    desc.type = lrengine::render::TextureType::Texture2D;
    
    return std::shared_ptr<lrengine::render::LRTexture>(
        mRenderContext->CreateTexture(desc),
        [](lrengine::render::LRTexture* t) { delete t; });
    */
    
    // 临时返回nullptr，实际使用时需要实现
    return nullptr;
}

size_t TexturePool::calculateTextureSize(const TextureSpec& spec) const {
    size_t bytesPerPixel = getPixelFormatBytesPerPixel(spec.format);
    if (bytesPerPixel == 0) {
        bytesPerPixel = 4; // 默认RGBA
    }
    return spec.width * spec.height * bytesPerPixel;
}

uint32_t TexturePool::convertPixelFormat(PixelFormat format) const {
    // 转换为LREngine的像素格式
    // 这里需要根据LREngine的实际枚举值来实现
    switch (format) {
        case PixelFormat::RGBA8:
            return 0; // lrengine::render::PixelFormat::RGBA8
        case PixelFormat::BGRA8:
            return 1;
        case PixelFormat::RGB8:
            return 2;
        case PixelFormat::RGBA16F:
            return 3;
        case PixelFormat::RGBA32F:
            return 4;
        default:
            return 0;
    }
}

} // namespace pipeline
