/**
 * @file PipelineFacade.cpp
 * @brief Pipeline外观接口实现
 * 
 * 使用新的双路 I/O 模块：
 * - InputEntity: 支持双路分发（GPU + CPU）
 * - OutputEntity: 支持多目标输出
 * - DisplaySurface: 平台特定显示表面
 */

#include "pipeline/PipelineFacade.h"
#include "pipeline/utils/PipelineLog.h"
#include <algorithm>
#include <vector>

// 新的 I/O 模块
#include "pipeline/input/InputEntity.h"
#include "pipeline/output/OutputEntity.h"
#include "pipeline/output/DisplaySurface.h"

#include <lrengine/core/LRRenderContext.h>
#include <lrengine/core/LRTypes.h>

#if defined(__APPLE__)
#include <CoreVideo/CoreVideo.h>
#include "pipeline/input/ios/PixelBufferInputStrategy.h"
#include "pipeline/output/ios/iOSMetalSurface.h"
#endif

#if defined(__ANDROID__)
#include "pipeline/input/android/OESTextureInputStrategy.h"
#include "pipeline/output/android/AndroidEGLSurface.h"
#endif

namespace pipeline {

PipelineFacade::PipelineFacade(const PipelineFacadeConfig& config)
    : mConfig(config)
    , mRenderContext(nullptr)
    , mInitialized(false)
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
            PIPELINE_LOGW("PipelineFacade already initialized");
            return true;
        }
    }

    // 1. 初始化平台上下文
    if (!initializePlatformContext()) {
        if (mCallbacks.onError) {
            mCallbacks.onError("Failed to initialize PlatformContext");
        }
        PIPELINE_LOGE("Failed to initialize PlatformContext");
        return false;
    }
    
    // 2. 创建渲染上下文
    if (!initializeRenderContext()) {
        if (mCallbacks.onError) {
            mCallbacks.onError("Failed to initialize RenderContext");
        }
        PIPELINE_LOGE("Failed to initialize RenderContext");
        return false;
    }
    
    // 3. 创建 PipelineManager
    if (!mPipelineManager) {
        PipelineConfig pipelineConfig;
        pipelineConfig.name = "Pipeline";
#if defined(__APPLE__)
        pipelineConfig.preferMetal = true;
#endif
        pipelineConfig.enableProfiling = mConfig.enableProfiling;
        pipelineConfig.enableLogging = mConfig.enableDebugLog;
        
        mPipelineManager = PipelineManager::create(mRenderContext, pipelineConfig);
        if (!mPipelineManager || !mPipelineManager->initialize()) {
            if (mCallbacks.onError) {
                mCallbacks.onError("Failed to create PipelineManager");
            }
            PIPELINE_LOGE("Failed to create PipelineManager");
            return false;
        }
        
        // 4. 根据预设配置输入
        if (!setupInputBasedOnPreset()) {
            if (mCallbacks.onError) {
                mCallbacks.onError("Failed to setup input based on preset");
            }
            PIPELINE_LOGE("Failed to setup input based on preset");
            return false;
        }
        
        // 5. 创建输出实体
        if (!setupOutputEntity()) {
            if (mCallbacks.onError) {
                mCallbacks.onError("Failed to setup output entity");
            }
            PIPELINE_LOGE("Failed to setup output entity");
            return false;
        }
        
        // 6. 配置回调桥接
        setupCallbackBridges();
    }

    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        mInitialized = true;
    }

    PIPELINE_LOGI("PipelineFacade initialized successfully");
    return true;
}

bool PipelineFacade::start() {
    if (!initialize()) {
        PIPELINE_LOGE("Failed to initialize PipelineFacade");
        return false;
    }

    if (!mPipelineManager) {
        PIPELINE_LOGE("PipelineManager not created");
        return false;
    }

    if (!mPipelineManager->start()) {
        if (mCallbacks.onError) {
            mCallbacks.onError("Failed to start pipeline");
        }
        PIPELINE_LOGE("Failed to start pipeline");
        return false;
    }

    PIPELINE_LOGI("PipelineFacade started");
    return true;
}

void PipelineFacade::pause() {
    if (mPipelineManager) {
        mPipelineManager->pause();
    }
    PIPELINE_LOGI("PipelineFacade paused");
}

