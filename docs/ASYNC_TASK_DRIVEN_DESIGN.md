# Pipeline 异步任务驱动架构 - 实施指南

> **文档类型**: 可执行实施文档  
> **版本**: v2.0  
> **日期**: 2026-01-22  
> **目标**: 将 Pipeline 从同步层级遍历改为异步任务链式驱动

---

## 📋 目录

- [第一部分: 架构概览](#第一部分-架构概览)
- [第二部分: 实施计划](#第二部分-实施计划)
- [第三部分: 分阶段实施](#第三部分-分阶段实施)
  - [阶段 1: PipelineGraph 扩展](#阶段-1-pipelinegraph-扩展)
  - [阶段 2: PipelineExecutor 核心机制](#阶段-2-pipelineexecutor-核心机制)
  - [阶段 3: InputEntity 改造](#阶段-3-inputentity-改造)
  - [阶段 4: MergeEntity 多路汇聚](#阶段-4-mergeentity-多路汇聚)
  - [阶段 5: ProcessEntity Port 机制](#阶段-5-processentity-port-机制)
  - [阶段 6: PipelineManager 集成](#阶段-6-pipelinemanager-集成)
- [第四部分: 测试验证](#第四部分-测试验证)
- [第五部分: 完整执行流程](#第五部分-完整执行流程)
- [附录: 架构对比](#附录-架构对比)

---

## 第一部分: 架构概览

### 1.1 设计目标

将 Pipeline 从**同步层级遍历执行**改为**异步任务链式驱动**。

**核心特性**:
- ✅ InputEntity 在 TaskQueue 中等待数据
- ✅ submitData 触发条件变量唤醒
- ✅ Entity 完成后链式投递下游任务
- ✅ Pipeline 完成后自动循环
- ✅ 支持多分支 DAG 拓扑

### 1.2 架构分层

```
┌─────────────────────────────────────┐
│     应用层 (PipelineManager)        │  ← 阶段 6
├─────────────────────────────────────┤
│   任务调度层 (PipelineExecutor)     │  ← 阶段 2
│   - submitEntityTask                │
│   - submitDownstreamTasks           │
│   - restartPipelineLoop             │
├─────────────────────────────────────┤
│   数据传递层 (Port + FramePacket)   │  ← 阶段 5
│   - InputPort / OutputPort          │
│   - FramePacket 在 Port 间流转      │
├─────────────────────────────────────┤
│   处理层 (ProcessEntity)            │  ← 阶段 3, 4
│   - InputEntity (条件变量等待)      │
│   - MergeEntity (FrameSynchronizer) │
│   - 其他 Entity (process 逻辑)      │
├─────────────────────────────────────┤
│   图拓扑层 (PipelineGraph)          │  ← 阶段 1
│   - getDownstreamEntities           │
│   - getUpstreamEntities             │
├─────────────────────────────────────┤
│   线程池层 (TaskQueue)              │  ← 已存在
│   - GPU Queue / CPU Queue / IO Queue│
└─────────────────────────────────────┘
```

### 1.3 与现有架构对比

| 特性 | 现有架构 (同步) | 新架构 (异步) |
|------|----------------|---------------|
| 执行方式 | 层级遍历 | 任务链驱动 |
| InputEntity | 必须立即有数据 | 可等待数据 |
| 并发控制 | 阻塞等待完成 | 异步非阻塞 |
| 循环驱动 | 外部调用 | 自动循环 |
| 多路汇聚 | 不支持 | 原生支持 |

---

## 第二部分: 实施计划

### 2.1 总体时间规划

| 阶段 | 任务内容 | 预计时间 | 依赖 |
|------|---------|---------|------|
| **阶段 1** | PipelineGraph 扩展 | 0.5天 | 无 |
| **阶段 2** | PipelineExecutor 核心机制 | 1.5天 | 阶段 1 |
| **阶段 3** | InputEntity 改造 | 1天 | 阶段 2 |
| **阶段 4** | MergeEntity 多路汇聚 | 1天 | 阶段 2 |
| **阶段 5** | ProcessEntity Port 机制 | 0.5天 | 阶段 2 |
| **阶段 6** | PipelineManager 集成 | 0.5天 | 阶段 3,4,5 |
| **测试验证** | 单元测试 + 集成测试 | 1天 | 全部 |
| **总计** | | **6天** | |

### 2.2 实施原则

✅ **自底向上**: 从 Graph 层开始，逐层向上实施  
✅ **最小改动**: 每个阶段改动文件数最少  
✅ **可独立测试**: 每个阶段完成后可单独测试  
✅ **向后兼容**: 不破坏现有功能  

### 2.3 修改文件清单

#### 核心文件
```
Pipeline/Pipeline/
├── include/pipeline/
│   ├── core/
│   │   ├── PipelineGraph.h       ← 修改 (阶段 1)
│   │   └── PipelineExecutor.h    ← 修改 (阶段 2)
│   ├── entity/
│   │   ├── ProcessEntity.h       ← 修改 (阶段 5)
│   │   ├── InputEntity.h         ← 修改 (阶段 3)
│   │   └── MergeEntity.h         ← 修改 (阶段 4)
│   └── PipelineManager.h         ← 修改 (阶段 6)
├── src/
│   ├── core/
│   │   ├── PipelineGraph.cpp     ← 修改 (阶段 1)
│   │   └── PipelineExecutor.cpp  ← 修改 (阶段 2)
│   ├── entity/
│   │   ├── ProcessEntity.cpp     ← 修改 (阶段 5)
│   │   ├── InputEntity.cpp       ← 修改 (阶段 3)
│   │   └── MergeEntity.cpp       ← 修改 (阶段 4)
│   └── PipelineManager.cpp       ← 修改 (阶段 6)
```

---

## 第三部分: 分阶段实施

## 阶段 1: PipelineGraph 扩展

### 📌 目标
添加获取上下游 Entity 的接口，为任务链调度提供拓扑信息。

### 📝 实施步骤

#### Step 1.1: 修改 PipelineGraph.h

**文件**: `include/pipeline/core/PipelineGraph.h`

```cpp
// 在 PipelineGraph 类中添加以下公开方法
class PipelineGraph {
public:
    // ... 现有代码 ...
    
    /**
     * @brief 获取指定 Entity 的所有下游 Entity
     * @param entityId Entity ID
     * @return 下游 Entity ID 列表
     */
    std::vector<EntityId> getDownstreamEntities(EntityId entityId) const;
    
    /**
     * @brief 获取指定 Entity 的所有上游 Entity
     * @param entityId Entity ID
     * @return 上游 Entity ID 列表
     */
    std::vector<EntityId> getUpstreamEntities(EntityId entityId) const;
};
```

#### Step 1.2: 实现 PipelineGraph.cpp

**文件**: `src/core/PipelineGraph.cpp`

```cpp
std::vector<EntityId> PipelineGraph::getDownstreamEntities(EntityId entityId) const {
    std::vector<EntityId> result;
    
    auto entity = getEntity(entityId);
    if (!entity) {
        return result;
    }
    
    // 遍历所有输出端口
    for (size_t i = 0; i < entity->getOutputPortCount(); ++i) {
        auto port = entity->getOutputPort(i);
        if (!port) continue;
        
        // 获取连接的下游端口
        auto connections = port->getConnections();
        for (auto* inputPort : connections) {
            if (inputPort && inputPort->getEntity()) {
                EntityId downstreamId = inputPort->getEntity()->getId();
                // 去重
                if (std::find(result.begin(), result.end(), downstreamId) == result.end()) {
                    result.push_back(downstreamId);
                }
            }
        }
    }
    
    return result;
}

std::vector<EntityId> PipelineGraph::getUpstreamEntities(EntityId entityId) const {
    std::vector<EntityId> result;
    
    auto entity = getEntity(entityId);
    if (!entity) {
        return result;
    }
    
    // 遍历所有输入端口
    for (size_t i = 0; i < entity->getInputPortCount(); ++i) {
        auto port = entity->getInputPort(i);
        if (!port || !port->isConnected()) continue;
        
        // 获取连接的上游端口
        auto outputPort = port->getConnectedPort();
        if (outputPort && outputPort->getEntity()) {
            EntityId upstreamId = outputPort->getEntity()->getId();
            // 去重
            if (std::find(result.begin(), result.end(), upstreamId) == result.end()) {
                result.push_back(upstreamId);
            }
        }
    }
    
    return result;
}
```

#### ✅ 阶段 1 验证

编写单元测试验证：
```cpp
// 测试：简单链式连接
// InputEntity -> BeautyEntity -> OutputEntity
auto downstreams = graph->getDownstreamEntities(inputEntityId);
ASSERT_EQ(downstreams.size(), 1);
ASSERT_EQ(downstreams[0], beautyEntityId);

auto upstreams = graph->getUpstreamEntities(beautyEntityId);
ASSERT_EQ(upstreams.size(), 1);
ASSERT_EQ(upstreams[0], inputEntityId);
```

---

## 阶段 2: PipelineExecutor 核心机制

### 📌 目标
实现异步任务链的核心调度逻辑。

### 📝 实施步骤

#### Step 2.1: 修改 PipelineExecutor.h

**文件**: `include/pipeline/core/PipelineExecutor.h`

```cpp
class PipelineExecutor {
public:
    // ... 现有代码 ...
    
    /**
     * @brief 提交 Entity 任务 (🔥 新增接口)
     * @param entityId Entity ID
     * @param contextData 上下文数据 (可选)
     * @return 是否成功提交
     */
    bool submitEntityTask(EntityId entityId, 
                          std::shared_ptr<void> contextData = nullptr);
    
    /**
     * @brief 提交下游任务 (🔥 新增接口)
     * @param entityId 当前 Entity ID
     */
    void submitDownstreamTasks(EntityId entityId);
    
    /**
     * @brief 检查 Pipeline 是否完成
     * @param entityId 当前完成的 Entity ID
     * @return 是否整个 Pipeline 已完成
     */
    bool isPipelineCompleted(EntityId entityId);
    
    /**
     * @brief 重启 Pipeline 循环
     */
    void restartPipelineLoop();
    
    /**
     * @brief 设置 InputEntity ID (🔥 新增)
     */
    void setInputEntityId(EntityId entityId) { mInputEntityId = entityId; }

private:
    /**
     * @brief 执行单个 Entity 任务 (内部方法)
     */
    void executeEntityTask(EntityId entityId, std::shared_ptr<void> contextData);
    
    /**
     * @brief 检查所有依赖是否就绪
     */
    bool areAllDependenciesReady(EntityId entityId);
    
    // 跟踪每帧的 Entity 完成状态
    struct FrameExecutionState {
        std::set<EntityId> completedEntities;
        std::mutex mutex;
        uint64_t frameId = 0;
        int64_t timestamp = 0;
    };
    
    // 当前帧执行状态
    std::shared_ptr<FrameExecutionState> mCurrentFrameState;
    std::mutex mFrameStateMutex;
    
    // InputEntity ID (用于重启循环)
    EntityId mInputEntityId = InvalidEntityId;
};
```

#### Step 2.2: 实现 submitEntityTask

**文件**: `src/core/PipelineExecutor.cpp`

```cpp
bool PipelineExecutor::submitEntityTask(EntityId entityId, 
                                        std::shared_ptr<void> contextData) {
    if (!mRunning.load()) {
        return false;
    }
    
    auto entity = mGraph->getEntity(entityId);
    if (!entity || !entity->isEnabled()) {
        return false;
    }
    
    // 获取对应的任务队列
    auto queue = getQueueForEntity(entityId);
    if (!queue) {
        PIPELINE_LOGE("No queue found for entity %llu", entityId);
        return false;
    }
    
    // 🔥 关键: 创建任务并投递到队列
    auto taskOp = std::make_shared<task::TaskOperator>(
        [this, entityId, contextData](const std::shared_ptr<task::TaskOperator>&) {
            this->executeEntityTask(entityId, contextData);
        }
    );
    
    queue->async(taskOp);
    return true;
}
```

#### Step 2.3: 实现 executeEntityTask

```cpp
void PipelineExecutor::executeEntityTask(EntityId entityId, 
                                         std::shared_ptr<void> contextData) {
    auto entity = mGraph->getEntity(entityId);
    if (!entity) {
        return;
    }
    
    // 执行 Entity
    bool success = entity->execute(*mContext);
    
    if (!success) {
        // 🔥 特殊处理: 如果是 MergeEntity 且返回 false
        // 说明正在等待其他路,不算错误
        if (entity->getType() == EntityType::Composite) {
            return;  // 不投递下游任务,等待下次被触发
        }
        PIPELINE_LOGE("Entity %llu execution failed", entityId);
        return;
    }
    
    // 🔥 关键: 记录完成状态
    {
        std::lock_guard<std::mutex> lock(mFrameStateMutex);
        if (mCurrentFrameState) {
            std::lock_guard<std::mutex> stateLock(mCurrentFrameState->mutex);
            mCurrentFrameState->completedEntities.insert(entityId);
        }
    }
    
    // 🔥 关键: 投递下游任务
    submitDownstreamTasks(entityId);
    
    // 🔥 关键: 检查是否 Pipeline 完成
    if (isPipelineCompleted(entityId)) {
        onPipelineCompleted();
        restartPipelineLoop();
    }
}
```

#### Step 2.4: 实现 submitDownstreamTasks

```cpp
void PipelineExecutor::submitDownstreamTasks(EntityId entityId) {
    auto downstreams = mGraph->getDownstreamEntities(entityId);
    
    for (EntityId downstreamId : downstreams) {
        auto downstream = mGraph->getEntity(downstreamId);
        
        // 🔥 特殊处理: 如果下游是 MergeEntity
        if (downstream && downstream->getType() == EntityType::Composite) {
            // 检查是否所有上游都已完成
            if (!areAllDependenciesReady(downstreamId)) {
                continue;  // 上游未全部完成,不投递
            }
            
            // 🔥 额外检查: MergeEntity 的 Synchronizer 是否有已同步帧
            auto mergeEntity = std::dynamic_pointer_cast<MergeEntity>(downstream);
            if (mergeEntity && mergeEntity->getSynchronizer()) {
                if (!mergeEntity->getSynchronizer()->hasSyncedFrame()) {
                    continue;  // Synchronizer 中还没有同步好的帧,不投递
                }
            }
        }
        
        // 检查依赖
        if (areAllDependenciesReady(downstreamId)) {
            submitEntityTask(downstreamId);
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
```

#### Step 2.5: 实现 isPipelineCompleted

```cpp
bool PipelineExecutor::isPipelineCompleted(EntityId entityId) {
    // 检查是否是 sink entity (没有下游)
    auto downstreams = mGraph->getDownstreamEntities(entityId);
    if (!downstreams.empty()) {
        return false;  // 还有下游,未完成
    }
    
    // 检查所有 Entity 是否都已完成
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
            return false;  // 有 Entity 未完成
        }
    }
    
    return true;
}
```

#### Step 2.6: 实现 restartPipelineLoop

```cpp
void PipelineExecutor::restartPipelineLoop() {
    // 触发完成回调
    if (mFrameCompleteCallback && mCurrentFrameState) {
        // TODO: 构造 FramePacket 传递给回调
    }
    
    // 更新统计
    mStats.totalFrames++;
    
    // 🔥 关键: 创建新的帧状态
    {
        std::lock_guard<std::mutex> lock(mFrameStateMutex);
        mCurrentFrameState = std::make_shared<FrameExecutionState>();
        mCurrentFrameState->frameId = mStats.totalFrames;
    }
    
    // 🔥 关键: 重新投递 InputEntity 任务
    if (mInputEntityId != InvalidEntityId) {
        submitEntityTask(mInputEntityId);
    }
}
```

#### ✅ 阶段 2 验证

```cpp
// 测试：任务链调度
executor->submitEntityTask(inputEntityId);
// 验证：任务被正确投递到队列
// 验证：executeEntityTask 被调用
// 验证：submitDownstreamTasks 正确执行
```

---

## 阶段 3: InputEntity 改造

### 📌 目标
实现 InputEntity 的条件变量等待和队列预检机制。

### 📝 实施步骤

#### Step 3.1: 修改 InputEntity.h 数据结构

**文件**: `include/pipeline/entity/InputEntity.h`

```cpp
class InputEntity : public ProcessEntity {
public:
    // 🔥 新增: 设置 PipelineExecutor 引用
    void setExecutor(PipelineExecutor* executor) { mExecutor = executor; }
    
    // 🔥 新增: 启动/停止处理循环
    void startProcessingLoop();
    void stopProcessingLoop();
    
    // 🔥 新增: 提交数据接口
    bool submitData(const InputData& data);

private:
    // 输入数据队列 (线程安全)
    std::queue<InputData> mInputQueue;
    std::mutex mQueueMutex;
    std::condition_variable mDataAvailableCV;
    
    // 任务控制
    std::atomic<bool> mTaskRunning{false};      // 任务是否在运行
    std::atomic<bool> mWaitingForData{false};   // 是否等待数据
    
    // PipelineExecutor 引用 (用于投递下游任务)
    PipelineExecutor* mExecutor = nullptr;
    
    // 队列配置
    size_t mMaxQueueSize = 3;
    bool mDropOldestOnFull = true;
};
```

#### Step 3.2: 实现 submitData (数据提交 + 唤醒)

**文件**: `src/entity/InputEntity.cpp`

```cpp
bool InputEntity::submitData(const InputData& data) {
    std::unique_lock<std::mutex> lock(mQueueMutex);
    
    // 检查队列是否满
    if (mInputQueue.size() >= mMaxQueueSize) {
        if (mDropOldestOnFull) {
            mInputQueue.pop();  // 丢弃最旧帧
            PIPELINE_LOGW("Input queue full, dropping oldest frame");
        } else {
            PIPELINE_LOGE("Input queue full, dropping new frame");
            return false;
        }
    }
    
    // 入队
    mInputQueue.push(data);
    
    // 🔥 关键: 唤醒等待的 process 任务
    mDataAvailableCV.notify_one();
    
    return true;
}
```

#### Step 3.3: 实现 process (队列预检 + 等待)

```cpp
bool InputEntity::process(const std::vector<FramePacketPtr>& inputs,
                          std::vector<FramePacketPtr>& outputs,
                          PipelineContext& context) {
    InputData inputData;
    
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        
        // 🔥 关键改进: 先检查队列是否有数据
        if (!mInputQueue.empty()) {
            // 队列有数据,立即处理,不等待
            inputData = mInputQueue.front();
            mInputQueue.pop();
            PIPELINE_LOGD("Processing queued data immediately");
        } else {
            // 队列为空,等待数据到达
            mWaitingForData.store(true);
            mDataAvailableCV.wait(lock, [this] { 
                return !mInputQueue.empty() || !mTaskRunning.load(); 
            });
            mWaitingForData.store(false);
            
            // 检查任务是否被取消
            if (!mTaskRunning.load()) {
                return false;
            }
            
            // 再次检查队列
            if (mInputQueue.empty()) {
                PIPELINE_LOGW("Woke up but queue is empty");
                return false;
            }
            
            // 出队
            inputData = mInputQueue.front();
            mInputQueue.pop();
        }
    }
    
    // 处理数据
    if (!processInputData(inputData)) {
        return false;
    }
    
    // 生成输出
    if (isGPUOutputEnabled()) {
        auto gpuPacket = createGPUOutputPacket();
        if (gpuPacket) {
            outputs.push_back(gpuPacket);
        }
    }
    
    return true;
}
```

#### Step 3.4: 实现任务生命周期

```cpp
void InputEntity::startProcessingLoop() {
    mTaskRunning.store(true);
    
    // 将 process 任务投递到 TaskQueue (通过 PipelineExecutor)
    if (mExecutor) {
        mExecutor->submitEntityTask(this->getId());
    }
}

void InputEntity::stopProcessingLoop() {
    mTaskRunning.store(false);
    mDataAvailableCV.notify_all();  // 唤醒所有等待的任务
}
```

#### ✅ 阶段 3 验证

```cpp
// 测试 1: submitData 唤醒机制
inputEntity->startProcessingLoop();
// process 任务进入等待...
inputEntity->submitData(data);
// 验证: process 被唤醒并处理数据

// 测试 2: 队列预检
inputEntity->submitData(data1);
inputEntity->submitData(data2);
// 验证: 第二次 process 立即处理 data2,不等待 CV
```

---

## 阶段 4: MergeEntity 多路汇聚

### 📌 目标
集成 FrameSynchronizer 实现 GPU/CPU 多路汇聚。

### 📝 实施步骤

#### Step 4.1: 修改 MergeEntity.h

**文件**: `include/pipeline/entity/MergeEntity.h`

```cpp
class MergeEntity : public ProcessEntity {
public:
    // 🔥 新增: 设置 PipelineExecutor 引用
    void setExecutor(PipelineExecutor* executor) { mExecutor = executor; }
    
    // 🔥 新增: 获取 Synchronizer
    FrameSynchronizer* getSynchronizer() { return mSynchronizer.get(); }
    
protected:
    bool process(const std::vector<FramePacketPtr>& inputs,
                std::vector<FramePacketPtr>& outputs,
                PipelineContext& context) override;
                
private:
    PipelineExecutor* mExecutor = nullptr;
    std::atomic<bool> mWaitingForSync{false};
};
```

#### Step 4.2: 实现 MergeEntity::process (非阻塞汇聚)

**文件**: `src/entity/MergeEntity.cpp`

```cpp
bool MergeEntity::process(const std::vector<FramePacketPtr>& inputs,
                          std::vector<FramePacketPtr>& outputs,
                          PipelineContext& context) {
    // 🔥 关键设计: MergeEntity 不直接等待
    // 而是检查 FrameSynchronizer 是否有已同步的帧
    
    if (!mSynchronizer) {
        return false;
    }
    
    // 尝试获取已同步的帧 (非阻塞)
    auto syncedFrame = mSynchronizer->tryGetSyncedFrame();
    
    if (!syncedFrame) {
        // 没有已同步的帧,说明还在等待其他路
        // 🔥 关键: 返回 false,不生成输出
        // PipelineExecutor 会知道此 Entity 未完成,不投递下游任务
        return false;
    }
    
    // 有已同步的帧,创建合并输出
    auto mergedPacket = std::make_shared<FramePacket>();
    
    // 设置 GPU 纹理 (如果有)
    if (syncedFrame->hasGPU && syncedFrame->gpuFrame) {
        mergedPacket->setTexture(syncedFrame->gpuFrame->getTexture());
    }
    
    // 设置 CPU 数据 (如果有)
    if (syncedFrame->hasCPU && syncedFrame->cpuFrame) {
        mergedPacket->setCpuBuffer(
            syncedFrame->cpuFrame->getCpuBuffer(),
            syncedFrame->cpuFrame->getCpuBufferSize()
        );
    }
    
    mergedPacket->setTimestamp(syncedFrame->timestamp);
    outputs.push_back(mergedPacket);
    
    ++mMergedFrameCount;
    return true;
}
```

#### Step 4.3: 上游 Entity 推送到 Synchronizer

```cpp
// BeautyEntity.cpp (GPU路径)
bool BeautyEntity::process(const std::vector<FramePacketPtr>& inputs,
                          std::vector<FramePacketPtr>& outputs,
                          PipelineContext& context) {
    // ... 美颜处理 ...
    
    auto outputPacket = ...; // 处理结果
    outputs.push_back(outputPacket);
    
    // 🔥 关键: 推送到 MergeEntity 的 Synchronizer
    if (mMergeEntity && mMergeEntity->getSynchronizer()) {
        mMergeEntity->getSynchronizer()->pushGPUFrame(
            outputPacket, 
            outputPacket->getTimestamp()
        );
    }
    
    return true;
}

// FaceDetectionEntity.cpp (CPU路径)
bool FaceDetectionEntity::process(const std::vector<FramePacketPtr>& inputs,
                                  std::vector<FramePacketPtr>& outputs,
                                  PipelineContext& context) {
    // ... 人脸检测 ...
    
    auto outputPacket = ...; // 检测结果
    outputs.push_back(outputPacket);
    
    // 🔥 关键: 推送到 MergeEntity 的 Synchronizer
    if (mMergeEntity && mMergeEntity->getSynchronizer()) {
        mMergeEntity->getSynchronizer()->pushCPUFrame(
            outputPacket,
            outputPacket->getTimestamp()
        );
    }
    
    return true;
}
```

#### ✅ 阶段 4 验证

```cpp
// 测试: 多路汇聚
// GPU 路: InputEntity -> BeautyEntity -> MergeEntity
// CPU 路: InputEntity -> FaceDetectionEntity -> MergeEntity
// 验证: MergeEntity 只有在双路都完成后才输出
```

---

## 阶段 5: ProcessEntity Port 机制

### 📌 目标
确保 Port 机制与异步任务链兼容。

### 📝 实施步骤

#### Step 5.1: 修改 ProcessEntity::execute

**文件**: `src/entity/ProcessEntity.cpp`

```cpp
bool ProcessEntity::execute(PipelineContext& context) {
    if (!isEnabled() || !isReady()) {
        return false;
    }
    
    setState(EntityState::Processing);
    
    try {
        // 🔥 Step 1: 从 InputPort 收集输入
        std::vector<FramePacketPtr> inputs;
        for (size_t i = 0; i < getInputPortCount(); ++i) {
            auto port = getInputPort(i);
            if (port && port->hasPacket()) {
                inputs.push_back(port->getPacket());
            }
        }
        
        // 🔥 Step 2: 调用子类的 process
        std::vector<FramePacketPtr> outputs;
        bool success = process(inputs, outputs, context);
        
        if (!success) {
            setState(EntityState::Error);
            return false;
        }
        
        // 🔥 Step 3: 将输出写入 OutputPort
        for (size_t i = 0; i < outputs.size() && i < getOutputPortCount(); ++i) {
            auto port = getOutputPort(i);
            if (port) {
                port->setPacket(outputs[i]);
            }
        }
        
        setState(EntityState::Completed);
        return true;
        
    } catch (const std::exception& e) {
        setState(EntityState::Error);
        setErrorMessage(e.what());
        return false;
    }
}
```

#### Step 5.2: 修改 OutputPort::setPacket

**文件**: `src/data/FramePort.cpp` 或 `OutputPort.cpp`

```cpp
void OutputPort::setPacket(FramePacketPtr packet) {
    std::lock_guard<std::mutex> lock(mMutex);
    mPacket = packet;
    mHasPacket = true;
    
    // 🔥 不再调用 send() 通知下游
    // 由 PipelineExecutor 负责投递下游任务
}
```

#### ✅ 阶段 5 验证

```cpp
// 测试: Port 数据传递
// BeautyEntity 输出到 OutputPort
// OutputEntity 从连接的 InputPort 读取
// 验证: 数据正确传递
```

---

## 阶段 6: PipelineManager 集成

### 📌 目标
将异步任务链机制集成到 PipelineManager。

### 📝 实施步骤

#### Step 6.1: 修改 PipelineManager::start

**文件**: `src/PipelineManager.cpp`

```cpp
bool PipelineManager::start() {
    // ... 现有代码 ...
    
    // 🔥 新增: 查找 InputEntity
    auto inputEntities = mGraph->getEntitiesByType(EntityType::Input);
    if (!inputEntities.empty()) {
        auto inputEntity = std::dynamic_pointer_cast<input::InputEntity>(inputEntities[0]);
        if (inputEntity) {
            mExecutor->setInputEntityId(inputEntity->getId());
            inputEntity->setExecutor(mExecutor.get());
            
            // 🔥 启动 InputEntity 处理循环
            inputEntity->startProcessingLoop();
        }
    }
    
    setState(PipelineState::Running);
    return true;
}
```

#### Step 6.2: 简化 feedFrame

```cpp
bool PipelineManager::feedRGBA(const uint8_t* data, uint32_t width, uint32_t height,
                               uint32_t stride, uint64_t timestamp) {
    auto* inputEntity = getInputEntity();
    if (!inputEntity) {
        PIPELINE_LOGE("No InputEntity configured");
        return false;
    }
    
    // 🔥 简化: 直接提交数据,无需回调
    // InputEntity 内部会自动唤醒等待的 process 任务
    return inputEntity->submitRGBA(data, width, height, timestamp);
}
```

#### ✅ 阶段 6 验证

```cpp
// 测试: 完整集成
manager->start();
manager->feedRGBA(data, width, height, timestamp);
// 验证: Pipeline 自动执行并循环
```

---

## 第四部分: 测试验证

### 4.1 单元测试

#### 测试 1: PipelineGraph 拓扑查询

```cpp
TEST(PipelineGraphTest, GetDownstreamEntities) {
    // 构建: Input -> Beauty -> Output
    auto graph = createSimpleGraph();
    
    auto downstreams = graph->getDownstreamEntities(inputId);
    ASSERT_EQ(downstreams.size(), 1);
    ASSERT_EQ(downstreams[0], beautyId);
}

TEST(PipelineGraphTest, GetUpstreamEntities) {
    auto upstreams = graph->getUpstreamEntities(beautyId);
    ASSERT_EQ(upstreams.size(), 1);
    ASSERT_EQ(upstreams[0], inputId);
}
```

#### 测试 2: InputEntity 条件变量

```cpp
TEST(InputEntityTest, ConditionVariableWakeup) {
    auto inputEntity = createInputEntity();
    inputEntity->startProcessingLoop();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(inputEntity->isWaitingForData());
    
    inputEntity->submitData(testData);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_FALSE(inputEntity->isWaitingForData());
}
```

#### 测试 3: 任务链调度

```cpp
TEST(PipelineExecutorTest, TaskChainExecution) {
    auto executor = createExecutor();
    
    std::atomic<int> completedCount{0};
    executor->setEntityCompletedCallback([&](EntityId id) {
        completedCount++;
    });
    
    executor->submitEntityTask(inputId);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_EQ(completedCount, 3);  // Input, Beauty, Output
}
```

### 4.2 集成测试

#### 测试场景 1: 简单线性 Pipeline

```cpp
TEST(IntegrationTest, SimpleLinearPipeline) {
    // Input -> Beauty -> Output
    auto manager = createPipelineManager();
    manager->start();
    
    int frameCount = 0;
    manager->setFrameCompleteCallback([&](const FramePacket& packet) {
        frameCount++;
    });
    
    // 提交10帧
    for (int i = 0; i < 10; ++i) {
        manager->feedRGBA(data, width, height, timestamp + i);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_EQ(frameCount, 10);
}
```

#### 测试场景 2: 多分支汇聚

```cpp
TEST(IntegrationTest, MultiBranchMerge) {
    // Input -> [Beauty, FaceDetection] -> Merge -> Output
    auto manager = createMultiBranchPipeline();
    manager->start();
    
    int mergeCount = 0;
    auto mergeEntity = manager->getGraph()->getEntity(mergeId);
    mergeEntity->setCompletedCallback([&]() {
        mergeCount++;
    });
    
    for (int i = 0; i < 10; ++i) {
        manager->feedRGBA(data, width, height, timestamp + i);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ASSERT_EQ(mergeCount, 10);
}
```

### 4.3 性能测试

```cpp
TEST(PerformanceTest, Throughput) {
    auto manager = createPipelineManager();
    manager->start();
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000; ++i) {
        manager->feedRGBA(data, width, height, timestamp + i);
    }
    
    // 等待全部处理完
    waitForCompletion(manager);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    double fps = 1000.0 / (duration.count() / 1000.0);
    std::cout << "Throughput: " << fps << " FPS" << std::endl;
    ASSERT_GT(fps, 30.0);  // 期望超过 30 FPS
}
```

---

## 第五部分: 完整执行流程

### 5.1 启动阶段

```cpp
// 1. 创建并配置 Pipeline
auto manager = PipelineManager::create(renderContext);
manager->setupRGBAInput(1920, 1080);
manager->initialize();

// 2. 启动 Pipeline
manager->start();

// 内部执行:
// → PipelineExecutor::initialize()
//    → 创建 FrameExecutionState
// → InputEntity::startProcessingLoop()
//    → mTaskRunning = true
//    → PipelineExecutor::submitEntityTask(inputEntityId)
//       → 将 InputEntity process 任务投递到 IO 队列
//       → 任务在队列中等待,进入 mDataAvailableCV.wait()
```

### 5.2 运行阶段 (单帧)

```
[T0: Camera Thread] manager->feedRGBA(data, width, height, timestamp)
    ↓
    InputEntity::submitRGBA(data)
    → InputEntity::submitData(data)
       → mInputQueue.push(data)
       → mDataAvailableCV.notify_one()  🔥 唤醒等待的 process
    ↓
[T1: IO Queue Thread] InputEntity::process() 被唤醒
    → 检查队列: !mInputQueue.empty() = true
    → 立即出队: inputData = mInputQueue.front()
    → processInputData(inputData)
    → 生成 GPU/CPU 输出
    → 写入 OutputPort
    → 返回 true
    ↓
[T1: IO Queue Thread] PipelineExecutor::executeEntityTask(inputId)
    → entity->execute() 完成
    → mCurrentFrameState->completedEntities.insert(inputId)
    → submitDownstreamTasks(inputId)
       → 查找下游: [beautyEntityId]
       → submitEntityTask(beautyEntityId)
          → 将 BeautyEntity process 任务投递到 GPU 队列
    ↓
[T2: GPU Queue Thread] BeautyEntity::process()
    → 从 InputPort 获取输入纹理
    → 应用美颜算法
    → 输出到 OutputPort
    → 返回 true
    ↓
[T2: GPU Queue Thread] PipelineExecutor::executeEntityTask(beautyId)
    → entity->execute() 完成
    → mCurrentFrameState->completedEntities.insert(beautyId)
    → submitDownstreamTasks(beautyId)
       → 查找下游: [outputEntityId]
       → submitEntityTask(outputEntityId)
          → 将 OutputEntity process 任务投递到 GPU 队列
    ↓
[T3: GPU Queue Thread] OutputEntity::process()
    → 从 InputPort 获取输入纹理
    → 渲染到屏幕 / 触发回调
    → 返回 true
    ↓
[T3: GPU Queue Thread] PipelineExecutor::executeEntityTask(outputId)
    → entity->execute() 完成
    → mCurrentFrameState->completedEntities.insert(outputId)
    → submitDownstreamTasks(outputId)  // 无下游
    → isPipelineCompleted(outputId)
       → 检查所有 Entity 都已完成: true
       → 返回 true
    → onPipelineCompleted()
       → 触发 mFrameCompleteCallback
    → restartPipelineLoop()
       → 创建新的 mCurrentFrameState
       → submitEntityTask(inputEntityId)  🔥 重新投递 InputEntity 任务
    ↓
[T4: IO Queue Thread] InputEntity::process() 再次开始
    → 检查队列: 有数据? 立即处理 : 进入 mDataAvailableCV.wait()
    → 等待下一帧数据...
```

### 5.3 停止阶段

```cpp
// 1. 停止 Pipeline
manager->stop();

// 内部执行:
// → InputEntity::stopProcessingLoop()
//    → mTaskRunning = false
//    → mDataAvailableCV.notify_all()  // 唤醒所有等待,让任务退出
// → PipelineExecutor::shutdown()
//    → 清理任务队列
```

### 5.4 多分支汇聚流程

```
InputEntity
    ├─> (GPU Queue) BeautyEntity → FilterEntity
    └─> (CPU Queue) FaceDetectionEntity
            ↓                     ↓
         (汇聚)  MergeEntity (等待双路完成)
                     ↓
                OutputEntity

流程：
[T0] InputEntity 生成 GPU packet 和 CPU packet
  → submitDownstreamTasks(inputId)
     → 找到下游: [beautyId, faceDetectionId]
     → submitEntityTask(beautyId) -> GPU Queue
     → submitEntityTask(faceDetectionId) -> CPU Queue

[T1] (GPU Queue) BeautyEntity process
  → 美颜处理
  → pushGPUFrame(packet, ts) to MergeEntity.Synchronizer
     → Synchronizer: hasGPU=true, hasCPU=false, 不满足 WaitBoth
  → submitDownstreamTasks(beautyId)
     → 找到下游: [mergeId]
     → 检查 areAllDependenciesReady(mergeId)
        → 上游: [beautyId✅, faceDetectionId❌]
        → 依赖未满足,不投递

[T2] (CPU Queue) FaceDetectionEntity process
  → 人脸检测
  → pushCPUFrame(packet, ts) to MergeEntity.Synchronizer
     → Synchronizer: hasGPU=true, hasCPU=true, 满足 WaitBoth!
     → 生成 SyncedFrame,加入队列
  → submitDownstreamTasks(faceDetectionId)
     → 找到下游: [mergeId]
     → 检查 areAllDependenciesReady(mergeId)
        → 上游: [beautyId✅, faceDetectionId✅]
        → 依赖满足!
     → 检查 hasSyncedFrame() → true
     → submitEntityTask(mergeId) -> GPU Queue ✅

[T3] (GPU Queue) MergeEntity process
  → tryGetSyncedFrame() → 成功获取
  → 创建合并输出
  → submitDownstreamTasks(mergeId)
     → 找到下游: [outputId]
     → submitEntityTask(outputId)

[T4] (GPU Queue) OutputEntity process
  → 输出到屏幕/回调
  → isPipelineCompleted(outputId) → true
  → restartPipelineLoop()
```

---

## 附录: 架构对比

### A.1 现有架构 (同步层级遍历)

```
PipelineExecutor::processFrame(packet)
  ↓
遍历 Level 0: [InputEntity]
  → 同步执行 InputEntity::execute()
  ↓
遍历 Level 1: [BeautyEntity, FilterEntity]
  → 并行执行 BeautyEntity::execute()
  → 并行执行 FilterEntity::execute()
  ↓
遍历 Level 2: [OutputEntity]
  → 同步执行 OutputEntity::execute()
  ↓
完成,返回
```

**问题**:
- ❌ InputEntity 必须立即有数据,无法等待
- ❌ 必须等整个 Pipeline 执行完才能处理下一帧
- ❌ processFrame 是阻塞接口
- ❌ 不支持多路汇聚

### A.2 新架构 (异步任务链)

```
[T0] submitData(data)
  → 唤醒 InputEntity process 任务
  ↓
[T1] InputEntity process 任务执行
  → 处理数据
  → 投递 BeautyEntity 任务到 GPU 队列
  ↓
[T2] BeautyEntity process 任务执行
  → 处理纹理
  → 投递 FilterEntity 任务到 GPU 队列
  ↓
[T3] FilterEntity process 任务执行
  → 应用滤镜
  → 投递 OutputEntity 任务到 GPU 队列
  ↓
[T4] OutputEntity process 任务执行
  → 输出到屏幕/回调
  → 检测到 Pipeline 完成
  → 重新投递 InputEntity 任务(等待下一帧)
```

**优势**:
- ✅ InputEntity 可以在队列中等待数据
- ✅ 任务链异步执行,非阻塞
- ✅ 自动循环,无需外部驱动
- ✅ 支持多路汇聚 (FrameSynchronizer + MergeEntity)

### A.3 关键设计点总结

| 设计点 | 实现方案 | 作用 |
|---------|----------|------|
| **队列预检** | 先检查队列再等待 | 避免不必要延迟 (~1-2ms) |
| **任务链调度** | submitDownstreamTasks | 自动链式触发 |
| **依赖管理** | areAllDependenciesReady | 确保数据依赖满足 |
| **循环机制** | restartPipelineLoop | 自动重启下一帧 |
| **多路汇聚** | FrameSynchronizer | 基于时间戳同步 |
| **Port 兼容** | ProcessEntity::execute | 数据传递保持不变 |

---

## 🎉 实施完成检查列表

- [ ] 阶段 1: PipelineGraph 扩展
  - [ ] 添加 getDownstreamEntities
  - [ ] 添加 getUpstreamEntities
  - [ ] 单元测试通过

- [ ] 阶段 2: PipelineExecutor 核心机制
  - [ ] submitEntityTask 实现
  - [ ] executeEntityTask 实现
  - [ ] submitDownstreamTasks 实现
  - [ ] isPipelineCompleted 实现
  - [ ] restartPipelineLoop 实现
  - [ ] 单元测试通过

- [ ] 阶段 3: InputEntity 改造
  - [ ] 添加条件变量和队列
  - [ ] submitData 实现
  - [ ] process 队列预检实现
  - [ ] 单元测试通过

- [ ] 阶段 4: MergeEntity 多路汇聚
  - [ ] MergeEntity::process 非阻塞实现
  - [ ] 上游 Entity 推送到 Synchronizer
  - [ ] 单元测试通过

- [ ] 阶段 5: ProcessEntity Port 机制
  - [ ] ProcessEntity::execute 自动处理 Port
  - [ ] OutputPort::setPacket 修改
  - [ ] 单元测试通过

- [ ] 阶段 6: PipelineManager 集成
  - [ ] start() 集成
  - [ ] feedFrame 简化
  - [ ] 集成测试通过

- [ ] 测试验证
  - [ ] 简单线性 Pipeline 测试
  - [ ] 多分支汇聚测试
  - [ ] 性能测试 (>30 FPS)

---

**文档版本**: v2.0 (可执行实施版)  
**最后更新**: 2026-01-22  
**下一步**: 开始阶段 1 实施
