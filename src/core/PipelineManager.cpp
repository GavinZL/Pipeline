/**
 * @file PipelineManager.cpp
 * @brief PipelineManager实现 - 管线管理器
 */

#include "pipeline/core/PipelineManager.h"
#include "pipeline/pool/TexturePool.h"
#include "pipeline/pool/FramePacketPool.h"
#include "pipeline/output/OutputEntity.h"
#include "pipeline/output/DisplaySurface.h"
#include "pipeline/input/InputEntity.h"
#include "pipeline/entity/GPUEntity.h"
#include "pipeline/entity/MergeEntity.h"
#include "pipeline/platform/PlatformContext.h"
#include "pipeline/utils/PipelineLog.h"

// 平台特定头文件
#if defined(__APPLE__)
#include "pipeline/input/ios/PixelBufferInputStrategy.h"
#endif
#if defined(__ANDROID__)
#include "pipeline/input/android/OESTextureInputStrategy.h"
#endif


namespace pipeline {

// =============================================================================
// 静态创建方法
// =============================================================================

std::shared_ptr<PipelineManager> PipelineManager::create(
    lrengine::render::LRRenderContext* renderContext,
    const PipelineConfig& config) {
    
    auto manager = std::shared_ptr<PipelineManager>(
        new PipelineManager(renderContext, config));
    
    PIPELINE_LOGI("Creating PipelineManager");
    return manager;
}

PipelineManager::PipelineManager(lrengine::render::LRRenderContext* renderContext,
                                 const PipelineConfig& config)
    : mRenderContext(renderContext)
{
    // 创建图
    mGraph = std::make_unique<PipelineGraph>();
    
    // 创建上下文
    mContext = std::make_shared<PipelineContext>();
    mContext->setRenderContext(renderContext);
    mContext->setConfig(config);

    PIPELINE_LOGI("Creating PipelineManager with config");
}

PipelineManager::~PipelineManager() {
    destroy();
    PIPELINE_LOGI("Destroying PipelineManager");
}

// =============================================================================
// 生命周期
// =============================================================================

bool PipelineManager::initialize() {
    if (mState != PipelineState::Created) {
        PIPELINE_LOGE("PipelineManager is not in Created state");
        return false;
    }
    
    // 创建资源池
    if (!createResourcePools()) {
        PIPELINE_LOGE("Failed to create resource pools");
        setState(PipelineState::Error);
        return false;
    }
    
    // 初始化GPU资源
    if (!initializeGPUResources()) {
        PIPELINE_LOGE("Failed to initialize GPU resources");
        setState(PipelineState::Error);
        return false;
    }
    
    // 创建执行器
    ExecutorConfig execConfig;
    execConfig.maxConcurrentFrames = getConfig().maxConcurrentFrames;
    execConfig.enableParallelExecution = getConfig().enableParallelExecution;
    execConfig.enableFrameSkipping = getConfig().enableFrameSkipping;
    
    mExecutor = std::make_unique<PipelineExecutor>(mGraph.get(), execConfig);
    
    mContext->setTexturePool(mTexturePool);
    mContext->setFramePacketPool(mFramePacketPool);
    mExecutor->setContext(mContext);
    
    if (!mExecutor->initialize()) {
        PIPELINE_LOGE("Failed to initialize executor");
        setState(PipelineState::Error);
        return false;
    }
    
    // 设置回调
    mExecutor->setFrameCompleteCallback(mFrameCompleteCallback);
    mExecutor->setFrameDroppedCallback(mFrameDroppedCallback);
    mExecutor->setErrorCallback(mErrorCallback);
    
    setState(PipelineState::Initialized);
    PIPELINE_LOGI("PipelineManager initialized");
    return true;
}

bool PipelineManager::start() {
    // 如果已经在运行，直接返回成功
    if (mState == PipelineState::Running) 
    {
        PIPELINE_LOGW("PipelineManager already running");
        return true;
    }
    
    if (mState != PipelineState::Initialized && mState != PipelineState::Stopped) 
    {
        // 尝试初始化
        if (mState == PipelineState::Created) {
            if (!initialize()) {
                return false;
            }
        } else {
            return false;
        }
    }
    
    // 验证图
    auto validation = mGraph->validate();
    if (!validation.valid) {
        PIPELINE_LOGE("Graph validation failed: %s", validation.errorMessage.c_str());
        return false;
    }
    
    // 🔥 异步任务链: 启动InputEntity的processing loop
    auto inputEntity = getInputEntity();
    if (inputEntity) {
        inputEntity->setExecutor(mExecutor.get());
        inputEntity->startProcessingLoop();
        
        // 设置PipelineExecutor的InputEntity ID
        if (mExecutor) {
            mExecutor->setInputEntityId(inputEntity->getId());
        }
        
        PIPELINE_LOGI("Started InputEntity processing loop, entityId: %d", inputEntity->getId());
    } else {
        PIPELINE_LOGW("No InputEntity found, pipeline may not receive input data");
    }
    
    // 🔥 设置所有Entity的Executor引用
    auto allEntities = mGraph->getAllEntities();
    for (auto& entity : allEntities) {
        if (entity->getType() == EntityType::Composite) {
            // MergeEntity需要Executor引用
            auto mergeEntity = std::dynamic_pointer_cast<MergeEntity>(entity);
            if (mergeEntity) {
                mergeEntity->setExecutor(mExecutor.get());
            }
        }
    }
    
    setState(PipelineState::Running);
    PIPELINE_LOGI("PipelineManager started successfully");
    return true;
}

void PipelineManager::pause() {
    if (mState == PipelineState::Running) {
        setState(PipelineState::Paused);
    }
}

void PipelineManager::resume() {
    if (mState == PipelineState::Paused) {
        setState(PipelineState::Running);
    }
}

void PipelineManager::stop() {
    if (mState == PipelineState::Running || mState == PipelineState::Paused) {
        // 🔥 异步任务链: 停止InputEntity的processing loop
        auto inputEntity = getInputEntity();
        if (inputEntity) {
            inputEntity->stopProcessingLoop();
            PIPELINE_LOGI("Stopped InputEntity processing loop, entityId: %d", inputEntity->getId());
        }
        
        // 等待所有帧完成
        if (mExecutor) {
            mExecutor->flush(3000);
        }
        
        setState(PipelineState::Stopped);
        PIPELINE_LOGI("PipelineManager stopped");
    }
}

void PipelineManager::destroy() {
    stop();
    
    // 销毁执行器
    if (mExecutor) {
        mExecutor->shutdown();
        mExecutor.reset();
    }
    
    // 清空图
    if (mGraph) {
        mGraph->clear();
    }
    
    // 清理资源池
    if (mFramePacketPool) {
        mFramePacketPool->clear();
    }
    if (mTexturePool) {
        mTexturePool->clear();
    }
    
    setState(PipelineState::Created);
}

// =============================================================================
// Entity管理
// =============================================================================

EntityId PipelineManager::addEntity(ProcessEntityPtr entity) {
    if (!entity || !mGraph) {
        return InvalidEntityId;
    }
    
    // 设置渲染上下文（如果是GPU/IO Entity）
    if (entity->getType() == EntityType::GPU) 
    {
        auto gpuEntity = std::dynamic_pointer_cast<GPUEntity>(entity);
        if (gpuEntity) {
            gpuEntity->setRenderContext(mRenderContext);
        }
    }

    return mGraph->addEntity(entity);
}

bool PipelineManager::removeEntity(EntityId entityId) {
    if (!mGraph) {
        return false;
    }
    
    // 检查是否是特殊Entity
    if (entityId == mInputEntityId) {
        mInputEntityId = InvalidEntityId;
        PIPELINE_LOGI("Removing input entity, entityId: %d", entityId);
    }
    if (entityId == mOutputEntityId) {
        mOutputEntityId = InvalidEntityId;
        PIPELINE_LOGI("Removing output entity, entityId: %d", entityId);
    }
    
    return mGraph->removeEntity(entityId);
}

ProcessEntityPtr PipelineManager::getEntity(EntityId entityId) const {
    if (!mGraph) {
        return nullptr;
    }
    return mGraph->getEntity(entityId);
}

ProcessEntityPtr PipelineManager::getEntityByName(const std::string& name) const {
    if (!mGraph) {
        return nullptr;
    }
    return mGraph->findEntityByName(name);
}

std::vector<ProcessEntityPtr> PipelineManager::getAllEntities() const {
    if (!mGraph) {
        return {};
    }
    return mGraph->getAllEntities();
}

// =============================================================================
// 连接管理
// =============================================================================

bool PipelineManager::connect(EntityId srcId, const std::string& srcPort,
                              EntityId dstId, const std::string& dstPort) {
    if (!mGraph) {
        return false;
    }
    return mGraph->connect(srcId, srcPort, dstId, dstPort);
}

bool PipelineManager::connect(EntityId srcId, EntityId dstId) {
    if (!mGraph) {
        return false;
    }
    return mGraph->connect(srcId, dstId);
}

bool PipelineManager::disconnect(EntityId srcId, EntityId dstId) {
    if (!mGraph) {
        return false;
    }
    return mGraph->disconnectAll(srcId, dstId);
}

ValidationResult PipelineManager::validate() const {
    if (!mGraph) {
        ValidationResult result;
        result.valid = false;
        result.errorMessage = "Graph not initialized";
        return result;
    }
    return mGraph->validate();
}

// =============================================================================
// 帧处理
// =============================================================================

FramePacketPtr PipelineManager::processFrame(FramePacketPtr input) {
    // 🔥 异步任务链架构: processFrame已废弃
    // 请使用 InputEntity::submitData() 直接提交数据
    // 或者使用 processFrameAsync() 并设置回调
    
    PIPELINE_LOGW("processFrame is deprecated in async task-driven architecture");
    PIPELINE_LOGW("Use InputEntity::submitData() or processFrameAsync() with callback instead");
    
    return nullptr;
}

bool PipelineManager::processFrameAsync(FramePacketPtr input,
                                        std::function<void(FramePacketPtr)> callback) {
    // 🔥 异步任务链架构: processFrameAsync已废弃
    // 请使用 InputEntity::submitData() 直接提交数据
    // 并通过 setFrameCompleteCallback() 设置回调
    
    PIPELINE_LOGW("processFrameAsync is deprecated in async task-driven architecture");
    PIPELINE_LOGW("Use InputEntity::submitData() and setFrameCompleteCallback() instead");
    
    return false;
}

bool PipelineManager::flush(int64_t timeoutMs) {
    if (!mExecutor) {
        return true;
    }
    return mExecutor->flush(timeoutMs);
}

// =============================================================================
// 输入输出快捷接口
// =============================================================================

input::InputEntity* PipelineManager::getInputEntity() const {
    // 如果有直接创建的 mInputEntity，返回它
    if (mInputEntity) {
        return mInputEntity.get();
    }
    
    // 否则从图中查找
    if (mInputEntityId == InvalidEntityId) {
        // 查找第一个InputEntity
        auto entities = mGraph->getEntitiesByType(EntityType::Input);
        if (!entities.empty()) {
            return dynamic_cast<input::InputEntity*>(entities[0].get());
        }
        return nullptr;
    }
    
    auto entity = mGraph->getEntity(mInputEntityId);
    return dynamic_cast<input::InputEntity*>(entity.get());
}

output::OutputEntity* PipelineManager::getOutputEntity() const {
    if (mOutputEntityId == InvalidEntityId) {
        // 查找第一个OutputEntity
        auto entities = mGraph->getEntitiesByType(EntityType::Output);
        if (!entities.empty()) {
            return dynamic_cast<output::OutputEntity*>(entities[0].get());
        }
        return nullptr;
    }
    
    auto entity = mGraph->getEntity(mOutputEntityId);
    return dynamic_cast<output::OutputEntity*>(entity.get());
}

void PipelineManager::setInputEntity(EntityId entityId) {
    mInputEntityId = entityId;
}

void PipelineManager::setOutputEntity(EntityId entityId) {
    mOutputEntityId = entityId;
}

// =============================================================================
// 输出配置(扩展)
// =============================================================================

int32_t PipelineManager::setupDisplayOutput(void* surface, uint32_t width, uint32_t height) {
    if (!surface) {
        PIPELINE_LOGE("Invalid surface");
        return -1;
    }
    
    auto outputEntity = dynamic_cast<output::OutputEntity*>(getOutputEntity());
    if (!outputEntity) {
        PIPELINE_LOGE("No OutputEntity available");
        return -1;
    }
    
    // 1. 创建平台特定的 DisplaySurface
    auto displaySurface = output::createPlatformDisplaySurface();
    if (!displaySurface) {
        PIPELINE_LOGE("Failed to create DisplaySurface");
        return -1;
    }
    
    // 2. 绑定到平台 Surface
#if defined(__APPLE__)
    if (!displaySurface->attachToLayer(surface)) {
        PIPELINE_LOGE("Failed to attach DisplaySurface to layer");
        return -1;
    }
#elif defined(__ANDROID__)
    if (!displaySurface->attachToWindow(surface)) {
        PIPELINE_LOGE("Failed to attach DisplaySurface to window");
        return -1;
    }
#else
    PIPELINE_LOGE("Platform not supported");
    return -1;
#endif
    
    // 3. 初始化 DisplaySurface
    if (!displaySurface->initialize(mRenderContext)) {
        PIPELINE_LOGE("Failed to initialize DisplaySurface");
        return -1;
    }
    
    // 4. 设置尺寸
    displaySurface->setSize(width, height);
    
    // 5. 创建 DisplayOutputTarget
    int32_t targetId = mNextTargetId.fetch_add(1);
    auto displayTarget = std::make_shared<output::DisplayOutputTarget>(
        "display_" + std::to_string(targetId));
    displayTarget->setDisplaySurface(displaySurface);
    
    // 6. 设置默认配置
    output::DisplayConfig config;
    config.fillMode = output::DisplayFillMode::AspectFit;
    displayTarget->setDisplayConfig(config);
    
    // 7. 添加到 OutputEntity
    outputEntity->addTarget(displayTarget);
    
    // 8. 记录
    mOutputTargets[targetId] = displayTarget;
    
    PIPELINE_LOGI("Display output configured, target ID: %d", targetId);
    return targetId;
}

// =============================================================================
// 输入配置
// =============================================================================

EntityId PipelineManager::setupInput(const input::InputConfig& config) {
    if (mInputEntity) {
        PIPELINE_LOGW("InputEntity already exists, replacing it");
        // 移除旧的
        if (mInputEntityId != InvalidEntityId) {
            removeEntity(mInputEntityId);
        }
    }
    
    // 创建 InputEntity
    mInputEntity = std::make_shared<input::InputEntity>("InputEntity");
    mInputEntity->setRenderContext(mRenderContext);
    mInputEntity->configure(config);
    
    // 添加到图中
    EntityId inputId = addEntity(mInputEntity);
    setInputEntity(inputId);
    
    PIPELINE_LOGI("Input configured with generic config, entity ID: %d", inputId);
    return inputId;
}

#if defined(__APPLE__)
EntityId PipelineManager::setupPixelBufferInput(uint32_t width, uint32_t height, void* metalManager) {
    if (mInputEntity) {
        PIPELINE_LOGW("InputEntity already exists, replacing it");
        if (mInputEntityId != InvalidEntityId) {
            removeEntity(mInputEntityId);
        }
    }
    
    // 创建 InputEntity
    mInputEntity = std::make_shared<input::InputEntity>("InputEntity");
    mInputEntity->setRenderContext(mRenderContext);
    
    // 配置
    input::InputConfig config;
    config.enableDualOutput = true;
    config.width = width;
    config.height = height;
    mInputEntity->configure(config);
    
    // 创建 PixelBuffer 策略
    mPixelBufferStrategy = std::make_shared<input::ios::PixelBufferInputStrategy>();
    
    // 设置 Metal 管理器（如果提供）
    if (metalManager) {
        auto* manager = static_cast<IOSMetalContextManager*>(metalManager);
        mPixelBufferStrategy->setMetalContextManager(manager);
    }
    
    // 初始化策略
    if (mPixelBufferStrategy->initialize(mRenderContext)) {
        mInputEntity->setInputStrategy(mPixelBufferStrategy);
        PIPELINE_LOGI("PixelBufferInputStrategy initialized successfully");
    } else {
        PIPELINE_LOGE("Failed to initialize PixelBufferInputStrategy");
        mPixelBufferStrategy.reset();
        return InvalidEntityId;
    }
    
    // 添加到图中
    EntityId inputId = addEntity(mInputEntity);
    setInputEntity(inputId);
    
    PIPELINE_LOGI("PixelBuffer input configured, entity ID: %d, size: %ux%u", 
                  inputId, width, height);
    return inputId;
}
#endif

#if defined(__ANDROID__)
EntityId PipelineManager::setupOESInput(uint32_t width, uint32_t height) {
    if (mInputEntity) {
        PIPELINE_LOGW("InputEntity already exists, replacing it");
        if (mInputEntityId != InvalidEntityId) {
            removeEntity(mInputEntityId);
        }
    }
    
    // 创建 InputEntity
    mInputEntity = std::make_shared<input::InputEntity>("InputEntity");
    mInputEntity->setRenderContext(mRenderContext);
    
    // 配置
    input::InputConfig config;
    config.enableDualOutput = true;
    config.width = width;
    config.height = height;
    mInputEntity->configure(config);
    
    // 创建 OES 策略
    mOESStrategy = std::make_shared<input::android::OESTextureInputStrategy>();
    
    // 初始化策略
    if (mOESStrategy->initialize(mRenderContext)) {
        mInputEntity->setInputStrategy(mOESStrategy);
        PIPELINE_LOGI("OESTextureInputStrategy initialized successfully");
    } else {
        PIPELINE_LOGE("Failed to initialize OESTextureInputStrategy");
        mOESStrategy.reset();
        return InvalidEntityId;
    }
    
    // 添加到图中
    EntityId inputId = addEntity(mInputEntity);
    setInputEntity(inputId);
    
    PIPELINE_LOGI("OES input configured, entity ID: %d, size: %ux%u", 
                  inputId, width, height);
    return inputId;
}
#endif

EntityId PipelineManager::setupRGBAInput(uint32_t width, uint32_t height) {
    if (mInputEntity) {
        PIPELINE_LOGW("InputEntity already exists, replacing it");
        if (mInputEntityId != InvalidEntityId) {
            removeEntity(mInputEntityId);
        }
    }
    
    // 创建 InputEntity
    mInputEntity = std::make_shared<input::InputEntity>("InputEntity");
    mInputEntity->setRenderContext(mRenderContext);
    
    // 配置
    input::InputConfig config;
    config.enableDualOutput = false; // RGBA 不需要双路输出
    config.width = width;
    config.height = height;
    mInputEntity->configure(config);
    
    // RGBA 不需要特殊策略，使用默认策略
    
    // 添加到图中
    EntityId inputId = addEntity(mInputEntity);
    setInputEntity(inputId);
    
    PIPELINE_LOGI("RGBA input configured, entity ID: %d, size: %ux%u", 
                  inputId, width, height);
    return inputId;
}

EntityId PipelineManager::setupYUVInput(uint32_t width, uint32_t height) {
    if (mInputEntity) {
        PIPELINE_LOGW("InputEntity already exists, replacing it");
        if (mInputEntityId != InvalidEntityId) {
            removeEntity(mInputEntityId);
        }
    }
    
    // 创建 InputEntity
    mInputEntity = std::make_shared<input::InputEntity>("InputEntity");
    mInputEntity->setRenderContext(mRenderContext);
    
    // 配置
    input::InputConfig config;
    config.enableDualOutput = false;
    config.width = width;
    config.height = height;
    mInputEntity->configure(config);
    
    // YUV 不需要特殊策略，使用默认策略
    
    // 添加到图中
    EntityId inputId = addEntity(mInputEntity);
    setInputEntity(inputId);
    
    PIPELINE_LOGI("YUV input configured, entity ID: %d, size: %ux%u", 
                  inputId, width, height);
    return inputId;
}

// =============================================================================
// 输出配置
// =============================================================================

int32_t PipelineManager::setupCallbackOutput(
    std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, 
                      output::OutputFormat, int64_t)> callback,
    output::OutputFormat dataFormat) {
    
    auto outputEntity = dynamic_cast<output::OutputEntity*>(getOutputEntity());
    if (!outputEntity) {
        PIPELINE_LOGE("No OutputEntity available");
        return -1;
    }
    
    // 创建 CallbackOutputTarget
    int32_t targetId = mNextTargetId.fetch_add(1);
    auto callbackTarget = std::make_shared<output::CallbackOutputTarget>(
        "callback_" + std::to_string(targetId));
    
    // 设置回调
    callbackTarget->setCPUCallback(std::move(callback));
    
    // 添加到 OutputEntity
    outputEntity->addTarget(callbackTarget);
    
    // 记录
    mOutputTargets[targetId] = callbackTarget;
    
    PIPELINE_LOGI("Callback output configured, target ID: %d, format: %d", 
                  targetId, static_cast<int>(dataFormat));
    return targetId;
}

