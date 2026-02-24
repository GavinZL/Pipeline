/**
 * @file OutputEntity.h
 * @brief 输出实体 - Pipeline 输出管理的统一抽象
 * 
 * OutputEntity 支持多目标输出：
 * - 显示：渲染到屏幕
 * - 编码：输出到视频编码器
 * - 回调：返回给上层应用
 */

#pragma once

#include "pipeline/entity/ProcessEntity.h"
#include "pipeline/output/OutputConfig.h"
#include "pipeline/output/DisplaySurface.h"
#include <memory>
#include <vector>
#include <queue>

namespace pipeline {
namespace output {

// =============================================================================
// 常量定义
// =============================================================================

/// 默认输入端口名称
static constexpr const char* DEFAULT_INPUT_PORT = "input";

/// GPU 输入端口名称（用于双路输入）
static constexpr const char* GPU_INPUT_PORT = "gpu_in";

/// CPU 输入端口名称（用于双路输入）
static constexpr const char* CPU_INPUT_PORT = "cpu_in";

// =============================================================================
// 输出目标接口
// =============================================================================

/**
 * @brief 输出目标接口
 * 
 * 每种输出目标（显示、编码、回调）实现此接口。
 */
class OutputTarget {
public:
    virtual ~OutputTarget() = default;
    
    /**
     * @brief 获取目标名称
     */
    virtual const std::string& getName() const = 0;
    
    /**
     * @brief 获取目标类型
     */
    virtual OutputTargetType getType() const = 0;
    
    /**
     * @brief 初始化目标
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief 释放资源
     */
    virtual void release() = 0;
    
    /**
     * @brief 输出数据
     * @param data 输出数据
     * @return 是否成功
     */
    virtual bool output(const OutputData& data) = 0;
    
    /**
     * @brief 检查是否就绪
     */
    virtual bool isReady() const = 0;
    
    /**
     * @brief 是否启用
     */
    virtual bool isEnabled() const { return mEnabled; }
    
    /**
     * @brief 设置启用状态
     */
    virtual void setEnabled(bool enabled) { mEnabled = enabled; }
    
protected:
    bool mEnabled = true;
};

using OutputTargetPtr = std::shared_ptr<OutputTarget>;

// =============================================================================
// 显示输出目标
// =============================================================================

/**
 * @brief 显示输出目标
 */
class DisplayOutputTarget : public OutputTarget {
public:
    explicit DisplayOutputTarget(const std::string& name);
    ~DisplayOutputTarget() override;
    
    const std::string& getName() const override { return mName; }
    OutputTargetType getType() const override { return OutputTargetType::Display; }
    
    bool initialize() override;
    void release() override;
    bool output(const OutputData& data) override;
    bool isReady() const override;
    
    /**
     * @brief 设置显示表面
     */
    void setDisplaySurface(DisplaySurfacePtr surface);
    
    /**
     * @brief 获取显示表面
     */
    DisplaySurfacePtr getDisplaySurface() const { return mSurface; }
    
    /**
     * @brief 设置显示配置
     */
    void setDisplayConfig(const DisplayConfig& config);

private:
    // CPU 数据上传到纹理
    std::shared_ptr<lrengine::render::LRPlanarTexture> getOrCreateCpuPlanarTexture(
        lrengine::render::LRRenderContext* context,
        uint32_t width, uint32_t height,
        OutputFormat format);

private:
    std::string mName;
    DisplaySurfacePtr mSurface;
    DisplayConfig mDisplayConfig;
    
    // CPU 数据渲染用的临时纹理
    std::shared_ptr<lrengine::render::LRPlanarTexture> mCpuDataPlanarTexture;
    uint32_t mCpuDataWidth = 0;
    uint32_t mCpuDataHeight = 0;
    OutputFormat mCpuDataFormat = OutputFormat::RGBA;
};

// =============================================================================
// 回调输出目标
// =============================================================================

/**
 * @brief 回调输出目标
 */
class CallbackOutputTarget : public OutputTarget {
public:
    explicit CallbackOutputTarget(const std::string& name);
    ~CallbackOutputTarget() override;
    
    const std::string& getName() const override { return mName; }
    OutputTargetType getType() const override { return OutputTargetType::Callback; }
    
    bool initialize() override { return true; }
    void release() override {}
    bool output(const OutputData& data) override;
    bool isReady() const override { return mCpuCallback || mGpuCallback; }
    
    /**
     * @brief 设置 CPU 数据回调
     */
    void setCPUCallback(CPUOutputCallback callback);
    
