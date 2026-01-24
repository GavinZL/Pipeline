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

std::shared_ptr<lrengine::render::LRTexture> IOSMetalContextManager::createTextureFromPixelBuffer(
    CVPixelBufferRef pixelBuffer,
    lrengine::render::LRRenderContext* renderContext) {
    
    if (!mInitialized || !mTextureCache) {
        PIPELINE_LOGE("Not initialized or texture cache not available");
        return nullptr;
    }
    
    if (!pixelBuffer) {
        PIPELINE_LOGE("Invalid pixelBuffer");
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 获取 PixelBuffer 属性
    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    OSType pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
    
    PIPELINE_LOGD("Creating texture from CVPixelBuffer: %zux%zu, format: 0x%x", width, height, pixelFormat);
    
    // 确定 Metal 像素格式
    MTLPixelFormat metalFormat;
    switch (pixelFormat) {
        case kCVPixelFormatType_32BGRA:
            metalFormat = MTLPixelFormatBGRA8Unorm;
            break;
        case kCVPixelFormatType_32RGBA:
            metalFormat = MTLPixelFormatRGBA8Unorm;
            break;
        case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
        case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
            // NV12 格式 - 只处理 Y 平面
            metalFormat = MTLPixelFormatR8Unorm;
            PIPELINE_LOGD("NV12 format detected, using Y plane only");
            break;
        default:
            PIPELINE_LOGE("Unsupported pixel format: 0x%x", pixelFormat);
            return nullptr;
    }
    
    // 创建 CVMetalTexture
    CVMetalTextureRef cvMetalTexture = nullptr;
    CVReturn status = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        static_cast<CVMetalTextureCacheRef>(mTextureCache),
        pixelBuffer,
        nullptr,
        metalFormat,
        width,
        height,
        0,  // planeIndex
        &cvMetalTexture
    );
    
    if (status != kCVReturnSuccess) {
        PIPELINE_LOGE("CVMetalTextureCacheCreateTextureFromImage failed: %d", status);
        return nullptr;
    }
    
    // 获取 Metal 纹理
    id<MTLTexture> metalTexture = CVMetalTextureGetTexture(cvMetalTexture);
    if (!metalTexture) {
        PIPELINE_LOGE("Failed to get Metal texture from CVMetalTexture");
        CFRelease(cvMetalTexture);
        return nullptr;
    }
    
    PIPELINE_LOGI("Created Metal texture: %zux%zu, format: %lu", 
         metalTexture.width, metalTexture.height, (unsigned long)metalTexture.pixelFormat);
    
    // TODO: 封装为 LRTexture
    // 这需要扩展 LREngine 的 TextureMTL 类
    // 现在返回 nullptr，表示需要进一步实现
    
    CFRelease(cvMetalTexture);
    
    PIPELINE_LOGD("Note: LRTexture wrapping not yet implemented, returning nullptr");
    return nullptr;
}

bool IOSMetalContextManager::copyTextureToPixelBuffer(
    std::shared_ptr<lrengine::render::LRTexture> texture,
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
