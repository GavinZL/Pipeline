/**
 * @file PipelineImpl.h
 * @brief Pipeline PIMPL 实现头文件
 * 
 * 使用 PIMPL 模式隐藏实现细节，保持二进制兼容性
 * 
 * @version 2.0
 * @date 2026-03-10
 */

#pragma once

#include "pipeline/PipelineNew.h"
#include "pipeline/core/PipelineManager.h"
#include "pipeline/platform/PlatformContext.h"
#include "pipeline/platform/PlatformStrategy.h"
#include "pipeline/input/InputEntity.h"
#include "pipeline/output/OutputEntity.h"
#include "pipeline/data/FramePacket.h"
#include "pipeline/entity/ProcessEntity.h"
#include "pipeline/output/OutputConfig.h"
#include "pipeline/utils/PipelineLog.h"
#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>

namespace pipeline {

// 使用已有的定义
using PipelinePreset = pipeline::PipelinePreset;
using QualityLevel = pipeline::QualityLevel;

// 前向声明
namespace input {
class InputEntity;
struct InputConfig;
} // namespace input

namespace output {
class OutputEntity;
class DisplaySurface;
} // namespace output



// ============================================================================
// BuilderState - 构建器状态结构
// ============================================================================

/**
 * @brief PipelineBuilder 内部状态
 * 
 * 存储所有配置参数，直到 build() 时才创建实际资源
 */
struct BuilderState {
    // 基础配置
    PipelinePreset preset = PipelinePreset::Custom;
    PlatformContextConfig platformConfig;
    QualityLevel quality = QualityLevel::Medium;
    uint32_t width = 1920;
    uint32_t height = 1080;
    int32_t maxQueueSize = 3;
    bool enableGPUOpt = true;
    bool enableMultiThread = true;
    int32_t threadPoolSize = 4;
    int32_t rotation = 0;
    bool mirrorH = false;
    bool mirrorV = false;
    
    // 输入配置
    input::InputFormat inputFormat = input::InputFormat::RGBA;
    uint32_t inputWidth = 1920;
    uint32_t inputHeight = 1080;
    
    // 输出配置列表
    std::vector<OutputTargetConfig> outputs;
    
    // Entity 列表
    std::vector<ProcessEntityPtr> customEntities;
    
    // 回调配置
    std::function<void(const std::string&)> errorCallback;
    std::function<void(PipelineState)> stateCallback;
    std::function<void(FramePacketPtr)> frameCallback;
    
    /**
     * @brief 验证配置有效性
     * @return 如果有效返回空字符串，否则返回错误信息
     */
    std::string validate() const {
        if (width == 0 || height == 0) {
            return "Invalid resolution: width and height must be > 0";
        }
        if (inputWidth == 0 || inputHeight == 0) {
            return "Invalid input resolution: width and height must be > 0";
        }
        if (maxQueueSize <= 0) {
            return "Invalid maxQueueSize: must be > 0";
        }
        if (threadPoolSize <= 0) {
            return "Invalid threadPoolSize: must be > 0";
        }
        return "";
    }
};

// ============================================================================
// PipelineImpl - PIMPL 实现类
// ============================================================================

/**
 * @brief Pipeline 实现类（PIMPL 模式）
 * 
 * 封装所有实现细节，包括：
 * - PipelineManager 实例
 * - PlatformContext 管理
 * - 资源池管理
 * - 运行时状态管理
 */
class PipelineImpl {
public:
    /**
     * @brief 构造函数
     * @param state Builder 状态
     */
    explicit PipelineImpl(BuilderState state);
    
    /**
     * @brief 析构函数
     */
    ~PipelineImpl();
    
    // 禁止拷贝
    PipelineImpl(const PipelineImpl&) = delete;
    PipelineImpl& operator=(const PipelineImpl&) = delete;
    
    // ========================================================================
    // 生命周期
    // ========================================================================
    
    /**
     * @brief 启动 Pipeline
     * @return 是否成功
     */
    bool start();
    
    /**
     * @brief 暂停 Pipeline
     */
    void pause();
    
    /**
     * @brief 恢复 Pipeline
     */
    void resume();
    
    /**
     * @brief 停止 Pipeline
     */
    void stop();
    
    /**
     * @brief 销毁 Pipeline
     */
    void destroy();
    
    /**
     * @brief 获取状态
     * @return 当前状态
     */
    PipelineState getState() const;
    
    /**
     * @brief 检查是否运行中
     * @return 是否运行
     */
    bool isRunning() const;
    
    // ========================================================================
    // 输入接口
    // ========================================================================
    
