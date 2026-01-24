/**
 * @file PipelineFacade.cpp
 * @brief Pipeline外观接口实现
 * 
 * TODO: 完整实现 - Phase 3
 */

#include "pipeline/PipelineFacade.h"
#include "pipeline/entity/IOEntity.h"

#include <algorithm>
#include <vector>

#include <lrengine/core/LRRenderContext.h>
#include <lrengine/core/LRTypes.h>

#if defined(__APPLE__)
#include <CoreVideo/CoreVideo.h>
#endif

namespace pipeline {

namespace {

lrengine::render::LRRenderContext* createRenderContextForSurface(
    void* surface,
    uint32_t width,
    uint32_t height,
    bool debug)
{
    using namespace lrengine::render;

    RenderContextDescriptor desc;
    desc.backend = Backend::Metal;
    desc.windowHandle = surface;
    desc.width = width;
    desc.height = height;
    desc.vsync = true;
    desc.debug = debug;
    desc.sampleCount = 1;
    desc.applicationName = "PipelineCameraPreview";

    return LRRenderContext::Create(desc);
}

} // anonymous namespace

// 占位实现，Phase 3 会完整实现
PipelineFacade::PipelineFacade(const PipelineFacadeConfig& config)
    : mConfig(config)
    , mRenderContext(nullptr)
    , mInputEntityId(InvalidEntityId)
    , mOutputEntityId(InvalidEntityId)
    , mOutputEntity(nullptr)
    , mInitialized(false)
    , mCurrentFPS(0.0)
{
}

PipelineFacade::~PipelineFacade() {
    destroy();
}

std::shared_ptr<PipelineFacade> PipelineFacade::create(const PipelineFacadeConfig& config) {
    return std::shared_ptr<PipelineFacade>(new PipelineFacade(config));
}

bool PipelineFacade::initialize() {
    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        if (mInitialized) {
            return true;
        }
    }

    // 初始化平台上下文（在iOS/Android等需要平台抽象的场景）
    if (!initializePlatformContext()) {
        if (mCallbacks.onError) {
            mCallbacks.onError("Failed to initialize PlatformContext");
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        mInitialized = true;
    }

    return true;
}

bool PipelineFacade::start() {
    if (!initialize()) {
        return false;
    }

    if (!mPipelineManager) {
        // 需要先通过输出配置接口创建实际管线（例如 setupDisplayOutput）
        return false;
    }

    if (!mPipelineManager->start()) {
        if (mCallbacks.onError) {
            mCallbacks.onError("Failed to start pipeline");
        }
        return false;
    }

    if (mOutputEntity) {
        mOutputEntity->start();
    }

    return true;
}

void PipelineFacade::pause() {
    if (mPipelineManager) {
        mPipelineManager->pause();
    }
    if (mOutputEntity) {
        mOutputEntity->pause();
    }
}

void PipelineFacade::resume() {
    if (mPipelineManager) {
        mPipelineManager->resume();
    }
    if (mOutputEntity) {
        mOutputEntity->resume();
    }
}

void PipelineFacade::stop() {
    if (mPipelineManager) {
        mPipelineManager->stop();
    }
    if (mOutputEntity) {
        mOutputEntity->stop();
    }
}

void PipelineFacade::destroy() {
    if (mPipelineManager) {
        mPipelineManager->destroy();
        mPipelineManager.reset();
    }

    if (mPlatformContext) {
        mPlatformContext->destroy();
        mPlatformContext.reset();
    }

    if (mRenderContext) {
        lrengine::render::LRRenderContext::Destroy(mRenderContext);
        mRenderContext = nullptr;
    }

    mInputEntityId = InvalidEntityId;
    mOutputEntityId = InvalidEntityId;
    mOutputEntity = nullptr;

    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        mInitialized = false;
        mCurrentFPS = 0.0;
    }
}

PipelineState PipelineFacade::getState() const {
    if (mPipelineManager) {
        return mPipelineManager->getState();
    }

    std::lock_guard<std::mutex> lock(mStateMutex);
    return mInitialized ? PipelineState::Initialized : PipelineState::Created;
}

