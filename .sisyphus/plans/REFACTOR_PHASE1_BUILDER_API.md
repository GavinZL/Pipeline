# Phase 1: Builder API 重构方案

> **Phase**: 1 / 6  
> **预计工期**: 3 天  
> **目标**: 统一 PipelineFacade 和 PipelineManager，提供 Builder + Fluent API  
> **语言标准**: C++17（严格遵守）

---

## 一、问题分析

### 1.1 当前痛点

**复杂的初始化流程**：
```cpp
// 当前使用方式 - 步骤繁琐，容易出错
PipelineFacadeConfig config;
config.preset = PipelinePreset::CameraPreview;
config.platformConfig.platform = PlatformType::iOS;
config.renderWidth = 1920;
config.renderHeight = 1080;

auto pipeline = PipelineFacade::create(config);
if (!pipeline->initialize()) {
    // 错误处理
}
if (!pipeline->setupDisplayOutput(surface, w, h)) {
    // 错误处理
}
pipeline->addBeautyFilter(0.7f, 0.3f);
pipeline->start();
```

**双重维护成本**：
- PipelineFacade: 面向用户的简化 API
- PipelineManager: 内部管理 API
- 两者职责重叠，大量方法只是转发调用

**缺乏类型安全**：
- 配置参数分散在多个 struct 中
- 运行时才能发现配置错误

### 1.2 重构目标

1. **统一接口**：合并 Facade 和 Manager，只暴露一个 `Pipeline` 类
2. **Builder 模式**：提供流畅的链式调用 API
3. **延迟初始化**：build() 时才创建资源，start() 自动完成初始化
4. **类型安全**：使用强类型配置，编译期发现错误
5. **向后兼容**：保留旧 API（标记为 deprecated）

---

## 二、设计方案

### 2.1 核心类设计

