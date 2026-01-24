/**
 * @file OutputEntityExt.h
 * @brief 扩展的输出实体 - 支持多种输出目标和平台特定格式
 * 
 * 功能扩展：
 * 1. 多输出目标支持（显示/编码/回调/文件/纹理）
 * 2. 平台特定输出（CVPixelBuffer/SurfaceTexture）
 * 3. 硬件编码器集成
 * 4. 多路输出支持（同时输出到多个目标）
 */

#pragma once

#include "ProcessEntity.h"
#include "IOEntity.h"
#include <vector>
#include <functional>
#include <string>
#include <unordered_map>

// 前向声明
namespace lrengine {
namespace render {
class LRRenderContext;
class LRTexture;
class LRFrameBuffer;
} // namespace render
} // namespace lrengine

namespace pipeline {

class PlatformContext;

// =============================================================================
// 输出目标配置
// =============================================================================

/**
 * @brief 输出目标类型（扩展版本）
 */
enum class OutputTargetType : uint8_t {
    Display,        // 显示到屏幕
    Encoder,        // 输出到编码器（硬件/软件）
    Callback,       // 回调给应用层
    Texture,        // 输出为GPU纹理（用于后续处理）
    File,           // 保存到文件
    PixelBuffer,    // iOS CVPixelBuffer输出
    SurfaceTexture, // Android SurfaceTexture输出
    Custom          // 自定义输出（用户扩展）
};

/**
 * @brief 编码器类型
 */
enum class EncoderType : uint8_t {
    Software,       // 软件编码器
    Hardware,       // 硬件编码器
    MediaCodec,     // Android MediaCodec
    VideoToolbox    // iOS VideoToolbox
};

/**
 * @brief 输出数据格式
 */
enum class OutputDataFormat : uint8_t {
    RGBA8,          // 标准RGBA8
    BGRA8,          // BGRA8（Metal默认）
    NV12,           // NV12（编码器常用）
    NV21,           // NV21（Android）
    YUV420P,        // YUV420 Planar
    Texture         // 保持GPU纹理
};

/**
 * @brief 显示输出配置
 */
struct DisplayOutputConfig {
    void* surface = nullptr;                    // 平台Surface（ANativeWindow/CAMetalLayer）
    int32_t x = 0;                              // 视口X
    int32_t y = 0;                              // 视口Y
    int32_t width = 0;                          // 视口宽度
    int32_t height = 0;                         // 视口高度
    OutputEntity::ScaleMode scaleMode = OutputEntity::ScaleMode::Fit;
    float backgroundColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool vsync = true;
};

/**
 * @brief 编码器输出配置
 */
struct EncoderOutputConfig {
    EncoderType encoderType = EncoderType::Hardware;
    void* encoderSurface = nullptr;             // 编码器输入Surface
    void* encoderHandle = nullptr;              // 编码器句柄
    OutputDataFormat dataFormat = OutputDataFormat::NV12;
    int32_t bitrate = 5000000;                  // 比特率
    int32_t fps = 30;                           // 帧率
    bool useHardwareBuffer = true;              // 使用硬件缓冲区
};

/**
 * @brief 回调输出配置
 */
struct CallbackOutputConfig {
    FrameCallback frameCallback;                // 帧回调
    OutputDataFormat dataFormat = OutputDataFormat::RGBA8;
    bool asyncCallback = true;                  // 异步回调
    int32_t maxPendingFrames = 3;               // 最大待处理帧数
};

/**
 * @brief 纹理输出配置
 */
struct TextureOutputConfig {
    bool shareTexture = true;                   // 共享纹理（避免复制）
    bool keepOnGPU = true;                      // 保持在GPU上
    PixelFormat textureFormat = PixelFormat::RGBA8;
};

/**
 * @brief 文件输出配置
 */
struct FileOutputConfig {
    std::string filePath;                       // 输出文件路径
    std::string fileFormat = "png";             // 文件格式（png/jpg/raw）
    int32_t quality = 95;                       // 质量（JPEG）
    bool appendTimestamp = true;                // 文件名添加时间戳
    int32_t maxFiles = 100;                     // 最大文件数
};

/**
 * @brief 平台特定输出配置
 */
struct PlatformOutputConfig {
#if defined(__APPLE__)
    void* pixelBufferPool = nullptr;            // CVPixelBufferPoolRef（iOS）
    void* metalDevice = nullptr;                // MTLDevice（iOS/macOS）
#endif
    
#ifdef __ANDROID__
    void* surfaceTexture = nullptr;             // Android SurfaceTexture
    int32_t oesTextureId = 0;                   // OES纹理ID
#endif
};

/**
 * @brief 统一输出配置
 */
struct OutputConfig {
    OutputTargetType targetType = OutputTargetType::Display;
    bool enabled = true;
    
