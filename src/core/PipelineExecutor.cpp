/**
 * @file PipelineExecutor.cpp
 * @brief PipelineExecutor实现 - 管线执行调度器
 */

#include "pipeline/core/PipelineExecutor.h"
#include "pipeline/core/PipelineConfig.h"
#include "pipeline/entity/ProcessEntity.h"
#include "pipeline/pool/TexturePool.h"
#include "pipeline/pool/FramePacketPool.h"
#include "pipeline/utils/PipelineLog.h"


// TaskQueue头文件
#include "TaskQueue.h"
#include "TaskQueueFactory.h"
#include "TaskGroup.h"
#include "TaskOperator.h"

#include <chrono>

namespace pipeline {

PipelineExecutor::PipelineExecutor(PipelineGraph* graph, const ExecutorConfig& config)
    : mConfig(config)
    , mGraph(graph)
{
    PIPELINE_LOGI("Creating PipelineExecutor");
}

PipelineExecutor::~PipelineExecutor() {
    shutdown();
    PIPELINE_LOGI("Destroying PipelineExecutor");
}

// =============================================================================
// 生命周期
// =============================================================================

bool PipelineExecutor::initialize() {
    if (mInitialized.load()) {
        PIPELINE_LOGW("PipelineExecutor already initialized");
        return true;
    }
    
    if (!mGraph) {
        PIPELINE_LOGE("PipelineExecutor graph is null");
        return false;
    }
    
    // 创建任务队列
    if (!createTaskQueues()) {
        PIPELINE_LOGE("Failed to create task queues");
        return false;
    }
    
    // 更新执行计划
    updateExecutionPlan();
    
    // 初始化帧状态
    {
        std::lock_guard<std::mutex> lock(mFrameStateMutex);
        mCurrentFrameState = std::make_shared<FrameExecutionState>();
        mCurrentFrameState->frameId = 0;
    }
    
    mInitialized.store(true);
    mRunning.store(true);
    PIPELINE_LOGI("PipelineExecutor initialized");
    return true;
}

void PipelineExecutor::shutdown() {
    PIPELINE_LOGI("Shutting down PipelineExecutor");
    mRunning.store(false);
    
    // 等待所有任务完成
    flush(5000);
    
    // 清理队列
    mGPUQueue.reset();
    mCPUQueue.reset();
    mIOQueue.reset();
    
    mInitialized.store(false);
    PIPELINE_LOGI("PipelineExecutor shut down");
}

// =============================================================================
// 执行控制
// =============================================================================

bool PipelineExecutor::processFrame(FramePacketPtr input) {
    if (!mRunning.load() || !input) {
        PIPELINE_LOGW("PipelineExecutor is not running or input is null");
        return false;
    }
    
    // 检查是否应该跳帧
    if (shouldSkipFrame()) {
        if (mFrameDroppedCallback) {
            mFrameDroppedCallback(input);
        }
        mStats.droppedFrames++;
        PIPELINE_LOGW("Dropped frame %llu", input->getFrameId());
        return false;
    }
    
    // 检查图是否有变化
    if (mGraph->getVersion() != mLastGraphVersion) {
        updateExecutionPlan();
        PIPELINE_LOGI("Graph changed, updated execution plan");
    }
    
    mPendingFrames.fetch_add(1);
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // 更新上下文
    mContext->setCurrentFrameId(input->getFrameId());
    mContext->setCurrentTimestamp(input->getTimestamp());
    
    // 重置所有Entity状态
    for (const auto& level : mExecutionLevels) {
        for (EntityId id : level) {
            auto entity = mGraph->getEntity(id);
            if (entity) {
                entity->resetForNextFrame();
            }
        }
    }
    
    // 设置输入到第一个Entity
    auto sourceEntities = mGraph->getSourceEntities();
    for (EntityId srcId : sourceEntities) {
        auto entity = mGraph->getEntity(srcId);
        if (entity && entity->getOutputPortCount() > 0) {
            entity->getOutputPort(size_t(0))->setPacket(input);
            entity->getOutputPort(size_t(0))->send();
        }
    }
    
    // 按层级执行
    for (const auto& level : mExecutionLevels) {
        if (!mRunning.load()) {
            break;
        }
        
        if (mConfig.enableParallelExecution && level.size() > 1) {
            // 并行执行同层Entity
            auto group = task::TaskQueueFactory::GetInstance().createTaskGroup();
            executeLevel(level, input, group);
            group->wait();
        } else {
            // 串行执行
            for (EntityId id : level) {
                executeEntity(id, input);
            }
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto frameTime = std::chrono::duration_cast<std::chrono::microseconds>(
        endTime - startTime).count();
    
    updateStats(frameTime);
    
    mPendingFrames.fetch_sub(1);
    
    // 触发完成回调
    onFrameComplete(input);
    
    return true;
}

bool PipelineExecutor::processFrameAsync(FramePacketPtr input,
                                         std::function<void(FramePacketPtr)> callback) {
    if (!mRunning.load() || !input) {
        PIPELINE_LOGW("PipelineExecutor is not running or input is null");
        return false;
    }
    
    // 检查是否应该跳帧
    if (shouldSkipFrame()) {
        if (mFrameDroppedCallback) {
            mFrameDroppedCallback(input);
        }
        mStats.droppedFrames++;
        PIPELINE_LOGW("Dropped frame %llu", input->getFrameId());
        return false;
    }
    
    mPendingFrames.fetch_add(1);
    
    // 在IO队列异步执行
    mIOQueue->async([this, input, callback]() {
        processFrame(input);
        if (callback) {
            callback(input);
        }
    });
    
    return true;
}

bool PipelineExecutor::flush(int64_t timeoutMs) {
    auto startTime = std::chrono::steady_clock::now();
    
    while (mPendingFrames.load() > 0) {
        if (timeoutMs >= 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed >= timeoutMs) {
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    return true;
}

void PipelineExecutor::cancelAll() {
    // 取消所有Entity
    auto entities = mGraph->getAllEntities();
    for (auto& entity : entities) {
        entity->cancel();
    }
    PIPELINE_LOGI("Cancelled all entities");
}

// =============================================================================
// 状态查询
// =============================================================================

ExecutionStats PipelineExecutor::getStats() const {
    std::lock_guard<std::mutex> lock(mStatsMutex);
    return mStats;
}

void PipelineExecutor::resetStats() {
    std::lock_guard<std::mutex> lock(mStatsMutex);
    mStats = ExecutionStats();
}

// =============================================================================
// 配置
// =============================================================================

void PipelineExecutor::setParallelExecutionEnabled(bool enabled) {
    mConfig.enableParallelExecution = enabled;
}

void PipelineExecutor::setFrameSkippingEnabled(bool enabled) {
    mConfig.enableFrameSkipping = enabled;
}

void PipelineExecutor::setFrameCompleteCallback(std::function<void(FramePacketPtr)> callback) {
    mFrameCompleteCallback = std::move(callback);
}

void PipelineExecutor::setFrameDroppedCallback(std::function<void(FramePacketPtr)> callback) {
    mFrameDroppedCallback = std::move(callback);
}

void PipelineExecutor::setErrorCallback(std::function<void(EntityId, const std::string&)> callback) {
    mErrorCallback = std::move(callback);
}

// =============================================================================
// 内部方法
// =============================================================================

bool PipelineExecutor::createTaskQueues() {
    auto& factory = task::TaskQueueFactory::GetInstance();
    
    // GPU串行队列（独占线程，保证OpenGL上下文安全）
    mGPUQueue = factory.createSerialTaskQueue(
        mConfig.gpuQueueLabel,
        task::WorkThreadPriority::WTP_High,
        true  // 独占线程
    );
    
    // CPU并行队列
    mCPUQueue = factory.createConcurrencyTaskQueue(
        mConfig.cpuQueueLabel,
        task::TaskQueuePriority::TQP_Normal
    );
    
    // IO串行队列
    mIOQueue = factory.createSerialTaskQueue(
        mConfig.ioQueueLabel,
        task::WorkThreadPriority::WTP_Normal,
        false  // 共享线程
    );
    
    return mGPUQueue && mCPUQueue && mIOQueue;
}

void PipelineExecutor::updateExecutionPlan() {
    mExecutionLevels = mGraph->getExecutionLevels();
    mLastGraphVersion = mGraph->getVersion();
}

void PipelineExecutor::executeEntity(EntityId entityId, FramePacketPtr frameContext) {
    auto entity = mGraph->getEntity(entityId);
    if (!entity) {
        PIPELINE_LOGW("Entity %llu not found", entityId);
        return;
    }
    
    // 获取对应的队列
    auto queue = getQueueForEntity(entityId);
    if (!queue) {
        queue = mGPUQueue; // 默认使用GPU队列
    }
    
    // 同步执行（在对应队列中）
    queue->sync([this, entity, &frameContext]() {
        bool success = entity->execute(*mContext);
        if (!success && entity->hasError()) {
            onEntityError(entity->getId(), "Entity execution failed");
        }
    });
}

void PipelineExecutor::executeLevel(const std::vector<EntityId>& level,
                                    FramePacketPtr frameContext,
                                    std::shared_ptr<task::TaskGroup> group) {
    for (EntityId id : level) {
        auto entity = mGraph->getEntity(id);
        if (!entity) {
            continue;
        }
        
        auto queue = getQueueForEntity(id);
        
        // 异步执行
        group->asyncQueue(
            std::make_shared<task::TaskOperator>([this, entity, &frameContext](
                const std::shared_ptr<task::TaskOperator>&) {
                bool success = entity->execute(*mContext);
                if (!success && entity->hasError()) {
                    onEntityError(entity->getId(), "Entity execution failed");
                }
            }),
            queue
        );
    }
}

std::shared_ptr<task::TaskQueue> PipelineExecutor::getQueueForEntity(EntityId entityId) const {
    auto entity = mGraph->getEntity(entityId);
    if (!entity) {
        return mGPUQueue;
    }
    
    switch (entity->getExecutionQueue()) {
        case ExecutionQueue::GPU:
            return mGPUQueue;
        case ExecutionQueue::CPUParallel:
            return mCPUQueue;
        case ExecutionQueue::IO:
            return mIOQueue;
        default:
            return mGPUQueue;
    }
}

void PipelineExecutor::onEntityComplete(EntityId entityId, FramePacketPtr frameContext) {
    // Entity完成处理（可用于统计）
}

void PipelineExecutor::onEntityError(EntityId entityId, const std::string& error) {
    if (mErrorCallback) {
        mErrorCallback(entityId, error);
    }
}

void PipelineExecutor::onFrameComplete(FramePacketPtr frameContext) {
    if (mFrameCompleteCallback) {
        mFrameCompleteCallback(frameContext);
    }
}

void PipelineExecutor::updateStats(uint64_t frameTime) {
    std::lock_guard<std::mutex> lock(mStatsMutex);
    
    mStats.totalFrames++;
    mStats.lastFrameTime = frameTime;
    
    if (frameTime > mStats.peakFrameTime) {
        mStats.peakFrameTime = frameTime;
    }
    
    // 计算移动平均
    if (mStats.averageFrameTime == 0) {
        mStats.averageFrameTime = frameTime;
    } else {
        mStats.averageFrameTime = (mStats.averageFrameTime * 7 + frameTime) / 8;
    }
}

bool PipelineExecutor::shouldSkipFrame() const {
    if (!mConfig.enableFrameSkipping) {
        return false;
    }
    
    return mPendingFrames.load() >= mConfig.maxPendingFrames;
}

// =============================================================================
// 异步任务链实现 (新增)
// =============================================================================

bool PipelineExecutor::submitEntityTask(EntityId entityId, 
                                        std::shared_ptr<void> contextData) {
    if (!mRunning.load()) {
        PIPELINE_LOGW("PipelineExecutor is not running");
        return false;
    }
    
    auto entity = mGraph->getEntity(entityId);
    if (!entity || !entity->isEnabled()) {
        PIPELINE_LOGW("Entity %llu not found or disabled", entityId);
        return false;
    }
    
    // 获取对应的任务队列
    auto queue = getQueueForEntity(entityId);
    if (!queue) {
        PIPELINE_LOGE("No queue found for entity %llu", entityId);
        return false;
    }
    
    // 使用 weak_ptr 捕获 this，避免悬空指针
    // 当 PipelineExecutor 被销毁后，weak_ptr 会失效，回调会安全退出
    auto weakSelf = std::weak_ptr<PipelineExecutor>(shared_from_this());
    
    auto taskOp = std::make_shared<task::TaskOperator>(
        [weakSelf, entityId, contextData](const std::shared_ptr<task::TaskOperator>&) {
            // 尝试获取 shared_ptr
            auto self = weakSelf.lock();
            if (!self) {
                // PipelineExecutor 已被销毁，安全退出
                return;
            }
            
            // 再次检查运行状态
            if (!self->mRunning.load()) {
                return;
            }
            
            self->executeEntityTask(entityId, contextData);
        }
    );
    
    queue->async(taskOp);
    PIPELINE_LOGD("Submitted task for entity %llu to queue", entityId);
    return true;
}

void PipelineExecutor::executeEntityTask(EntityId entityId, 
                                         std::shared_ptr<void> contextData) {
    auto entity = mGraph->getEntity(entityId);
    if (!entity) {
        PIPELINE_LOGW("Entity %llu not found in executeEntityTask", entityId);
        return;
    }
    
    PIPELINE_LOGD("Executing entity %llu (%s)", entityId, entity->getName().c_str());
    
    // 执行Entity
    bool success = entity->execute(*mContext);
    
    if (!success) {
        // 如果是MergeEntity且返回false
        // 说明正在等待其他路,不算错误
        if (entity->getType() == EntityType::Composite) {
            PIPELINE_LOGD("MergeEntity %llu waiting for other paths", entityId);
            return;  // 不投递下游任务,等待下次被触发
        }
        
        PIPELINE_LOGE("Entity %llu execution failed", entityId);
        onEntityError(entityId, "Entity execution failed");
        return;
    }
    
    // 记录完成状态
    {
        std::lock_guard<std::mutex> lock(mFrameStateMutex);
        if (mCurrentFrameState) {
            std::lock_guard<std::mutex> stateLock(mCurrentFrameState->mutex);
            mCurrentFrameState->completedEntities.insert(entityId);
            PIPELINE_LOGD("Entity %llu completed, total completed: %zu", 
                         entityId, mCurrentFrameState->completedEntities.size());
        }
    }
    
    // 投递下游任务
    submitDownstreamTasks(entityId);
    
    // 检查是否Pipeline完成
    if (isPipelineCompleted(entityId)) {
        PIPELINE_LOGI("Pipeline completed for frame");
        onFrameComplete(nullptr);  // TODO: 构造FramePacket传递给回调
        restartPipelineLoop();
    }
}

void PipelineExecutor::submitDownstreamTasks(EntityId entityId) {
    auto downstreams = mGraph->getDownstreamEntities(entityId);
    
    PIPELINE_LOGD("Entity %llu has %zu downstream entities", entityId, downstreams.size());
    
    for (EntityId downstreamId : downstreams) {
        auto downstream = mGraph->getEntity(downstreamId);
        if (!downstream) {
            continue;
        }
        
        // 如果下游是MergeEntity
        if (downstream->getType() == EntityType::Composite) {
            // 检查是否所有上游都已完成
            if (!areAllDependenciesReady(downstreamId)) {
                PIPELINE_LOGD("MergeEntity %llu dependencies not ready, skipping", downstreamId);
                continue;  // 上游未全部完成,不投递
            }
        }
        
        // 检查依赖
        if (areAllDependenciesReady(downstreamId)) {
            PIPELINE_LOGD("Submitting downstream task for entity %llu", downstreamId);
            submitEntityTask(downstreamId);
        } else {
            PIPELINE_LOGD("Entity %llu dependencies not ready", downstreamId);
        }
    }
}

bool PipelineExecutor::areAllDependenciesReady(EntityId entityId) {
    auto upstreams = mGraph->getUpstreamEntities(entityId);
    
    std::lock_guard<std::mutex> lock(mFrameStateMutex);
    if (!mCurrentFrameState) {
        return false;
    }
    
    std::lock_guard<std::mutex> stateLock(mCurrentFrameState->mutex);
    for (EntityId upstreamId : upstreams) {
        if (mCurrentFrameState->completedEntities.find(upstreamId) == 
            mCurrentFrameState->completedEntities.end()) {
            return false;  // 有上游未完成
        }
    }
    return true;
}

bool PipelineExecutor::isPipelineCompleted(EntityId entityId) {
    // 检查是否是sink entity（没有下游）
    auto downstreams = mGraph->getDownstreamEntities(entityId);
    if (!downstreams.empty()) {
        return false;  // 还有下游,未完成
    }
    
    // 检查所有Entity是否都已完成
    std::lock_guard<std::mutex> lock(mFrameStateMutex);
    if (!mCurrentFrameState) {
        return false;
    }
    
    auto allEntities = mGraph->getAllEntities();
    std::lock_guard<std::mutex> stateLock(mCurrentFrameState->mutex);
    
    for (auto& entity : allEntities) {
        EntityId id = entity->getId();
        if (entity->isEnabled() && 
            mCurrentFrameState->completedEntities.find(id) == 
            mCurrentFrameState->completedEntities.end()) {
            return false;  // 有Entity未完成
        }
    }
    
    return true;
}

void PipelineExecutor::restartPipelineLoop() {
    PIPELINE_LOGI("Restarting pipeline loop");
    
    // 更新统计
    {
        std::lock_guard<std::mutex> lock(mStatsMutex);
        mStats.totalFrames++;
    }
    
    {
        std::lock_guard<std::mutex> lock(mFrameStateMutex);
        mCurrentFrameState = std::make_shared<FrameExecutionState>();
        mCurrentFrameState->frameId = mStats.totalFrames;
    }
    
    if (mInputEntityId != InvalidEntityId) {
        PIPELINE_LOGD("Resubmitting InputEntity %llu", mInputEntityId);
        submitEntityTask(mInputEntityId);
    } else {
        PIPELINE_LOGW("InputEntity ID not set, cannot restart loop");
    }
}

} // namespace pipeline