```cpp
// include/pipeline/Pipeline.h
#pragma once

#include "pipeline/core/PipelineConfig.h"
#include "pipeline/data/EntityTypes.h"
#include "pipeline/platform/PlatformContext.h"
#include <memory>
#include <string>
#include <functional>
#include <vector>

namespace pipeline {

// 前向声明
class PipelineBuilder;
class PipelineImpl;

// ============================================================================
// 公共类型定义
// ============================================================================

enum class PipelinePreset : uint8_t {
    CameraPreview,      // 相机预览（实时显示）
    CameraRecord,       // 相机录制（编码输出）
    ImageProcess,       // 图像处理（文件输入输出）
    LiveStream,         // 直播推流
    VideoPlayback,      // 视频播放
    Custom              // 自定义
};

enum class QualityLevel : uint8_t {
    Low,        // 低质量（高性能）
    Medium,     // 中等质量
    High,       // 高质量
    Ultra       // 超高质量（低性能）
};

// 输出目标类型（用于强类型区分）
struct DisplayOutput {
    void* surface;
    uint32_t width;
    uint32_t height;
};

struct CallbackOutput {
    std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback;
    OutputFormat format;
};

// ============================================================================
// Pipeline 主类（统一接口）
// ============================================================================

/**
 * @brief Pipeline 主类 - 统一的外观接口
 * 
 * 使用示例：
 * @code
 * auto pipeline = Pipeline::create()
 *     .withPreset(PipelinePreset::CameraPreview)
 *     .withPlatform(PlatformType::iOS)
 *     .withDisplayOutput(surface, 1920, 1080)
 *     .withBeautyFilter(0.7f, 0.3f)
 *     .build();
 * 
 * pipeline->start();
 * @endcode
 */
class Pipeline : public std::enable_shared_from_this<Pipeline> {
public:
    // ========================================================================
    // 工厂方法 - 创建 Builder
    // ========================================================================
    
    /**
     * @brief 创建 Pipeline Builder
     * @return PipelineBuilder 实例
     */
    static PipelineBuilder create();
    
    /**
     * @brief 从 JSON 配置文件创建（Phase 4 实现）
     * @param configFilePath JSON 配置文件路径
     * @return Pipeline 实例，失败返回 nullptr
     */
    static std::shared_ptr<Pipeline> fromJsonFile(const std::string& configFilePath);
    
    ~Pipeline();
    
    // 禁止拷贝
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    
    // ========================================================================
    // 生命周期管理
    // ========================================================================
    
    /**
     * @brief 启动 Pipeline（自动完成初始化）
     * @return 是否成功
     */
    [[nodiscard]] bool start();
    
    /**
     * @brief 暂停 Pipeline
     */
    void pause();
    
    /**
     * @brief 恢复 Pipeline
     */
    void resume();
    
    /**
     * @brief 停止 Pipeline
     */
    void stop();
    
    /**
     * @brief 销毁 Pipeline 资源
     */
    void destroy();
    
    /**
     * @brief 获取当前状态
     */
    PipelineState getState() const;
    
    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const;
    
    // ========================================================================
    // 输入接口（统一）
    // ========================================================================
    
    /**
     * @brief 输入 RGBA 数据
     */
    bool feedRGBA(const uint8_t* data, uint32_t width, uint32_t height, 
                  uint32_t stride = 0, uint64_t timestamp = 0);
    
    /**
     * @brief 输入 YUV420 数据
     */
    bool feedYUV420(const uint8_t* yData, const uint8_t* uData, 
                    const uint8_t* vData, uint32_t width, uint32_t height,
                    uint64_t timestamp = 0);
    
    /**
     * @brief 输入 NV12/NV21 数据
     */
    bool feedNV12(const uint8_t* yData, const uint8_t* uvData,
                  uint32_t width, uint32_t height, bool isNV21 = false,
                  uint64_t timestamp = 0);
    
    /**
     * @brief 输入 GPU 纹理
     */
    bool feedTexture(std::shared_ptr<LRTexture> texture, 
                     uint32_t width, uint32_t height, uint64_t timestamp = 0);
    
    // ========================================================================
    // 运行时 Entity 管理
    // ========================================================================
    
    /**
     * @brief 添加自定义 Entity
     * @param entity Entity 实例
     * @return Entity ID，失败返回 InvalidEntityId
     */
    EntityId addEntity(ProcessEntityPtr entity);
    
    /**
     * @brief 移除 Entity
     */
    bool removeEntity(EntityId entityId);
    
    /**
     * @brief 启用/禁用 Entity
     */
    void setEntityEnabled(EntityId entityId, bool enabled);
    
    /**
     * @brief 添加美颜滤镜（便捷方法）
     */
    EntityId addBeautyFilter(float smoothLevel, float whitenLevel);
    
    /**
     * @brief 添加颜色滤镜（便捷方法）
     */
    EntityId addColorFilter(const std::string& filterName, float intensity = 1.0f);
    
    /**
     * @brief 添加锐化滤镜（便捷方法）
     */
    EntityId addSharpenFilter(float amount);
    
    // ========================================================================
    // 输出管理
    // ========================================================================
    
    /**
     * @brief 添加显示输出（运行时）
     * @return 输出目标 ID，失败返回 -1
     */
    int32_t addDisplayOutput(void* surface, uint32_t width, uint32_t height);
    
    /**
     * @brief 添加回调输出（运行时）
     */
    int32_t addCallbackOutput(
        std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback,
        OutputFormat format);
    
    /**
     * @brief 移除输出目标
     */
    bool removeOutputTarget(int32_t targetId);
    
    /**
     * @brief 启用/禁用输出目标
     */
    void setOutputTargetEnabled(int32_t targetId, bool enabled);
    
    // ========================================================================
    // 配置和状态
    // ========================================================================
    
    /**
     * @brief 设置输出分辨率
     */
    void setOutputResolution(uint32_t width, uint32_t height);
    
    /**
     * @brief 设置旋转角度
     */
    void setRotation(int32_t degrees);
    
    /**
     * @brief 设置镜像
     */
    void setMirror(bool horizontal, bool vertical);
    
    /**
     * @brief 设置帧率限制
     */
    void setFrameRateLimit(int32_t fps);
    
    // ========================================================================
    // 回调设置
    // ========================================================================
    
    /**
     * @brief 设置帧处理完成回调
     */
    void setFrameProcessedCallback(std::function<void(FramePacketPtr)> callback);
    
    /**
     * @brief 设置错误回调
     */
    void setErrorCallback(std::function<void(const std::string&)> callback);
    
    /**
     * @brief 设置状态变更回调
     */
    void setStateCallback(std::function<void(PipelineState)> callback);
    
    // ========================================================================
    // 统计和调试
    // ========================================================================
    
    /**
     * @brief 获取性能统计
     */
    ExecutionStats getStats() const;
    
    /**
     * @brief 获取平均处理时间（毫秒）
     */
    double getAverageProcessTime() const;
    
    /**
     * @brief 导出当前配置到 JSON（Phase 4 实现）
     */
    bool exportConfig(const std::string& filePath) const;
    
    /**
     * @brief 导出管线图（DOT 格式）
     */
    std::string exportGraph() const;
    
private:
    // 只有 Builder 可以构造
    friend class PipelineBuilder;
    explicit Pipeline(std::unique_ptr<PipelineImpl> impl);
    
    std::unique_ptr<PipelineImpl> mImpl;
};

// ============================================================================
// PipelineBuilder - Builder 类
// ============================================================================

/**
 * @brief Pipeline Builder - 用于流畅地配置 Pipeline
 * 
 * 使用示例：
 * @code
 * auto pipeline = Pipeline::create()
 *     .withPreset(PipelinePreset::CameraPreview)
 *     .withPlatform(PlatformType::iOS)
 *     .withQuality(QualityLevel::High)
 *     .withDisplayOutput(surface, 1920, 1080)
 *     .withCallbackOutput(callback, OutputFormat::RGBA)
 *     .withBeautyFilter(0.7f, 0.3f)
 *     .withColorFilter("vintage", 0.8f)
 *     .build();
 * @endcode
 */
class PipelineBuilder {
public:
    PipelineBuilder();
    ~PipelineBuilder();
    
    // 禁止拷贝，允许移动
    PipelineBuilder(const PipelineBuilder&) = delete;
    PipelineBuilder& operator=(const PipelineBuilder&) = delete;
    PipelineBuilder(PipelineBuilder&&) noexcept;
    PipelineBuilder& operator=(PipelineBuilder&&) noexcept;
    
    // ========================================================================
    // 基础配置（Fluent API，返回自身引用）
    // ========================================================================
    
    /**
     * @brief 设置管线预设类型
     */
    PipelineBuilder& withPreset(PipelinePreset preset);
    
    /**
     * @brief 设置平台类型
     */
    PipelineBuilder& withPlatform(PlatformType platform);
    
    /**
     * @brief 设置平台配置（详细配置）
     */
    PipelineBuilder& withPlatformConfig(const PlatformContextConfig& config);
    
    /**
     * @brief 设置质量级别
     */
    PipelineBuilder& withQuality(QualityLevel quality);
    
    /**
     * @brief 设置渲染分辨率
     */
    PipelineBuilder& withResolution(uint32_t width, uint32_t height);
    
    /**
     * @brief 设置最大队列大小
     */
    PipelineBuilder& withMaxQueueSize(int32_t size);
    
    /**
     * @brief 启用/禁用 GPU 优化
     */
    PipelineBuilder& withGPUOptimization(bool enable);
    
    /**
     * @brief 启用/禁用多线程
     */
    PipelineBuilder& withMultiThreading(bool enable);
    
    /**
     * @brief 设置线程池大小
     */
    PipelineBuilder& withThreadPoolSize(int32_t size);
    
    /**
     * @brief 设置旋转角度
     */
    PipelineBuilder& withRotation(int32_t degrees);
    
    /**
     * @brief 设置镜像
     */
    PipelineBuilder& withMirror(bool horizontal, bool vertical);
    
    // ========================================================================
    // 输入配置
    // ========================================================================
    
    /**
     * @brief 设置 RGBA 输入配置
     */
    PipelineBuilder& withRGBAInput(uint32_t width, uint32_t height);
    
    /**
     * @brief 设置 YUV 输入配置
     */
    PipelineBuilder& withYUVInput(uint32_t width, uint32_t height);
    
    /**
     * @brief 设置 NV12 输入配置
     */
    PipelineBuilder& withNV12Input(uint32_t width, uint32_t height);
    
    // ========================================================================
    // 输出配置（支持链式添加多个输出）
    // ========================================================================
    
    /**
     * @brief 添加显示输出
     */
    PipelineBuilder& withDisplayOutput(void* surface, uint32_t width, uint32_t height);
    
    /**
     * @brief 添加回调输出
     */
    PipelineBuilder& withCallbackOutput(
        std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback,
        OutputFormat format);
    
    /**
     * @brief 添加编码器输出
     */
    PipelineBuilder& withEncoderOutput(void* encoderSurface, EncoderType type);
    
    /**
     * @brief 添加文件输出
     */
    PipelineBuilder& withFileOutput(const std::string& filePath);
    
    // ========================================================================
    // 滤镜配置（预设滤镜）
    // ========================================================================
    
    /**
     * @brief 添加美颜滤镜
     */
    PipelineBuilder& withBeautyFilter(float smoothLevel, float whitenLevel);
    
    /**
     * @brief 添加颜色滤镜
     */
    PipelineBuilder& withColorFilter(const std::string& filterName, float intensity = 1.0f);
    
    /**
     * @brief 添加锐化滤镜
     */
    PipelineBuilder& withSharpenFilter(float amount);
    
    /**
     * @brief 添加模糊滤镜
     */
    PipelineBuilder& withBlurFilter(float radius);
    
    /**
     * @brief 添加自定义 Entity
     */
    PipelineBuilder& withCustomEntity(ProcessEntityPtr entity);
    
    // ========================================================================
    // 回调配置
    // ========================================================================
    
    /**
     * @brief 设置错误回调
     */
    PipelineBuilder& withErrorCallback(std::function<void(const std::string&)> callback);
    
    /**
     * @brief 设置状态变更回调
     */
    PipelineBuilder& withStateCallback(std::function<void(PipelineState)> callback);
    
    /**
     * @brief 设置帧处理完成回调
     */
    PipelineBuilder& withFrameProcessedCallback(std::function<void(FramePacketPtr)> callback);
    
    // ========================================================================
    // 构建方法
    // ========================================================================
    
    /**
     * @brief 构建 Pipeline 实例
     * @return Pipeline 实例，失败返回 nullptr
     * @throws std::invalid_argument 如果配置无效
     */
    [[nodiscard]] std::shared_ptr<Pipeline> build();
    
    /**
     * @brief 验证当前配置
     * @return 如果配置有效返回空字符串，否则返回错误信息
     */
    [[nodiscard]] std::string validate() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace pipeline

#endif // PIPELINE_PIPELINE_H

---

## 三、实现细节

### 3.1 PipelineImpl（PIMPL 模式）

使用 PIMPL 模式隐藏实现细节，保持二进制兼容性：

```cpp
// src/PipelineImpl.h
#pragma once

