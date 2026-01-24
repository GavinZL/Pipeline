/**
 * @file PlatformContext.h
 * @brief 平台相关上下文管理 - 处理跨平台图形上下文共享和平台特定数据格式
 * 
 * 核心职责：
 * 1. 管理平台特定的图形上下文（EGL/Metal）
 * 2. 处理跨线程/跨组件的上下文共享
 * 3. 提供平台数据格式转换接口（CVPixelBuffer/SurfaceTexture）
 */

#pragma once

#include "pipeline/core/PipelineConfig.h"
#include <memory>
#include <mutex>

// 前向声明
namespace lrengine {
namespace render {
class LRRenderContext;
class LRTexture;
} // namespace render
} // namespace lrengine

namespace pipeline {

// =============================================================================
// 平台相关枚举
// =============================================================================

/**
 * @brief 平台类型
 */
enum class PlatformType : uint8_t {
    Android,
    iOS,
    macOS,
    Windows,
    Linux,
    Unknown
};

/**
 * @brief 图形API类型
 */
enum class GraphicsAPI : uint8_t {
    OpenGLES,   // Android GLES
    Metal,      // iOS/macOS Metal
    OpenGL,     // Desktop OpenGL
    Vulkan      // Vulkan
};

// =============================================================================
// Android平台特定
// =============================================================================

#ifdef __ANDROID__

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

/**
 * @brief Android EGL上下文管理器
 * 
 * 负责：
 * - EGL上下文的创建和共享
 * - 跨线程上下文切换
 * - OES纹理处理
 * - SurfaceTexture互操作
 */
class AndroidEGLContextManager {
public:
    /**
     * @brief 上下文配置
     */
    struct Config {
        EGLContext sharedContext = EGL_NO_CONTEXT;  // 共享源上下文（如相机线程）
        EGLDisplay display = EGL_NO_DISPLAY;
        int32_t glesVersion = 3;                     // GLES版本（2或3）
        bool enableDebug = false;
        
        // Surface配置
        bool offscreen = false;                      // 是否离屏
        int32_t pbufferWidth = 1;
        int32_t pbufferHeight = 1;
    };
    
    AndroidEGLContextManager();
    ~AndroidEGLContextManager();
    
    /**
     * @brief 初始化EGL上下文
     * @param config 配置参数
     * @return 是否成功
     */
    bool initialize(const Config& config);
    
    /**
     * @brief 创建共享上下文
     * @param sourceContext 源上下文（用于共享资源）
     * @return 新的EGL上下文
     */
    EGLContext createSharedContext(EGLContext sourceContext = EGL_NO_CONTEXT);
    
    /**
     * @brief 激活上下文到当前线程
     */
    bool makeCurrent();
    
    /**
     * @brief 取消当前上下文绑定
     */
    bool releaseCurrent();
    
    /**
     * @brief 获取当前EGL上下文
     */
    EGLContext getContext() const { return mContext; }
    
    /**
     * @brief 获取EGL显示
     */
    EGLDisplay getDisplay() const { return mDisplay; }
    
    /**
     * @brief 获取EGL Surface
     */
    EGLSurface getSurface() const { return mSurface; }
    
    /**
     * @brief 检查是否当前线程持有上下文
     */
    bool isCurrent() const;
    
    /**
     * @brief 销毁资源
     */
    void destroy();
    
private:
    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    EGLContext mContext = EGL_NO_CONTEXT;
    EGLSurface mSurface = EGL_NO_SURFACE;
    EGLConfig mConfig = nullptr;
    
    bool mInitialized = false;
    std::mutex mMutex;
};

#endif // __ANDROID__

// =============================================================================
// iOS/macOS平台特定
// =============================================================================

#if defined(__APPLE__)

#include <TargetConditionals.h>
#include <CoreVideo/CoreVideo.h>
#include <CoreVideo/CVMetalTexture.h>

#if TARGET_OS_IPHONE || TARGET_OS_IOS
    // iOS平台
    #define PIPELINE_PLATFORM_IOS 1
#elif TARGET_OS_MAC
    // macOS平台
    #define PIPELINE_PLATFORM_MACOS 1
#endif


/**
 * @brief iOS Metal上下文管理器
 * 
 * 负责：
 * - CVPixelBuffer与Metal纹理互操作
 * - 纹理缓存管理
 * - Metal共享资源管理
 */
class IOSMetalContextManager {
public:
    /**
     * @brief 上下文配置
     */
    struct Config {
        void* metalDevice = nullptr;                 // MTLDevice*
        bool enableTextureCache = true;
        int32_t textureCacheMaxSize = 10;
    };
    
    IOSMetalContextManager();
    ~IOSMetalContextManager();
    
    /**
     * @brief 初始化Metal上下文
     */
    bool initialize(const Config& config);
    