void PipelineFacade::resume() {
    if (mPipelineManager) {
        mPipelineManager->resume();
    }
    PIPELINE_LOGI("PipelineFacade resumed");
}

void PipelineFacade::stop() {
    if (mPipelineManager) {
        mPipelineManager->stop();
    }
    PIPELINE_LOGI("PipelineFacade stopped");
}

void PipelineFacade::destroy() {
    // PipelineManager 会负责释放所有内部资源（InputEntity, OutputEntity, DisplaySurface 等）
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

    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        mInitialized = false;
    }
    PIPELINE_LOGI("PipelineFacade destroyed");
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
            return false;
        case InputFormat::YUV420:
            return false;
        case InputFormat::NV12:
        case InputFormat::NV21:
        case InputFormat::OES:
        default:
            return false;
    }
}

#if defined(__APPLE__)

bool PipelineFacade::feedPixelBuffer(void* pixelBuffer, uint64_t timestamp) {
    if (!pixelBuffer) {
        PIPELINE_LOGE("Invalid pixel buffer");
        return false;
    }

    if (!mPipelineManager) {
        PIPELINE_LOGE("Pipeline not initialized");
        return false;
    }

    // 委托给 PipelineManager 的 InputEntity
    auto inputEntity = mPipelineManager->getInputEntity();
    if (!inputEntity) {
        PIPELINE_LOGE("InputEntity not configured");
        return false;
    }

    CVPixelBufferRef buffer = static_cast<CVPixelBufferRef>(pixelBuffer);

    // CVPixelBufferRef 的生命周期由调用者管理，异步任务可能在 buffer 释放后执行
    CVPixelBufferRetain(buffer);
    
    // 使用 InputEntity 提交 PixelBuffer
    input::InputData inputData;
    inputData.dataType = input::InputDataType::PlatformBuffer;
    inputData.platformBuffer = pixelBuffer;  // 传递平台 buffer
    
    // 当所有引用都释放后，自动调用 CVPixelBufferRelease
    inputData.platformBufferHolder = std::shared_ptr<void>(
        buffer,
        [](void* ptr) {
            if (ptr) {
                CVPixelBufferRelease(static_cast<CVPixelBufferRef>(ptr));
            }
        }
    );
    
    inputData.cpu.timestamp = static_cast<int64_t>(timestamp);
    inputData.cpu.width = static_cast<uint32_t>(CVPixelBufferGetWidth(buffer));
    inputData.cpu.height = static_cast<uint32_t>(CVPixelBufferGetHeight(buffer));
    
    return inputEntity->submitData(inputData);
}

#elif defined(__ANDROID__)
bool feedOES(uint32_t oesTextureId, uint32_t width, uint32_t height,
                 const float* transformMatrix = nullptr, uint64_t timestamp = 0) {
    if (!mPipelineManager) {
        PIPELINE_LOGE("Pipeline not initialized");
        return false;
    }

    return false;
}

#endif // defined(__APPLE__)

int32_t PipelineFacade::setupDisplayOutput(void* surface, int32_t width, int32_t height) {
    if (!surface) {
        PIPELINE_LOGE("Invalid surface");
        return -1;
    }

    // 确保已初始化
    if (!mPipelineManager) {
        PIPELINE_LOGE("Pipeline not initialized");
        return -1;
    }

    uint32_t w = width > 0 ? static_cast<uint32_t>(width) : mConfig.renderWidth;
    uint32_t h = height > 0 ? static_cast<uint32_t>(height) : mConfig.renderHeight;
    
    // 获取 MetalContextManager（iOS/macOS）
    void* metalManager = nullptr;
#if defined(__APPLE__)
    if (mPlatformContext) {
        metalManager = mPlatformContext->getIOSMetalManager();
        if (metalManager) {
            PIPELINE_LOGD("Passing MetalContextManager to setupDisplayOutput");
        }
    }
#endif
    
    int32_t targetId = mPipelineManager->setupDisplayOutput(surface, w, h, metalManager);
    if (targetId < 0) {
        PIPELINE_LOGE("Failed to setup display output");
        if (mCallbacks.onError) {
            mCallbacks.onError("Failed to setup display output");
        }
        return -1;
    }
    
    PIPELINE_LOGI("Display output setup successful, target ID: %d", targetId);
    return targetId;
}