#include "pipeline/Pipeline.h"
#include "pipeline/core/PipelineManager.h"
#include "pipeline/platform/PlatformContext.h"
#include "pipeline/input/InputEntity.h"
#include "pipeline/output/OutputEntity.h"

namespace pipeline {

class PipelineImpl {
public:
    // 构建器状态
    struct BuilderState {
        PipelinePreset preset = PipelinePreset::Custom;
        PlatformContextConfig platformConfig;
        QualityLevel quality = QualityLevel::Medium;
        uint32_t width = 1920;
        uint32_t height = 1080;
        int32_t maxQueueSize = 3;
        bool enableGPUOpt = true;
        bool enableMultiThread = true;
        int32_t threadPoolSize = 4;
        int32_t rotation = 0;
        bool mirrorH = false;
        bool mirrorV = false;
        
        // 输入配置
        input::InputFormat inputFormat = input::InputFormat::RGBA;
        
        // 输出配置列表
        struct OutputConfig {
            enum Type { Display, Callback, Encoder, File };
            Type type;
            // Union 或 variant 存储不同类型配置
            struct {
                void* surface;
                uint32_t width;
                uint32_t height;
            } display;
            struct {
                std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback;
                OutputFormat format;
            } callback;
        };
        std::vector<OutputConfig> outputs;
        