    // 各类型特定配置
    DisplayOutputConfig displayConfig;
    EncoderOutputConfig encoderConfig;
    CallbackOutputConfig callbackConfig;
    TextureOutputConfig textureConfig;
    FileOutputConfig fileConfig;
    PlatformOutputConfig platformConfig;
    
    // 自定义输出回调
    std::function<bool(FramePacketPtr)> customOutputFunc;
};

// =============================================================================
// 扩展输出实体
// =============================================================================

/**
 * @brief 扩展输出实体
 * 
 * 相比基础OutputEntity，增加了：
 * 1. 多输出目标同时支持
 * 2. 平台特定格式输出
 * 3. 硬件编码器集成
 * 4. 灵活的数据格式转换
 */
class OutputEntityExt : public ProcessEntity {
public:
    /**
     * @brief 构造函数
     * @param name Entity名称
     */
    explicit OutputEntityExt(const std::string& name = "OutputEntityExt");
    
    ~OutputEntityExt() override;
    
    // ==========================================================================
    // 类型信息
    // ==========================================================================
    
    EntityType getType() const override { return EntityType::Output; }
    ExecutionQueue getExecutionQueue() const override { return ExecutionQueue::IO; }
    
    // ==========================================================================
    // 初始化
    // ==========================================================================
    
    /**
     * @brief 设置渲染上下文
     */
    void setRenderContext(lrengine::render::LRRenderContext* context);
    
    /**
     * @brief 设置平台上下文
     */
    void setPlatformContext(PlatformContext* platformContext);
    
    // ==========================================================================
    // 输出目标管理
    // ==========================================================================
    
    /**
     * @brief 添加输出目标
     * @param config 输出配置
     * @return 输出目标ID
     */
    int32_t addOutputTarget(const OutputConfig& config);
    
    /**
     * @brief 移除输出目标
     * @param targetId 目标ID
     * @return 是否成功
     */
    bool removeOutputTarget(int32_t targetId);
    
    /**
     * @brief 更新输出目标配置
     * @param targetId 目标ID
     * @param config 新配置
     * @return 是否成功
     */
    bool updateOutputTarget(int32_t targetId, const OutputConfig& config);
    
    /**
     * @brief 启用/禁用输出目标
     * @param targetId 目标ID
     * @param enabled 是否启用
     */
    void setOutputTargetEnabled(int32_t targetId, bool enabled);
    
    /**
     * @brief 获取所有输出目标
     */
    std::vector<int32_t> getOutputTargets() const;
    
    /**
     * @brief 清除所有输出目标
     */
    void clearOutputTargets();
    
    // ==========================================================================
    // 快捷配置方法
    // ==========================================================================
    
    /**
     * @brief 配置显示输出
     * @param surface 平台Surface
     * @param width 视口宽度
     * @param height 视口高度
     * @return 输出目标ID
     */
    int32_t setupDisplayOutput(void* surface, int32_t width, int32_t height);
    
    /**
     * @brief 更新显示输出尺寸
     * @param targetId 目标ID
     * @param width 新宽度
     * @param height 新高度
     * @return 是否成功
     */
    bool updateDisplayOutputSize(int32_t targetId, int32_t width, int32_t height);
    
    /**
     * @brief 配置编码器输出
     * @param encoderSurface 编码器Surface
     * @param encoderType 编码器类型
     * @return 输出目标ID
     */
    int32_t setupEncoderOutput(void* encoderSurface, EncoderType encoderType = EncoderType::Hardware);
    
    /**
     * @brief 配置回调输出
     * @param callback 帧回调函数
     * @param dataFormat 数据格式
     * @return 输出目标ID
     */
    int32_t setupCallbackOutput(FrameCallback callback, OutputDataFormat dataFormat = OutputDataFormat::RGBA8);
    
    /**
     * @brief 配置纹理输出
     * @param shareTexture 是否共享纹理
     * @return 输出目标ID
     */
    int32_t setupTextureOutput(bool shareTexture = true);
    
    /**
     * @brief 配置文件输出
     * @param filePath 文件路径
     * @param fileFormat 文件格式
     * @return 输出目标ID
     */
    int32_t setupFileOutput(const std::string& filePath, const std::string& fileFormat = "png");
    
#if defined(__APPLE__)
    /**
     * @brief 配置CVPixelBuffer输出（iOS）
     * @param pixelBufferPool CVPixelBufferPoolRef
     * @return 输出目标ID
     */
    int32_t setupPixelBufferOutput(void* pixelBufferPool = nullptr);
#endif
    
#ifdef __ANDROID__
    /**
     * @brief 配置SurfaceTexture输出（Android）
     * @param surfaceTexture SurfaceTexture对象
     * @return 输出目标ID
     */
    int32_t setupSurfaceTextureOutput(void* surfaceTexture);
#endif
    