int32_t PipelineFacade::setupEncoderOutput(void* encoderSurface, EncoderType encoderType) {
    if (!mPipelineManager) {
        PIPELINE_LOGE("Pipeline not initialized");
        return -1;
    }
    
    return mPipelineManager->setupEncoderOutput(encoderSurface, encoderType);
}

int32_t PipelineFacade::setupCallbackOutput(FrameCallback callback, OutputDataFormat dataFormat) {
    if (!mPipelineManager) {
        PIPELINE_LOGE("Pipeline not initialized");
        return -1;
    }
    
    // 将 FrameCallback 转换为 PipelineManager 需要的格式
    auto cpuCallback = [callback](const uint8_t* data, size_t size,
                                   uint32_t w, uint32_t h,
                                   output::OutputFormat fmt, int64_t ts) {
        if (callback) {
            // TODO: 将 OutputData 转换为 FramePacket 传给回调
            // 暂时略过，需要完善回调数据传递
        }
    };
    
    return mPipelineManager->setupCallbackOutput(cpuCallback, dataFormat);
}

int32_t PipelineFacade::setupFileOutput(const std::string& filePath) {
    if (!mPipelineManager) {
        PIPELINE_LOGE("Pipeline not initialized");
        return -1;
    }
    
    // TODO: 实现文件输出
    (void)filePath;
    PIPELINE_LOGW("setupFileOutput not yet implemented");
    return -1;
}

bool PipelineFacade::removeOutputTarget(int32_t targetId) {
    if (!mPipelineManager) {
        return false;
    }
    return mPipelineManager->removeOutputTarget(targetId);
}

void PipelineFacade::setOutputTargetEnabled(int32_t targetId, bool enabled) {
    // TODO: 在 PipelineManager 中实现
    (void)targetId;
    (void)enabled;
}