        // Entity 配置列表
        std::vector<ProcessEntityPtr> customEntities;
        
        // 滤镜配置
        struct FilterConfig {
            enum Type { Beauty, Color, Sharpen, Blur };
            Type type;
            // 参数...
        };
        std::vector<FilterConfig> filters;
        
        // 回调
        std::function<void(const std::string&)> errorCallback;
        std::function<void(PipelineState)> stateCallback;
        std::function<void(FramePacketPtr)> frameCallback;
    };
    
    explicit PipelineImpl(BuilderState state);
    ~PipelineImpl();
    
    // 生命周期
    bool start();
    void pause();
    void resume();
    void stop();
    void destroy();
    PipelineState getState() const;
    
    // 输入
    bool feedRGBA(const uint8_t* data, uint32_t width, uint32_t height, 
                  uint32_t stride, uint64_t timestamp);
    bool feedYUV420(const uint8_t* yData, const uint8_t* uData, 
                    const uint8_t* vData, uint32_t width, uint32_t height,
                    uint64_t timestamp);
    bool feedNV12(const uint8_t* yData, const uint8_t* uvData,
                  uint32_t width, uint32_t height, bool isNV21,
                  uint64_t timestamp);
    bool feedTexture(std::shared_ptr<LRTexture> texture, 
                     uint32_t width, uint32_t height, uint64_t timestamp);
    
