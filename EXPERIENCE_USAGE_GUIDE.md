# Pipeline经验知识使用指南

本文档详细说明如何在日常开发中有效利用已存储到AI复利框架中的经验知识。

## 📚 知识存储位置

### 1. AI复利框架 (主知识库)

**位置**: `/Volumes/LiSSD/AI_Compound_Framework/`

```
AI_Compound_Framework/
├── projects/pipeline/               # Pipeline项目专属经验
│   ├── experiences/
│   │   └── async-task-driven-refactoring.md  # 完整重构实施经验
│   └── summary/
│       └── 2026-01-27-async-refactoring.md   # 项目总结
│
├── common/patterns/                 # 可复用模式
│   ├── async-task-chain-pattern.md          # 异步任务链架构模式
│   └── queue-precheck-pattern.md            # 队列预检优化模式
│
└── common/practices/refactoring/    # 最佳实践
    └── cleanup-checklist.md                 # 重构清理检查清单
```

### 2. Pipeline项目规则文件

**位置**: `/Volumes/LiSSD/ProjectT/MyProject/github/Pipeline/.qoder/rules/`

```
Pipeline/.qoder/rules/
└── architecture-guidelines.md      # 架构设计指南（引用AI复利框架）
```

## 🎯 使用场景与检索方式

### 场景1: 设计新功能

**需求**: 新增一个处理Entity，不知道如何设计

**检索步骤**:

1. **查看规则文件**（AI会自动读取）
   ```bash
   cat Pipeline/.qoder/rules/architecture-guidelines.md
   ```
   
2. **查找相关模式**
   ```bash
   # 搜索异步任务链相关经验
   grep -r "异步任务链" /Volumes/LiSSD/AI_Compound_Framework/
   
   # 或直接打开模式文档
   cat /Volumes/LiSSD/AI_Compound_Framework/common/patterns/async-task-chain-pattern.md
   ```

3. **参考项目经验**
   ```bash
   # 查看之前的实施经验
   cat /Volumes/LiSSD/AI_Compound_Framework/projects/pipeline/experiences/async-task-driven-refactoring.md
   ```

**应用方式**:
- ✅ 继承ProcessEntity基类
- ✅ 实现非阻塞的execute()方法
- ✅ 使用areInputsReady()而非waitInputsReady(-1)
- ✅ 返回false让调度器稍后重试

**代码模板**（来自经验）:
```cpp
class MyNewEntity : public ProcessEntity {
public:
    bool execute(PipelineContext& context) override {
        // 🔥 非阻塞检查
        if (!areInputsReady()) {
            setState(EntityState::Blocked);
            return false;  // 等待下次调度
        }
        
        // 正常处理
        // ...
        
        setState(EntityState::Completed);
        return true;
    }
};
```

### 场景2: 性能优化

**需求**: InputEntity处理延迟高，需要优化

**检索步骤**:

1. **搜索性能优化相关经验**
   ```bash
   grep -r "性能优化\|队列预检" /Volumes/LiSSD/AI_Compound_Framework/
   ```

2. **查看队列预检模式**
   ```bash
   cat /Volumes/LiSSD/AI_Compound_Framework/common/patterns/queue-precheck-pattern.md
   ```

**应用方式**:
- ✅ 先检查队列是否有数据，再决定是否wait
- ✅ 避免1-2ms的条件变量延迟
- ✅ 监控队列命中率，验证优化效果

**优化前后对比**（来自经验）:
```cpp
// ❌ 旧方式 - 直接wait (延迟1.5ms)
{
    std::unique_lock<std::mutex> lock(mQueueMutex);
    mDataAvailableCV.wait(lock, [this] { 
        return !mInputQueue.empty(); 
    });
    data = mInputQueue.front();
    mInputQueue.pop();
}

// ✅ 新方式 - 队列预检 (延迟0.1ms)
{
    std::unique_lock<std::mutex> lock(mQueueMutex);
    
    if (!mInputQueue.empty()) {
        // 快速路径 - 立即处理
        data = mInputQueue.front();
        mInputQueue.pop();
    } else {
        // 慢速路径 - 等待唤醒
        mDataAvailableCV.wait(lock, [this] { 
            return !mInputQueue.empty() || !mTaskRunning.load(); 
        });
        // ...
    }
}
```

