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

#include <vector>

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
    // 旧接口，使用 processToGPUPlanar 并返回第一个平面
    std::shared_ptr<lrengine::render::LRPlanarTexture> planarTexture;
    if (!processToGPUPlanar(input, planarTexture)) {
        return false;
    }
    
    if (!planarTexture) {
        return false;
    }
    
    // 获取第一个平面纹理
    auto* planeTexture = planarTexture->GetPlaneTexture(0);
    if (!planeTexture) {
        PIPELINE_LOGE("Failed to get first plane texture");
        return false;
    }
    
    // 创建 shared_ptr（不拥有所有权，因为纹理属于 LRPlanarTexture）
    // 注意：这里需要小心处理生命周期
    outputTexture = std::shared_ptr<lrengine::render::LRTexture>(
        planarTexture, planeTexture  // 别名构造器：共享 planarTexture 的生命周期
    );
    
    return true;
}

bool PixelBufferInputStrategy::processToGPUPlanar(const InputData& input,
                                                   std::shared_ptr<lrengine::render::LRPlanarTexture>& outputTexture) {
    if (!mInitialized) {
        PIPELINE_LOGE("PixelBufferInputStrategy not initialized");
        return false;
    }
    
    CVPixelBufferRef pixelBuffer = nullptr;
    
    // 优先使用 platformBuffer
    if (input.platformBuffer) {
        pixelBuffer = static_cast<CVPixelBufferRef>(input.platformBuffer);
    } else if (mCurrentPixelBuffer) {
        pixelBuffer = mCurrentPixelBuffer;
    }
    
    if (!pixelBuffer) {
        PIPELINE_LOGE("No pixel buffer available");
        return false;
    }
    
    // 创建 Metal 纹理
    if (!createMetalTextureFromPixelBuffer(pixelBuffer)) {
        PIPELINE_LOGE("Failed to create Metal texture from PixelBuffer");
        return false;
    }
    
    // 🔥 关键修复：将 mOutputTexture 赋值给 outputTexture 参数
    outputTexture = mOutputTexture;
    
    return outputTexture != nullptr;
}

bool PixelBufferInputStrategy::processToCPU(const InputData& input,
                                             uint8_t* outputBuffer,
                                             size_t& outputSize,
                                             uint32_t targetWidth,
                                             uint32_t targetHeight) {
    if (!mInitialized || !outputBuffer) {
        return false;
    }
    
    CVPixelBufferRef pixelBuffer = nullptr;
    
    // 优先使用 platformBuffer
    if (input.platformBuffer) {
        pixelBuffer = static_cast<CVPixelBufferRef>(input.platformBuffer);
    } else if (mCurrentPixelBuffer) {
        pixelBuffer = mCurrentPixelBuffer;
    }
    
    if (!pixelBuffer) {
        PIPELINE_LOGE("No pixel buffer available");
        return false;
    }
    
    return readCPUDataFromPixelBuffer(pixelBuffer, outputBuffer, outputSize, targetWidth, targetHeight);
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
                                                           size_t& outputSize,
                                                           uint32_t targetWidth,
                                                           uint32_t targetHeight) {
    uint32_t srcWidth, srcHeight;
    OSType pixelFormat;
    
    if (!getPixelBufferInfo(pixelBuffer, srcWidth, srcHeight, pixelFormat)) {
        return false;
    }
    
    // 确定目标尺寸（如果未指定则使用源尺寸）
    uint32_t dstWidth = (targetWidth > 0) ? targetWidth : srcWidth;
    uint32_t dstHeight = (targetHeight > 0) ? targetHeight : srcHeight;
    
    // 检查是否需要缩放
    bool needScale = (srcWidth != dstWidth || srcHeight != dstHeight);
    
    // 计算所需的输出缓冲区大小（RGBA 格式，4 字节/像素）
    size_t requiredSize = static_cast<size_t>(dstWidth) * dstHeight * 4;
    
    // 检查输出缓冲区是否足够
    if (outputSize < requiredSize) {
        PIPELINE_LOGW("Output buffer too small: %zu < %zu, required for %ux%u",
                     outputSize, requiredSize, dstWidth, dstHeight);
        outputSize = requiredSize;
        return false;
    }
    
    // 锁定像素数据
    CVReturn result = CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    if (result != kCVReturnSuccess) {
        PIPELINE_LOGE("Failed to lock pixel buffer: %d", result);
        return false;
    }
    
    bool success = false;
    
    // 临时缓冲区：用于缩放时的中间 RGBA 数据
    // 无论是否缩放，都使用统一的处理流程
    std::vector<uint8_t> tempBuffer;
    uint8_t* rgbaBuffer = outputBuffer;
    int rgbaStride = static_cast<int>(dstWidth) * 4;
    
    if (needScale) {
        // 需要缩放：先转换到源尺寸的临时缓冲区，再缩放
        size_t tempSize = static_cast<size_t>(srcWidth) * srcHeight * 4;
        tempBuffer.resize(tempSize);
        rgbaBuffer = tempBuffer.data();
        rgbaStride = static_cast<int>(srcWidth) * 4;
        PIPELINE_LOGD("Scaling from %ux%u to %ux%u", srcWidth, srcHeight, dstWidth, dstHeight);
    }
    
    // 格式转换：根据输入格式选择正确的转换函数
    // 注意：libyuv 的 ARGB 格式实际上是 BGRA 内存布局（B 在最低位）
    if (pixelFormat == kCVPixelFormatType_32BGRA) {
        // BGRA -> RGBA（使用 ARGBToABGR 进行通道重排）
        void* baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
        
        libyuv::ARGBToABGR(
            static_cast<const uint8_t*>(baseAddress), static_cast<int>(bytesPerRow),
            rgbaBuffer, rgbaStride,
            static_cast<int>(srcWidth), static_cast<int>(srcHeight));
        
        success = true;
        
    } else if (pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
               pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
        // NV12 -> RGBA（NV12ToARGB 输出 ARGB 格式，即 BGRA 内存布局）
        void* yPlane = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
        void* uvPlane = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
        size_t yBytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);
        size_t uvBytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);
        
        libyuv::NV12ToARGB(
            static_cast<const uint8_t*>(yPlane), static_cast<int>(yBytesPerRow),
            static_cast<const uint8_t*>(uvPlane), static_cast<int>(uvBytesPerRow),
            rgbaBuffer, rgbaStride,
            static_cast<int>(srcWidth), static_cast<int>(srcHeight));
        
        success = true;
        
    } else {
        PIPELINE_LOGE("Unsupported pixel format: 0x%08X (%.4s)", 
                     (unsigned)pixelFormat, (char*)&pixelFormat);
    }
    
    // 缩放处理：从源尺寸缩放到目标尺寸
    if (success && needScale) {
        libyuv::ARGBScale(
            tempBuffer.data(), static_cast<int>(srcWidth) * 4,
            static_cast<int>(srcWidth), static_cast<int>(srcHeight),
            outputBuffer, static_cast<int>(dstWidth) * 4,
            static_cast<int>(dstWidth), static_cast<int>(dstHeight),
            libyuv::kFilterBilinear);
    }
    
    if (success) {
        outputSize = requiredSize;
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