    // ==========================================================================
    // 输出控制
    // ==========================================================================
    
    /**
     * @brief 开始输出
     */
    bool start();
    
    /**
     * @brief 停止输出
     */
    void stop();
    
    /**
     * @brief 暂停输出
     */
    void pause();
    
    /**
     * @brief 恢复输出
     */
    void resume();
    
    /**
     * @brief 获取输出状态
     */
    bool isRunning() const { return mIsRunning; }
    
    // ==========================================================================
    // 输出查询
    // ==========================================================================
    
    /**
     * @brief 获取最后输出的纹理
     */
    std::shared_ptr<lrengine::render::LRTexture> getOutputTexture() const;
    
    /**
     * @brief 获取最后输出的数据包
     */
    FramePacketPtr getLastOutput() const;
    
    /**
     * @brief 读取输出像素数据
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @param dataFormat 数据格式
     * @return 实际读取的字节数
     */
    size_t readPixels(uint8_t* buffer, size_t bufferSize, OutputDataFormat dataFormat = OutputDataFormat::RGBA8);
    
    // ==========================================================================
    // 统计信息
    // ==========================================================================
    
    /**
     * @brief 输出统计
     */
    struct OutputStats {
        uint64_t totalFrames = 0;               // 总帧数
        uint64_t droppedFrames = 0;             // 丢弃帧数
        uint64_t errorFrames = 0;               // 错误帧数
        double averageFPS = 0.0;                // 平均帧率
        double averageLatency = 0.0;            // 平均延迟（毫秒）
        
        // 各目标统计
        std::unordered_map<int32_t, uint64_t> targetFrameCounts;
    };
    
    /**
     * @brief 获取输出统计
     */
    OutputStats getStats() const;
    
    /**
     * @brief 重置统计
     */
    void resetStats();
    
protected:
    bool process(const std::vector<FramePacketPtr>& inputs,
                std::vector<FramePacketPtr>& outputs,
                PipelineContext& context) override;
    
    /**
     * @brief 处理单个输出目标
     */
    bool processOutputTarget(const OutputConfig& config, FramePacketPtr input);
    
    /**
     * @brief 渲染到显示
     */
    bool renderToDisplay(const DisplayOutputConfig& config, FramePacketPtr input);
    
    /**
     * @brief 输出到编码器
     */
    bool outputToEncoder(const EncoderOutputConfig& config, FramePacketPtr input);
    
    /**
     * @brief 执行回调输出
     */
    bool executeCallback(const CallbackOutputConfig& config, FramePacketPtr input);
    
    /**
     * @brief 输出纹理
     */
    bool outputTexture(const TextureOutputConfig& config, FramePacketPtr input);
    
    /**
     * @brief 保存到文件
     */
    bool saveToFile(const FileOutputConfig& config, FramePacketPtr input);
    
    /**
     * @brief 输出到平台特定格式
     */
    bool outputToPlatform(const PlatformOutputConfig& config, FramePacketPtr input);
    
    /**
     * @brief 格式转换
     */
    bool convertFormat(FramePacketPtr input, OutputDataFormat targetFormat, uint8_t* outputBuffer);
    
private:
    lrengine::render::LRRenderContext* mRenderContext = nullptr;
    PlatformContext* mPlatformContext = nullptr;
    
    // 输出目标
    std::unordered_map<int32_t, OutputConfig> mOutputTargets;
    int32_t mNextTargetId = 1;
    mutable std::mutex mTargetsMutex;
    
    // 运行状态
    bool mIsRunning = false;
    bool mIsPaused = false;
    
    // 渲染资源
    std::shared_ptr<lrengine::render::LRShaderProgram> mDisplayShader;
    std::shared_ptr<lrengine::render::LRShaderProgram> mConversionShader;
    std::shared_ptr<lrengine::render::LRVertexBuffer> mQuadVBO;
    std::unordered_map<int32_t, std::shared_ptr<lrengine::render::LRFrameBuffer>> mTargetFBOs;
    
    // 最后输出
    mutable std::mutex mLastOutputMutex;
    FramePacketPtr mLastOutput;
    
    // 统计
    OutputStats mStats;
    mutable std::mutex mStatsMutex;
    std::chrono::steady_clock::time_point mLastStatsTime;
};

} // namespace pipeline