bool PipelineFacade::isRunning() const {
    if (mPipelineManager) {
        return mPipelineManager->isRunning();
    }
    return false;
}

bool PipelineFacade::feedFrame(const uint8_t* data, uint32_t width, uint32_t height,
                               InputFormat format, uint64_t timestamp) {
    switch (format) {
        case InputFormat::RGBA:
            return feedRGBA(data, width, height, 0, timestamp);
        case InputFormat::YUV420:
            // 该接口不区分 Y/U/V 指针，仅作为简单包装时暂不支持
            return false;
        case InputFormat::NV12:
        case InputFormat::NV21:
        case InputFormat::OES:
        default:
            return false;
    }
}

bool PipelineFacade::feedRGBA(const uint8_t* data, uint32_t width, uint32_t height,
                              uint32_t stride, uint64_t timestamp) {
    if (!initialize() || !mPipelineManager) {
        return false;
    }

    auto packet = mPipelineManager->feedRGBA(data, width, height, stride, timestamp);
    return packet != nullptr;
}

bool PipelineFacade::feedYUV420(const uint8_t* yData, const uint8_t* uData, const uint8_t* vData,
                                uint32_t width, uint32_t height, uint64_t timestamp) {
    if (!initialize() || !mPipelineManager) {
        return false;
    }

    auto packet = mPipelineManager->feedYUV420(yData, uData, vData, width, height, timestamp);
    return packet != nullptr;
}

bool PipelineFacade::feedNV12(const uint8_t* yData, const uint8_t* uvData,
                              uint32_t width, uint32_t height, bool isNV21, uint64_t timestamp) {
    (void)yData;
    (void)uvData;
    (void)width;
    (void)height;
    (void)isNV21;
    (void)timestamp;
    // NV12/NV21 入口在当前版本中暂未实现
    return false;
}

bool PipelineFacade::feedTexture(std::shared_ptr<lrengine::render::LRTexture> texture,
                                 uint32_t width, uint32_t height, uint64_t timestamp) {
    if (!initialize() || !mPipelineManager) {
        return false;
    }

    auto inputEntity = mPipelineManager->getInputEntity();
    if (!inputEntity || !texture) {
        return false;
    }

    auto packet = inputEntity->feedTexture(std::move(texture), width, height, timestamp);
    if (packet && mPipelineManager->isRunning()) {
        mPipelineManager->processFrame(packet);
        return true;
    }
    return false;
}

#if defined(__APPLE__)

bool PipelineFacade::feedPixelBuffer(void* pixelBuffer, uint64_t timestamp) {
    if (!pixelBuffer) {
        return false;
    }

    if (!initialize() || !mPipelineManager) {
        // 通常需要先通过 setupDisplayOutput 创建并启动管线
        return false;
    }

    auto inputEntity = mPipelineManager->getInputEntity();
    if (!inputEntity) {
        return false;
    }

    CVPixelBufferRef buffer = static_cast<CVPixelBufferRef>(pixelBuffer);
    CVReturn lockStatus = CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    if (lockStatus != kCVReturnSuccess) {
        return false;
    }

    const size_t width = CVPixelBufferGetWidth(buffer);
    const size_t height = CVPixelBufferGetHeight(buffer);
    const OSType format = CVPixelBufferGetPixelFormatType(buffer);
    const size_t bytesPerRow = CVPixelBufferGetBytesPerRow(buffer);
    auto* base = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(buffer));

    bool ok = false;

    if (format == kCVPixelFormatType_32BGRA) {
        // 将 BGRA 转为 RGBA 再送入管线
        const size_t pixelCount = width * height;
        std::vector<uint8_t> rgba(pixelCount * 4);

        for (size_t y = 0; y < height; ++y) {
            const uint8_t* srcRow = base + y * bytesPerRow;
            uint8_t* dstRow = rgba.data() + y * width * 4;
            for (size_t x = 0; x < width; ++x) {
                const uint8_t* s = srcRow + x * 4;
                uint8_t* d = dstRow + x * 4;
                d[0] = s[2]; // R
                d[1] = s[1]; // G
                d[2] = s[0]; // B
                d[3] = s[3]; // A
            }
        }

        auto packet = inputEntity->feedRGBA(rgba.data(),
                                            static_cast<uint32_t>(width),
                                            static_cast<uint32_t>(height),
                                            static_cast<uint32_t>(width * 4),
                                            timestamp);
        if (packet && mPipelineManager->isRunning()) {
            mPipelineManager->processFrame(packet);
            ok = true;
        }
    } else if (format == kCVPixelFormatType_32RGBA) {
        auto packet = inputEntity->feedRGBA(base,
                                            static_cast<uint32_t>(width),
                                            static_cast<uint32_t>(height),
                                            static_cast<uint32_t>(bytesPerRow),
                                            timestamp);
        if (packet && mPipelineManager->isRunning()) {
            mPipelineManager->processFrame(packet);
            ok = true;
        }
    } else {
        // 其他格式（如 NV12）当前未实现，可在之后扩展
        ok = false;
    }

    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    return ok;
}