int32_t PipelineManager::setupEncoderOutput(void* encoderSurface, output::EncoderType encoderType) {
    auto outputEntity = dynamic_cast<output::OutputEntity*>(getOutputEntity());
    if (!outputEntity) {
        PIPELINE_LOGE("No OutputEntity available");
        return -1;
    }
    
    // TODO: 实现编码器输出
    (void)encoderSurface;
    (void)encoderType;
    PIPELINE_LOGW("setupEncoderOutput not yet implemented");
    return -1;
}

bool PipelineManager::removeOutputTarget(int32_t targetId) {
    auto it = mOutputTargets.find(targetId);
    if (it == mOutputTargets.end()) {
        PIPELINE_LOGW("Output target %d not found", targetId);
        return false;
    }
    
    auto outputEntity = dynamic_cast<output::OutputEntity*>(getOutputEntity());
    if (outputEntity) {
        const auto& target = it->second;
        outputEntity->removeTarget(target->getName());
    }
    
    mOutputTargets.erase(it);
    PIPELINE_LOGI("Output target %d removed", targetId);
    return true;
}

bool PipelineManager::updateDisplayOutputSize(int32_t targetId, uint32_t width, uint32_t height) {
    auto it = mOutputTargets.find(targetId);
    if (it == mOutputTargets.end()) {
        PIPELINE_LOGW("Output target %d not found", targetId);
        return false;
    }
    
    // 尝试转换为 DisplayOutputTarget
    auto displayTarget = std::dynamic_pointer_cast<output::DisplayOutputTarget>(it->second);
    if (!displayTarget) {
        PIPELINE_LOGW("Output target %d is not a display target", targetId);
        return false;
    }
    
    // 更新 DisplaySurface 尺寸
    auto surface = displayTarget->getDisplaySurface();
    if (surface) {
        surface->setSize(width, height);
        PIPELINE_LOGI("Display output %d size updated: %ux%u", targetId, width, height);
        return true;
    }
    
    return false;
}

