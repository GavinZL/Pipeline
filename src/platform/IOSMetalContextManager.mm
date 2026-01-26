/**
 * @file IOSMetalContextManager.mm
 * @brief iOS Metal 上下文管理器实现
 */

#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <CoreVideo/CoreVideo.h>

#include "pipeline/platform/PlatformContext.h"
#include "pipeline/utils/PipelineLog.h"
#include "lrengine/utils/ImageBuffer.h"
#include "lrengine/core/LRPlanarTexture.h"
#include "lrengine/core/LRRenderContext.h"
#include <iostream>

namespace pipeline {

// =============================================================================
// IOSMetalContextManager 实现
// =============================================================================

IOSMetalContextManager::IOSMetalContextManager()
    : mMetalDevice(nullptr)
    , mTextureCache(nullptr)
    , mInitialized(false)
{
}

IOSMetalContextManager::~IOSMetalContextManager() {
    destroy();
}

bool IOSMetalContextManager::initialize(const Config& config) {
    std::lock_guard<std::mutex> lock(mMutex);
    
    if (mInitialized) {
        PIPELINE_LOGW("Already initialized");
        return true;
    }
    
    PIPELINE_LOGI("Initializing IOSMetalContextManager");
    
    // 1. 获取或创建 Metal 设备
    if (config.metalDevice != nullptr) {
        mMetalDevice = config.metalDevice;
        PIPELINE_LOGD("Using provided Metal device: %p", mMetalDevice);
    } else {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            PIPELINE_LOGE("Failed to create Metal device");
            return false;
        }
        mMetalDevice = (__bridge void*)device;
        PIPELINE_LOGI("Created system default Metal device: %s", [device.name UTF8String]);
    }
    
    // 2. 创建 CVMetalTextureCache
    if (config.enableTextureCache) {
        CVReturn status = CVMetalTextureCacheCreate(
            kCFAllocatorDefault,
            nullptr,
            (__bridge id<MTLDevice>)mMetalDevice,
            nullptr,
            reinterpret_cast<CVMetalTextureCacheRef*>(&mTextureCache)
        );
        
        if (status != kCVReturnSuccess) {
            PIPELINE_LOGE("CVMetalTextureCacheCreate failed with status: %d", status);
            return false;
        }
        
        PIPELINE_LOGI("Created CVMetalTextureCache with max size: %d", config.textureCacheMaxSize);
    }
    
    mInitialized = true;
    PIPELINE_LOGI("IOSMetalContextManager initialized successfully");
    
    return true;
}

