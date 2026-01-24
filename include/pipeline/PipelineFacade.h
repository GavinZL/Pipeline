/**
 * @file PipelineFacade.h
 * @brief Pipeline外观接口 - 提供完整统一的对外API
 * 
 * 核心设计目标：
 * 1. 封装Pipeline/LREngine/TaskQueue的复杂性
 * 2. 提供跨平台一致的接口
 * 3. 简化集成流程，降低使用门槛
 * 4. 支持平台特定的扩展功能
 * 
 * 使用场景：
 * - 相机预览处理管线
 * - 视频录制管线
 * - 实时滤镜管线
 * - 图像处理管线
 */

#pragma once

#include "pipeline/core/PipelineManager.h"
#include "pipeline/entity/OutputEntityExt.h"
#include "pipeline/platform/PlatformContext.h"
#include <memory>
#include <string>
#include <functional>

namespace pipeline {

// =============================================================================
// 管线预设类型
// =============================================================================

/**
 * @brief 管线预设类型
 */
enum class PipelinePreset : uint8_t {
    CameraPreview,      // 相机预览（实时显示）
    CameraRecord,       // 相机录制（编码输出）
    ImageProcess,       // 图像处理（文件输入输出）
    LiveStream,         // 直播推流
    VideoPlayback,      // 视频播放
    Custom              // 自定义
};

/**
 * @brief 处理质量级别
 */
enum class QualityLevel : uint8_t {
    Low,        // 低质量（高性能）
    Medium,     // 中等质量
    High,       // 高质量
    Ultra       // 超高质量（低性能）
};

// =============================================================================
// Pipeline配置
// =============================================================================

/**
 * @brief Pipeline外观配置
 */
struct PipelineFacadeConfig {
    // 基础配置
    PipelinePreset preset = PipelinePreset::CameraPreview;
    QualityLevel quality = QualityLevel::Medium;
    
    // 平台配置
    PlatformContextConfig platformConfig;
    
    // 渲染配置
    uint32_t renderWidth = 1920;
    uint32_t renderHeight = 1080;
    bool enableAsync = true;
    int32_t maxQueueSize = 3;
    
    // 性能配置
    bool enableGPUOptimization = true;
    bool enableMultiThreading = true;
    int32_t threadPoolSize = 4;
    
    // 调试配置
    bool enableProfiling = false;
    bool enableDebugLog = false;
    std::string logFilePath;
};

// =============================================================================
// 回调定义
// =============================================================================

/**
 * @brief Pipeline事件回调
 */
struct PipelineCallbacks {
    // 帧处理回调
    std::function<void(FramePacketPtr)> onFrameProcessed;
    std::function<void(FramePacketPtr)> onFrameDropped;
    
    // 状态回调
    std::function<void(PipelineState)> onStateChanged;
    std::function<void(const std::string&)> onError;
    
    // 性能回调
    std::function<void(const ExecutionStats&)> onStatsUpdate;
};

// =============================================================================
// Pipeline外观主类
// =============================================================================

/**
 * @brief Pipeline外观接口
 * 
 * 统一封装Pipeline的所有功能，提供简洁易用的API。
 * 
 * 使用示例：
 * @code
 * // 1. 创建Pipeline
 * PipelineFacadeConfig config;
 * config.preset = PipelinePreset::CameraPreview;
 * config.platformConfig.platform = PlatformType::Android;
 * 
 * auto pipeline = PipelineFacade::create(config);
 * 
 * // 2. 配置输出
 * pipeline->setupDisplayOutput(surface, width, height);
 * 
 * // 3. 添加处理效果
 * pipeline->addBeautyFilter(0.7f, 0.3f);
 * pipeline->addFilter("vintage");
 * 
 * // 4. 启动
 * pipeline->start();
 * 
 * // 5. 输入帧数据
 * pipeline->feedFrame(data, width, height, InputFormat::NV12);
 * 
 * // 6. 停止
 * pipeline->stop();
 * @endcode
 */
class PipelineFacade {
public:
    /**
     * @brief 创建Pipeline实例
     * @param config 配置参数
     * @return Pipeline外观实例
     */
    static std::shared_ptr<PipelineFacade> create(const PipelineFacadeConfig& config);
    
    ~PipelineFacade();
    
    // 禁止拷贝
    PipelineFacade(const PipelineFacade&) = delete;
    PipelineFacade& operator=(const PipelineFacade&) = delete;
    
    // ==========================================================================
    // 生命周期管理
    // ==========================================================================
    
    /**
     * @brief 初始化Pipeline
     * @return 是否成功
     */
    bool initialize();
    
    /**
     * @brief 启动Pipeline
     * @return 是否成功
     */
    bool start();
    
    /**
     * @brief 暂停Pipeline
     */
    void pause();
    
    /**
     * @brief 恢复Pipeline
     */
    void resume();
    