### 场景3: 重构代码

**需求**: 删除一个旧的成员变量，担心遗漏使用点

**检索步骤**:

1. **查看重构清理清单**
   ```bash
   cat /Volumes/LiSSD/AI_Compound_Framework/common/practices/refactoring/cleanup-checklist.md
   ```

2. **搜索相关案例**
   ```bash
   grep -r "mCurrentTimestamp\|mInputMutex" /Volumes/LiSSD/AI_Compound_Framework/projects/pipeline/
   ```

**应用方式**（来自清单）:

**步骤1: 全局搜索**
```bash
grep -r "mVariableName" Pipeline/
```

**步骤2: 分析每个使用点**
- 直接读取 → 从其他数据源获取
- 在方法中使用 → 添加方法参数
- 作为临时存储 → 使用局部变量

**步骤3: 修改方法签名**
```cpp
// 头文件 (.h)
FramePacketPtr createPacket(int64_t timestamp);  // 添加参数

// 实现文件 (.cpp)
FramePacketPtr MyClass::createPacket(int64_t timestamp) {
    packet->setTimestamp(timestamp);  // 使用参数而非成员变量
}
```

**步骤4-6**: 同步修改、编译验证、运行测试

### 场景4: 排查Bug

**需求**: MergeEntity没有正确触发下游Entity

**检索步骤**:

1. **查看规则文件中的MergeEntity规则**
   ```bash
   grep -A 20 "### MergeEntity" Pipeline/.qoder/rules/architecture-guidelines.md
   ```

2. **查找项目经验**
   ```bash
   grep -r "MergeEntity.*submitDownstreamTasks" /Volumes/LiSSD/AI_Compound_Framework/
   ```

**发现问题**（来自经验）:
MergeEntity的submitDownstreamTasks需要**额外检查hasSyncedFrame()**

**正确实现**:
```cpp
void PipelineExecutor::submitDownstreamTasks(EntityId entityId) {
    auto downstreams = getDownstreamEntities(entityId);
    
    for (auto downstreamId : downstreams) {
        // 🔥 MergeEntity特殊检查
        if (isMergeEntity(downstreamId)) {
            auto mergeEntity = getMergeEntity(downstreamId);
            if (!mergeEntity->getSynchronizer()->hasSyncedFrame()) {
                continue;  // 跳过，等待其他路数据
            }
        }
        
        if (areAllDependenciesReady(downstreamId)) {
            submitTask(downstreamId);
        }
    }
}
```

### 场景5: Code Review

**需求**: 审查PR，确保符合架构规范

**检索步骤**:

1. **对照规则文件检查**
   ```bash
   # 打开规则文件作为Checklist
   cat Pipeline/.qoder/rules/architecture-guidelines.md
   ```

2. **检查关键点**:
   - [ ] 是否有阻塞调用？
   - [ ] 是否使用了队列预检？
   - [ ] 删除代码是否遵循清理清单？
   - [ ] 是否添加了必要的注释？

**示例PR审查意见**（基于经验）:
```
❌ 问题: execute()方法中使用了waitInputsReady(-1)阻塞等待

参考: Pipeline/.qoder/rules/architecture-guidelines.md 第15行
应改为: areInputsReady()非阻塞检查

详细说明: https://file:///Volumes/LiSSD/AI_Compound_Framework/common/patterns/async-task-chain-pattern.md
```

## 🤖 AI如何使用这些经验

### 自动触发机制

当您与AI对话时，Qoder会自动：

1. **读取规则文件** - `.qoder/rules/` 下的所有.md文件
2. **匹配场景** - 根据您的问题类型匹配相关规则
3. **检索经验** - 根据规则中的链接查找AI复利框架中的经验
4. **应用模式** - 使用经验中的代码模板和最佳实践

### 手动触发方式

您也可以明确要求AI使用特定经验：

**示例1: 引用规则**
```
User: "我要新增一个Entity，请按照架构规则设计"

AI会:
1. 读取 Pipeline/.qoder/rules/architecture-guidelines.md
2. 应用"异步任务链驱动架构"原则
3. 生成符合规范的代码
```

