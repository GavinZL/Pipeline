/**
 * @file PixelBufferInputStrategy.h
 * @brief iOS CVPixelBuffer 输入策略
 * 
 * 处理 iOS 相机的 CVPixelBuffer 输入：
 * - 零拷贝映射到 Metal 纹理
 * - 支持 NV12/BGRA 格式
 * - 支持 CPU 数据访问
 */

#pragma once

#include "pipeline/input/InputEntity.h"
#include "pipeline/platform/PlatformContext.h"

#if defined(__APPLE__)

#include <CoreVideo/CoreVideo.h>

namespace pipeline {
namespace input {
namespace ios {

/**
 * @brief CVPixelBuffer 输入策略
 * 
 * 适用于 iOS/macOS 相机 AVCaptureVideoDataOutput。
 * 利用 CVMetalTextureCache 实现零拷贝。
 * 
 * 使用示例：
 * @code
 * auto strategy = std::make_shared<PixelBufferInputStrategy>();
 * strategy->setMetalContextManager(metalManager);
 * strategy->initialize(renderContext);
 * 
 * inputEntity->setInputStrategy(strategy);
 * 
 * // 相机回调中 (captureOutput:didOutputSampleBuffer:)
 * CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
 * strategy->submitPixelBuffer(pixelBuffer, timestamp);
 * @endcode
 */
class PixelBufferInputStrategy : public InputStrategy {
public:
    PixelBufferInputStrategy();
    ~PixelBufferInputStrategy() override;
    
    // ==========================================================================
    // InputStrategy 接口实现
    // ==========================================================================
    
    bool initialize(lrengine::render::LRRenderContext* context) override;
    
    bool processToGPU(const InputData& input,
                      lrengine::LRTexturePtr& outputTexture) override;
    
    bool processToGPUPlanar(const InputData& input,
                            std::shared_ptr<lrengine::render::LRPlanarTexture>& outputTexture) override;
    
    bool processToCPU(const InputData& input,
                      uint8_t* outputBuffer,
                      size_t& outputSize,
                      uint32_t targetWidth = 0,
                      uint32_t targetHeight = 0) override;
    
    void release() override;
    
    const char* getName() const override { return "PixelBufferInputStrategy"; }
    
    // ==========================================================================
    // iOS 特定接口
    // ==========================================================================
    
    /**
     * @brief 设置 Metal 上下文管理器
     */
    void setMetalContextManager(IOSMetalContextManager* manager);
    
    /**
     * @brief 直接提交 CVPixelBuffer
     * @param pixelBuffer CVPixelBuffer
     * @param timestamp 时间戳（微秒）
     * @return 是否成功
     */
    bool submitPixelBuffer(CVPixelBufferRef pixelBuffer, int64_t timestamp);
    
    /**
     * @brief 设置是否使用纹理缓存（零拷贝）
     */
    void setUseTextureCache(bool use) { mUseTextureCache = use; }
    
    /**
     * @brief 设置输出颜色空间
     */
    void setOutputColorSpace(bool isBT709) { mUseBT709 = isBT709; }
    
private:
    // 从 CVPixelBuffer 创建 Metal 纹理（零拷贝）
    bool createMetalTextureFromPixelBuffer(CVPixelBufferRef pixelBuffer);
    
    // 从 CVPixelBuffer 读取 CPU 数据
    bool readCPUDataFromPixelBuffer(CVPixelBufferRef pixelBuffer,
                                     uint8_t* outputBuffer,
                                     size_t& outputSize,
                                     uint32_t targetWidth = 0,
                                     uint32_t targetHeight = 0);
    
    // 获取 PixelBuffer 格式信息
    bool getPixelBufferInfo(CVPixelBufferRef pixelBuffer,
                            uint32_t& width, uint32_t& height,
                            OSType& pixelFormat);
    
private:
    lrengine::render::LRRenderContext* mRenderContext = nullptr;
    IOSMetalContextManager* mMetalManager = nullptr;
    
    // 当前处理的 PixelBuffer
    CVPixelBufferRef mCurrentPixelBuffer = nullptr;
    int64_t mCurrentTimestamp = 0;
    
    // 输出纹理
    std::shared_ptr<lrengine::render::LRPlanarTexture> mOutputTexture;
    
    // 配置
    bool mUseTextureCache = true;
    bool mUseBT709 = true;
    
    bool mInitialized = false;
};

using PixelBufferInputStrategyPtr = std::shared_ptr<PixelBufferInputStrategy>;

} // namespace ios
} // namespace input
} // namespace pipeline

#endif // __APPLE__
