/**
 * @file iOSMetalSurface.h
 * @brief iOS Metal 显示表面
 * 
 * 通过 CAMetalLayer 实现 iOS/macOS Metal 显示输出。
 */

#pragma once

#include "pipeline/output/DisplaySurface.h"
#include "pipeline/platform/PlatformContext.h"

#if defined(__APPLE__)

namespace pipeline {
namespace output {
namespace ios {

/**
 * @brief iOS Metal 显示表面
 * 
 * 使用 CAMetalLayer 渲染 Metal 纹理。
 * 
 * 使用示例：
 * @code
 * auto surface = std::make_shared<iOSMetalSurface>();
 * surface->setMetalContextManager(metalManager);
 * surface->attachToLayer(metalLayer);  // CAMetalLayer
 * surface->initialize(renderContext);
 * 
 * // 渲染循环
 * surface->beginFrame();
 * surface->renderTexture(texture, config);
 * surface->endFrame();
 * @endcode
 */
class iOSMetalSurface : public DisplaySurface {
public:
    iOSMetalSurface();
    ~iOSMetalSurface() override;
    
    // ==========================================================================
    // DisplaySurface 接口实现
    // ==========================================================================
    
    bool initialize(lrengine::render::LRRenderContext* context) override;
    void release() override;
    
    bool attachToLayer(void* layer) override;
    void detach() override;
    bool isAttached() const override { return mMetalLayer != nullptr; }
    
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
    // iOS 特定接口
    // ==========================================================================
    
    /**
     * @brief 设置 Metal 上下文管理器
     */
    void setMetalContextManager(IOSMetalContextManager* manager);
    
    /**
     * @brief 获取 Metal Layer
     */
    void* getMetalLayer() const { return mMetalLayer; }
    
    /**
     * @brief 设置像素格式
     */
    void setPixelFormat(uint32_t format) { mPixelFormat = format; }
    
    /**
     * @brief 设置颜色空间
     */
    void setColorSpace(void* colorSpace);
    
private:
    // 配置 Metal Layer
    bool configureMetalLayer();
    
    // 获取下一个 drawable
    bool acquireNextDrawable();
    
    // 创建渲染管线
    bool createRenderPipeline();
    
    // 清理资源
    void cleanupResources();
    
private:
    IOSMetalContextManager* mMetalManager = nullptr;
    
    // Metal Layer
    void* mMetalLayer = nullptr;  // CAMetalLayer*
    
    // Metal 设备和命令队列
    void* mDevice = nullptr;       // id<MTLDevice>
    void* mCommandQueue = nullptr; // id<MTLCommandQueue>
    
    // 渲染管线
    void* mRenderPipelineState = nullptr;  // id<MTLRenderPipelineState>
    void* mSamplerState = nullptr;         // id<MTLSamplerState>
    void* mVertexBuffer = nullptr;         // id<MTLBuffer>
    
    // 当前帧
    void* mCurrentDrawable = nullptr;      // id<CAMetalDrawable>
    void* mCurrentCommandBuffer = nullptr; // id<MTLCommandBuffer>
    
    // 表面尺寸
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    float mScaleFactor = 1.0f;
    
    // 配置
    uint32_t mPixelFormat = 0;  // MTLPixelFormat
    void* mColorSpace = nullptr; // CGColorSpaceRef
    
    bool mResourcesInitialized = false;
};

using iOSMetalSurfacePtr = std::shared_ptr<iOSMetalSurface>;

} // namespace ios
} // namespace output
} // namespace pipeline

#endif // __APPLE__
