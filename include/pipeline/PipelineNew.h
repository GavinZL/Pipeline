/**
 * @file PipelineNew.h
 * @brief Pipeline 2.0 统一接口 - 新的外观接口
 */

#pragma once

#include "pipeline/PipelineFacade.h"
#include "pipeline/core/PipelineManager.h"
#include "pipeline/core/PipelineError.h"
#include "pipeline/data/EntityTypes.h"
#include "pipeline/data/FramePacket.h"
#include "pipeline/platform/PlatformContext.h"
#include "pipeline/output/OutputConfig.h"
#include "pipeline/input/InputEntity.h"
#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <mutex>

namespace lrengine {
namespace render {
class LRTexture;
class LRRenderContext;
}
}

namespace pipeline {

class PipelineBuilder;
class PipelineImpl;

using PipelinePreset = pipeline::PipelinePreset;
using QualityLevel = pipeline::QualityLevel;

struct OutputTargetConfig {
    enum class Type { Display, Callback, Encoder, File };
    Type type;
    struct { void* surface; uint32_t width; uint32_t height; } display;
    struct { std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback; output::OutputFormat format; } callback;
    struct { void* encoderSurface; output::EncoderType encoderType; } encoder;
    struct { std::string filePath; } file;
};

class Pipeline : public std::enable_shared_from_this<Pipeline> {
public:
    static PipelineBuilder create();
    static std::shared_ptr<Pipeline> fromJsonFile(const std::string& configFilePath);
    
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    
    [[nodiscard]] Result<void> start();
    void pause();
    void resume();
    void stop();
    void destroy();
    PipelineState getState() const;
    bool isRunning() const;

    Result<void> feedRGBA(const uint8_t* data, uint32_t width, uint32_t height, uint32_t stride = 0, uint64_t timestamp = 0);
    Result<void> feedYUV420(const uint8_t* yData, const uint8_t* uData, const uint8_t* vData, uint32_t width, uint32_t height, uint64_t timestamp = 0);
    Result<void> feedNV12(const uint8_t* yData, const uint8_t* uvData, uint32_t width, uint32_t height, bool isNV21 = false, uint64_t timestamp = 0);
    Result<void> feedTexture(std::shared_ptr<::lrengine::render::LRTexture> texture, uint32_t width, uint32_t height, uint64_t timestamp = 0);
    
    EntityId addEntity(ProcessEntityPtr entity);
    bool removeEntity(EntityId entityId);
    void setEntityEnabled(EntityId entityId, bool enabled);
    EntityId addBeautyFilter(float smoothLevel, float whitenLevel);
    EntityId addColorFilter(const std::string& filterName, float intensity = 1.0f);
    EntityId addSharpenFilter(float amount);
    
    int32_t addDisplayOutput(void* surface, uint32_t width, uint32_t height);
    int32_t addCallbackOutput(std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback, output::OutputFormat format);
    bool removeOutputTarget(int32_t targetId);
    void setOutputTargetEnabled(int32_t targetId, bool enabled);
    
    void setOutputResolution(uint32_t width, uint32_t height);
    void setRotation(int32_t degrees);
    void setMirror(bool horizontal, bool vertical);
    void setFrameRateLimit(int32_t fps);
    
    void setFrameProcessedCallback(std::function<void(FramePacketPtr)> callback);
    void setErrorCallback(std::function<void(const std::string&)> callback);
    void setStateCallback(std::function<void(PipelineState)> callback);
    
    ExecutionStats getStats() const;
    double getAverageProcessTime() const;
    bool exportConfig(const std::string& filePath) const;
    std::string exportGraph() const;
    
private:
    friend class PipelineBuilder;
    explicit Pipeline(std::unique_ptr<PipelineImpl> impl);
    std::unique_ptr<PipelineImpl> mImpl;
};

class PipelineBuilder {
public:
    PipelineBuilder();
    ~PipelineBuilder();
    PipelineBuilder(const PipelineBuilder&) = delete;
    PipelineBuilder& operator=(const PipelineBuilder&) = delete;
    PipelineBuilder(PipelineBuilder&&) noexcept;
    PipelineBuilder& operator=(PipelineBuilder&&) noexcept;
    
    PipelineBuilder& withPreset(PipelinePreset preset);
    PipelineBuilder& withPlatform(PlatformType platform);
    PipelineBuilder& withPlatformConfig(const PlatformContextConfig& config);
    PipelineBuilder& withQuality(QualityLevel quality);
    PipelineBuilder& withResolution(uint32_t width, uint32_t height);
    PipelineBuilder& withMaxQueueSize(int32_t size);
    PipelineBuilder& withGPUOptimization(bool enable);
    PipelineBuilder& withMultiThreading(bool enable);
    PipelineBuilder& withThreadPoolSize(int32_t size);
    PipelineBuilder& withRotation(int32_t degrees);
    PipelineBuilder& withMirror(bool horizontal, bool vertical);
    
    PipelineBuilder& withRGBAInput(uint32_t width, uint32_t height);
    PipelineBuilder& withYUVInput(uint32_t width, uint32_t height);
    PipelineBuilder& withNV12Input(uint32_t width, uint32_t height);
    
    PipelineBuilder& withDisplayOutput(void* surface, uint32_t width, uint32_t height);
    PipelineBuilder& withCallbackOutput(std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback, output::OutputFormat format);
    PipelineBuilder& withEncoderOutput(void* encoderSurface, output::EncoderType type);
    PipelineBuilder& withFileOutput(const std::string& filePath);
    
    PipelineBuilder& withBeautyFilter(float smoothLevel, float whitenLevel);
    PipelineBuilder& withColorFilter(const std::string& filterName, float intensity = 1.0f);
    PipelineBuilder& withSharpenFilter(float amount);
    PipelineBuilder& withBlurFilter(float radius);
    PipelineBuilder& withCustomEntity(ProcessEntityPtr entity);
    
    PipelineBuilder& withErrorCallback(std::function<void(const std::string&)> callback);
    PipelineBuilder& withStateCallback(std::function<void(PipelineState)> callback);
    PipelineBuilder& withFrameProcessedCallback(std::function<void(FramePacketPtr)> callback);
    
    [[nodiscard]] std::shared_ptr<Pipeline> build();
    [[nodiscard]] std::string validate() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

}
