# Pipeline AI Agents 配置指南

本文件定义了 Pipeline 项目中 AI Agent 的行为规则和知识库配置。

## 配置文件结构

```
Pipeline/
├── .qoder/
│   └── rules/
│       └── architecture-guidelines.md   # 架构设计规则（核心）
├── AGENTS.md                             # 本文件
└── docs/
    └── ASYNC_TASK_DRIVEN_DESIGN.md      # 异步任务设计文档
```

## 核心规则文件

### architecture-guidelines.md

**触发条件**: always_on（始终生效）

**主要内容**:
1. **异步任务链驱动架构** - 非阻塞执行原则
2. **队列预检优化** - 条件变量使用模式
3. **重构清理检查清单** - 代码重构规范
4. **Pipeline 初始化时序** - 必须遵循的调用顺序
5. **平台兼容性规则** - iOS/macOS/Android 特定要求
6. **线程安全规则** - shared_from_this() 安全调用

## 关键经验文档

### 项目级经验

| 文档 | 描述 | 关键场景 |
|------|------|----------|
| [MacCameraApp Pipeline全链路问题排查](/Volumes/LiSSD/AI_Compound_Framework/projects/pipeline/experiences/maccameraapp-pipeline-troubleshooting.md) | macOS 集成 Pipeline 的完整排查经验 | 初始化时序、权限配置、CVPixelBuffer 传递 |
| [Pipeline异步任务驱动架构重构](/Volumes/LiSSD/AI_Compound_Framework/projects/pipeline/experiences/async-task-driven-refactoring.md) | 异步任务链架构重构经验 | Entity 设计、任务调度、条件变量 |

### 通用模式

| 模式 | 路径 | 适用场景 |
|------|------|----------|
| 异步任务链模式 | `/Volumes/LiSSD/AI_Compound_Framework/common/patterns/async-task-chain-pattern.md` | Entity 非阻塞设计 |
| 队列预检优化 | `/Volumes/LiSSD/AI_Compound_Framework/common/patterns/queue-precheck-pattern.md` | 高频数据处理 |

## 常见问题快速参考

### 1. Pipeline 初始化失败

```
错误: "Pipeline not initialized"
原因: initialize() 未在 setupDisplayOutput() 之前调用
解决: 调整调用顺序为 create() → initialize() → setupDisplayOutput() → start()
```

### 2. MetalContextManager 未设置

```
错误: "MetalContextManager not set"
原因: iOSMetalSurface 未收到 MetalContextManager
解决: 从 PlatformContext::getIOSMetalManager() 获取并传递
```

### 3. CVPixelBuffer 传递失败

```
错误: "No pixel buffer available"
原因: InputData.platformBuffer 字段未设置
解决: 设置 inputData.platformBuffer = pixelBuffer
```

### 4. Metal 纹理 CPU 访问失败

```
错误: "CPU access for textures with MTLStorageModePrivate"
原因: macOS 纹理使用了 Private 存储模式
解决: 非深度纹理使用 MTLStorageModeManaged
```

### 5. 相机权限被拒绝

```
错误: "Camera error: notAuthorized"
原因: macOS 缺少权限声明
解决: 创建 entitlements 文件，配置 NSCameraUsageDescription
```

## AI Agent 使用指南

### 排查问题时

1. 首先检查是否违反 architecture-guidelines.md 中的核心原则
2. 参考 MacCameraApp 全链路排查经验中的解决模式
3. 检查初始化时序是否正确

### 编写新代码时

1. 遵循异步任务链架构的非阻塞原则
2. 使用队列预检优化提高性能
3. 注意平台特定的兼容性要求

### 重构代码时

1. 使用重构清理检查清单
2. 确保不破坏现有的依赖关系
3. 编译验证后运行测试

---

**版本**: v1.0  
**创建日期**: 2026-02-24  
**维护者**: Pipeline Team
