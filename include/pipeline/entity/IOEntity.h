/**
 * @file IOEntity.h
 * @brief 输入输出节点 - 管线的边界
 */

#pragma once

#include "ProcessEntity.h"
#include "GPUEntity.h"

// 前向声明
namespace lrengine {
namespace render {
class LRRenderContext;
class LRTexture;
class LRFrameBuffer;
class LRShaderProgram;
class LRVertexBuffer;
} // namespace render
} // namespace lrengine

namespace pipeline {

// =============================================================================
// InputEntity - 输入节点
// =============================================================================

/**
 * @brief 输入数据格式
 */
enum class InputFormat : uint8_t {
    RGBA,       // RGBA像素数据
    YUV420,     // YUV420平面数据
    NV12,       // NV12数据
    NV21,       // NV21数据
    OES         // Android OES纹理
};

/**
 * @brief 输入节点
 * 
 * 管线的入口，负责：
 * - 接收相机数据（YUV/OES格式）
 * - 转换为标准RGBA纹理
 * - 生成初始FramePacket
 * - 触发管线执行
 */
class InputEntity : public ProcessEntity {
public:
    /**
     * @brief 构造函数
     * @param name Entity名称
     */
    explicit InputEntity(const std::string& name = "InputEntity");
    
    ~InputEntity() override;
    
    // ==========================================================================
    // 类型信息
    // ==========================================================================
    
    EntityType getType() const override { return EntityType::Input; }
    ExecutionQueue getExecutionQueue() const override { return ExecutionQueue::IO; }
    
    // ==========================================================================
    // 渲染上下文
    // ==========================================================================
    
    /**
     * @brief 设置渲染上下文
     */
    void setRenderContext(lrengine::render::LRRenderContext* context);
    
    /**
     * @brief 获取渲染上下文
     */
    lrengine::render::LRRenderContext* getRenderContext() const { return mRenderContext; }
    
    // ==========================================================================
    // 数据输入接口
    // ==========================================================================
    
    /**
     * @brief 输入RGBA数据
     * @param data 像素数据
     * @param width 宽度
     * @param height 高度
     * @param stride 行步长（0表示使用width*4）
     * @param timestamp 时间戳（毫秒）
     * @return 生成的FramePacket
     */
    FramePacketPtr feedRGBA(const uint8_t* data, 
                           uint32_t width, 
                           uint32_t height,
                           uint32_t stride = 0,
                           uint64_t timestamp = 0);
    
    /**
     * @brief 输入YUV420数据
     * @param yData Y平面数据
     * @param uData U平面数据
     * @param vData V平面数据
     * @param width 宽度
     * @param height 高度
     * @param yStride Y平面行步长
     * @param uvStride UV平面行步长
     * @param timestamp 时间戳
     * @return 生成的FramePacket
     */
    FramePacketPtr feedYUV420(const uint8_t* yData,
                             const uint8_t* uData,
                             const uint8_t* vData,
                             uint32_t width,
                             uint32_t height,
                             uint32_t yStride = 0,
                             uint32_t uvStride = 0,
                             uint64_t timestamp = 0);
    
    /**
     * @brief 输入NV12/NV21数据
     * @param yData Y平面数据
     * @param uvData UV交织数据
     * @param width 宽度
     * @param height 高度
     * @param isNV21 是否为NV21格式
     * @param timestamp 时间戳
     * @return 生成的FramePacket
     */
    FramePacketPtr feedNV12(const uint8_t* yData,
                           const uint8_t* uvData,
                           uint32_t width,
                           uint32_t height,
                           bool isNV21 = false,
                           uint64_t timestamp = 0);
    
    /**
     * @brief 输入已有的GPU纹理
     * @param texture 纹理对象
     * @param width 宽度
     * @param height 高度
     * @param timestamp 时间戳
     * @return 生成的FramePacket
     */
    FramePacketPtr feedTexture(std::shared_ptr<lrengine::render::LRTexture> texture,
                              uint32_t width,
                              uint32_t height,
                              uint64_t timestamp = 0);
    
    /**
     * @brief 输入OES纹理（Android）
     * @param oesTextureId OES纹理ID
     * @param width 宽度
     * @param height 高度
     * @param transformMatrix 4x4纹理变换矩阵（可选）
     * @param timestamp 时间戳
     * @return 生成的FramePacket
     */
    FramePacketPtr feedOES(uint32_t oesTextureId,
                          uint32_t width,
                          uint32_t height,
                          const float* transformMatrix = nullptr,
                          uint64_t timestamp = 0);
    