**示例2: 引用模式**
```
User: "InputEntity的性能不好，参考队列预检模式优化"

AI会:
1. 访问 AI复利框架/common/patterns/queue-precheck-pattern.md
2. 应用模式中的实现模板
3. 提供优化代码
```

**示例3: 引用清单**
```
User: "我要删除mOldVariable这个成员变量，帮我检查"

AI会:
1. 访问 AI复利框架/common/practices/refactoring/cleanup-checklist.md
2. 执行6步检查流程
3. 全局搜索使用点
4. 提供修改方案
```

## 💡 最佳实践

### 1. 开始新任务前先查规则

```bash
# 养成习惯：开始编码前先看规则
cat Pipeline/.qoder/rules/architecture-guidelines.md
```

### 2. 遇到问题先搜经验

```bash
# 搜索关键词
grep -r "关键词" /Volumes/LiSSD/AI_Compound_Framework/

# 按类型搜索
grep -r "性能优化" /Volumes/LiSSD/AI_Compound_Framework/common/patterns/
grep -r "重构" /Volumes/LiSSD/AI_Compound_Framework/common/practices/
```

### 3. 将新经验及时记录

当您遇到新问题并解决后：

```bash
# 使用AI复利框架的脚本记录
cd /Volumes/LiSSD/AI_Compound_Framework
python scripts/init-project.py pipeline

# 或手动添加到项目经验
cat > projects/pipeline/experiences/新问题-解决方案.md << EOF
---
title: 新问题的解决方案
date: $(date +%Y-%m-%d)
type: experience
project: pipeline
tags: [问题类型, 关键技术]
---

## 问题描述
...

## 解决方案
...

## 可提炼的模式
...
EOF
```

### 4. 定期更新规则文件

当架构演进或有新的最佳实践时：

```bash
# 编辑规则文件
vim Pipeline/.qoder/rules/architecture-guidelines.md

# 添加新的规则或更新引用链接
```

## 🔍 快速检索命令

### 搜索所有Pipeline相关经验
```bash
find /Volumes/LiSSD/AI_Compound_Framework -name "*.md" | xargs grep -l "pipeline"
```

### 按标签搜索
```bash
grep -r "tags:.*async" /Volumes/LiSSD/AI_Compound_Framework/
grep -r "tags:.*performance" /Volumes/LiSSD/AI_Compound_Framework/
grep -r "tags:.*refactoring" /Volumes/LiSSD/AI_Compound_Framework/
```

### 搜索特定模式
```bash
grep -r "队列预检\|异步任务链\|重构清理" /Volumes/LiSSD/AI_Compound_Framework/common/patterns/
```

### 搜索代码示例
```bash
# 搜索包含代码块的文档
grep -l "```cpp" /Volumes/LiSSD/AI_Compound_Framework/common/patterns/*.md
```

## 📊 经验使用效果评估

### 指标

- ✅ **开发速度**: 使用模板/模式后，新功能开发时间减少
- ✅ **Bug率**: 遵循规则后，架构违反类bug减少
- ✅ **Code Review效率**: 对照清单后，Review时间缩短
- ✅ **知识传承**: 新成员通过规则和经验快速上手

### 持续改进

- 📝 记录哪些经验最常用
- 📝 记录哪些经验需要补充
- 📝 定期更新和优化规则文件

## 📞 获取帮助

### 与AI对话时

直接说明您的需求，AI会自动应用相关经验：

```
"帮我设计一个新的FilterEntity，要符合异步任务链架构"
"优化InputEntity的性能，延迟有点高"
"我要删除mOldData这个变量，帮我检查所有使用点"
```

### 自助查询

使用grep搜索关键词，找到相关文档后直接阅读：

```bash
grep -r "您的问题关键词" /Volumes/LiSSD/AI_Compound_Framework/
```

---

**版本**: v1.0  
**创建日期**: 2026-01-27  
**更新日期**: 2026-01-27

通过系统化地使用这些经验知识，您可以：
- 🚀 加速开发效率
- 🛡️ 提升代码质量
- 📚 积累团队知识
- 🔄 实现知识复利