std::shared_ptr<lrengine::render::LRPlanarTexture> IOSMetalContextManager::createTextureFromPixelBuffer(
    CVPixelBufferRef pixelBuffer,
    lrengine::render::LRRenderContext* renderContext) {
    
    if (!mInitialized || !mTextureCache) {
        PIPELINE_LOGE("Not initialized or texture cache not available");
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 获取 PixelBuffer 属性
    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    OSType pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
    
    PIPELINE_LOGD("Creating texture from CVPixelBuffer: %zux%zu, format: 0x%x", width, height, pixelFormat);
    
    // 判断是否为 YUV 格式，如果是则使用 LRPlanarTexture
    bool isYUV = (pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange ||
                  pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
                  pixelFormat == kCVPixelFormatType_420YpCbCr8Planar);
    
    if (isYUV) {
        // 使用 LRPlanarTexture 处理 YUV 格式
        PIPELINE_LOGI("Creating LRPlanarTexture for YUV format");
        
        // 准备图像数据描述
        lrengine::render::ImageDataDesc imageDesc;
        imageDesc.width = static_cast<uint32_t>(width);
        imageDesc.height = static_cast<uint32_t>(height);
        
        // 根据 CVPixelBuffer 格式确定 ImageFormat
        if (pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
            imageDesc.format = lrengine::render::ImageFormat::NV12;
            imageDesc.colorSpace = lrengine::render::ColorSpace::BT709;
            imageDesc.range = lrengine::render::ColorRange::Full;
        } else if (pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
            imageDesc.format = lrengine::render::ImageFormat::NV12;
            imageDesc.colorSpace = lrengine::render::ColorSpace::BT709;
            imageDesc.range = lrengine::render::ColorRange::Video;
        } else {
            imageDesc.format = lrengine::render::ImageFormat::YUV420P;
            imageDesc.colorSpace = lrengine::render::ColorSpace::BT709;
            imageDesc.range = lrengine::render::ColorRange::Video;
        }
        
        // 使用 ImageBuffer 包装 CVPixelBuffer
        auto buffer = std::make_unique<lrengine::utils::CVPixelBufferWrapper>(pixelBuffer);
        if (!buffer) {
            PIPELINE_LOGE("Failed to create CVPixelBufferWrapper");
            return nullptr;
        }
        
        // 锁定以获取数据
        if (!buffer->Lock(true)) {  // true = 只读
            PIPELINE_LOGE("Failed to lock CVPixelBuffer");
            return nullptr;
        }
        
        // 获取并更新 imageDesc 的平面信息
        imageDesc = buffer->GetImageDesc();
        buffer->Unlock();
        
        // 创建 LRPlanarTexture
        lrengine::render::PlanarFormat planarFormat;
        if (imageDesc.format == lrengine::render::ImageFormat::NV12) {
            planarFormat = lrengine::render::PlanarFormat::NV12;
        } else if (imageDesc.format == lrengine::render::ImageFormat::NV21) {
            planarFormat = lrengine::render::PlanarFormat::NV21;
        } else {
            planarFormat = lrengine::render::PlanarFormat::YUV420P;
        }
        
        lrengine::render::PlanarTextureDescriptor texDesc;
        texDesc.width = imageDesc.width;
        texDesc.height = imageDesc.height;
        texDesc.format = planarFormat;
        texDesc.debugName = "CVPixelBuffer_YUV";
        
        auto* planarTexture = renderContext->CreatePlanarTexture(texDesc);
        if (!planarTexture) {
            PIPELINE_LOGE("Failed to create LRPlanarTexture");
            return nullptr;
        }
        
        // 使用 UpdateFromImage 更新纹理数据
        // 需要再次锁定 buffer 并更新
        if (!buffer->Lock(true)) {
            delete planarTexture;
            PIPELINE_LOGE("Failed to lock CVPixelBuffer for update");
            return nullptr;
        }
        
        bool updateSuccess = planarTexture->UpdateFromImage(buffer->GetImageDesc());
        buffer->Unlock();
        
        if (!updateSuccess) {
            delete planarTexture;
            PIPELINE_LOGE("Failed to update LRPlanarTexture from CVPixelBuffer");
            return nullptr;
        }
        
        PIPELINE_LOGI("Successfully created LRPlanarTexture: %ux%u", texDesc.width, texDesc.height);
        
        return std::shared_ptr<lrengine::render::LRPlanarTexture>(planarTexture);
        
    } else {
        // RGBA/BGRA 单平面格式：使用 LRPlanarTexture（统一路径）
        PIPELINE_LOGI("Creating LRPlanarTexture for RGBA format");
        
        // 准备图像数据描述
        lrengine::render::ImageDataDesc imageDesc;
        imageDesc.width = static_cast<uint32_t>(width);
        imageDesc.height = static_cast<uint32_t>(height);
        imageDesc.format = lrengine::render::ImageFormat::RGBA8;
        imageDesc.colorSpace = lrengine::render::ColorSpace::BT709;
        imageDesc.range = lrengine::render::ColorRange::Full;
        
        // 使用 ImageBuffer 包装 CVPixelBuffer
        auto buffer = std::make_unique<lrengine::utils::CVPixelBufferWrapper>(pixelBuffer);
        if (!buffer) {
            PIPELINE_LOGE("Failed to create CVPixelBufferWrapper for RGBA");
            return nullptr;
        }
        
        // 锁定以获取数据
        if (!buffer->Lock(true)) {  // true = 只读
            PIPELINE_LOGE("Failed to lock CVPixelBuffer");
            return nullptr;
        }
        
        // 获取并更新 imageDesc
        imageDesc = buffer->GetImageDesc();
        buffer->Unlock();
        
        // 创建 LRPlanarTexture（RGBA 单平面模式）
        lrengine::render::PlanarTextureDescriptor texDesc;
        texDesc.width = imageDesc.width;
        texDesc.height = imageDesc.height;
        texDesc.format = lrengine::render::PlanarFormat::RGBA;
        texDesc.debugName = "CVPixelBuffer_RGBA";
        
        auto* planarTexture = renderContext->CreatePlanarTexture(texDesc);
        if (!planarTexture) {
            PIPELINE_LOGE("Failed to create LRPlanarTexture for RGBA");
            return nullptr;
        }
        
        // 使用 UpdateFromImage 更新纹理数据
        if (!buffer->Lock(true)) {
            delete planarTexture;
            PIPELINE_LOGE("Failed to lock CVPixelBuffer for update");
            return nullptr;
        }
        
        bool updateSuccess = planarTexture->UpdateFromImage(buffer->GetImageDesc());
        buffer->Unlock();
        
        if (!updateSuccess) {
            delete planarTexture;
            PIPELINE_LOGE("Failed to update LRPlanarTexture from CVPixelBuffer (RGBA)");
            return nullptr;
        }
        
        PIPELINE_LOGI("Successfully created LRPlanarTexture (RGBA): %ux%u", texDesc.width, texDesc.height);
        
        return std::shared_ptr<lrengine::render::LRPlanarTexture>(planarTexture);
    }
}

bool IOSMetalContextManager::copyTextureToPixelBuffer(
    std::shared_ptr<lrengine::render::LRPlanarTexture> texture,
    CVPixelBufferRef pixelBuffer) {
    
    if (!mInitialized) {
        PIPELINE_LOGE("Not initialized");
        return false;
    }
    
    if (!texture || !pixelBuffer) {
        PIPELINE_LOGE("Invalid texture or pixelBuffer");
        return false;
    }
    
    // TODO: 实现纹理到 CVPixelBuffer 的拷贝
    // 这需要：
    // 1. 获取 LRTexture 底层的 Metal 纹理
    // 2. 使用 Metal Blit 命令编码器或 Metal Performance Shaders
    // 3. 将纹理内容拷贝到 CVPixelBuffer
    
    PIPELINE_LOGD("copyTextureToPixelBuffer not yet fully implemented");
    
    return false;
}

void IOSMetalContextManager::flushTextureCache() {
    if (!mTextureCache) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mMutex);
    CVMetalTextureCacheFlush(static_cast<CVMetalTextureCacheRef>(mTextureCache), 0);
    PIPELINE_LOGD("Flushed texture cache");
}

void IOSMetalContextManager::destroy() {
    std::lock_guard<std::mutex> lock(mMutex);
    
    if (!mInitialized) {
        return;
    }
    
    PIPELINE_LOGI("Destroying IOSMetalContextManager");
    
    // 释放纹理缓存
    if (mTextureCache) {
        CFRelease(static_cast<CVMetalTextureCacheRef>(mTextureCache));
        mTextureCache = nullptr;
    }
    
    // 不释放 Metal 设备，因为可能是外部提供的
    mMetalDevice = nullptr;
    
    mInitialized = false;
    PIPELINE_LOGI("IOSMetalContextManager destroyed");
}

} // namespace pipeline

#endif // __APPLE__