    // ==========================================================================
    // 配置
    // ==========================================================================
    
    /**
     * @brief 设置输出格式
     */
    void setOutputFormat(PixelFormat format) { mOutputFormat = format; }
    
    /**
     * @brief 获取输出格式
     */
    PixelFormat getOutputFormat() const { return mOutputFormat; }
    
    /**
     * @brief 设置是否自动旋转
     */
    void setAutoRotate(bool autoRotate) { mAutoRotate = autoRotate; }
    
    /**
     * @brief 设置旋转角度
     * @param degrees 角度（0, 90, 180, 270）
     */
    void setRotation(int32_t degrees);
    
    /**
     * @brief 设置是否水平翻转
     */
    void setFlipHorizontal(bool flip) { mFlipHorizontal = flip; }
    
    /**
     * @brief 设置是否垂直翻转
     */
    void setFlipVertical(bool flip) { mFlipVertical = flip; }
    
protected:
    bool process(const std::vector<FramePacketPtr>& inputs,
                std::vector<FramePacketPtr>& outputs,
                PipelineContext& context) override;
    
    /**
     * @brief 创建YUV转换着色器
     */
    bool createYUVConversionShader();
    
    /**
     * @brief 创建OES转换着色器
     */
    bool createOESConversionShader();
    
    /**
     * @brief 执行YUV到RGBA的转换
     */
    std::shared_ptr<lrengine::render::LRTexture> convertYUVToRGBA(
        const uint8_t* yData, const uint8_t* uData, const uint8_t* vData,
        uint32_t width, uint32_t height,
        uint32_t yStride, uint32_t uvStride);
    
    /**
     * @brief 执行OES到RGBA的转换
     */
    std::shared_ptr<lrengine::render::LRTexture> convertOESToRGBA(
        uint32_t oesTextureId,
        uint32_t width, uint32_t height,
        const float* transformMatrix);
    
private:
    lrengine::render::LRRenderContext* mRenderContext = nullptr;
    
    // 当前输入的数据包（用于process）
    FramePacketPtr mPendingPacket;
    std::mutex mPendingMutex;
    
    // 输出配置
    PixelFormat mOutputFormat = PixelFormat::RGBA8;
    bool mAutoRotate = false;
    int32_t mRotation = 0;
    bool mFlipHorizontal = false;
    bool mFlipVertical = false;
    
    // YUV转换资源
    std::shared_ptr<lrengine::render::LRShaderProgram> mYUVShader;
    std::shared_ptr<lrengine::render::LRFrameBuffer> mConversionFBO;
    std::shared_ptr<lrengine::render::LRTexture> mYTexture;
    std::shared_ptr<lrengine::render::LRTexture> mUTexture;
    std::shared_ptr<lrengine::render::LRTexture> mVTexture;
    std::shared_ptr<lrengine::render::LRTexture> mUVTexture; // for NV12
    
    // OES转换资源
    std::shared_ptr<lrengine::render::LRShaderProgram> mOESShader;
    
    // 渲染资源
    std::shared_ptr<lrengine::render::LRVertexBuffer> mQuadVBO;
    
    // 帧计数
    std::atomic<uint64_t> mFrameCounter{0};
};

// =============================================================================
// OutputEntity - 输出节点
// =============================================================================

/**
 * @brief 输出目标类型
 */
enum class OutputTarget : uint8_t {
    Display,     // 显示到屏幕
    Encoder,     // 输出到编码器
    Callback,    // 回调给应用
    Texture,     // 输出为纹理
    File         // 保存到文件
};

/**
 * @brief 输出节点
 * 
 * 管线的出口，负责：
 * - 渲染到屏幕显示
 * - 输出到视频编码器
 * - 回调给应用层
 */
class OutputEntity : public ProcessEntity {
public:
    /**
     * @brief 构造函数
     * @param name Entity名称
     */
    explicit OutputEntity(const std::string& name = "OutputEntity");
    
    ~OutputEntity() override;
    
    // ==========================================================================
    // 类型信息
    // ==========================================================================
    
