# Pipeline 架构设计总结

## 设计概览

Pipeline 作为完整的独立前处理模块，已成功集成 LREngine 和 TaskQueue 作为 submodule。本文档总结核心设计决策和架构要点。

---

## 核心设计决策

### 1. 模块化设计 ✅

**分层架构**：
```
Application → PipelineFacade → PipelineManager → Platform/Entities → LREngine/TaskQueue
```

**优势**：
- 清晰的模块边界
- 易于测试和维护
- 支持独立编译和发布

### 2. 多平台兼容性 ✅

#### Android 平台：EGL 上下文共享方案

**问题**：相机线程的 OES 纹理无法直接在 Pipeline 线程使用

**解决方案**：
```cpp
// 创建共享 EGL 上下文
AndroidEGLContextManager manager;
manager.initialize({
    .sharedContext = cameraEGLContext,  // 与相机共享
    .display = cameraDisplay,
    .glesVersion = 3
});

// Pipeline 线程激活自己的上下文后可访问相机纹理
manager.makeCurrent();
glBindTexture(GL_TEXTURE_EXTERNAL_OES, cameraOESTexture);
```

**关键技术点**：
- EGL 上下文共享机制
- OES 纹理转标准 2D 纹理
- 跨线程上下文切换

#### iOS 平台：CVPixelBuffer 与 Metal 集成

**问题**：CVPixelBuffer 需要高效转换为 Metal 纹理

**解决方案**：
```cpp
// 使用 CVMetalTextureCache 零拷贝转换
IOSMetalContextManager manager;
auto texture = manager.createTextureFromPixelBuffer(
    pixelBuffer, 
    renderContext
);

// 内部实现
CVMetalTextureCacheCreateTextureFromImage(
    cache, pixelBuffer, ..., &cvMetalTexture
);
id<MTLTexture> metalTexture = CVMetalTextureGetTexture(cvMetalTexture);
```

**关键技术点**：
- CVMetalTextureCache 纹理缓存
- 零拷贝纹理转换
- 纹理生命周期管理

### 3. 扩展输出架构 ✅

#### 多输出目标设计

**需求**：同时支持预览显示 + 视频录制 + 应用回调

**解决方案**：
```cpp
// 添加多个输出目标
int displayId = pipeline->setupDisplayOutput(surface, 1920, 1080);
int encoderId = pipeline->setupEncoderOutput(encoderSurface);
int callbackId = pipeline->setupCallbackOutput(callback);

// 独立控制每个输出
pipeline->setOutputTargetEnabled(encoderId, false);  // 停止录制
```

**支持的输出类型**：
| 类型 | 用途 | 平台 |
|------|------|------|
| Display | 屏幕显示 | All |
| Encoder | 硬件编码 | All |
| Callback | 应用回调 | All |
| Texture | GPU 纹理 | All |
| File | 文件保存 | All |
| CVPixelBuffer | VideoToolbox | iOS |
| SurfaceTexture | MediaCodec | Android |

### 4. 易用接口设计 ✅

#### Facade 模式简化使用

**设计理念**：提供两层 API
1. **高级 API**（PipelineFacade）：快速开发，开箱即用
2. **底层 API**（PipelineManager）：灵活定制，高级控制

**示例**：
```cpp
// 方式 1：使用 Facade（推荐）
PipelineFacadeConfig config;
config.preset = PipelinePreset::CameraPreview;
auto pipeline = PipelineFacade::create(config);
pipeline->setupDisplayOutput(surface, 1920, 1080);
pipeline->addBeautyFilter(0.7f, 0.3f);
pipeline->start();

// 方式 2：使用 Manager（高级）
auto manager = PipelineManager::create(renderContext);
auto inputId = manager->createEntity<InputEntity>();
auto beautyId = manager->createEntity<BeautyEntity>();
manager->connect(inputId, beautyId);
manager->start();
```

---

## 关键接口设计

### 1. 平台上下文接口

