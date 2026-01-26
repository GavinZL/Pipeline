/**
 * @file PixelBufferInputStrategy.mm
 * @brief PixelBufferInputStrategy 实现
 */

#if defined(__APPLE__)

#import "pipeline/input/ios/PixelBufferInputStrategy.h"
#import "pipeline/utils/PipelineLog.h"
#import <Metal/Metal.h>
#import <CoreVideo/CVMetalTextureCache.h>

#include "lrengine/core/LRPlanarTexture.h"
#include "pipeline/utils/PipelineLog.h"
// libyuv for format conversion
#include "libyuv.h"

namespace pipeline {
namespace input {
namespace ios {

// =============================================================================
// 构造与析构
// =============================================================================

PixelBufferInputStrategy::PixelBufferInputStrategy() = default;

PixelBufferInputStrategy::~PixelBufferInputStrategy() {
    release();
}

// =============================================================================
// InputStrategy 接口实现
// =============================================================================

bool PixelBufferInputStrategy::initialize(lrengine::render::LRRenderContext* context) {
    if (mInitialized) {
        return true;
    }
    
    mRenderContext = context;
    
    // 检查 Metal 管理器
    if (!mMetalManager) {
        PIPELINE_LOGW("MetalContextManager not set, texture cache disabled");
        mUseTextureCache = false;
    }
    
    mInitialized = true;
    PIPELINE_LOGI("PixelBufferInputStrategy initialized");
    return true;
}

bool PixelBufferInputStrategy::processToGPU(const InputData& input,
                                             lrengine::LRTexturePtr& outputTexture) {
    if (!mInitialized) {
        return false;
    }
    
    // 使用当前存储的 PixelBuffer
    if (!mCurrentPixelBuffer) {
        PIPELINE_LOGE("No pixel buffer available");
        return false;
    }
    
    // 创建 Metal 纹理
    return createMetalTextureFromPixelBuffer(mCurrentPixelBuffer);
}

bool PixelBufferInputStrategy::processToCPU(const InputData& input,
                                             uint8_t* outputBuffer,
                                             size_t& outputSize) {
    if (!mInitialized || !outputBuffer) {
        return false;
    }
    
    if (!mCurrentPixelBuffer) {
        PIPELINE_LOGE("No pixel buffer available");
        return false;
    }
    
    return readCPUDataFromPixelBuffer(mCurrentPixelBuffer, outputBuffer, outputSize);
}

void PixelBufferInputStrategy::release() {
    if (mCurrentPixelBuffer) {
        CVPixelBufferRelease(mCurrentPixelBuffer);
        mCurrentPixelBuffer = nullptr;
    }
    
    mOutputTexture.reset();
    mInitialized = false;
    
    PIPELINE_LOGI("PixelBufferInputStrategy released");
}

// =============================================================================
// iOS 特定接口
// =============================================================================

void PixelBufferInputStrategy::setMetalContextManager(IOSMetalContextManager* manager) {
    mMetalManager = manager;
}

bool PixelBufferInputStrategy::submitPixelBuffer(CVPixelBufferRef pixelBuffer, int64_t timestamp) {
    if (!pixelBuffer) {
        PIPELINE_LOGE("Invalid pixel buffer");
        return false;
    }
    
    // 释放旧的
    if (mCurrentPixelBuffer) {
        CVPixelBufferRelease(mCurrentPixelBuffer);
    }
    
    // 保留新的
    mCurrentPixelBuffer = CVPixelBufferRetain(pixelBuffer);
    mCurrentTimestamp = timestamp;
    
    return true;
}

// =============================================================================
// 内部方法
// =============================================================================

bool PixelBufferInputStrategy::createMetalTextureFromPixelBuffer(CVPixelBufferRef pixelBuffer) {
    if (!mMetalManager || !mUseTextureCache) {
        // 无缓存模式，需要手动创建纹理并上传
        PIPELINE_LOGW("Texture cache not available, falling back to upload");
        return false;
    }
    
    // 使用 IOSMetalContextManager 创建纹理
    mOutputTexture = mMetalManager->createTextureFromPixelBuffer(pixelBuffer, mRenderContext);
    
    return mOutputTexture != nullptr;
}

bool PixelBufferInputStrategy::readCPUDataFromPixelBuffer(CVPixelBufferRef pixelBuffer,
                                                           uint8_t* outputBuffer,
                                                           size_t& outputSize) {
    uint32_t width, height;
    OSType pixelFormat;
    
    if (!getPixelBufferInfo(pixelBuffer, width, height, pixelFormat)) {
        return false;
    }
    
    // 锁定像素数据
    CVReturn result = CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    if (result != kCVReturnSuccess) {
        PIPELINE_LOGE("Failed to lock pixel buffer: %d", result);
        return false;
    }
    
    bool success = false;
    size_t requiredSize = width * height * 4; // RGBA output
    
    if (outputSize < requiredSize) {
        outputSize = requiredSize;
        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        return false;
    }
    
    if (pixelFormat == kCVPixelFormatType_32BGRA) {
        // BGRA -> RGBA
        void* baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
        
        libyuv::ARGBToABGR(
            static_cast<const uint8_t*>(baseAddress), static_cast<int>(bytesPerRow),
            outputBuffer, width * 4,
            width, height);
        
        success = true;
        outputSize = requiredSize;
        
    } else if (pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
               pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
        // NV12 -> RGBA
        void* yPlane = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
        void* uvPlane = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
        size_t yBytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);
        size_t uvBytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);
        
        libyuv::NV12ToARGB(
            static_cast<const uint8_t*>(yPlane), static_cast<int>(yBytesPerRow),
            static_cast<const uint8_t*>(uvPlane), static_cast<int>(uvBytesPerRow),
            outputBuffer, width * 4,
            width, height);
        
        success = true;
        outputSize = requiredSize;
        
    } else {
        PIPELINE_LOGE("Unsupported pixel format: %u", (unsigned)pixelFormat);
    }
    
    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    return success;
}

bool PixelBufferInputStrategy::getPixelBufferInfo(CVPixelBufferRef pixelBuffer,
                                                   uint32_t& width, uint32_t& height,
                                                   OSType& pixelFormat) {
    if (!pixelBuffer) {
        return false;
    }
    
    width = static_cast<uint32_t>(CVPixelBufferGetWidth(pixelBuffer));
    height = static_cast<uint32_t>(CVPixelBufferGetHeight(pixelBuffer));
    pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
    
    return true;
}

} // namespace ios
} // namespace input
} // namespace pipeline

#endif // __APPLE__