    Result<void> feedRGBA(const uint8_t* data, uint32_t width, uint32_t height, 
                          uint32_t stride, uint64_t timestamp);
    
    Result<void> feedYUV420(const uint8_t* yData, const uint8_t* uData, 
                            const uint8_t* vData, uint32_t width, uint32_t height,
                            uint64_t timestamp);
    
    Result<void> feedNV12(const uint8_t* yData, const uint8_t* uvData,
                          uint32_t width, uint32_t height, bool isNV21,
                          uint64_t timestamp);
    
    Result<void> feedTexture(std::shared_ptr<::lrengine::render::LRTexture> texture, 
                             uint32_t width, uint32_t height, uint64_t timestamp);
    
    // ========================================================================
    // Entity 管理
    // ========================================================================
    
    EntityId addEntity(ProcessEntityPtr entity);
    bool removeEntity(EntityId entityId);
    void setEntityEnabled(EntityId entityId, bool enabled);
    
    EntityId addBeautyFilter(float smoothLevel, float whitenLevel);
    EntityId addColorFilter(const std::string& filterName, float intensity);
    EntityId addSharpenFilter(float amount);
    
    // ========================================================================
    // 输出管理
    // ========================================================================
    
    int32_t addDisplayOutput(void* surface, uint32_t width, uint32_t height);
    int32_t addCallbackOutput(
        std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback,
        output::OutputFormat format);
    
    bool removeOutputTarget(int32_t targetId);
    void setOutputTargetEnabled(int32_t targetId, bool enabled);
    
    // ========================================================================
    // 配置
    // ========================================================================
    
    void setOutputResolution(uint32_t width, uint32_t height);
    void setRotation(int32_t degrees);
    void setMirror(bool horizontal, bool vertical);
    void setFrameRateLimit(int32_t fps);
    
    // ========================================================================
    // 回调
    // ========================================================================
    
    void setFrameProcessedCallback(std::function<void(FramePacketPtr)> callback);
    void setErrorCallback(std::function<void(const std::string&)> callback);
    void setStateCallback(std::function<void(PipelineState)> callback);
    
    // ========================================================================
    // 统计
    // ========================================================================
    
    ExecutionStats getStats() const;
    double getAverageProcessTime() const;
    std::string exportGraph() const;
    
private:
    // ========================================================================
    // 内部辅助方法
    // ========================================================================
    
    /**
     * @brief 初始化内部组件
     * @return 是否成功
     */
    bool initializeInternal();
    
    /**
     * @brief 设置输入 Entity
     * @return 是否成功
     */
    bool setupInputEntity();
    
    /**
     * @brief 设置输出 Entity
     * @return 是否成功
     */
    bool setupOutputEntity();
    
    /**
     * @brief 应用质量设置
     * @return 是否成功
     */
    bool applyQualitySettings();
    
    /**
     * @brief 根据预设配置默认参数
     */
    void applyPresetConfig();
    
    /**
     * @brief 设置运行时状态
     * @param state 新状态
     */
    void setRuntimeState(PipelineState state);
    
    /**
     * @brief 触发错误回调
     * @param message 错误信息
     */
    void notifyError(const std::string& message);
    
    /**
     * @brief 触发状态变更回调
     * @param state 新状态
     */
    void notifyStateChange(PipelineState state);
    
private:
    // 构建时状态（配置参数）
    BuilderState mBuilderState;
    
    // 运行时状态
    PipelineState mRuntimeState = PipelineState::Created;
    mutable std::mutex mStateMutex;
    
    // 核心组件
    std::shared_ptr<PipelineManager> mManager;
    std::unique_ptr<PlatformContext> mPlatformContext;
    lrengine::render::LRRenderContext* mRenderContext = nullptr;
    
    // 输入/输出实体
    std::shared_ptr<input::InputEntity> mInputEntity;
    std::shared_ptr<output::OutputEntity> mOutputEntity;
    
    // 输出目标管理
    std::map<int32_t, std::shared_ptr<output::OutputTarget>> mOutputTargets;
    std::atomic<int32_t> mNextTargetId{0};
    
    // 策略对象
    std::unique_ptr<strategy::InputStrategy> mInputStrategy;
    std::unique_ptr<strategy::OutputStrategy> mOutputStrategy;

    // 回调
    std::function<void(FramePacketPtr)> mFrameCallback;
    std::function<void(const std::string&)> mErrorCallback;
    std::function<void(PipelineState)> mStateCallback;

    // 初始化标志
    bool mInitialized = false;
    std::mutex mInitMutex;
};

} // namespace pipeline
