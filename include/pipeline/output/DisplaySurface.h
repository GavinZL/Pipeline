/**
 * @file DisplaySurface.h
 * @brief 显示表面抽象 - 跨平台显示输出接口
 * 
 * DisplaySurface 提供统一的显示输出抽象：
 * - Android: 通过 EGL/ANativeWindow 实现
 * - iOS: 通过 CAMetalLayer/CAEAGLLayer 实现
 */

#pragma once

#include "pipeline/output/OutputConfig.h"
#include <memory>
#include <cstdint>

// 前向声明 LREngine 类型
namespace lrengine {
namespace render {
class LRTexture;
class LRRenderContext;
class LRFrameBuffer;
} // namespace render
} // namespace lrengine

namespace pipeline {
namespace output {

/**
 * @brief 显示表面状态
 */
enum class SurfaceState : uint8_t {
    Uninitialized,  ///< 未初始化
    Ready,          ///< 就绪
    Rendering,      ///< 渲染中
    Paused,         ///< 已暂停
    Error           ///< 错误状态
};

/**
 * @brief 表面尺寸信息
 */
struct SurfaceSize {
    uint32_t width = 0;
    uint32_t height = 0;
    float scaleFactor = 1.0f;   ///< 设备像素比（retina）
    
    uint32_t physicalWidth() const { 
        return static_cast<uint32_t>(width * scaleFactor); 
    }
    uint32_t physicalHeight() const { 
        return static_cast<uint32_t>(height * scaleFactor); 
    }
};

/**
 * @brief 显示表面抽象基类
 * 
 * 子类实现：
 * - AndroidEGLSurface: Android EGL 表面
 * - iOSMetalSurface: iOS Metal 表面
 * - iOSGLESSurface: iOS OpenGL ES 表面
 * 
 * 使用示例：
 * @code
 * // Android
 * auto surface = std::make_shared<AndroidEGLSurface>();
 * surface->attachToWindow(nativeWindow);
 * surface->initialize(renderContext);
 * 
 * // iOS
 * auto surface = std::make_shared<iOSMetalSurface>();
 * surface->attachToLayer(metalLayer);
 * surface->initialize(renderContext);
 * 
 * // 渲染
 * surface->beginFrame();
 * surface->renderTexture(texture, displayConfig);
 * surface->endFrame();
 * @endcode
 */
class DisplaySurface {
public:
    virtual ~DisplaySurface() = default;
    
    // ==========================================================================
    // 生命周期
    // ==========================================================================
    
    /**
     * @brief 初始化显示表面
     * @param context 渲染上下文
     * @return 是否成功
     */
    virtual bool initialize(lrengine::render::LRRenderContext* context) = 0;
    
    /**
     * @brief 释放资源
     */
    virtual void release() = 0;
    
    /**
     * @brief 暂停渲染
     */
    virtual void pause() { mState = SurfaceState::Paused; }
    
    /**
     * @brief 恢复渲染
     */
    virtual void resume() { 
        if (mState == SurfaceState::Paused) {
            mState = SurfaceState::Ready;
        }
    }
    
    // ==========================================================================
    // 表面绑定
    // ==========================================================================
    
    /**
     * @brief 绑定到原生窗口（Android）
     * @param window ANativeWindow 指针
     * @return 是否成功
     */
    virtual bool attachToWindow(void* window) { return false; }
    
    /**
     * @brief 绑定到 CALayer（iOS）
     * @param layer CAMetalLayer/CAEAGLLayer 指针
     * @return 是否成功
     */
    virtual bool attachToLayer(void* layer) { return false; }
    
    /**
     * @brief 解除绑定
     */
    virtual void detach() = 0;
    
    /**
     * @brief 检查是否已绑定
     */
    virtual bool isAttached() const = 0;
    
    // ==========================================================================
    // 尺寸管理
    // ==========================================================================
    
    /**
     * @brief 获取表面尺寸
     */
    virtual SurfaceSize getSize() const = 0;
    
    /**
     * @brief 设置表面尺寸
     * @param width 宽度
     * @param height 高度
     */
    virtual void setSize(uint32_t width, uint32_t height) = 0;
    
    /**
     * @brief 处理尺寸变化
     * 
     * 当窗口/图层尺寸变化时调用。
     */
    virtual void onSizeChanged(uint32_t width, uint32_t height) {}
    
    // ==========================================================================
    // 渲染帧
    // ==========================================================================
    
    /**
     * @brief 开始帧渲染
     * @return 是否成功
     */
    virtual bool beginFrame() = 0;
    
    /**
     * @brief 渲染纹理到表面
     * @param texture 源纹理
     * @param config 显示配置
     * @return 是否成功
     */
    virtual bool renderTexture(
        std::shared_ptr<lrengine::render::LRTexture> texture,
        const DisplayConfig& config) = 0;
    
    /**
     * @brief 结束帧渲染并呈现
     * @return 是否成功
     */
    virtual bool endFrame() = 0;
    
    /**
     * @brief 等待 GPU 完成
     */
    virtual void waitGPU() {}
    
    // ==========================================================================
    // 状态查询
    // ==========================================================================
    
    /**
     * @brief 获取当前状态
     */
    SurfaceState getState() const { return mState; }
    
    /**
     * @brief 检查是否就绪
     */
    bool isReady() const { return mState == SurfaceState::Ready; }
    
    /**
     * @brief 获取帧缓冲
     */
    virtual std::shared_ptr<lrengine::render::LRFrameBuffer> getFrameBuffer() const {
        return nullptr;
    }
    
    // ==========================================================================
    // 配置
    // ==========================================================================
    
    /**
     * @brief 设置显示配置
     */
    void setDisplayConfig(const DisplayConfig& config) { mDisplayConfig = config; }
    
    /**
     * @brief 获取显示配置
     */
    const DisplayConfig& getDisplayConfig() const { return mDisplayConfig; }
    
    /**
     * @brief 设置 VSync
     */
    virtual void setVSyncEnabled(bool enabled) { mVSyncEnabled = enabled; }
    
    /**
     * @brief 检查 VSync 是否启用
     */
    bool isVSyncEnabled() const { return mVSyncEnabled; }
    
    /**
     * @brief 获取渲染上下文
     */
    lrengine::render::LRRenderContext* getRenderContext() const { return mRenderContext; }
    
protected:
    SurfaceState mState = SurfaceState::Uninitialized;
    DisplayConfig mDisplayConfig;
    bool mVSyncEnabled = true;
    lrengine::render::LRRenderContext* mRenderContext = nullptr;
};

using DisplaySurfacePtr = std::shared_ptr<DisplaySurface>;

// =============================================================================
// 平台工厂
// =============================================================================

/**
 * @brief 创建平台特定的显示表面
 * @return 显示表面实例
 */
DisplaySurfacePtr createPlatformDisplaySurface();

} // namespace output
} // namespace pipeline