    /**
     * @brief 从CVPixelBuffer创建LRTexture
     * @param pixelBuffer CVPixelBufferRef
     * @param renderContext LREngine渲染上下文
     * @return LRTexture包装对象
     */
    std::shared_ptr<lrengine::render::LRTexture> createTextureFromPixelBuffer(
        CVPixelBufferRef pixelBuffer,
        lrengine::render::LRRenderContext* renderContext);
    
    /**
     * @brief 将LRTexture内容复制到CVPixelBuffer
     * @param texture 源纹理
     * @param pixelBuffer 目标CVPixelBuffer
     * @return 是否成功
     */
    bool copyTextureToPixelBuffer(
        std::shared_ptr<lrengine::render::LRTexture> texture,
        CVPixelBufferRef pixelBuffer);
    
    /**
     * @brief 获取Metal设备
     */
    void* getMetalDevice() const { return mMetalDevice; }
    
    /**
     * @brief 获取纹理缓存
     */
    void* getTextureCache() const { return mTextureCache; }
    
    /**
     * @brief 刷新纹理缓存
     */
    void flushTextureCache();
    
    /**
     * @brief 销毁资源
     */
    void destroy();
    
private:
    void* mMetalDevice = nullptr;                    // MTLDevice*
    void* mTextureCache = nullptr;
    
    bool mInitialized = false;
    std::mutex mMutex;
};

#endif // __APPLE__

// =============================================================================
// 平台上下文统一接口
// =============================================================================

/**
 * @brief 平台上下文配置
 */
struct PlatformContextConfig {
    // 通用配置
    PlatformType platform = PlatformType::Unknown;
    GraphicsAPI graphicsAPI = GraphicsAPI::OpenGLES;
    bool enableDebug = false;
    
#ifdef __ANDROID__
    // Android特定
    AndroidEGLContextManager::Config androidConfig;
#endif
    
#if defined(__APPLE__)
    // iOS特定
    IOSMetalContextManager::Config iosConfig;
#endif
};

/**
 * @brief 平台上下文管理器（统一接口）
 * 
 * 封装平台特定的上下文管理，对外提供统一API。
 * Pipeline通过此接口与底层平台交互。
 */
class PlatformContext {
public:
    PlatformContext();
    ~PlatformContext();
    
    /**
     * @brief 初始化平台上下文
     * @param config 配置参数
     * @return 是否成功
     */
    bool initialize(const PlatformContextConfig& config);
    
    /**
     * @brief 获取平台类型
     */
    PlatformType getPlatformType() const { return mPlatformType; }
    
    /**
     * @brief 获取图形API类型
     */
    GraphicsAPI getGraphicsAPI() const { return mGraphicsAPI; }
    
    /**
     * @brief 激活上下文（跨平台）
     */
    bool makeCurrent();
    
    /**
     * @brief 释放上下文
     */
    bool releaseCurrent();
    
    /**
     * @brief 检查上下文是否已初始化
     */
    bool isInitialized() const { return mInitialized; }
    
    // =========================================================================
    // Android特定接口
    // =========================================================================
    
#ifdef __ANDROID__
    /**
     * @brief 获取Android EGL管理器
     */
    AndroidEGLContextManager* getAndroidEGLManager() { return mAndroidEGLManager.get(); }
    
    /**
     * @brief 从OES纹理创建LRTexture（Android相机）
     * @param oesTextureId OES纹理ID
     * @param width 宽度
     * @param height 高度
     * @param transformMatrix 纹理变换矩阵
     * @return LRTexture包装对象
     */
    std::shared_ptr<lrengine::render::LRTexture> createTextureFromOES(
        uint32_t oesTextureId,
        uint32_t width,
        uint32_t height,
        const float* transformMatrix = nullptr);
#endif
    
    // =========================================================================
    // iOS特定接口
    // =========================================================================
    
#if defined(PIPELINE_PLATFORM_IOS)
    /**
     * @brief 获取iOS Metal管理器
     */
    IOSMetalContextManager* getIOSMetalManager() { return mIOSMetalManager.get(); }
    
    /**
     * @brief 从CVPixelBuffer创建LRTexture
     */
    std::shared_ptr<lrengine::render::LRTexture> createTextureFromPixelBuffer(
        CVPixelBufferRef pixelBuffer,
        lrengine::render::LRRenderContext* renderContext);
    
    /**
     * @brief 将LRTexture复制到CVPixelBuffer
     */
    bool copyTextureToPixelBuffer(
        std::shared_ptr<lrengine::render::LRTexture> texture,
        CVPixelBufferRef pixelBuffer);
#endif
    
    /**
     * @brief 销毁平台上下文
     */
    void destroy();
    
private:
    PlatformType mPlatformType = PlatformType::Unknown;
    GraphicsAPI mGraphicsAPI = GraphicsAPI::OpenGLES;
    bool mInitialized = false;
    
#ifdef __ANDROID__
    std::unique_ptr<AndroidEGLContextManager> mAndroidEGLManager;
#endif
    
#if defined(PIPELINE_PLATFORM_IOS)
    std::unique_ptr<IOSMetalContextManager> mIOSMetalManager;
#endif
};

} // namespace pipeline
