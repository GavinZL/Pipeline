/**
 * @file PlatformContext.cpp
 * @brief 平台上下文通用实现
 */

#include "pipeline/platform/PlatformContext.h"
#include <stdexcept>

#ifdef __ANDROID__
    #include <android/log.h>
    #define LOG_TAG "PlatformContext"
    #define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
    #include <cstdio>
    #define LOGD(...) printf("[DEBUG] " __VA_ARGS__); printf("\n")
    #define LOGE(...) fprintf(stderr, "[ERROR] " __VA_ARGS__); fprintf(stderr, "\n")
#endif

namespace pipeline {

// =============================================================================
// PlatformContext 实现
// =============================================================================

PlatformContext::PlatformContext()
    : mPlatformType(PlatformType::Unknown)
    , mGraphicsAPI(GraphicsAPI::OpenGLES)
    , mInitialized(false)
{
}

PlatformContext::~PlatformContext() {
    destroy();
}

bool PlatformContext::initialize(const PlatformContextConfig& config) {
    if (mInitialized) {
        LOGD("PlatformContext already initialized");
        return true;
    }
    
    mPlatformType = config.platform;
    mGraphicsAPI = config.graphicsAPI;
    
    // 平台检测（如果未指定）
    if (mPlatformType == PlatformType::Unknown) {
#ifdef __ANDROID__
        mPlatformType = PlatformType::Android;
        mGraphicsAPI = GraphicsAPI::OpenGLES;
        LOGD("Auto-detected platform: Android");
#elif defined(__APPLE__)
    #if TARGET_OS_IPHONE || TARGET_OS_IOS
        mPlatformType = PlatformType::iOS;
        mGraphicsAPI = GraphicsAPI::Metal;
        LOGD("Auto-detected platform: iOS");
    #elif TARGET_OS_MAC
        mPlatformType = PlatformType::macOS;
        mGraphicsAPI = GraphicsAPI::Metal;
        LOGD("Auto-detected platform: macOS");
    #endif
#elif defined(_WIN32)
        mPlatformType = PlatformType::Windows;
        LOGD("Auto-detected platform: Windows");
#else
        mPlatformType = PlatformType::Linux;
        LOGD("Auto-detected platform: Linux");
#endif
    }
    
    // 根据平台初始化
    bool success = false;
    
#ifdef __ANDROID__
    if (mPlatformType == PlatformType::Android) {
        mAndroidEGLManager = std::make_unique<AndroidEGLContextManager>();
        success = mAndroidEGLManager->initialize(config.androidConfig);
        if (!success) {
            LOGE("Failed to initialize AndroidEGLContextManager");
        }
    }
#endif
    
#if defined(PIPELINE_PLATFORM_IOS)
    if (mPlatformType == PlatformType::iOS) {
        mIOSMetalManager = std::make_unique<IOSMetalContextManager>();
        success = mIOSMetalManager->initialize(config.iosConfig);
        if (!success) {
            LOGE("Failed to initialize IOSMetalContextManager");
        }
    }
#endif
    
    if (!success) {
        LOGE("Platform initialization failed for platform type: %d", (int)mPlatformType);
        return false;
    }
    
    mInitialized = success;
    LOGD("PlatformContext initialized successfully for platform: %d", (int)mPlatformType);
    
    return true;
}

bool PlatformContext::makeCurrent() {
    if (!mInitialized) {
        LOGE("PlatformContext not initialized");
        return false;
    }
    
#ifdef __ANDROID__
    if (mAndroidEGLManager) {
        return mAndroidEGLManager->makeCurrent();
    }
#endif
    
    // iOS Metal 不需要显式的 makeCurrent
    return true;
}

bool PlatformContext::releaseCurrent() {
    if (!mInitialized) {
        return false;
    }
    
#ifdef __ANDROID__
    if (mAndroidEGLManager) {
        return mAndroidEGLManager->releaseCurrent();
    }
#endif
    
    return true;
}

void PlatformContext::destroy() {
    if (!mInitialized) {
        return;
    }
    
    LOGD("Destroying PlatformContext");
    
#ifdef __ANDROID__
    if (mAndroidEGLManager) {
        mAndroidEGLManager->destroy();
        mAndroidEGLManager.reset();
    }
#endif
    
#if defined(PIPELINE_PLATFORM_IOS)
    if (mIOSMetalManager) {
        mIOSMetalManager->destroy();
        mIOSMetalManager.reset();
    }
#endif
    
    mInitialized = false;
}

#ifdef __ANDROID__

std::shared_ptr<lrengine::render::LRTexture> PlatformContext::createTextureFromOES(
    uint32_t oesTextureId,
    uint32_t width,
    uint32_t height,
    const float* transformMatrix) {
    
    if (!mAndroidEGLManager) {
        LOGE("AndroidEGLManager not initialized");
        return nullptr;
    }
    
    // TODO: 实现 OES 纹理转换
    // 这需要：
    // 1. 创建一个 FBO 和标准 2D 纹理
    // 2. 使用 OES 采样着色器将 OES 纹理渲染到 2D 纹理
    // 3. 返回包装后的 LRTexture
    
    LOGD("createTextureFromOES: oesId=%u, size=%ux%u", oesTextureId, width, height);
    
    // 暂时返回 nullptr，完整实现需要依赖 LREngine
    return nullptr;
}

#endif // __ANDROID__

#if defined(PIPELINE_PLATFORM_IOS)

std::shared_ptr<lrengine::render::LRTexture> PlatformContext::createTextureFromPixelBuffer(
    CVPixelBufferRef pixelBuffer,
    lrengine::render::LRRenderContext* renderContext) {
    
    if (!mIOSMetalManager) {
        LOGE("IOSMetalContextManager not initialized");
        return nullptr;
    }
    
    return mIOSMetalManager->createTextureFromPixelBuffer(pixelBuffer, renderContext);
}

bool PlatformContext::copyTextureToPixelBuffer(
    std::shared_ptr<lrengine::render::LRTexture> texture,
    CVPixelBufferRef pixelBuffer) {
    
    if (!mIOSMetalManager) {
        LOGE("IOSMetalContextManager not initialized");
        return false;
    }
    
    return mIOSMetalManager->copyTextureToPixelBuffer(texture, pixelBuffer);
}

#endif // PIPELINE_PLATFORM_IOS

} // namespace pipeline