#endif // defined(__APPLE__)

int32_t PipelineFacade::setupDisplayOutput(void* surface, int32_t width, int32_t height) {
    if (!surface) {
        return -1;
    }

    if (!initialize()) {
        return -1;
    }

    // 基于 surface 创建渲染上下文（仅第一次调用时）
    if (!mRenderContext) {
        uint32_t w = width > 0 ? static_cast<uint32_t>(width) : mConfig.renderWidth;
        uint32_t h = height > 0 ? static_cast<uint32_t>(height) : mConfig.renderHeight;
        bool debug = mConfig.enableDebugLog;

        mRenderContext = createRenderContextForSurface(surface, w, h, debug);
        if (!mRenderContext) {
            if (mCallbacks.onError) {
                mCallbacks.onError("Failed to create render context for display output");
            }
            return -1;
        }
    }

    // 创建 PipelineManager 与预设管线
    if (!mPipelineManager) {
        PipelineConfig pipelineConfig;
        pipelineConfig.name = "CameraPreview";
#if defined(__APPLE__)
        pipelineConfig.preferMetal = true;
#endif
        pipelineConfig.enableProfiling = mConfig.enableProfiling;
        pipelineConfig.enableLogging = mConfig.enableDebugLog;

        mPipelineManager = PipelineManager::create(mRenderContext, pipelineConfig);
        if (!mPipelineManager || !mPipelineManager->initialize()) {
            if (mCallbacks.onError) {
                mCallbacks.onError("Failed to initialize PipelineManager");
            }
            mPipelineManager.reset();
            return -1;
        }

        if (!createPresetPipeline(mConfig.preset)) {
            if (mCallbacks.onError) {
                mCallbacks.onError("Failed to create preset pipeline");
            }
            return -1;
        }

        // 将回调桥接到底层 PipelineManager
        if (mCallbacks.onFrameProcessed) {
            mPipelineManager->setFrameCompleteCallback(mCallbacks.onFrameProcessed);
        }
        if (mCallbacks.onFrameDropped) {
            mPipelineManager->setFrameDroppedCallback(mCallbacks.onFrameDropped);
        }
        if (mCallbacks.onError) {
            mPipelineManager->setErrorCallback(
                [this](EntityId, const std::string& msg) {
                    if (mCallbacks.onError) {
                        mCallbacks.onError(msg);
                    }
                });
        }
        if (mCallbacks.onStateChanged) {
            mPipelineManager->setStateCallback(mCallbacks.onStateChanged);
        }

        mPipelineManager->start();
    }

    if (!mOutputEntity) {
        auto outputBase = mPipelineManager->getEntity(mOutputEntityId);
        mOutputEntity = outputBase ? dynamic_cast<OutputEntityExt*>(outputBase.get()) : nullptr;
        if (mOutputEntity) {
            mOutputEntity->setRenderContext(mRenderContext);
            if (mPlatformContext) {
                mOutputEntity->setPlatformContext(mPlatformContext.get());
            }
        }
    }

    if (!mOutputEntity) {
        return -1;
    }

    return mOutputEntity->setupDisplayOutput(surface, width, height);
}