bool PipelineFacade::updateDisplayOutputSize(int32_t targetId, int32_t width, int32_t height) {
    if (!mPipelineManager) {
        return false;
    }
    
    return mPipelineManager->updateDisplayOutputSize(targetId, 
                                                     static_cast<uint32_t>(width), 
                                                     static_cast<uint32_t>(height));
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

uint64_t PipelineFacade::getOutputFrameCount() const {
    if (mPipelineManager) {
        auto outputEntity = mPipelineManager->getOutputEntity();
        if (outputEntity) {
            // 由 PipelineManager 统计,避免直接访问 OutputEntity
            return 0; // TODO: 通过 PipelineManager::getStats() 获取
        }
    }
    return 0;
}

void PipelineFacade::resetStats() {}

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

// =============================================================================
// 辅助方法
// =============================================================================

bool PipelineFacade::setupInputBasedOnPreset() {
    uint32_t width = mConfig.renderWidth;
    uint32_t height = mConfig.renderHeight;
    
    // 根据预设和平台选择输入类型
    switch (mConfig.preset) {
        case PipelinePreset::CameraPreview:
        case PipelinePreset::CameraRecord:
#if defined(__APPLE__)
            // iOS: 使用 PixelBuffer 输入
            {
                void* metalManager = nullptr;
                if (mPlatformContext) {
                    metalManager = mPlatformContext->getIOSMetalManager();
                }
                // 🔥 优化：CameraPreview 不需要 CPU 输出，CameraRecord 可能需要用于编码
                bool enableCPUOutput = (mConfig.preset == PipelinePreset::CameraRecord);
                EntityId inputId = mPipelineManager->setupPixelBufferInput(width, height, metalManager, enableCPUOutput);
                if (inputId == InvalidEntityId) {
                    PIPELINE_LOGE("Failed to setup PixelBuffer input");
                    return false;
                }
                PIPELINE_LOGI("PixelBuffer input configured for preset, CPU output: %s", 
                             enableCPUOutput ? "enabled" : "disabled");
                return true;
            }
#elif defined(__ANDROID__)
            // Android: 使用 OES 输入
            {
                EntityId inputId = mPipelineManager->setupOESInput(width, height);
                if (inputId == InvalidEntityId) {
                    PIPELINE_LOGE("Failed to setup OES input");
                    return false;
                }
                PIPELINE_LOGI("OES input configured for preset");
                return true;
            }
#else
            PIPELINE_LOGW("Camera preset not supported on this platform, using RGBA");
            // Fall through to RGBA
#endif
            
        case PipelinePreset::ImageProcess:
            // 图像处理：使用 RGBA 输入
            {
                EntityId inputId = mPipelineManager->setupRGBAInput(width, height);
                if (inputId == InvalidEntityId) {
                    PIPELINE_LOGE("Failed to setup RGBA input");
                    return false;
                }
                PIPELINE_LOGI("RGBA input configured for preset");
                return true;
            }
            
        default:
            // 默认使用 RGBA
            {
                EntityId inputId = mPipelineManager->setupRGBAInput(width, height);
                if (inputId == InvalidEntityId) {
                    PIPELINE_LOGE("Failed to setup default RGBA input");
                    return false;
                }
                PIPELINE_LOGI("Default RGBA input configured");
                return true;
            }
    }
}

bool PipelineFacade::setupOutputEntity() {
    // 委托给 PipelineManager 创建 OutputEntity
    auto outputEntity = std::make_shared<output::OutputEntity>("OutputEntity");
    
    output::OutputConfig outputConfig;
    outputConfig.enableMultiTarget = true;
    outputConfig.asyncOutput = mConfig.enableAsync;
    outputConfig.outputQueueSize = static_cast<size_t>(mConfig.maxQueueSize);
    outputEntity->configure(outputConfig);
    
    EntityId outputId = mPipelineManager->addEntity(outputEntity);
    mPipelineManager->setOutputEntity(outputId);
    
    if (outputId == InvalidEntityId) {
        PIPELINE_LOGE("Failed to create output entity");
        return false;
    }
    
    // 建立连接：InputEntity -> OutputEntity
    EntityId inputId = mPipelineManager->getInputEntityId();
    if (inputId != InvalidEntityId) {
        // InputEntity 有两个输出端口: "gpu_out" 和 "cpu_out"
        // OutputEntity 有两个输入端口: "gpu_in" 和 "cpu_in"
        
        // 连接 GPU 输出端口
        if (!mPipelineManager->connect(inputId, input::GPU_OUTPUT_PORT, 
                                       outputId, output::GPU_INPUT_PORT)) {
            PIPELINE_LOGE("Failed to connect InputEntity(gpu_out) to OutputEntity(gpu_in)");
        } else {
            PIPELINE_LOGI("Connected InputEntity(%llu):gpu_out -> OutputEntity(%llu):gpu_in", 
                         inputId, outputId);
        }
        
        // 连接 CPU 输出端口
        if (!mPipelineManager->connect(inputId, input::CPU_OUTPUT_PORT, 
                                       outputId, output::CPU_INPUT_PORT)) {
            PIPELINE_LOGE("Failed to connect InputEntity(cpu_out) to OutputEntity(cpu_in)");
        } else {
            PIPELINE_LOGI("Connected InputEntity(%llu):cpu_out -> OutputEntity(%llu):cpu_in", 
                         inputId, outputId);
        }
    } else {
        PIPELINE_LOGW("InputEntity not found, skip connection");
    }
    
    PIPELINE_LOGI("Output entity created, ID: %d", outputId);
    return true;
}

void PipelineFacade::setupCallbackBridges() {
    // 桥接回调到 PipelineManager
    if (mCallbacks.onFrameProcessed) {
        mPipelineManager->setFrameCompleteCallback(mCallbacks.onFrameProcessed);
    }
    if (mCallbacks.onFrameDropped) {
        mPipelineManager->setFrameDroppedCallback(mCallbacks.onFrameDropped);
    }
    if (mCallbacks.onError) {
        mPipelineManager->setErrorCallback([this](EntityId, const std::string& msg) {
            if (mCallbacks.onError) {
                mCallbacks.onError(msg);
            }
        });
    }
    if (mCallbacks.onStateChanged) {
        mPipelineManager->setStateCallback(mCallbacks.onStateChanged);
    }
    
    PIPELINE_LOGI("Callback bridges configured");
}

bool PipelineFacade::initializePlatformContext() {
#if defined(__ANDROID__) || defined(__APPLE__)
    if (!mPlatformContext) {
        mPlatformContext = std::make_unique<PlatformContext>();
    }

    PlatformContextConfig cfg = mConfig.platformConfig;

    // 设置默认平台和图形API
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

    // 具体平台初始化
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