```cpp
class PlatformContext {
public:
    // 统一初始化
    bool initialize(const PlatformContextConfig& config);
    
    // 跨平台通用
    bool makeCurrent();
    bool releaseCurrent();
    
    // Android 特定
#ifdef __ANDROID__
    std::shared_ptr<LRTexture> createTextureFromOES(...);
    AndroidEGLContextManager* getAndroidEGLManager();
#endif
    
    // iOS 特定
#ifdef __APPLE__
    std::shared_ptr<LRTexture> createTextureFromPixelBuffer(...);
    IOSMetalContextManager* getIOSMetalManager();
#endif
};
```

### 2. 扩展输出实体接口

```cpp
class OutputEntityExt : public ProcessEntity {
public:
    // 输出目标管理
    int32_t addOutputTarget(const OutputConfig& config);
    bool removeOutputTarget(int32_t targetId);
    void setOutputTargetEnabled(int32_t targetId, bool enabled);
    
    // 快捷配置
    int32_t setupDisplayOutput(void* surface, int w, int h);
    int32_t setupEncoderOutput(void* surface);
    int32_t setupCallbackOutput(FrameCallback callback);
    
    // 平台特定
#ifdef __ANDROID__
    int32_t setupSurfaceTextureOutput(void* surfaceTexture);
#endif
#ifdef __APPLE__
    int32_t setupPixelBufferOutput(void* pixelBufferPool);
#endif
};
```

### 3. 外观接口

```cpp
class PipelineFacade {
public:
    // 创建和生命周期
    static std::shared_ptr<PipelineFacade> create(const Config& config);
    bool initialize();
    bool start();
    void stop();
    
    // 输入接口（跨平台）
    bool feedFrame(const uint8_t* data, uint32_t w, uint32_t h, InputFormat fmt);
    bool feedRGBA(...);
    bool feedYUV420(...);
    bool feedNV12(...);
    bool feedTexture(...);
    
    // 输入接口（平台特定）
#ifdef __ANDROID__
    bool feedOES(uint32_t oesTextureId, ...);
#endif
#ifdef __APPLE__
    bool feedPixelBuffer(void* pixelBuffer);
#endif
    
    // 输出配置
    int32_t setupDisplayOutput(...);
    int32_t setupEncoderOutput(...);
    int32_t setupCallbackOutput(...);
    
    // 效果添加
    EntityId addBeautyFilter(float smooth, float whiten);
    EntityId addColorFilter(const std::string& name, float intensity);
    EntityId addSharpenFilter(float amount);
    EntityId addBlurFilter(float radius);
};
```

---

## 架构优势

### 1. 模块独立性
- ✅ Pipeline 可独立编译和测试
- ✅ LREngine 和 TaskQueue 作为 submodule 管理
- ✅ 清晰的依赖关系

### 2. 跨平台一致性
- ✅ 统一的 API 接口
- ✅ 平台差异被完全封装
- ✅ 条件编译隔离平台代码

### 3. 高性能
- ✅ GPU 加速处理
- ✅ 多线程并行（TaskQueue）
- ✅ 零拷贝纹理传递
- ✅ 平台原生格式支持

### 4. 易用性
- ✅ Facade 模式简化使用
- ✅ 预设配置快速启动
- ✅ 丰富的文档和示例

### 5. 可扩展性
- ✅ 插件化 Entity 架构
- ✅ 自定义输出目标
- ✅ 灵活的效果组合

---

## 技术亮点

### 1. EGL 上下文共享（Android）
**创新点**：解决了相机 OES 纹理在不同线程访问的问题

**技术方案**：
```cpp
// 从相机上下文创建共享上下文
EGLContext pipelineContext = eglCreateContext(
    display, config, 
    cameraContext,  // 共享源
    contextAttribs
);
```

### 2. CVMetalTextureCache（iOS）
**创新点**：零拷贝 CVPixelBuffer 转 Metal 纹理