    /**
     * @brief 设置 GPU 数据回调
     */
    void setGPUCallback(GPUOutputCallback callback);
    
private:
    std::string mName;
    CPUOutputCallback mCpuCallback;
    GPUOutputCallback mGpuCallback;
};

// =============================================================================
// OutputEntity
// =============================================================================

/**
 * @brief 输出实体类
 * 
 * 作为 Pipeline 的数据出口，负责：
 * 1. 接收处理后的帧数据
 * 2. 分发到多个输出目标
 * 3. 管理输出队列和同步
 * 
 * 使用示例：
 * @code
 * auto outputEntity = std::make_shared<OutputEntity>("output");
 * 
 * // 添加显示目标
 * auto displayTarget = std::make_shared<DisplayOutputTarget>("display");
 * displayTarget->setDisplaySurface(surface);
 * outputEntity->addTarget(displayTarget);
 * 
 * // 添加回调目标
 * auto callbackTarget = std::make_shared<CallbackOutputTarget>("callback");
 * callbackTarget->setCPUCallback([](const uint8_t* data, ...) {
 *     // 处理 CPU 数据
 * });
 * outputEntity->addTarget(callbackTarget);
 * @endcode
 */
class OutputEntity : public ProcessEntity {
public:
    /**
     * @brief 构造函数
     * @param name Entity 名称
     */
    explicit OutputEntity(const std::string& name = "OutputEntity");
    
    ~OutputEntity() override;
    
    // ==========================================================================
    // ProcessEntity 接口实现
    // ==========================================================================
    
    EntityType getType() const override { return EntityType::Output; }
    
    ExecutionQueue getExecutionQueue() const override { return ExecutionQueue::GPU; }
    
    // ==========================================================================
    // 配置
    // ==========================================================================
    
    /**
     * @brief 配置输出参数
     * @param config 输出配置
     */
    void configure(const OutputConfig& config);
    
    /**
     * @brief 获取当前配置
     */
    const OutputConfig& getOutputConfig() const { return mConfig; }
    
    /**
     * @brief 设置双路输入模式
     * @param enabled 是否启用
     */
    void setDualInputMode(bool enabled);
    
    /**
     * @brief 是否双路输入模式
     */
    bool isDualInputMode() const { return mDualInputMode; }
    
    // ==========================================================================
    // 输出目标管理
    // ==========================================================================
    
    /**
     * @brief 添加输出目标
     * @param target 输出目标
     */
    void addTarget(OutputTargetPtr target);
    
    /**
     * @brief 移除输出目标
     * @param name 目标名称
     */
    void removeTarget(const std::string& name);
    
    /**
     * @brief 获取输出目标
     * @param name 目标名称
     */
    OutputTargetPtr getTarget(const std::string& name) const;
    
    /**
     * @brief 获取所有输出目标
     */
    const std::vector<OutputTargetPtr>& getTargets() const { return mTargets; }
    
    /**
     * @brief 清除所有输出目标
     */
    void clearTargets();
    
    // ==========================================================================
    // 便捷方法
    // ==========================================================================
    
    /**
     * @brief 设置显示表面
     */
    void setDisplaySurface(DisplaySurfacePtr surface);
    
    /**
     * @brief 设置 CPU 输出回调
     */
    void setCPUOutputCallback(CPUOutputCallback callback);
    
    /**
     * @brief 设置 GPU 输出回调
     */
    void setGPUOutputCallback(GPUOutputCallback callback);
    
    // ==========================================================================
    // 状态查询
    // ==========================================================================
    
    /**
     * @brief 获取已输出帧数
     */
    uint64_t getOutputFrameCount() const { return mOutputFrameCount; }
    
    /**
     * @brief 获取丢弃帧数
     */
    uint64_t getDroppedFrameCount() const { return mDroppedFrameCount; }
    
protected:
    // ==========================================================================
    // ProcessEntity 生命周期
    // ==========================================================================
    
    bool prepare(PipelineContext& context) override;
    
    bool process(const std::vector<FramePacketPtr>& inputs,
                std::vector<FramePacketPtr>& outputs,
                PipelineContext& context) override;
    
    void finalize(PipelineContext& context) override;
    
private:
    // 初始化端口
    void initializePorts();
    
    // 处理输出
    bool processOutput(FramePacketPtr packet);
    
    // 分发到所有目标
    void dispatchToTargets(const OutputData& data);
    
private:
    // 配置
    OutputConfig mConfig;
    bool mDualInputMode = false;
    
    // 输出目标
    std::vector<OutputTargetPtr> mTargets;
    mutable std::mutex mTargetsMutex;
    
    // 默认目标（便捷访问）
    std::shared_ptr<DisplayOutputTarget> mDisplayTarget;
    std::shared_ptr<CallbackOutputTarget> mCallbackTarget;
    
    // 统计
    uint64_t mOutputFrameCount = 0;
    uint64_t mDroppedFrameCount = 0;
};

using OutputEntityPtr = std::shared_ptr<OutputEntity>;

} // namespace output
} // namespace pipeline