    EntityType getType() const override { return EntityType::Output; }
    ExecutionQueue getExecutionQueue() const override { return ExecutionQueue::IO; }
    
    // ==========================================================================
    // 渲染上下文
    // ==========================================================================
    
    /**
     * @brief 设置渲染上下文
     */
    void setRenderContext(lrengine::render::LRRenderContext* context);
    
    // ==========================================================================
    // 输出目标配置
    // ==========================================================================
    
    /**
     * @brief 设置输出目标
     */
    void setOutputTarget(OutputTarget target) { mOutputTarget = target; }
    
    /**
     * @brief 获取输出目标
     */
    OutputTarget getOutputTarget() const { return mOutputTarget; }
    
    /**
     * @brief 设置显示Surface（用于Display目标）
     * @param surface 平台特定的Surface对象
     */
    void setDisplaySurface(void* surface);
    
    /**
     * @brief 设置编码器Surface（用于Encoder目标）
     * @param surface 编码器输入Surface
     */
    void setEncoderSurface(void* surface);
    
    /**
     * @brief 设置帧回调（用于Callback目标）
     * @param callback 回调函数
     */
    void setFrameCallback(FrameCallback callback);
    
    /**
     * @brief 设置输出文件路径（用于File目标）
     */
    void setOutputFilePath(const std::string& path);
    
    // ==========================================================================
    // 显示配置
    // ==========================================================================
    
    /**
     * @brief 设置视口
     * @param x X坐标
     * @param y Y坐标
     * @param width 宽度
     * @param height 高度
     */
    void setViewport(int32_t x, int32_t y, int32_t width, int32_t height);
    
    /**
     * @brief 设置缩放模式
     */
    enum class ScaleMode : uint8_t {
        Fit,        // 适应（保持比例，可能有黑边）
        Fill,       // 填充（保持比例，可能裁剪）
        Stretch     // 拉伸（可能变形）
    };
    void setScaleMode(ScaleMode mode) { mScaleMode = mode; }
    
    /**
     * @brief 设置背景颜色
     */
    void setBackgroundColor(float r, float g, float b, float a = 1.0f);
    
    // ==========================================================================
    // 输出数据获取
    // ==========================================================================
    
    /**
     * @brief 获取最后输出的数据包
     */
    FramePacketPtr getLastOutput() const;
    
    /**
     * @brief 获取最后输出的纹理
     */
    std::shared_ptr<lrengine::render::LRTexture> getOutputTexture() const;
    
    /**
     * @brief 读取输出像素数据
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return 实际读取的字节数
     */
    size_t readPixels(uint8_t* buffer, size_t bufferSize);
    
protected:
    bool process(const std::vector<FramePacketPtr>& inputs,
                std::vector<FramePacketPtr>& outputs,
                PipelineContext& context) override;
    
    /**
     * @brief 渲染到显示
     */
    bool renderToDisplay(FramePacketPtr input);
    
    /**
     * @brief 渲染到编码器
     */
    bool renderToEncoder(FramePacketPtr input);
    
    /**
     * @brief 执行帧回调
     */
    void executeCallback(FramePacketPtr input);
    
    /**
     * @brief 保存到文件
     */
    bool saveToFile(FramePacketPtr input);
    
private:
    lrengine::render::LRRenderContext* mRenderContext = nullptr;
    
    // 输出配置
    OutputTarget mOutputTarget = OutputTarget::Display;
    void* mDisplaySurface = nullptr;
    void* mEncoderSurface = nullptr;
    FrameCallback mFrameCallback;
    std::string mOutputFilePath;
    
    // 显示配置
    int32_t mViewportX = 0;
    int32_t mViewportY = 0;
    int32_t mViewportWidth = 0;
    int32_t mViewportHeight = 0;
    ScaleMode mScaleMode = ScaleMode::Fit;
    float mBackgroundColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    
    // 渲染资源
    std::shared_ptr<lrengine::render::LRShaderProgram> mDisplayShader;
    std::shared_ptr<lrengine::render::LRVertexBuffer> mDisplayQuadVBO;
    std::shared_ptr<lrengine::render::LRFrameBuffer> mEncoderFBO;
    
    // 最后输出
    mutable std::mutex mLastOutputMutex;
    FramePacketPtr mLastOutput;
};

} // namespace pipeline