**技术方案**：
```cpp
CVMetalTextureCacheCreateTextureFromImage(
    cache, pixelBuffer, ..., &cvMetalTexture
);
```

### 3. 多输出目标架构
**创新点**：一个管线同时输出到多个目标，独立控制

**技术方案**：
```cpp
// 内部循环处理所有启用的输出
for (auto& [id, config] : outputs) {
    if (!config.enabled) continue;
    processOutput(config, frame);
}
```

### 4. Facade 模式简化 API
**创新点**：两层 API 设计，兼顾易用性和灵活性

**价值**：
- 新手：使用 Facade，5 行代码启动
- 高级用户：使用 Manager，完全定制

---

## 使用场景

### 场景 1：Android 相机预览
```cpp
auto pipeline = PipelineFacade::create(
    getRecommendedConfig(PipelinePreset::CameraPreview, PlatformType::Android)
);
pipeline->setupDisplayOutput(surface, 1920, 1080);
pipeline->addBeautyFilter(0.7f, 0.3f);
pipeline->start();
pipeline->feedOES(oesTextureId, width, height, matrix);
```

### 场景 2：iOS 相机录制
```cpp
auto pipeline = PipelineFacade::create(
    getRecommendedConfig(PipelinePreset::CameraRecord, PlatformType::iOS)
);
pipeline->setupDisplayOutput(metalLayer, 1920, 1080);
pipeline->setupPixelBufferOutput(pixelBufferPool);
pipeline->start();
pipeline->feedPixelBuffer(pixelBuffer);
```

### 场景 3：图像批处理
```cpp
auto pipeline = PipelineFacade::create(
    getRecommendedConfig(PipelinePreset::ImageProcess, getCurrentPlatform())
);
pipeline->setupFileOutput("/output/");
pipeline->addSharpenFilter(0.8f);
pipeline->start();
for (auto& file : files) {
    pipeline->feedRGBA(data, width, height);
}
```

---

## 文件清单

### 新增头文件
```
include/pipeline/
├── PipelineFacade.h              # 外观接口
├── platform/
│   └── PlatformContext.h         # 平台上下文
└── entity/
    └── OutputEntityExt.h         # 扩展输出实体
```

### 待实现源文件
```
src/
├── PipelineFacade.cpp
├── platform/
│   ├── PlatformContext.cpp
│   ├── AndroidEGLContextManager.cpp
│   ├── PlatformContext_Android.cpp
│   ├── IOSMetalContextManager.mm
│   └── PlatformContext_iOS.mm
└── entity/
    └── OutputEntityExt.cpp
```

### 文档
```
Pipeline/Pipeline/
├── ARCHITECTURE_DESIGN.md        # 架构设计文档（完整版）
├── API_QUICK_REFERENCE.md        # API 快速参考
├── IMPLEMENTATION_PLAN.md        # 实现计划
└── DESIGN_SUMMARY.md             # 本文档
```

---

## 下一步

### 立即行动
1. **创建源文件骨架**（见 IMPLEMENTATION_PLAN.md）
2. **实现 AndroidEGLContextManager**
3. **编写第一个单元测试**

### 完整计划
详见 **IMPLEMENTATION_PLAN.md**，包括：
- 6 个 Sprint 的详细计划
- 每个功能的实现清单
- 测试和验证方案

---

## 总结

本架构设计完成了以下目标：

✅ **模块独立性**：Pipeline 作为完整模块，清晰边界  
✅ **平台兼容性**：Android EGL + iOS Metal 完整方案  
✅ **输出扩展性**：多输出目标，灵活配置  
✅ **接口易用性**：Facade 模式，简洁 API  
✅ **文档完善性**：设计文档 + API 文档 + 实现计划  

**下一阶段**：按照 IMPLEMENTATION_PLAN.md 进行实现开发。

---

**文档版本**: v1.0  
**最后更新**: 2026-01-22  
**作者**: AI Assistant & Pipeline Team