    /**
     * @brief 停止Pipeline
     */
    void stop();
    
    /**
     * @brief 销毁Pipeline
     */
    void destroy();
    
    /**
     * @brief 获取Pipeline状态
     */
    PipelineState getState() const;
    
    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const;
    
    // ==========================================================================
    // 输入接口
    // ==========================================================================
    
    /**
     * @brief 输入帧数据（通用）
     * @param data 数据指针
     * @param width 宽度
     * @param height 高度
     * @param format 输入格式
     * @param timestamp 时间戳
     * @return 是否成功
     */
    bool feedFrame(const uint8_t* data, uint32_t width, uint32_t height,
                   InputFormat format = InputFormat::RGBA,
                   uint64_t timestamp = 0);
    
    /**
     * @brief 输入RGBA数据
     */
    bool feedRGBA(const uint8_t* data, uint32_t width, uint32_t height,
                  uint32_t stride = 0, uint64_t timestamp = 0);
    
    /**
     * @brief 输入YUV420数据
     */
    bool feedYUV420(const uint8_t* yData, const uint8_t* uData, const uint8_t* vData,
                    uint32_t width, uint32_t height, uint64_t timestamp = 0);
    
    /**
     * @brief 输入NV12/NV21数据
     */
    bool feedNV12(const uint8_t* yData, const uint8_t* uvData,
                  uint32_t width, uint32_t height,
                  bool isNV21 = false, uint64_t timestamp = 0);
    
    /**
     * @brief 输入GPU纹理
     */
    bool feedTexture(std::shared_ptr<lrengine::render::LRTexture> texture,
                     uint32_t width, uint32_t height, uint64_t timestamp = 0);
    
#ifdef __ANDROID__
    /**
     * @brief 输入OES纹理（Android相机）
     */
    bool feedOES(uint32_t oesTextureId, uint32_t width, uint32_t height,
                 const float* transformMatrix = nullptr, uint64_t timestamp = 0);
#endif
    
#if defined(__APPLE__)
    /**
     * @brief 输入CVPixelBuffer（iOS相机）
     */
    bool feedPixelBuffer(void* pixelBuffer, uint64_t timestamp = 0);
#endif
    
    // ==========================================================================
    // 输出配置
    // ==========================================================================
    
    /**
     * @brief 设置显示输出
     * @param surface 平台Surface
     * @param width 显示宽度
     * @param height 显示高度
     * @return 输出目标ID
     */
    int32_t setupDisplayOutput(void* surface, int32_t width, int32_t height);
    
    /**
     * @brief 设置编码器输出
     * @param encoderSurface 编码器Surface
     * @param encoderType 编码器类型
     * @return 输出目标ID
     */
    int32_t setupEncoderOutput(void* encoderSurface, EncoderType encoderType = EncoderType::Hardware);
    
    /**
     * @brief 设置回调输出
     * @param callback 帧回调
     * @param dataFormat 数据格式
     * @return 输出目标ID
     */
    int32_t setupCallbackOutput(FrameCallback callback,
                                OutputDataFormat dataFormat = OutputDataFormat::RGBA8);
    
    /**
     * @brief 设置文件输出
     * @param filePath 文件路径
     * @return 输出目标ID
     */
    int32_t setupFileOutput(const std::string& filePath);
    
    /**
     * @brief 移除输出目标
     * @param targetId 目标ID
     * @return 是否成功
     */
    bool removeOutputTarget(int32_t targetId);
    
    /**
     * @brief 启用/禁用输出目标
     */
    void setOutputTargetEnabled(int32_t targetId, bool enabled);
    
    // ==========================================================================
    // 滤镜和效果
    // ==========================================================================
    
    /**
     * @brief 添加美颜滤镜
     * @param smoothLevel 磨皮级别 [0.0, 1.0]
     * @param whitenLevel 美白级别 [0.0, 1.0]
     * @return Entity ID
     */
    EntityId addBeautyFilter(float smoothLevel, float whitenLevel);
    
    /**
     * @brief 添加颜色滤镜
     * @param filterName 滤镜名称（vintage/warm/cool等）
     * @param intensity 强度 [0.0, 1.0]
     * @return Entity ID
     */
    EntityId addColorFilter(const std::string& filterName, float intensity = 1.0f);
    
    /**
     * @brief 添加锐化滤镜
     * @param amount 锐化强度
     * @return Entity ID
     */
    EntityId addSharpenFilter(float amount);
    
    /**
     * @brief 添加模糊滤镜
     * @param radius 模糊半径
     * @return Entity ID
     */
    EntityId addBlurFilter(float radius);
    
    /**
     * @brief 添加自定义Entity
     * @param entity 自定义Entity
     * @return Entity ID
     */
    EntityId addCustomEntity(ProcessEntityPtr entity);
    