// =============================================================================
// 配置
// =============================================================================

const PipelineConfig& PipelineManager::getConfig() const {
    return mContext->getConfig();
}

void PipelineManager::updateConfig(const PipelineConfig& config) {
    if (mContext) {
        mContext->setConfig(config);
    }
}

std::shared_ptr<PipelineContext> PipelineManager::getContext() const {
    return mContext;
}

// =============================================================================
// 回调
// =============================================================================

void PipelineManager::setFrameCompleteCallback(std::function<void(FramePacketPtr)> callback) {
    mFrameCompleteCallback = std::move(callback);
    if (mExecutor) {
        mExecutor->setFrameCompleteCallback(mFrameCompleteCallback);
    }
}

void PipelineManager::setFrameDroppedCallback(std::function<void(FramePacketPtr)> callback) {
    mFrameDroppedCallback = std::move(callback);
    if (mExecutor) {
        mExecutor->setFrameDroppedCallback(mFrameDroppedCallback);
    }
}

void PipelineManager::setErrorCallback(std::function<void(EntityId, const std::string&)> callback) {
    mErrorCallback = std::move(callback);
    if (mExecutor) {
        mExecutor->setErrorCallback(mErrorCallback);
    }
}

void PipelineManager::setStateCallback(std::function<void(PipelineState)> callback) {
    mStateCallback = std::move(callback);
}

