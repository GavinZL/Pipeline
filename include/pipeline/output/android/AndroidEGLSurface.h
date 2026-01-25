/**
 * @file AndroidEGLSurface.h
 * @brief Android EGL 显示表面
 * 
 * 通过 EGL 和 ANativeWindow 实现 Android 显示输出。
 */

#pragma once

#include "pipeline/output/DisplaySurface.h"
#include "pipeline/platform/PlatformContext.h"

#ifdef __ANDROID__

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>

namespace pipeline {
namespace output {
namespace android {

/**
 * @brief Android EGL 显示表面
 * 
 * 使用 EGL window surface 渲染到 ANativeWindow。
 * 
 * 使用示例：
 * @code
 * auto surface = std::make_shared<AndroidEGLSurface>();
 * surface->setEGLContextManager(eglManager);
 * surface->attachToWindow(nativeWindow);
 * surface->initialize(renderContext);
 * 
 * // 渲染循环
 * surface->beginFrame();
 * surface->renderTexture(texture, config);
 * surface->endFrame();
 * @endcode
 */
class AndroidEGLSurface : public DisplaySurface {
public:
    AndroidEGLSurface();
    ~AndroidEGLSurface() override;
    
    // ==========================================================================
    // DisplaySurface 接口实现
    // ==========================================================================
    
    bool initialize(lrengine::render::LRRenderContext* context) override;
    void release() override;
    
    bool attachToWindow(void* window) override;
    void detach() override;
    bool isAttached() const override { return mNativeWindow != nullptr; }
    
    SurfaceSize getSize() const override;
    void setSize(uint32_t width, uint32_t height) override;
    void onSizeChanged(uint32_t width, uint32_t height) override;
    
    bool beginFrame() override;
    bool renderTexture(std::shared_ptr<lrengine::render::LRTexture> texture,
                       const DisplayConfig& config) override;
    bool endFrame() override;
    
    void waitGPU() override;
    void setVSyncEnabled(bool enabled) override;
    
    // ==========================================================================
    // Android 特定接口
    // ==========================================================================
    
    /**
     * @brief 设置 EGL 上下文管理器
     */
    void setEGLContextManager(AndroidEGLContextManager* manager);
    
    /**
     * @brief 获取 EGL Surface
     */
    EGLSurface getEGLSurface() const { return mEGLSurface; }
    
    /**
     * @brief 设置共享上下文模式
     */
    void setSharedContextMode(bool shared) { mUseSharedContext = shared; }
    
private:
    // 创建 EGL window surface
    bool createEGLWindowSurface();
    
    // 销毁 EGL surface
    void destroyEGLSurface();
    
    // 初始化渲染资源
    bool initializeRenderResources();
    
    // 清理渲染资源
    void cleanupRenderResources();
    
    // 绘制纹理到屏幕
    bool drawTextureToScreen(GLuint textureId, const DisplayConfig& config);
    
private:
    AndroidEGLContextManager* mEGLManager = nullptr;
    ANativeWindow* mNativeWindow = nullptr;
    
    // EGL 资源
    EGLDisplay mEGLDisplay = EGL_NO_DISPLAY;
    EGLSurface mEGLSurface = EGL_NO_SURFACE;
    EGLConfig mEGLConfig = nullptr;
    
    // 表面尺寸
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    
    // 渲染资源
    GLuint mDisplayShaderProgram = 0;
    GLint mTextureLocation = -1;
    GLint mTransformLocation = -1;
    GLuint mVAO = 0;
    GLuint mVBO = 0;
    
    // 配置
    bool mUseSharedContext = true;
    
    // 状态
    bool mResourcesInitialized = false;
};

using AndroidEGLSurfacePtr = std::shared_ptr<AndroidEGLSurface>;

} // namespace android
} // namespace output
} // namespace pipeline

#endif // __ANDROID__
