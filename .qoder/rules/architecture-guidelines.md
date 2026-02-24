---
trigger: always_on
---
# Pipeline 架构设计指南

## 规则概述

本规则文件定义了Pipeline项目的架构设计原则和经验应用指南，帮助AI在设计、开发和重构时做出正确决策。

## 核心架构原则

### 1. 异步任务链驱动架构（必须遵循）

**来源**: `/Volumes/LiSSD/AI_Compound_Framework/common/patterns/async-task-chain-pattern.md`

**原则**:
- ✅ **非阻塞执行**: 所有Entity的execute()方法必须立即返回，不能阻塞等待
- ✅ **事件驱动**: 使用条件变量(std::condition_variable)实现事件驱动，而非轮询
- ✅ **任务链调度**: Entity完成后自动投递下游任务
- ✅ **自动循环**: Pipeline完成后自动重启下一帧，无需外部驱动

**应用场景**:
- 新增Entity时：继承ProcessEntity并实现非阻塞的execute()
- 修改执行逻辑时：确保不引入阻塞调用（如waitInputsReady(-1)）
- 优化性能时：检查是否有不必要的等待和阻塞

**错误示例**:
```cpp
// ❌ 错误 - 阻塞等待
bool MyEntity::execute(PipelineContext& context) {
    if (!waitInputsReady(-1)) return false;  // 阻塞！
    // ...
}
```

**正确示例**:
```cpp
// ✅ 正确 - 非阻塞检查
bool MyEntity::execute(PipelineContext& context) {
    if (!areInputsReady()) {
        return false;  // 立即返回，等待下次调度
    }
    // ...
}
```

### 2. 队列预检优化（性能关键）

**来源**: `/Volumes/LiSSD/AI_Compound_Framework/common/patterns/queue-precheck-pattern.md`

**原则**:
- ✅ 在条件变量wait之前，先检查队列是否有数据
- ✅ 避免1-2ms的不必要延迟
- ✅ 适用于高频场景（>60fps）

**应用场景**:
- InputEntity::process() - 队列为空时才wait
- 任何使用条件变量等待数据的场景

### 3. 重构清理检查清单（重构时必用）

**来源**: `/Volumes/LiSSD/AI_Compound_Framework/common/practices/refactoring/cleanup-checklist.md`

**原则**:
- ✅ 删除成员变量前必须全局搜索所有使用点
- ✅ 修改方法签名时同步更新头文件和实现文件
- ✅ 删除锁时分析是否真的需要锁保护
- ✅ 编译验证后必须运行测试

## Pipeline 初始化时序规则（必须遵循）

**来源**: `/Volumes/LiSSD/AI_Compound_Framework/projects/pipeline/experiences/maccameraapp-pipeline-troubleshooting.md`

**正确调用顺序**:
```
create(config) → initialize() → setupDisplayOutput() → start() → feedPixelBuffer()
```

**依赖关系**:
- `create()`: 创建 PipelineFacade 实例（不初始化内部组件）
- `initialize()`: 创建 PlatformContext、RenderContext、PipelineManager
- `setupDisplayOutput()`: 需要 mPipelineManager 已存在，需要 MetalContextManager
- `start()`: 启动 Pipeline 执行
- `feedPixelBuffer()`: 需要 Pipeline 处于 Running 状态

**常见错误**:
```cpp
// ❌ 错误 - setupDisplayOutput 在 initialize 之前调用
auto pipeline = PipelineFacade::create(config);
pipeline->setupDisplayOutput(...);  // mPipelineManager 为空！
pipeline->start();

// ✅ 正确 - 先 initialize 再 setupDisplayOutput
auto pipeline = PipelineFacade::create(config);
pipeline->initialize();             // 创建 mPipelineManager
pipeline->setupDisplayOutput(...);  // 现在可以工作了
pipeline->start();
```

**MetalContextManager 传递**:
- `setupDisplayOutput()` 需要 `MetalContextManager` 来创建 `iOSMetalSurface`
- 必须从 `PlatformContext::getIOSMetalManager()` 获取并传递
- 链式传递: `PipelineFacade` → `PipelineManager` → `iOSMetalSurface`

## 模块特定规则

### InputEntity

**规则**:
- 必须使用队列预检优化
- submitData触发条件变量唤醒
- process()中从队列取数据，避免阻塞
- 支持优雅停止（mTaskRunning标志）

### MergeEntity

**规则**:
- 使用FrameSynchronizer实现非阻塞汇聚
- process()调用tryGetSyncedFrame()（非阻塞）
- 没有同步帧时返回false，不生成输出
- submitDownstreamTasks需额外检查hasSyncedFrame()

### ProcessEntity

**规则**:
- execute()自动处理InputPort→process→OutputPort流程
- 子类只需实现process()方法
- 不要在子类的execute()中阻塞等待

### OutputEntity

**规则**:
- 支持多输出目标（Display/Encoder/Callback等）
- 每个输出目标独立开关
- 平台特定输出使用条件编译

## 平台兼容性规则

### Android平台

**EGL上下文共享**:
- 必须从相机线程的EGL Context创建共享上下文
- Pipeline线程使用自己的上下文
- OES纹理转换为标准2D纹理