// =============================================================================
// 统计和调试
// =============================================================================

ExecutionStats PipelineManager::getStats() const {
    if (!mExecutor) {
        return ExecutionStats();
    }
    return mExecutor->getStats();
}

void PipelineManager::resetStats() {
    if (mExecutor) {
        mExecutor->resetStats();
    }
}

std::string PipelineManager::exportGraphToDot() const {
    if (!mGraph) {
        return "";
    }
    return mGraph->exportToDot();
}

std::string PipelineManager::exportGraphToJson() const {
    if (!mGraph) {
        return "";
    }
    return mGraph->exportToJson();
}

// =============================================================================
// 私有方法
// =============================================================================

bool PipelineManager::createResourcePools() {
    // 创建纹理池
    TexturePoolConfig textureConfig;
    textureConfig.maxTexturesPerBucket = 4;
    textureConfig.maxTotalTextures = getConfig().texturePoolSize;
    
    mTexturePool = std::make_shared<TexturePool>(mRenderContext, textureConfig);
    
    // 创建帧包池
    FramePacketPoolConfig packetConfig;
    packetConfig.capacity = getConfig().framePacketPoolSize;
    packetConfig.blockOnEmpty = true;
    packetConfig.enableBackpressure = true;
    
    mFramePacketPool = std::make_shared<FramePacketPool>(packetConfig);
    mFramePacketPool->preallocate();
    
    // 设置到上下文
    mContext->setTexturePool(mTexturePool);
    mContext->setFramePacketPool(mFramePacketPool);
    
    return true;
}

void PipelineManager::setState(PipelineState state) {
    if (mState != state) {
        mState = state;
        if (mStateCallback) {
            mStateCallback(state);
        }
    }
}

bool PipelineManager::initializeGPUResources() {
    // 预热纹理池（常用尺寸）
    if (mTexturePool) {
        std::vector<TextureSpec> specs = {
            {1920, 1080, PixelFormat::RGBA8},
            {1280, 720, PixelFormat::RGBA8},
            {640, 480, PixelFormat::RGBA8}
        };
        mTexturePool->warmup(specs);
    }
    
    return true;
}

} // namespace pipeline
