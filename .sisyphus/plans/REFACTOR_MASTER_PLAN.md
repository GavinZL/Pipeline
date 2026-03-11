# Pipeline 框架重构计划

> **文档版本**: v1.0  
> **创建日期**: 2026-03-10  
> **目标**: 将 Pipeline 从现有架构升级为工程级成熟框架  
> **语言标准**: C++17（严格遵守，不使用 C++20/23 特性）

---

## 一、当前架构分析

### 1.1 架构现状

Pipeline 是一个跨平台的 C++17 视频处理框架，采用异步任务链驱动架构：

```
Application (PipelineFacade) - 简化 API
    ↓
Core (PipelineManager/Graph/Executor) - 任务调度
    ↓
Entity (Input/Process/Output/Merge) - 处理节点
    ↓
Platform (Context/Metal/EGL) - 平台适配
    ↓
Foundation (LREngine/TaskQueue) - 渲染/线程
```

### 1.2 当前痛点

| 痛点 | 说明 | 严重程度 |
|------|------|----------|
| **双重接口** | PipelineFacade + PipelineManager 职责重叠 | 🔴 高 |
| **平台 ifdef 泛滥** | 头文件中大量 `#if defined(__APPLE__)` | 🔴 高 |
| **配置分散** | 多个 Config 结构，难以统一管理 | 🟡 中 |
| **生命周期耦合** | 难以测试和 mock | 🟡 中 |
| **错误处理弱** | bool 返回值，缺乏错误信息 | 🟡 中 |
| **缺少可观测性** | 缺乏 metrics 和 tracing | 🟢 低 |

---

## 二、重构总体计划

### 2.1 重构目标

1. **统一接口层**：消除双重维护成本
2. **平台策略模式**：消除条件编译，提升可测试性
3. **配置系统化**：支持 JSON 配置和版本管理
4. **错误处理现代化**：详细错误信息不丢失
5. **可观测性增强**：生产环境可监控

### 2.2 实施阶段

| 阶段 | 内容 | 预计工期 | 依赖 |
|------|------|----------|------|
| **Phase 1** | 统一接口层（Builder + Fluent API） | 3 天 | 无 |
| **Phase 2** | 平台策略模式重构 | 2 天 | Phase 1 |
| **Phase 3** | 错误处理现代化 | 1 天 | Phase 1 |
| **Phase 4** | 配置系统 JSON 化 | 2 天 | Phase 1 |
| **Phase 5** | 可观测性增强 | 3 天 | Phase 1-4 |
| **Phase 6** | Entity 注册表（插件化） | 2 天 | Phase 1-5 |

### 2.3 C++17 限制说明

⚠️ **严格遵守 C++17 标准，不使用以下特性：**

- ❌ `std::expected` (C++23) → 使用 `std::optional` 或自定义 `Result<T>`
- ❌ Concepts (C++20) → 使用 SFINAE 或 static_assert
- ❌ Modules (C++20) → 继续使用头文件
- ❌ Coroutines (C++20) → 使用回调或 Future
- ❌ Ranges (C++20) → 使用标准算法

✅ **允许使用的现代特性：**
- `std::optional`, `std::variant`, `std::any`
- `std::shared_ptr`, `std::unique_ptr`, `std::make_shared`
- Lambda 表达式（C++14/17 扩展）
- `constexpr`, `noexcept`, `[[nodiscard]]`
- 结构化绑定（C++17）

---

## 三、各阶段详细说明

### Phase 1: 统一接口层（Builder + Fluent API）

**目标**：合并 PipelineFacade 和 PipelineManager，提供简洁的 Builder API

**当前问题**：
```cpp
// 复杂的初始化流程
auto pipeline = PipelineFacade::create(config);
pipeline->initialize();
pipeline->setupDisplayOutput(surface, w, h);
pipeline->start();
```

**改进方案**：
```cpp
// 简洁的 Builder API
auto pipeline = Pipeline::create()
    .withPreset(PipelinePreset::CameraPreview)
    .withPlatform(PlatformType::iOS)
    .withDisplayOutput(surface, w, h)
    .withBeautyFilter(0.7f, 0.3f)
    .build();

pipeline->start();  // 自动初始化
```

**详细方案见**: `.sisyphus/plans/REFACTOR_PHASE1_BUILDER_API.md`

---

### Phase 2: 平台策略模式

**目标**：消除头文件中的平台条件编译

**当前问题**：
```cpp
class PipelineFacade {
#if defined(__APPLE__)
    EntityId setupPixelBufferInput(...);
#endif
#if defined(__ANDROID__)
    EntityId setupOESInput(...);
#endif
};
```

**改进方案**：
```cpp
class InputStrategy {
public:
    virtual ~InputStrategy() = default;
    virtual bool feed(const void* data) = 0;
};

// 平台特定实现
class PixelBufferInputStrategy : public InputStrategy { ... };
class OESTextureInputStrategy : public InputStrategy { ... };
```

---

### Phase 3: 错误处理现代化

**目标**：提供详细的错误信息

**当前问题**：
```cpp
bool initialize();  // 失败原因未知
```

**改进方案（C++17）**：
```cpp
// 使用 std::optional 和错误信息输出参数
std::optional<std::string> initialize();  // 返回空表示成功，否则返回错误信息

// 或使用结构体
struct Result {
    bool success;
    std::string errorMessage;
    std::string errorCategory;
};
Result initialize();
```

---

### Phase 4: 配置系统 JSON 化

**目标**：支持 JSON 配置和版本管理

**改进方案**：
```cpp
// 从 JSON 文件加载
auto pipeline = Pipeline::fromJsonFile("config.json");

// 导出当前配置
pipeline->exportConfig("current_config.json");

// JSON Schema 验证
PipelineConfigValidator validator;
validator.validate(jsonConfig);  // 检查必需字段、类型、范围
```

---

### Phase 5: 可观测性增强

**目标**：生产环境可监控

**改进方案**：
```cpp
class PipelineMetrics {
public:
    void recordProcessLatency(double ms);
    void recordFps(double fps);
    void recordDroppedFrame();
    void recordQueueDepth(size_t depth);
};

// 使用
pipeline->setMetricsCollector(std::make_shared<PipelineMetrics>());
```

---

### Phase 6: Entity 注册表（插件化）

**目标**：支持运行时扩展

**改进方案**：
```cpp
// 注册 Entity
REGISTER_ENTITY("beauty", BeautyEntity);
REGISTER_ENTITY("sharpen", SharpenEntity);

// 运行时创建
pipeline->addEntity("beauty", R"({"smooth": 0.7})");
```

---

## 四、风险评估

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| API 破坏性变更 | 高 | 保留旧 API 为 deprecated，提供迁移指南 |
| 性能回归 | 中 | 每个 Phase 完成后进行性能基准测试 |
| 平台兼容性问题 | 中 | 在 Android/iOS 模拟器和真机上充分测试 |
| 工期延误 | 低 | 每个 Phase 可独立交付，允许调整范围 |

---

## 五、验收标准

### Phase 1 验收标准
- [ ] 新的 Builder API 可用
- [ ] 旧 API 标记为 deprecated 但仍可编译
- [ ] 所有现有测试通过
- [ ] 性能无退化（< 5% 差异）
- [ ] 文档更新完成

---

## 六、下一步行动

1. **Review Phase 1 详细方案**: `.sisyphus/plans/REFACTOR_PHASE1_BUILDER_API.md`
2. **确认方案后**：开始 Phase 1 实施
3. **Phase 1 完成后**：Review 并进入 Phase 2

---

**维护者**: Pipeline Team  
**最后更新**: 2026-03-10
