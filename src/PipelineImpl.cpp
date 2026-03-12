#include "PipelineImpl.h"
#include "pipeline/PipelineNew.h"
#include "pipeline/core/PipelineManager.h"
#include "pipeline/core/PipelineError.h"
#include "pipeline/platform/PlatformContext.h"
#include "pipeline/utils/PipelineLog.h"
#include <string>

namespace pipeline {

// ============================================================================
// PipelineImpl 实现
// ============================================================================

PipelineImpl::PipelineImpl(BuilderState state) 
    : mBuilderState(std::move(state))
    , mRuntimeState(PipelineState::Created) {
}

PipelineImpl::~PipelineImpl() {
    destroy();
}

bool PipelineImpl::start() {
    std::lock_guard<std::mutex> lock(mInitMutex);
    
    if (mInitialized) {
        if (mRuntimeState == PipelineState::Paused) {
            resume();
            return true;
        }
        return true;
    }
    
    // 应用预设配置
    applyPresetConfig();
    
    // 初始化内部组件
    if (!initializeInternal()) {
        PIPELINE_LOGE("PipelineImpl::start() - initializeInternal failed");
        return false;
    }
    
    // 设置输入实体
    if (!setupInputEntity()) {
        PIPELINE_LOGE("PipelineImpl::start() - setupInputEntity failed");
        return false;
    }
    
    // 设置输出实体
    if (!setupOutputEntity()) {
        PIPELINE_LOGE("PipelineImpl::start() - setupOutputEntity failed");
        return false;
    }
    
    // 应用质量设置
    if (!applyQualitySettings()) {
        PIPELINE_LOGE("PipelineImpl::start() - applyQualitySettings failed");
        return false;
    }
    
    // 启动 PipelineManager
    if (mManager && !mManager->start()) {
        PIPELINE_LOGE("PipelineImpl::start() - manager start failed");
        return false;
    }
    
    mInitialized = true;
    setRuntimeState(PipelineState::Running);
    
    return true;
}

void PipelineImpl::pause() {
    if (mManager && mRuntimeState == PipelineState::Running) {
        mManager->pause();
        setRuntimeState(PipelineState::Paused);
    }
}

void PipelineImpl::resume() {
    if (mManager && mRuntimeState == PipelineState::Paused) {
        mManager->resume();
        setRuntimeState(PipelineState::Running);
    }
}

void PipelineImpl::stop() {
    if (mManager) {
        mManager->stop();
    }
    setRuntimeState(PipelineState::Stopped);
}

void PipelineImpl::destroy() {
    stop();

    if (mInputStrategy) {
        mInputStrategy->destroy();
        mInputStrategy.reset();
    }

    if (mOutputStrategy) {
        mOutputStrategy->destroy();
        mOutputStrategy.reset();
    }

    if (mManager) {
        mManager->destroy();
        mManager.reset();
    }

    mPlatformContext.reset();
    mInitialized = false;
    setRuntimeState(PipelineState::Created);
}

PipelineState PipelineImpl::getState() const {
    std::lock_guard<std::mutex> lock(mStateMutex);
    return mRuntimeState;
}

bool PipelineImpl::isRunning() const {
    std::lock_guard<std::mutex> lock(mStateMutex);
    return mRuntimeState == PipelineState::Running;
}

// ============================================================================
// 输入接口
// ============================================================================

Result<void> PipelineImpl::feedRGBA(const uint8_t* data, uint32_t width, uint32_t height,
                                    uint32_t stride, uint64_t timestamp) {
    if (!mInputStrategy) {
        return Result<void>::error(PipelineError::internalError("Input strategy not initialized"));
    }
    if (!mInputStrategy->feedRGBA(data, width, height, stride, timestamp)) {
        return Result<void>::error(PipelineError::runtimeError("Failed to feed RGBA data"));
    }
    return Result<void>::success();
}

Result<void> PipelineImpl::feedYUV420(const uint8_t* yData, const uint8_t* uData,
                                      const uint8_t* vData, uint32_t width, uint32_t height,
                                      uint64_t timestamp) {
    if (!mInputStrategy) {
        return Result<void>::error(PipelineError::internalError("Input strategy not initialized"));
    }
    if (!mInputStrategy->feedYUV420(yData, uData, vData, width, height, timestamp)) {
        return Result<void>::error(PipelineError::runtimeError("Failed to feed YUV420 data"));
    }
    return Result<void>::success();
}

Result<void> PipelineImpl::feedNV12(const uint8_t* yData, const uint8_t* uvData,
                                    uint32_t width, uint32_t height, bool isNV21,
                                    uint64_t timestamp) {
    if (!mInputStrategy) {
        return Result<void>::error(PipelineError::internalError("Input strategy not initialized"));
    }
    if (!mInputStrategy->feedNV12(yData, uvData, width, height, isNV21, timestamp)) {
        return Result<void>::error(PipelineError::runtimeError("Failed to feed NV12 data"));
    }
    return Result<void>::success();
}

Result<void> PipelineImpl::feedTexture(std::shared_ptr<::lrengine::render::LRTexture> texture,
                                       uint32_t width, uint32_t height, uint64_t timestamp) {
    if (!mInputStrategy) {
        return Result<void>::error(PipelineError::internalError("Input strategy not initialized"));
    }
    if (!mInputStrategy->feedTexture(std::move(texture), width, height, timestamp)) {
        return Result<void>::error(PipelineError::runtimeError("Failed to feed texture"));
    }
    return Result<void>::success();
}

// ============================================================================
// Entity 管理
// ============================================================================

EntityId PipelineImpl::addEntity(ProcessEntityPtr entity) {
    if (mManager && entity) {
        return mManager->addEntity(entity);
    }
    return InvalidEntityId;
}

bool PipelineImpl::removeEntity(EntityId entityId) {
    if (mManager) {
        return mManager->removeEntity(entityId);
    }
    return false;
}

void PipelineImpl::setEntityEnabled(EntityId entityId, bool enabled) {
    if (mManager) {
        auto entity = mManager->getEntity(entityId);
        if (entity) {
            entity->setEnabled(enabled);
        }
    }
}

EntityId PipelineImpl::addBeautyFilter(float smoothLevel, float whitenLevel) {
    (void)smoothLevel; (void)whitenLevel;
    // TODO: 创建 BeautyEntity
    return InvalidEntityId;
}

EntityId PipelineImpl::addColorFilter(const std::string& filterName, float intensity) {
    (void)filterName; (void)intensity;
    // TODO: 创建 ColorFilterEntity
    return InvalidEntityId;
}

EntityId PipelineImpl::addSharpenFilter(float amount) {
    (void)amount;
    // TODO: 创建 SharpenEntity
    return InvalidEntityId;
}

// ============================================================================
// 输出管理
// ============================================================================

int32_t PipelineImpl::addDisplayOutput(void* surface, uint32_t width, uint32_t height) {
    if (mOutputStrategy) {
        return mOutputStrategy->createDisplayOutput(surface, width, height);
    }
    return -1;
}

int32_t PipelineImpl::addCallbackOutput(
    std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback,
    output::OutputFormat format) {
    if (mOutputStrategy) {
        return mOutputStrategy->createCallbackOutput(std::move(callback), format);
    }
    return -1;
}

bool PipelineImpl::removeOutputTarget(int32_t targetId) {
    if (mOutputStrategy) {
        return mOutputStrategy->removeOutput(targetId);
    }
    return false;
}

void PipelineImpl::setOutputTargetEnabled(int32_t targetId, bool enabled) {
    if (mOutputStrategy) {
        mOutputStrategy->setOutputEnabled(targetId, enabled);
    }
}

// ============================================================================
// 配置
// ============================================================================

void PipelineImpl::setOutputResolution(uint32_t width, uint32_t height) {
    (void)width; (void)height;
    // TODO: 实现设置输出分辨率
}

void PipelineImpl::setRotation(int32_t degrees) {
    (void)degrees;
    // TODO: 实现设置旋转
}

void PipelineImpl::setMirror(bool horizontal, bool vertical) {
    (void)horizontal; (void)vertical;
    // TODO: 实现设置镜像
}

void PipelineImpl::setFrameRateLimit(int32_t fps) {
    (void)fps;
    // TODO: 实现帧率限制
}

// ============================================================================
// 回调
// ============================================================================

void PipelineImpl::setFrameProcessedCallback(std::function<void(FramePacketPtr)> callback) {
    mFrameCallback = std::move(callback);
    if (mManager) {
        mManager->setFrameCompleteCallback(mFrameCallback);
    }
}

void PipelineImpl::setErrorCallback(std::function<void(const std::string&)> callback) {
    mErrorCallback = std::move(callback);
    if (mManager) {
        mManager->setErrorCallback([this](EntityId id, const std::string& msg) {
            (void)id;
            if (mErrorCallback) {
                mErrorCallback(msg);
            }
        });
    }
}

void PipelineImpl::setStateCallback(std::function<void(PipelineState)> callback) {
    mStateCallback = std::move(callback);
}

// ============================================================================
// 统计
// ============================================================================

ExecutionStats PipelineImpl::getStats() const {
    if (mManager) {
        return mManager->getStats();
    }
    return ExecutionStats{};
}

double PipelineImpl::getAverageProcessTime() const {
    // TODO: 实现获取平均处理时间
    return 0.0;
}

std::string PipelineImpl::exportGraph() const {
    if (mManager) {
        return mManager->exportGraphToDot();
    }
    return "";
}

// ============================================================================
// 内部辅助方法
// ============================================================================

bool PipelineImpl::initializeInternal() {
    // 创建 PlatformContext
    mPlatformContext = std::make_unique<PlatformContext>();
    if (!mPlatformContext->initialize(mBuilderState.platformConfig)) {
        PIPELINE_LOGE("PipelineImpl::initializeInternal() - PlatformContext init failed");
        return false;
    }

    // 创建策略对象
    mInputStrategy = strategy::PlatformStrategyFactory::createInputStrategy(
        mBuilderState.platformConfig.platform, mBuilderState.inputFormat);
    mOutputStrategy = strategy::PlatformStrategyFactory::createOutputStrategy(
        mBuilderState.platformConfig.platform);

    if (!mInputStrategy || !mOutputStrategy) {
        PIPELINE_LOGE("PipelineImpl::initializeInternal() - Strategy creation failed");
        return false;
    }

    if (!mInputStrategy->initialize(this, mBuilderState.inputWidth, mBuilderState.inputHeight)) {
        PIPELINE_LOGE("PipelineImpl::initializeInternal() - InputStrategy init failed");
        return false;
    }

    if (!mOutputStrategy->initialize(this)) {
        PIPELINE_LOGE("PipelineImpl::initializeInternal() - OutputStrategy init failed");
        return false;
    }

    // 创建 PipelineManager
    PipelineConfig config;
    config.name = "Pipeline";
    config.maxConcurrentFrames = static_cast<uint32_t>(mBuilderState.maxQueueSize);
    config.enableParallelExecution = mBuilderState.enableMultiThread;

    mManager = PipelineManager::create(mRenderContext, config);
    if (!mManager) {
        PIPELINE_LOGE("PipelineImpl::initializeInternal() - PipelineManager create failed");
        return false;
    }

    if (!mManager->initialize()) {
        PIPELINE_LOGE("PipelineImpl::initializeInternal() - PipelineManager init failed");
        return false;
    }

    return true;
}

bool PipelineImpl::setupInputEntity() {
    // TODO: 根据 inputFormat 设置输入实体
    return true;
}

bool PipelineImpl::setupOutputEntity() {
    // TODO: 根据 outputs 配置设置输出实体
    return true;
}

bool PipelineImpl::applyQualitySettings() {
    // TODO: 根据 quality 设置质量参数
    return true;
}

void PipelineImpl::applyPresetConfig() {
    switch (mBuilderState.preset) {
        case PipelinePreset::CameraPreview:
            // 相机预览默认配置
            break;
        case PipelinePreset::CameraRecord:
            // 相机录制默认配置
            break;
        case PipelinePreset::ImageProcess:
            // 图像处理默认配置
            break;
        case PipelinePreset::LiveStream:
            // 直播推流默认配置
            break;
        case PipelinePreset::VideoPlayback:
            // 视频播放默认配置
            break;
        case PipelinePreset::Custom:
        default:
            // 自定义配置，使用用户设置
            break;
    }
}

void PipelineImpl::setRuntimeState(PipelineState state) {
    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        mRuntimeState = state;
    }
    notifyStateChange(state);
}

void PipelineImpl::notifyError(const std::string& message) {
    if (mErrorCallback) {
        mErrorCallback(message);
    }
}

void PipelineImpl::notifyStateChange(PipelineState state) {
    if (mStateCallback) {
        mStateCallback(state);
    }
}

} // namespace pipeline
