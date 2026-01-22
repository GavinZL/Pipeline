/**
 * @file PipelineExecutor.cpp
 * @brief PipelineExecutor实现 - 管线执行调度器
 */

#include "pipeline/core/PipelineExecutor.h"
#include "pipeline/core/PipelineConfig.h"
#include "pipeline/entity/ProcessEntity.h"
#include "pipeline/pool/TexturePool.h"
#include "pipeline/pool/FramePacketPool.h"

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
}

PipelineExecutor::~PipelineExecutor() {
    shutdown();
}

// =============================================================================
// 生命周期
// =============================================================================

bool PipelineExecutor::initialize() {
    if (mInitialized.load()) {
        return true;
    }
    
    if (!mGraph) {
        return false;
    }
    
    // 创建任务队列
    if (!createTaskQueues()) {
        return false;
    }
    
    // 创建管线上下文
    mContext = std::make_shared<PipelineContext>();
    
    // 设置资源池
    if (mTexturePool) {
        mContext->setTexturePool(mTexturePool);
    }
    if (mFramePacketPool) {
        mContext->setFramePacketPool(mFramePacketPool);
    }
    
    // 更新执行计划
    updateExecutionPlan();
    
    mInitialized.store(true);
    mRunning.store(true);
    
    return true;
}

void PipelineExecutor::shutdown() {
    mRunning.store(false);
    
    // 等待所有任务完成
    flush(5000);
    
    // 清理队列
    mGPUQueue.reset();
    mCPUQueue.reset();
    mIOQueue.reset();
    
    mInitialized.store(false);
}

// =============================================================================
// 资源池
// =============================================================================

void PipelineExecutor::setTexturePool(std::shared_ptr<TexturePool> pool) {
    mTexturePool = std::move(pool);
    if (mContext) {
        mContext->setTexturePool(mTexturePool);
    }
}

void PipelineExecutor::setFramePacketPool(std::shared_ptr<FramePacketPool> pool) {
    mFramePacketPool = std::move(pool);
    if (mContext) {
        mContext->setFramePacketPool(mFramePacketPool);
    }
}

// =============================================================================
// 执行控制
// =============================================================================

bool PipelineExecutor::processFrame(FramePacketPtr input) {
    if (!mRunning.load() || !input) {
        return false;
    }
    
    // 检查是否应该跳帧
    if (shouldSkipFrame()) {
        if (mFrameDroppedCallback) {
            mFrameDroppedCallback(input);
        }
        mStats.droppedFrames++;
        return false;
    }
    
    // 检查图是否有变化
    if (mGraph->getVersion() != mLastGraphVersion) {
        updateExecutionPlan();
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
        return false;
    }
    
    // 检查是否应该跳帧
    if (shouldSkipFrame()) {
        if (mFrameDroppedCallback) {
            mFrameDroppedCallback(input);
        }
        mStats.droppedFrames++;
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

} // namespace pipeline
