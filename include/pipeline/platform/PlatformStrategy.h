#pragma once

#include "pipeline/platform/PlatformContext.h"
#include "pipeline/output/OutputConfig.h"
#include "pipeline/input/InputEntity.h"
#include <memory>
#include <functional>

namespace lrengine {
namespace render {
class LRTexture;
class LRRenderContext;
}
}

namespace pipeline {

class PipelineImpl;

namespace strategy {

/**
 * @brief 平台输入策略接口
 * 
 * 抽象平台特定的输入处理方式（Android OES / iOS CVPixelBuffer / 通用）
 */
class InputStrategy {
public:
    virtual ~InputStrategy() = default;
    
    /**
     * @brief 初始化输入策略
     * @param impl PipelineImpl 实例
     * @param width 输入宽度
     * @param height 输入高度
     * @return 是否成功
     */
    virtual bool initialize(PipelineImpl* impl, uint32_t width, uint32_t height) = 0;
    
    /**
     * @brief 输入 RGBA 数据
     */
    virtual bool feedRGBA(const uint8_t* data, uint32_t width, uint32_t height, 
                          uint32_t stride, uint64_t timestamp) = 0;
    
    /**
     * @brief 输入 YUV420 数据
     */
    virtual bool feedYUV420(const uint8_t* yData, const uint8_t* uData, 
                            const uint8_t* vData, uint32_t width, uint32_t height,
                            uint64_t timestamp) = 0;
    
    /**
     * @brief 输入 NV12/NV21 数据
     */
    virtual bool feedNV12(const uint8_t* yData, const uint8_t* uvData,
                          uint32_t width, uint32_t height, bool isNV21,
                          uint64_t timestamp) = 0;
    
    /**
     * @brief 输入 GPU 纹理
     */
    virtual bool feedTexture(std::shared_ptr<::lrengine::render::LRTexture> texture, 
                             uint32_t width, uint32_t height, uint64_t timestamp) = 0;
    
    /**
     * @brief 销毁资源
     */
    virtual void destroy() = 0;
    
    /**
     * @brief 获取策略名称
     */
    virtual const char* getName() const = 0;
};

/**
 * @brief 平台输出策略接口
 * 
 * 抽象平台特定的输出处理方式
 */
class OutputStrategy {
public:
    virtual ~OutputStrategy() = default;
    
    /**
     * @brief 初始化输出策略
     * @param impl PipelineImpl 实例
     * @return 是否成功
     */
    virtual bool initialize(PipelineImpl* impl) = 0;
    
    /**
     * @brief 创建显示输出
     * @param surface 平台 Surface
     * @param width 宽度
     * @param height 高度
     * @return 输出目标 ID
     */
    virtual int32_t createDisplayOutput(void* surface, uint32_t width, uint32_t height) = 0;
    
    /**
     * @brief 创建回调输出
     */
    virtual int32_t createCallbackOutput(
        std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback,
        output::OutputFormat format) = 0;
    
    /**
     * @brief 移除输出目标
     */
    virtual bool removeOutput(int32_t targetId) = 0;
    
    /**
     * @brief 启用/禁用输出
     */
    virtual void setOutputEnabled(int32_t targetId, bool enabled) = 0;
    
    /**
     * @brief 销毁资源
     */
    virtual void destroy() = 0;
    
    /**
     * @brief 获取策略名称
     */
    virtual const char* getName() const = 0;
};

/**
 * @brief 平台上下文策略工厂
 * 
 * 根据平台类型创建对应的策略实例
 */
class PlatformStrategyFactory {
public:
    /**
     * @brief 创建输入策略
     * @param platform 平台类型
     * @param format 输入格式
     * @return 输入策略实例
     */
    static std::unique_ptr<InputStrategy> createInputStrategy(
        PlatformType platform, 
        input::InputFormat format);
    
    /**
     * @brief 创建输出策略
     * @param platform 平台类型
     * @return 输出策略实例
     */
    static std::unique_ptr<OutputStrategy> createOutputStrategy(PlatformType platform);
};

} // namespace strategy
} // namespace pipeline