    // Entity 管理
    EntityId addEntity(ProcessEntityPtr entity);
    bool removeEntity(EntityId entityId);
    void setEntityEnabled(EntityId entityId, bool enabled);
    EntityId addBeautyFilter(float smoothLevel, float whitenLevel);
    EntityId addColorFilter(const std::string& filterName, float intensity);
    EntityId addSharpenFilter(float amount);
    
    // 输出管理
    int32_t addDisplayOutput(void* surface, uint32_t width, uint32_t height);
    int32_t addCallbackOutput(
        std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback,
        OutputFormat format);
    bool removeOutputTarget(int32_t targetId);
    void setOutputTargetEnabled(int32_t targetId, bool enabled);
    
    // 配置
    void setOutputResolution(uint32_t width, uint32_t height);
    void setRotation(int32_t degrees);
    void setMirror(bool horizontal, bool vertical);
    void setFrameRateLimit(int32_t fps);
    
    // 回调
    void setFrameProcessedCallback(std::function<void(FramePacketPtr)> callback);
    void setErrorCallback(std::function<void(const std::string&)> callback);
    void setStateCallback(std::function<void(PipelineState)> callback);
    
    // 统计
    ExecutionStats getStats() const;
    double getAverageProcessTime() const;
    std::string exportGraph() const;
    
private:
    bool initializeInternal();
    bool setupInputEntity();
    bool setupOutputEntity();
    bool applyQualitySettings();
    
    BuilderState mState;
    
    // 核心组件
    std::shared_ptr<PipelineManager> mManager;
    std::unique_ptr<PlatformContext> mPlatformContext;
    lrengine::render::LRRenderContext* mRenderContext = nullptr;
    
    // 运行时状态
    PipelineState mRuntimeState = PipelineState::Created;
    mutable std::mutex mStateMutex;
};

} // namespace pipeline
```

### 3.2 向后兼容方案

保留旧的 PipelineFacade，但标记为 deprecated：

```cpp
// include/pipeline/PipelineFacade.h
#pragma once

#include "pipeline/Pipeline.h"

namespace pipeline {

/**
 * @deprecated 请使用新的 Pipeline 类
 * @code
 * // 旧代码
 * auto facade = PipelineFacade::create(config);
 * 
 * // 新代码
 * auto pipeline = Pipeline::create()
 *     .withPreset(config.preset)
 *     .withPlatform(config.platformConfig.platform)
 *     .build();
 * @endcode
 */
class [[deprecated("Use Pipeline class instead")]] PipelineFacade {
public:
    static std::shared_ptr<PipelineFacade> create(const PipelineFacadeConfig& config);
    