int32_t PipelineFacade::setupEncoderOutput(void* encoderSurface, EncoderType encoderType) {
    if (!mOutputEntity) {
        return -1;
    }
    return mOutputEntity->setupEncoderOutput(encoderSurface, encoderType);
}

int32_t PipelineFacade::setupCallbackOutput(FrameCallback callback, OutputDataFormat dataFormat) {
    if (!mOutputEntity) {
        return -1;
    }
    return mOutputEntity->setupCallbackOutput(std::move(callback), dataFormat);
}

int32_t PipelineFacade::setupFileOutput(const std::string& filePath) {
    if (!mOutputEntity) {
        return -1;
    }
    return mOutputEntity->setupFileOutput(filePath);
}

bool PipelineFacade::removeOutputTarget(int32_t targetId) {
    if (!mOutputEntity) {
        return false;
    }
    return mOutputEntity->removeOutputTarget(targetId);
}

void PipelineFacade::setOutputTargetEnabled(int32_t targetId, bool enabled) {
    if (!mOutputEntity) {
        return;
    }
    mOutputEntity->setOutputTargetEnabled(targetId, enabled);
}

// 滤镜占位
EntityId PipelineFacade::addBeautyFilter(float smoothLevel, float whitenLevel) {
    // TODO: 实现
    return InvalidEntityId;
}

EntityId PipelineFacade::addColorFilter(const std::string& filterName, float intensity) {
    // TODO: 实现
    return InvalidEntityId;
}

EntityId PipelineFacade::addSharpenFilter(float amount) {
    // TODO: 实现
    return InvalidEntityId;
}

EntityId PipelineFacade::addBlurFilter(float radius) {
    // TODO: 实现
    return InvalidEntityId;
}

EntityId PipelineFacade::addCustomEntity(ProcessEntityPtr entity) {
    // TODO: 实现
    return InvalidEntityId;
}

bool PipelineFacade::removeEntity(EntityId entityId) {
    // TODO: 实现
    return false;
}

void PipelineFacade::setEntityEnabled(EntityId entityId, bool enabled) {
    // TODO: 实现
}

// 其他方法占位
void PipelineFacade::setOutputResolution(uint32_t width, uint32_t height) {}
void PipelineFacade::setRotation(int32_t degrees) {}
void PipelineFacade::setMirror(bool horizontal, bool vertical) {}
void PipelineFacade::setCropRect(float x, float y, float width, float height) {}
void PipelineFacade::setFrameRateLimit(int32_t fps) {}

void PipelineFacade::setCallbacks(const PipelineCallbacks& callbacks) {
    mCallbacks = callbacks;
}

void PipelineFacade::setFrameProcessedCallback(std::function<void(FramePacketPtr)> callback) {
    mCallbacks.onFrameProcessed = callback;
}

void PipelineFacade::setErrorCallback(std::function<void(const std::string&)> callback) {
    mCallbacks.onError = callback;
}

void PipelineFacade::setStateCallback(std::function<void(PipelineState)> callback) {
    mCallbacks.onStateChanged = callback;
}

ExecutionStats PipelineFacade::getStats() const {
    // TODO: 实现
    return ExecutionStats();
}

OutputEntityExt::OutputStats PipelineFacade::getOutputStats() const {
    // TODO: 实现
    return OutputEntityExt::OutputStats();
}

void PipelineFacade::resetStats() {}

double PipelineFacade::getCurrentFPS() const {
    return mCurrentFPS;
}

double PipelineFacade::getAverageProcessTime() const {
    // TODO: 实现
    return 0.0;
}

std::string PipelineFacade::exportGraph() const {
    // TODO: 实现
    return "";
}

bool PipelineFacade::saveConfig(const std::string& filePath) const {
    // TODO: 实现
    return false;
}

bool PipelineFacade::loadConfig(const std::string& filePath) {
    // TODO: 实现配置文件加载
    (void)filePath;
    return false;
}

