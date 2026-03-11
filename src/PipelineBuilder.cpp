/**
 * @file PipelineBuilder.cpp
 * @brief PipelineBuilder 实现
 */

#include "pipeline/PipelineNew.h"
#include "PipelineImpl.h"
#include "PipelineImpl.h"
#include <stdexcept>
#include <sstream>

namespace pipeline {

// ============================================================================
// PipelineBuilder::Impl 定义
// ============================================================================

class PipelineBuilder::Impl {
public:
    BuilderState state;
};

// ============================================================================
// PipelineBuilder 构造/析构
// ============================================================================

PipelineBuilder::PipelineBuilder() 
    : mImpl(std::make_unique<Impl>()) {
}

PipelineBuilder::~PipelineBuilder() = default;

PipelineBuilder::PipelineBuilder(PipelineBuilder&&) noexcept = default;
PipelineBuilder& PipelineBuilder::operator=(PipelineBuilder&&) noexcept = default;

// ============================================================================
// 基础配置
// ============================================================================

PipelineBuilder& PipelineBuilder::withPreset(PipelinePreset preset) {
    mImpl->state.preset = preset;
    return *this;
}

PipelineBuilder& PipelineBuilder::withPlatform(PlatformType platform) {
    mImpl->state.platformConfig.platform = platform;
    return *this;
}

PipelineBuilder& PipelineBuilder::withPlatformConfig(const PlatformContextConfig& config) {
    mImpl->state.platformConfig = config;
    return *this;
}

PipelineBuilder& PipelineBuilder::withQuality(QualityLevel quality) {
    mImpl->state.quality = quality;
    return *this;
}

PipelineBuilder& PipelineBuilder::withResolution(uint32_t width, uint32_t height) {
    mImpl->state.width = width;
    mImpl->state.height = height;
    return *this;
}

PipelineBuilder& PipelineBuilder::withMaxQueueSize(int32_t size) {
    mImpl->state.maxQueueSize = size;
    return *this;
}

PipelineBuilder& PipelineBuilder::withGPUOptimization(bool enable) {
    mImpl->state.enableGPUOpt = enable;
    return *this;
}

PipelineBuilder& PipelineBuilder::withMultiThreading(bool enable) {
    mImpl->state.enableMultiThread = enable;
    return *this;
}

PipelineBuilder& PipelineBuilder::withThreadPoolSize(int32_t size) {
    mImpl->state.threadPoolSize = size;
    return *this;
}

PipelineBuilder& PipelineBuilder::withRotation(int32_t degrees) {
    mImpl->state.rotation = degrees;
    return *this;
}

PipelineBuilder& PipelineBuilder::withMirror(bool horizontal, bool vertical) {
    mImpl->state.mirrorH = horizontal;
    mImpl->state.mirrorV = vertical;
    return *this;
}

// ============================================================================
// 输入配置
// ============================================================================

PipelineBuilder& PipelineBuilder::withRGBAInput(uint32_t width, uint32_t height) {
    mImpl->state.inputFormat = input::InputFormat::RGBA;
    mImpl->state.inputWidth = width;
    mImpl->state.inputHeight = height;
    return *this;
}

PipelineBuilder& PipelineBuilder::withYUVInput(uint32_t width, uint32_t height) {
    mImpl->state.inputFormat = input::InputFormat::YUV420;
    mImpl->state.inputWidth = width;
    mImpl->state.inputHeight = height;
    return *this;
}

PipelineBuilder& PipelineBuilder::withNV12Input(uint32_t width, uint32_t height) {
    mImpl->state.inputFormat = input::InputFormat::NV12;
    mImpl->state.inputWidth = width;
    mImpl->state.inputHeight = height;
    return *this;
}

// ============================================================================
// 输出配置
// ============================================================================

PipelineBuilder& PipelineBuilder::withDisplayOutput(void* surface, uint32_t width, uint32_t height) {
    OutputTargetConfig config;
    config.type = OutputTargetConfig::Type::Display;
    config.display.surface = surface;
    config.display.width = width;
    config.display.height = height;
    mImpl->state.outputs.push_back(std::move(config));
    return *this;
}

PipelineBuilder& PipelineBuilder::withCallbackOutput(
    std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback,
    output::OutputFormat format) {
    OutputTargetConfig config;
    config.type = OutputTargetConfig::Type::Callback;
    config.callback.callback = std::move(callback);
    config.callback.format = format;
    mImpl->state.outputs.push_back(std::move(config));
    return *this;
}

PipelineBuilder& PipelineBuilder::withEncoderOutput(void* encoderSurface, output::EncoderType type) {
    OutputTargetConfig config;
    config.type = OutputTargetConfig::Type::Encoder;
    config.encoder.encoderSurface = encoderSurface;
    config.encoder.encoderType = type;
    mImpl->state.outputs.push_back(std::move(config));
    return *this;
}

PipelineBuilder& PipelineBuilder::withFileOutput(const std::string& filePath) {
    OutputTargetConfig config;
    config.type = OutputTargetConfig::Type::File;
    config.file.filePath = filePath;
    mImpl->state.outputs.push_back(std::move(config));
    return *this;
}

// ============================================================================
// 滤镜配置（占位符，实际实现需要创建对应 Entity）
// ============================================================================

PipelineBuilder& PipelineBuilder::withBeautyFilter(float smoothLevel, float whitenLevel) {
    // Phase 1: 仅保存配置，实际 Entity 在 build() 时创建
    // TODO: 创建 BeautyEntity 并添加到 customEntities
    (void)smoothLevel;
    (void)whitenLevel;
    return *this;
}

PipelineBuilder& PipelineBuilder::withColorFilter(const std::string& filterName, float intensity) {
    // TODO: 创建 ColorFilterEntity
    (void)filterName;
    (void)intensity;
    return *this;
}

PipelineBuilder& PipelineBuilder::withSharpenFilter(float amount) {
    // TODO: 创建 SharpenEntity
    (void)amount;
    return *this;
}

PipelineBuilder& PipelineBuilder::withBlurFilter(float radius) {
    // TODO: 创建 BlurEntity
    (void)radius;
    return *this;
}

PipelineBuilder& PipelineBuilder::withCustomEntity(ProcessEntityPtr entity) {
    if (entity) {
        mImpl->state.customEntities.push_back(std::move(entity));
    }
    return *this;
}

// ============================================================================
// 回调配置
// ============================================================================

PipelineBuilder& PipelineBuilder::withErrorCallback(std::function<void(const std::string&)> callback) {
    mImpl->state.errorCallback = std::move(callback);
    return *this;
}

PipelineBuilder& PipelineBuilder::withStateCallback(std::function<void(PipelineState)> callback) {
    mImpl->state.stateCallback = std::move(callback);
    return *this;
}

PipelineBuilder& PipelineBuilder::withFrameProcessedCallback(std::function<void(FramePacketPtr)> callback) {
    mImpl->state.frameCallback = std::move(callback);
    return *this;
}

// ============================================================================
// 构建方法
// ============================================================================

std::string PipelineBuilder::validate() const {
    return mImpl->state.validate();
}

std::shared_ptr<Pipeline> PipelineBuilder::build() {
    // 验证配置
    std::string error = validate();
    if (!error.empty()) {
        throw std::invalid_argument("PipelineBuilder validation failed: " + error);
    }
    
    // 创建 PipelineImpl
    auto impl = std::make_unique<PipelineImpl>(mImpl->state);
    
    // 创建 Pipeline 实例
    auto pipeline = std::shared_ptr<Pipeline>(new Pipeline(std::move(impl)));
    
    return pipeline;
}

} // namespace pipeline