### iOS平台

**CVPixelBuffer处理**:
- 使用CVMetalTextureCache避免拷贝
- 支持NV12/BGRA等多种格式
- 封装为LRTexture统一处理

**CVPixelBuffer数据传递（重要）**:
- ✅ `InputData` 必须包含 `platformBuffer` 字段来传递 CVPixelBuffer
- ✅ `PixelBufferInputStrategy::processToGPU()` 优先使用 `input.platformBuffer`
- ✅ 使用 `static_cast<CVPixelBufferRef>(input.platformBuffer)` 获取缓冲区
- ⚠️ 不要依赖 `mCurrentPixelBuffer` 成员变量，它可能未被设置

### macOS平台

**相机权限配置**:
- ✅ 必须创建 `.entitlements` 文件，声明 `com.apple.security.device.camera`
- ✅ Xcode 配置 `CODE_SIGN_ENTITLEMENTS` 指向该文件
- ✅ `Info.plist` 添加 `NSCameraUsageDescription`
- ⚠️ `ENABLE_APP_SANDBOX = YES` 时需要额外权限声明

**Metal纹理存储模式（重要）**:
- ✅ 非深度纹理使用 `MTLStorageModeManaged`（macOS）或 `MTLStorageModeShared`（iOS）
- ❌ 禁止对需要 CPU 数据更新的纹理使用 `MTLStorageModePrivate`
- 原因：`replaceRegion` 方法在 Private 模式下不可用

**CAMetalLayer时序**:
- ✅ 在 `viewDidAppear()` 中调用 `view.layoutSubtreeIfNeeded()` 强制布局
- ✅ 检查 `drawableSize` 和 `bounds` 都有效后再创建 Pipeline

## 性能优化规则

### 零拷贝
- ✅ GPU纹理直接传递，避免CPU中转
- ✅ 多输出目标共享同一纹理
- ✅ 使用平台原生格式（CVPixelBuffer/SurfaceTexture）

### 并行处理
- ✅ TaskQueue调度CPU和GPU Entity并行
- ✅ 多输出目标并行处理
- ✅ 回调在独立线程执行

### 资源池化
- ✅ 纹理池复用GPU纹理
- ✅ FramePacket池避免频繁分配
- ✅ FBO池复用帧缓冲对象

## 代码规范

### 命名约定
- 成员变量: `mVariableName`
- 方法: `camelCase`
- 类名: `PascalCase`
- 常量: `kConstantName`

### 注释规范
- 废弃API必须添加@deprecated注释
- 提供迁移指南
- 重要决策添加注释说明原因

### 线程安全
- 共享数据必须加锁保护
- 使用std::atomic<bool>作为状态标志
- 条件变量配合mutex使用

**shared_from_this() 安全调用**:
- ⚠️ `shared_from_this()` 要求对象必须被 `shared_ptr` 正确管理
- ✅ 使用 try-catch 保护 `shared_from_this()` 调用
- ✅ 在异常情况下提供降级处理
```cpp
// ✅ 正确 - 安全调用 shared_from_this
try {
    auto self = shared_from_this();
    mCallBack(self);
} catch (const std::bad_weak_ptr& e) {
    LOGE("shared_from_this() failed: %s", e.what());
    mCallBack(nullptr);  // 降级处理
}
```

## 参考资源

### AI复利框架经验
- [异步任务链架构模式](/Volumes/LiSSD/AI_Compound_Framework/common/patterns/async-task-chain-pattern.md)
- [队列预检优化模式](/Volumes/LiSSD/AI_Compound_Framework/common/patterns/queue-precheck-pattern.md)
- [重构清理检查清单](/Volumes/LiSSD/AI_Compound_Framework/common/practices/refactoring/cleanup-checklist.md)

### 项目文档
- [架构设计文档](../ARCHITECTURE_DESIGN.md)
- [API快速参考](../API_QUICK_REFERENCE.md)
- [异步任务设计](../docs/ASYNC_TASK_DRIVEN_DESIGN.md)

### 完整实施经验
- [Pipeline异步任务驱动架构重构](/Volumes/LiSSD/AI_Compound_Framework/projects/pipeline/experiences/async-task-driven-refactoring.md)
- [MacCameraApp Pipeline全链路问题排查](/Volumes/LiSSD/AI_Compound_Framework/projects/pipeline/experiences/maccameraapp-pipeline-troubleshooting.md)

## 使用指南

### AI使用本规则的时机
1. **设计新功能时**: 参考架构原则和模式
2. **编写代码时**: 遵循代码规范和模块规则
3. **重构代码时**: 使用清理检查清单
4. **优化性能时**: 应用性能优化规则
5. **排查问题时**: 检查是否违反核心原则

### 用户使用本规则的方式
1. **Code Review**: 检查PR是否符合规范
2. **架构评审**: 验证设计是否遵循原则
3. **新人培训**: 快速了解项目规范
4. **问题诊断**: 定位架构违反问题

---

**版本**: v1.1  
**创建日期**: 2026-01-27  
**最后更新**: 2026-02-24  
**维护者**: Pipeline Team
