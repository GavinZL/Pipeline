/**
 * @file OESTextureInputStrategy.h
 * @brief Android OES 纹理输入策略
 * 
 * 处理 Android 相机的 OES 纹理输入：
 * - 将 OES 纹理转换为标准 2D 纹理
 * - 支持纹理变换矩阵处理
 * - 支持回读到 CPU 内存
 */

#pragma once

#include "pipeline/input/InputEntity.h"
#include "pipeline/platform/PlatformContext.h"

#ifdef __ANDROID__

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

namespace pipeline {
namespace input {
namespace android {

/**
 * @brief OES 纹理输入策略
 * 
 * 适用于 Android 相机预览的 SurfaceTexture。
 * 
 * 使用示例：
 * @code
 * auto strategy = std::make_shared<OESTextureInputStrategy>();
 * strategy->initialize(renderContext);
 * 
 * inputEntity->setInputStrategy(strategy);
 * 
 * // 相机回调中
 * inputEntity->submitOESTexture(oesTextureId, width, height, 
 *                                transformMatrix, timestamp);
 * @endcode
 */
class OESTextureInputStrategy : public InputStrategy {
public:
    OESTextureInputStrategy();
    ~OESTextureInputStrategy() override;
    
    // ==========================================================================
    // InputStrategy 接口实现
    // ==========================================================================
    
    bool initialize(lrengine::render::LRRenderContext* context) override;
    
    bool processToGPU(const InputData& input,
                      lrengine::LRTexturePtr& outputTexture) override;
    
    bool processToCPU(const InputData& input,
                      uint8_t* outputBuffer,
                      size_t& outputSize,
                      uint32_t targetWidth = 0,
                      uint32_t targetHeight = 0) override;
    
    void release() override;
    
    const char* getName() const override { return "OESTextureInputStrategy"; }
    
    // ==========================================================================
    // Android 特定配置
    // ==========================================================================
    
    /**
     * @brief 设置 EGL 上下文管理器
     */
    void setEGLContextManager(AndroidEGLContextManager* manager);
    
    /**
     * @brief 设置是否需要回读到 CPU
     */
    void setNeedCPUReadback(bool need) { mNeedCPUReadback = need; }
    
    /**
     * @brief 设置输出格式（用于 CPU 回读）
     */
    void setOutputFormat(InputFormat format) { mOutputFormat = format; }
    
private:
    // 初始化 OES 转换 shader
    bool initializeOESShader();
    
    // 创建 FBO 用于渲染
    bool initializeFBO(uint32_t width, uint32_t height);
    
    // OES 纹理转换为 2D 纹理
    bool convertOESToTexture2D(uint32_t oesTextureId,
                               uint32_t width, uint32_t height,
                               const float* transformMatrix);
    
    // 从 FBO 回读像素
    bool readbackPixels(uint8_t* buffer, size_t bufferSize,
                        uint32_t width, uint32_t height);
    
    // 清理 GPU 资源
    void cleanupGPUResources();
    
private:
    lrengine::render::LRRenderContext* mRenderContext = nullptr;
    AndroidEGLContextManager* mEGLManager = nullptr;
    
    // Shader 资源
    GLuint mOESShaderProgram = 0;
    GLint mOESTextureLocation = -1;
    GLint mTransformMatrixLocation = -1;
    GLint mPositionLocation = -1;
    GLint mTexCoordLocation = -1;
    
    // FBO 资源
    GLuint mFBO = 0;
    GLuint mOutputTexture = 0;
    uint32_t mFBOWidth = 0;
    uint32_t mFBOHeight = 0;
    
    // VAO/VBO
    GLuint mVAO = 0;
    GLuint mVBO = 0;
    
    // 配置
    bool mNeedCPUReadback = false;
    InputFormat mOutputFormat = InputFormat::RGBA;
    
    // 输出纹理包装
    lrengine::LRTexturePtr mOutputTextureWrapper;
    
    // PBO 用于异步回读
    GLuint mPBO = 0;
    size_t mPBOSize = 0;
    
    bool mInitialized = false;
};

using OESTextureInputStrategyPtr = std::shared_ptr<OESTextureInputStrategy>;

} // namespace android
} // namespace input
} // namespace pipeline

#endif // __ANDROID__