    // 所有方法转发到内部的 Pipeline 实例
    bool initialize() { return mPipeline->start(); }
    bool start() { return mPipeline->start(); }
    void stop() { mPipeline->stop(); }
    // ... 其他方法
    
private:
    std::shared_ptr<Pipeline> mPipeline;
    PipelineFacadeConfig mConfig;
};

} // namespace pipeline
```

---

## 四、实施计划

### 4.1 文件修改清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/pipeline/Pipeline.h` | 新增 | 新的统一接口头文件 |
| `src/Pipeline.cpp` | 新增 | Pipeline 类实现 |
| `src/PipelineImpl.h` | 新增 | PIMPL 实现头文件 |
| `src/PipelineImpl.cpp` | 新增 | PIMPL 实现 |
| `include/pipeline/PipelineBuilder.h` | 可选 | Builder 可以单独拆分为头文件 |
| `include/pipeline/PipelineFacade.h` | 修改 | 标记为 deprecated |
| `CMakeLists.txt` | 修改 | 添加新源文件 |

### 4.2 详细任务分解

#### Day 1: 基础框架搭建

**上午：头文件设计**
- [ ] 创建 `include/pipeline/Pipeline.h`
  - 定义 Pipeline 类公共接口
  - 定义 PipelineBuilder 类
  - 确保所有方法使用 C++17 特性
  
- [ ] 创建 `src/PipelineImpl.h`
  - 设计 PIMPL 结构
  - 规划 BuilderState 结构
  - 规划初始化流程

**下午：实现基础类**
- [ ] 实现 `PipelineBuilder` 类
  - 实现所有 withXXX() 方法
  - 实现 build() 方法
  - 实现 validate() 方法
  
- [ ] 实现 `Pipeline` 类框架
  - 构造函数/析构函数
  - 工厂方法 create()
  - 生命周期方法（空实现）

#### Day 2: 核心逻辑实现

**上午：PipelineImpl 实现**
- [ ] 实现 `PipelineImpl` 初始化逻辑
  - PlatformContext 创建
  - PipelineManager 创建
  - 输入/输出 Entity 设置
  
- [ ] 实现输入接口
  - feedRGBA()
  - feedYUV420()
  - feedNV12()
  - feedTexture()

**下午：Entity 和输出管理**
- [ ] 实现 Entity 管理方法
  - addEntity()
  - removeEntity()
  - setEntityEnabled()
  
- [ ] 实现滤镜便捷方法
  - addBeautyFilter()
  - addColorFilter()
  - addSharpenFilter()
  
- [ ] 实现输出管理方法
  - addDisplayOutput()
  - addCallbackOutput()
  - removeOutputTarget()

#### Day 3: 集成测试和向后兼容

**上午：向后兼容层**
- [ ] 修改 `PipelineFacade`
  - 标记为 deprecated
  - 实现为 Pipeline 的包装
  - 确保所有旧 API 可用
  
- [ ] 更新 `CMakeLists.txt`
  - 添加新源文件
  - 确保编译通过

**下午：测试和文档**
- [ ] 创建单元测试
  - Builder 测试
  - Pipeline 生命周期测试
  - 输入/输出测试
  
- [ ] 验证向后兼容
  - 确保旧代码可以编译（有 deprecation warning）
  - 运行现有测试套件
  
- [ ] 更新文档
  - 添加迁移指南
  - 更新 API 文档

### 4.3 验收检查清单

- [ ] **功能完整性**
  - [ ] 所有 PipelineFacade 功能在新 Pipeline 中可用
  - [ ] Builder API 支持链式调用
  - [ ] 延迟初始化正常工作（build() 时才创建资源）
  
- [ ] **向后兼容**
  - [ ] PipelineFacade 仍可编译
  - [ ] 编译器显示 deprecation warning
  - [ ] 所有现有测试通过
  
