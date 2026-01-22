/**
 * @file OutputEntityExt.cpp
 * @brief 扩展输出实体实现
 * 
 * TODO: 完整实现 - Phase 2
 */

#include "pipeline/entity/OutputEntityExt.h"

namespace pipeline {

OutputEntityExt::OutputEntityExt(const std::string& name)
    : ProcessEntity(name)
    , mRenderContext(nullptr)
    , mPlatformContext(nullptr)
    , mNextTargetId(1)
    , mIsRunning(false)
    , mIsPaused(false)
{
}

OutputEntityExt::~OutputEntityExt() {
    stop();
}

void OutputEntityExt::setRenderContext(lrengine::render::LRRenderContext* context) {
    mRenderContext = context;
}

void OutputEntityExt::setPlatformContext(PlatformContext* platformContext) {
    mPlatformContext = platformContext;
}

int32_t OutputEntityExt::addOutputTarget(const OutputConfig& config) {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    int32_t targetId = mNextTargetId++;
    mOutputTargets[targetId] = config;
    
    return targetId;
}

bool OutputEntityExt::removeOutputTarget(int32_t targetId) {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    auto it = mOutputTargets.find(targetId);
    if (it == mOutputTargets.end()) {
        return false;
    }
    
    mOutputTargets.erase(it);
    return true;
}

bool OutputEntityExt::updateOutputTarget(int32_t targetId, const OutputConfig& config) {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    auto it = mOutputTargets.find(targetId);
    if (it == mOutputTargets.end()) {
        return false;
    }
    
    it->second = config;
    return true;
}

void OutputEntityExt::setOutputTargetEnabled(int32_t targetId, bool enabled) {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    auto it = mOutputTargets.find(targetId);
    if (it != mOutputTargets.end()) {
        it->second.enabled = enabled;
    }
}

std::vector<int32_t> OutputEntityExt::getOutputTargets() const {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    std::vector<int32_t> targets;
    targets.reserve(mOutputTargets.size());
    
    for (const auto& [id, config] : mOutputTargets) {
        targets.push_back(id);
    }
    
    return targets;
}

void OutputEntityExt::clearOutputTargets() {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    mOutputTargets.clear();
}

// 快捷配置方法（占位实现）
int32_t OutputEntityExt::setupDisplayOutput(void* surface, int32_t width, int32_t height) {
    OutputConfig config;
    config.targetType = OutputTargetType::Display;
    config.displayConfig.surface = surface;
    config.displayConfig.width = width;
    config.displayConfig.height = height;
    
    return addOutputTarget(config);
}

int32_t OutputEntityExt::setupEncoderOutput(void* encoderSurface, EncoderType encoderType) {
    OutputConfig config;
    config.targetType = OutputTargetType::Encoder;
    config.encoderConfig.encoderSurface = encoderSurface;
    config.encoderConfig.encoderType = encoderType;
    
    return addOutputTarget(config);
}

int32_t OutputEntityExt::setupCallbackOutput(FrameCallback callback, OutputDataFormat dataFormat) {
    OutputConfig config;
    config.targetType = OutputTargetType::Callback;
    config.callbackConfig.frameCallback = callback;
    config.callbackConfig.dataFormat = dataFormat;
    
    return addOutputTarget(config);
}

int32_t OutputEntityExt::setupTextureOutput(bool shareTexture) {
    OutputConfig config;
    config.targetType = OutputTargetType::Texture;
    config.textureConfig.shareTexture = shareTexture;
    
    return addOutputTarget(config);
}

int32_t OutputEntityExt::setupFileOutput(const std::string& filePath, const std::string& fileFormat) {
    OutputConfig config;
    config.targetType = OutputTargetType::File;
    config.fileConfig.filePath = filePath;
    config.fileConfig.fileFormat = fileFormat;
    
    return addOutputTarget(config);
}

bool OutputEntityExt::start() {
    mIsRunning = true;
    mIsPaused = false;
    return true;
}

void OutputEntityExt::stop() {
    mIsRunning = false;
    mIsPaused = false;
}

void OutputEntityExt::pause() {
    mIsPaused = true;
}

void OutputEntityExt::resume() {
    mIsPaused = false;
}

std::shared_ptr<lrengine::render::LRTexture> OutputEntityExt::getOutputTexture() const {
    std::lock_guard<std::mutex> lock(mLastOutputMutex);
    if (mLastOutput) {
        return mLastOutput->getTexture();
    }
    return nullptr;
}

FramePacketPtr OutputEntityExt::getLastOutput() const {
    std::lock_guard<std::mutex> lock(mLastOutputMutex);
    return mLastOutput;
}

size_t OutputEntityExt::readPixels(uint8_t* buffer, size_t bufferSize, OutputDataFormat dataFormat) {
    // TODO: 实现
    return 0;
}

OutputEntityExt::OutputStats OutputEntityExt::getStats() const {
    std::lock_guard<std::mutex> lock(mStatsMutex);
    return mStats;
}

void OutputEntityExt::resetStats() {
    std::lock_guard<std::mutex> lock(mStatsMutex);
    mStats = OutputStats();
}

bool OutputEntityExt::process(const std::vector<FramePacketPtr>& inputs,
                              std::vector<FramePacketPtr>& outputs,
                              PipelineContext& context) {
    // TODO: 完整实现
    if (inputs.empty()) {
        return false;
    }
    
    FramePacketPtr input = inputs[0];
    
    // 更新最后输出
    {
        std::lock_guard<std::mutex> lock(mLastOutputMutex);
        mLastOutput = input;
    }
    
    // 输出到各个目标
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    for (const auto& [id, config] : mOutputTargets) {
        if (!config.enabled) continue;
        
        // TODO: 实际处理
        processOutputTarget(config, input);
    }
    
    // 更新统计
    {
        std::lock_guard<std::mutex> lock(mStatsMutex);
        mStats.totalFrames++;
    }
    
    outputs.push_back(input);
    return true;
}

bool OutputEntityExt::processOutputTarget(const OutputConfig& config, FramePacketPtr input) {
    // TODO: 完整实现
    return true;
}

bool OutputEntityExt::renderToDisplay(const DisplayOutputConfig& config, FramePacketPtr input) {
    // TODO: 实现
    return true;
}

bool OutputEntityExt::outputToEncoder(const EncoderOutputConfig& config, FramePacketPtr input) {
    // TODO: 实现
    return true;
}

bool OutputEntityExt::executeCallback(const CallbackOutputConfig& config, FramePacketPtr input) {
    // TODO: 实现
    if (config.frameCallback) {
        config.frameCallback(input);
        return true;
    }
    return false;
}

bool OutputEntityExt::outputTexture(const TextureOutputConfig& config, FramePacketPtr input) {
    // TODO: 实现
    return true;
}

bool OutputEntityExt::saveToFile(const FileOutputConfig& config, FramePacketPtr input) {
    // TODO: 实现
    return true;
}

bool OutputEntityExt::outputToPlatform(const PlatformOutputConfig& config, FramePacketPtr input) {
    // TODO: 实现
    return true;
}

bool OutputEntityExt::convertFormat(FramePacketPtr input, OutputDataFormat targetFormat, uint8_t* outputBuffer) {
    // TODO: 实现
    return true;
}

} // namespace pipeline
