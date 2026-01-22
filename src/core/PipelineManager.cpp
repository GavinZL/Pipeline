/**
 * @file PipelineManager.cpp
 * @brief PipelineManager实现 - 管线管理器
 */

#include "pipeline/core/PipelineManager.h"
#include "pipeline/entity/IOEntity.h"
#include "pipeline/pool/TexturePool.h"
#include "pipeline/pool/FramePacketPool.h"

namespace pipeline {

// =============================================================================
// 静态创建方法
// =============================================================================

std::shared_ptr<PipelineManager> PipelineManager::create(
    lrengine::render::LRRenderContext* renderContext,
    const PipelineConfig& config) {
    
    auto manager = std::shared_ptr<PipelineManager>(
        new PipelineManager(renderContext, config));
    
    return manager;
}

PipelineManager::PipelineManager(lrengine::render::LRRenderContext* renderContext,
                                 const PipelineConfig& config)
    : mConfig(config)
    , mRenderContext(renderContext)
{
    // 创建图
    mGraph = std::make_unique<PipelineGraph>();
    
    // 创建上下文
    mContext = std::make_shared<PipelineContext>();
    mContext->setRenderContext(renderContext);
    mContext->setConfig(config);
}

PipelineManager::~PipelineManager() {
    destroy();
}

// =============================================================================
// 生命周期
// =============================================================================

bool PipelineManager::initialize() {
    if (mState != PipelineState::Created) {
        return false;
    }
    
    // 创建资源池
    if (!createResourcePools()) {
        setState(PipelineState::Error);
        return false;
    }
    
    // 初始化GPU资源
    if (!initializeGPUResources()) {
        setState(PipelineState::Error);
        return false;
    }
    
    // 创建执行器
    ExecutorConfig execConfig;
    execConfig.maxConcurrentFrames = mConfig.maxConcurrentFrames;
    execConfig.enableParallelExecution = mConfig.enableParallelExecution;
    execConfig.enableFrameSkipping = mConfig.enableFrameSkipping;
    
    mExecutor = std::make_unique<PipelineExecutor>(mGraph.get(), execConfig);
    mExecutor->setTexturePool(mTexturePool);
    mExecutor->setFramePacketPool(mFramePacketPool);
    
    if (!mExecutor->initialize()) {
        setState(PipelineState::Error);
        return false;
    }
    
    // 设置回调
    mExecutor->setFrameCompleteCallback(mFrameCompleteCallback);
    mExecutor->setFrameDroppedCallback(mFrameDroppedCallback);
    mExecutor->setErrorCallback(mErrorCallback);
    
    setState(PipelineState::Initialized);
    return true;
}

bool PipelineManager::start() {
    if (mState != PipelineState::Initialized && mState != PipelineState::Stopped) {
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
        return false;
    }
    
    setState(PipelineState::Running);
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
        // 等待所有帧完成
        if (mExecutor) {
            mExecutor->flush(3000);
        }
        setState(PipelineState::Stopped);
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
    if (auto gpuEntity = std::dynamic_pointer_cast<GPUEntity>(entity)) {
        gpuEntity->setRenderContext(mRenderContext);
    }
    if (auto inputEntity = std::dynamic_pointer_cast<InputEntity>(entity)) {
        inputEntity->setRenderContext(mRenderContext);
    }
    if (auto outputEntity = std::dynamic_pointer_cast<OutputEntity>(entity)) {
        outputEntity->setRenderContext(mRenderContext);
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
    }
    if (entityId == mOutputEntityId) {
        mOutputEntityId = InvalidEntityId;
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
    if (mState != PipelineState::Running || !mExecutor || !input) {
        return nullptr;
    }
    
    mExecutor->processFrame(input);
    
    // 获取输出
    auto outputEntity = getOutputEntity();
    if (outputEntity) {
        return outputEntity->getLastOutput();
    }
    
    return input;
}

bool PipelineManager::processFrameAsync(FramePacketPtr input,
                                        std::function<void(FramePacketPtr)> callback) {
    if (mState != PipelineState::Running || !mExecutor || !input) {
        return false;
    }
    
    return mExecutor->processFrameAsync(input, callback);
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

InputEntity* PipelineManager::getInputEntity() const {
    if (mInputEntityId == InvalidEntityId) {
        // 查找第一个InputEntity
        auto entities = mGraph->getEntitiesByType(EntityType::Input);
        if (!entities.empty()) {
            return dynamic_cast<InputEntity*>(entities[0].get());
        }
        return nullptr;
    }
    
    auto entity = mGraph->getEntity(mInputEntityId);
    return dynamic_cast<InputEntity*>(entity.get());
}

OutputEntity* PipelineManager::getOutputEntity() const {
    if (mOutputEntityId == InvalidEntityId) {
        // 查找第一个OutputEntity
        auto entities = mGraph->getEntitiesByType(EntityType::Output);
        if (!entities.empty()) {
            return dynamic_cast<OutputEntity*>(entities[0].get());
        }
        return nullptr;
    }
    
    auto entity = mGraph->getEntity(mOutputEntityId);
    return dynamic_cast<OutputEntity*>(entity.get());
}

void PipelineManager::setInputEntity(EntityId entityId) {
    mInputEntityId = entityId;
}

void PipelineManager::setOutputEntity(EntityId entityId) {
    mOutputEntityId = entityId;
}

FramePacketPtr PipelineManager::feedRGBA(const uint8_t* data,
                                         uint32_t width, uint32_t height,
                                         uint32_t stride,
                                         uint64_t timestamp) {
    auto inputEntity = getInputEntity();
    if (!inputEntity) {
        return nullptr;
    }
    
    auto packet = inputEntity->feedRGBA(data, width, height, stride, timestamp);
    if (packet && mState == PipelineState::Running) {
        processFrame(packet);
    }
    
    return packet;
}

FramePacketPtr PipelineManager::feedYUV420(const uint8_t* yData,
                                           const uint8_t* uData,
                                           const uint8_t* vData,
                                           uint32_t width, uint32_t height,
                                           uint64_t timestamp) {
    auto inputEntity = getInputEntity();
    if (!inputEntity) {
        return nullptr;
    }
    
    auto packet = inputEntity->feedYUV420(yData, uData, vData, width, height, 0, 0, timestamp);
    if (packet && mState == PipelineState::Running) {
        processFrame(packet);
    }
    
    return packet;
}

void PipelineManager::setDisplaySurface(void* surface) {
    auto outputEntity = getOutputEntity();
    if (outputEntity) {
        outputEntity->setDisplaySurface(surface);
    }
}

void PipelineManager::setEncoderSurface(void* surface) {
    auto outputEntity = getOutputEntity();
    if (outputEntity) {
        outputEntity->setEncoderSurface(surface);
    }
}

// =============================================================================
// 配置
// =============================================================================

void PipelineManager::updateConfig(const PipelineConfig& config) {
    mConfig = config;
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
    textureConfig.maxTotalTextures = mConfig.texturePoolSize;
    
    mTexturePool = std::make_shared<TexturePool>(mRenderContext, textureConfig);
    
    // 创建帧包池
    FramePacketPoolConfig packetConfig;
    packetConfig.capacity = mConfig.framePacketPoolSize;
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