- [ ] **代码质量**
  - [ ] 所有代码符合 C++17 标准
  - [ ] 使用 PIMPL 模式隐藏实现
  - [ ] 无内存泄漏（通过 valgrind 或类似工具检查）
  
- [ ] **性能**
  - [ ] 启动时间无退化（< 5%）
  - [ ] 处理帧率无退化（< 5%）
  - [ ] 内存使用无退化（< 10%）
  
- [ ] **文档**
  - [ ] 新 API 有完整注释
  - [ ] 迁移文档完成
  - [ ] 示例代码可用

---

## 五、使用示例

### 5.1 新 API 使用示例

```cpp
#include <pipeline/Pipeline.h>

// 简洁的 Builder API
auto pipeline = pipeline::Pipeline::create()
    .withPreset(pipeline::PipelinePreset::CameraPreview)
    .withPlatform(pipeline::PlatformType::iOS)
    .withQuality(pipeline::QualityLevel::High)
    .withResolution(1920, 1080)
    .withDisplayOutput(metalLayer, 1920, 1080)
    .withCallbackOutput([](const uint8_t* data, size_t size, 
                           uint32_t w, uint32_t h, int64_t ts) {
        // 处理输出帧
    }, pipeline::OutputFormat::RGBA)
    .withBeautyFilter(0.7f, 0.3f)
    .withColorFilter("vintage", 0.8f)
    .build();

if (!pipeline) {
    // 处理构建失败
    return;
}

// 启动（自动初始化）
if (!pipeline->start()) {
    // 处理启动失败
    return;
}

// 输入帧数据
pipeline->feedRGBA(rgbaData, 1920, 1080);

// 停止
pipeline->stop();
```

### 5.2 运行时动态配置示例

```cpp
auto pipeline = pipeline::Pipeline::create()
    .withPreset(pipeline::PipelinePreview::CameraPreview)
    .build();

// 启动后动态添加输出
auto displayId = pipeline->addDisplayOutput(surface, 1920, 1080);
auto callbackId = pipeline->addCallbackOutput(callback, format);

// 动态添加滤镜
pipeline->addBeautyFilter(0.5f, 0.2f);
pipeline->addColorFilter("warm", 0.6f);

// 启用/禁用输出
pipeline->setOutputTargetEnabled(displayId, true);
pipeline->setOutputTargetEnabled(callbackId, false);

pipeline->start();
```

### 5.3 从旧 API 迁移

```cpp
// ========== 旧代码 ==========
#include <pipeline/PipelineFacade.h>

PipelineFacadeConfig config;
config.preset = PipelinePreset::CameraPreview;
config.platformConfig.platform = PlatformType::iOS;

auto pipeline = PipelineFacade::create(config);
pipeline->initialize();
pipeline->setupDisplayOutput(surface, 1920, 1080);
pipeline->addBeautyFilter(0.7f, 0.3f);
pipeline->start();

// ========== 新代码 ==========
#include <pipeline/Pipeline.h>

auto pipeline = Pipeline::create()
    .withPreset(PipelinePreset::CameraPreview)
    .withPlatform(PlatformType::iOS)
    .withDisplayOutput(surface, 1920, 1080)
    .withBeautyFilter(0.7f, 0.3f)
    .build();

pipeline->start();  // 自动初始化
```

---

## 六、风险与缓解

| 风险 | 可能性 | 影响 | 缓解措施 |
|------|--------|------|----------|
| PIMPL 带来性能开销 | 低 | 中 | 使用内联和编译器优化，实测确认 |
| 构建器配置验证复杂 | 中 | 低 | 分阶段验证，提供详细错误信息 |
| 向后兼容层引入 bug | 中 | 高 | 完整测试覆盖，灰度发布 |
| API 设计不符合预期 | 低 | 高 | 先 review 设计，确认后再实现 |

---

## 七、下一步行动

1. **Review 本方案** - 确认设计符合预期
2. **批准实施** - 确认可以开始 Day 1 工作
3. **开始实施** - 按照 Day 1 计划开始编码

---

**准备实施**: 等待确认后开始 Phase 1 实施