bool PipelineFacade::createPresetPipeline(PipelinePreset preset) {
    if (!mPipelineManager) {
        return false;
    }

    // 当前仅实现 CameraPreview 预设，其它预设暂时复用相同结构
    if (preset != PipelinePreset::CameraPreview && preset != PipelinePreset::Custom) {
        preset = PipelinePreset::CameraPreview;
    }

    // 创建输入和输出实体
    EntityId inputId = mPipelineManager->createEntity<InputEntity>("InputEntity");
    EntityId outputId = mPipelineManager->createEntity<OutputEntityExt>("OutputEntityExt");

    mInputEntityId = inputId;
    mOutputEntityId = outputId;

    auto inputBase = mPipelineManager->getEntity(inputId);
    auto outputBase = mPipelineManager->getEntity(outputId);

    auto* inputEntity = dynamic_cast<InputEntity*>(inputBase.get());
    mOutputEntity = dynamic_cast<OutputEntityExt*>(outputBase.get());

    if (inputEntity) {
        inputEntity->setRenderContext(mRenderContext);
    }
    if (mOutputEntity) {
        mOutputEntity->setRenderContext(mRenderContext);
        if (mPlatformContext) {
            mOutputEntity->setPlatformContext(mPlatformContext.get());
        }
    }

    // 将输入与输出连接
    mPipelineManager->connect(inputId, outputId);
    mPipelineManager->setInputEntity(inputId);
    mPipelineManager->setOutputEntity(outputId);

    return inputEntity != nullptr && mOutputEntity != nullptr;
}

bool PipelineFacade::initializePlatformContext() {
#if defined(__ANDROID__) || defined(__APPLE__)
    if (!mPlatformContext) {
        mPlatformContext = std::make_unique<PlatformContext>();
    }

    PlatformContextConfig cfg = mConfig.platformConfig;

#if defined(__ANDROID__)
    if (cfg.platform == PlatformType::Unknown) {
        cfg.platform = PlatformType::Android;
        cfg.graphicsAPI = GraphicsAPI::OpenGLES;
    }
#endif

#if defined(__APPLE__)
    if (cfg.platform == PlatformType::Unknown) {
        cfg.platform = PlatformType::iOS;
        cfg.graphicsAPI = GraphicsAPI::Metal;
    }
#endif

    return mPlatformContext->initialize(cfg);
#else
    (void)this;
    return true;
#endif
}

bool PipelineFacade::initializeRenderContext() {
    if (mRenderContext) {
        return true;
    }

    using namespace lrengine::render;

    RenderContextDescriptor desc;
#if defined(__APPLE__)
    desc.backend = Backend::Metal;
#elif defined(__ANDROID__)
    desc.backend = Backend::OpenGLES;
#else
    desc.backend = Backend::OpenGL;
#endif
    desc.windowHandle = nullptr;
    desc.width = mConfig.renderWidth;
    desc.height = mConfig.renderHeight;
    desc.vsync = true;
    desc.debug = mConfig.enableDebugLog;
    desc.sampleCount = 1;
    desc.applicationName = "Pipeline";

    mRenderContext = LRRenderContext::Create(desc);
    return mRenderContext != nullptr;
}

// 工具函数
const char* getPipelineVersion() {
    return "1.0.0";
}

std::vector<PlatformType> getSupportedPlatforms() {
    return {
        PlatformType::Android,
        PlatformType::iOS,
        PlatformType::macOS,
        PlatformType::Windows,
        PlatformType::Linux
    };
}

bool isPlatformSupported(PlatformType platform) {
    auto platforms = getSupportedPlatforms();
    return std::find(platforms.begin(), platforms.end(), platform) != platforms.end();
}

PipelineFacadeConfig getRecommendedConfig(PipelinePreset preset, PlatformType platform) {
    PipelineFacadeConfig config;
    config.preset = preset;
    config.platformConfig.platform = platform;
    
    // TODO: 根据预设和平台设置推荐配置
    
    return config;
}

} // namespace pipeline