    /**
     * @brief 移除Entity
     * @param entityId Entity ID
     * @return 是否成功
     */
    bool removeEntity(EntityId entityId);
    
    /**
     * @brief 启用/禁用Entity
     */
    void setEntityEnabled(EntityId entityId, bool enabled);
    
    // ==========================================================================
    // 渲染配置
    // ==========================================================================
    
    /**
     * @brief 设置输出分辨率
     */
    void setOutputResolution(uint32_t width, uint32_t height);
    
    /**
     * @brief 设置旋转角度
     * @param degrees 角度（0/90/180/270）
     */
    void setRotation(int32_t degrees);
    
    /**
     * @brief 设置镜像
     */
    void setMirror(bool horizontal, bool vertical);
    
    /**
     * @brief 设置裁剪区域
     */
    void setCropRect(float x, float y, float width, float height);
    
    /**
     * @brief 设置帧率限制
     */
    void setFrameRateLimit(int32_t fps);
    
    // ==========================================================================
    // 回调设置
    // ==========================================================================
    
    /**
     * @brief 设置回调
     */
    void setCallbacks(const PipelineCallbacks& callbacks);
    
    /**
     * @brief 设置帧处理完成回调
     */
    void setFrameProcessedCallback(std::function<void(FramePacketPtr)> callback);
    
    /**
     * @brief 设置错误回调
     */
    void setErrorCallback(std::function<void(const std::string&)> callback);
    
    /**
     * @brief 设置状态变更回调
     */
    void setStateCallback(std::function<void(PipelineState)> callback);
    
    // ==========================================================================
    // 性能和统计
    // ==========================================================================
    
    /**
     * @brief 获取性能统计
     */
    ExecutionStats getStats() const;
    
    /**
     * @brief 获取输出统计
     */
    OutputEntityExt::OutputStats getOutputStats() const;
    
    /**
     * @brief 重置统计
     */
    void resetStats();
    
    /**
     * @brief 获取当前FPS
     */
    double getCurrentFPS() const;
    
    /**
     * @brief 获取平均处理时间（毫秒）
     */
    double getAverageProcessTime() const;
    
    // ==========================================================================
    // 高级功能
    // ==========================================================================
    
    /**
     * @brief 获取底层PipelineManager
     */
    std::shared_ptr<PipelineManager> getPipelineManager() const { return mPipelineManager; }
    
    /**
     * @brief 获取平台上下文
     */
    PlatformContext* getPlatformContext() const { return mPlatformContext.get(); }
    
    /**
     * @brief 获取渲染上下文
     */
    lrengine::render::LRRenderContext* getRenderContext() const { return mRenderContext; }
    
    /**
     * @brief 导出管线图（DOT格式）
     */
    std::string exportGraph() const;
    
    /**
     * @brief 保存配置到文件
     */
    bool saveConfig(const std::string& filePath) const;
    
    /**
     * @brief 从文件加载配置
     */
    bool loadConfig(const std::string& filePath);
    
private:
    /**
     * @brief 私有构造函数
     */
    explicit PipelineFacade(const PipelineFacadeConfig& config);
    
    /**
     * @brief 创建预设管线
     */
    bool createPresetPipeline(PipelinePreset preset);
    
    /**
     * @brief 初始化平台上下文
     */
    bool initializePlatformContext();
    
    /**
     * @brief 初始化渲染上下文
     */
    bool initializeRenderContext();
    
    /**
     * @brief 创建输入输出Entity
     */
    bool createIOEntities();
    
    /**
     * @brief 应用质量设置
     */
    void applyQualitySettings(QualityLevel quality);
    
private:
    // 配置
    PipelineFacadeConfig mConfig;
    PipelineCallbacks mCallbacks;
    
    // 核心组件
    std::shared_ptr<PipelineManager> mPipelineManager;
    std::unique_ptr<PlatformContext> mPlatformContext;
    lrengine::render::LRRenderContext* mRenderContext = nullptr;
    
    // Entity引用
    EntityId mInputEntityId = InvalidEntityId;
    EntityId mOutputEntityId = InvalidEntityId;
    OutputEntityExt* mOutputEntity = nullptr;
    
    // 状态
    bool mInitialized = false;
    mutable std::mutex mStateMutex;
    
    // 性能统计
    std::chrono::steady_clock::time_point mLastFrameTime;
    double mCurrentFPS = 0.0;
};

// =============================================================================
// 工具函数
// =============================================================================

/**
 * @brief 获取Pipeline版本信息
 */
const char* getPipelineVersion();

/**
 * @brief 获取支持的平台列表
 */
std::vector<PlatformType> getSupportedPlatforms();

/**
 * @brief 检查平台是否支持
 */
bool isPlatformSupported(PlatformType platform);

/**
 * @brief 获取推荐配置
 */
PipelineFacadeConfig getRecommendedConfig(PipelinePreset preset, PlatformType platform);

} // namespace pipeline
