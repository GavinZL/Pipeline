# Pipeline API 快速参考

## 快速开始

### 1. 包含头文件

```cpp
#include <pipeline/Pipeline.h>
// 或者只包含外观接口
#include <pipeline/PipelineFacade.h>
```

### 2. 创建 Pipeline (推荐方式)

```cpp
// 使用预设创建
PipelineFacadeConfig config;
config.preset = PipelinePreset::CameraPreview;
config.platformConfig.platform = PlatformType::Android;  // 或 iOS

auto pipeline = PipelineFacade::create(config);
pipeline->initialize();
```

### 3. 配置输出

```cpp
// 显示输出
int displayId = pipeline->setupDisplayOutput(surface, 1920, 1080);

// 编码器输出
int encoderId = pipeline->setupEncoderOutput(encoderSurface);

// 回调输出
int callbackId = pipeline->setupCallbackOutput([](FramePacketPtr frame) {
    // 处理帧数据
});
```

### 4. 添加效果

```cpp
pipeline->addBeautyFilter(0.7f, 0.3f);    // 美颜
pipeline->addColorFilter("vintage");       // 颜色滤镜
pipeline->addSharpenFilter(0.5f);          // 锐化
```

### 5. 启动和输入

```cpp
pipeline->start();

// 输入帧数据
pipeline->feedFrame(data, width, height, InputFormat::NV12);

// Android OES 纹理
#ifdef __ANDROID__
pipeline->feedOES(oesTextureId, width, height, transformMatrix);
#endif

// iOS CVPixelBuffer
#ifdef __APPLE__
pipeline->feedPixelBuffer(pixelBuffer);
#endif
```

---

## 平台特定配置

### Android 平台

```cpp
PipelineFacadeConfig config;
config.preset = PipelinePreset::CameraPreview;
config.platformConfig.platform = PlatformType::Android;
config.platformConfig.graphicsAPI = GraphicsAPI::OpenGLES;

// EGL 上下文共享（与相机线程共享）
config.platformConfig.androidConfig.sharedContext = cameraEGLContext;
config.platformConfig.androidConfig.display = eglGetCurrentDisplay();
config.platformConfig.androidConfig.glesVersion = 3;

auto pipeline = PipelineFacade::create(config);
```

### iOS 平台

```cpp
PipelineFacadeConfig config;
config.preset = PipelinePreset::CameraPreview;
config.platformConfig.platform = PlatformType::iOS;
config.platformConfig.graphicsAPI = GraphicsAPI::Metal;

// Metal 设备
config.platformConfig.iosConfig.metalDevice = MTLCreateSystemDefaultDevice();
config.platformConfig.iosConfig.enableTextureCache = true;

auto pipeline = PipelineFacade::create(config);
```

---

## 输入接口

### 通用输入

```cpp
// 自动识别格式
bool feedFrame(const uint8_t* data, uint32_t width, uint32_t height,
               InputFormat format, uint64_t timestamp = 0);

// RGBA 输入
bool feedRGBA(const uint8_t* data, uint32_t width, uint32_t height,
              uint32_t stride = 0, uint64_t timestamp = 0);

// YUV420 输入
bool feedYUV420(const uint8_t* yData, const uint8_t* uData, 
                const uint8_t* vData, uint32_t width, uint32_t height,
                uint64_t timestamp = 0);

// NV12/NV21 输入
bool feedNV12(const uint8_t* yData, const uint8_t* uvData,
              uint32_t width, uint32_t height, bool isNV21 = false,
              uint64_t timestamp = 0);

// GPU 纹理输入
bool feedTexture(std::shared_ptr<LRTexture> texture, 
                 uint32_t width, uint32_t height,
                 uint64_t timestamp = 0);
```

### Android 特定输入

```cpp
#ifdef __ANDROID__
// OES 纹理输入（Android 相机）
bool feedOES(uint32_t oesTextureId, uint32_t width, uint32_t height,
             const float* transformMatrix = nullptr,
             uint64_t timestamp = 0);
#endif
```

### iOS 特定输入

```cpp
#ifdef __APPLE__
// CVPixelBuffer 输入（iOS 相机）
bool feedPixelBuffer(void* pixelBuffer, uint64_t timestamp = 0);
#endif
```

---

## 输出配置

### 显示输出

```cpp
// 基础配置
int32_t setupDisplayOutput(void* surface, int32_t width, int32_t height);

// 高级配置
DisplayOutputConfig displayConfig;
displayConfig.surface = surface;
displayConfig.width = 1920;
displayConfig.height = 1080;
displayConfig.scaleMode = OutputEntity::ScaleMode::Fit;
displayConfig.vsync = true;

OutputConfig config;
config.targetType = OutputTargetType::Display;
config.displayConfig = displayConfig;

int32_t targetId = outputEntity->addOutputTarget(config);
```

### 编码器输出

```cpp
// 基础配置（硬件编码器）
int32_t setupEncoderOutput(void* encoderSurface, 
                           EncoderType type = EncoderType::Hardware);

// Android MediaCodec
#ifdef __ANDROID__
config.encoderConfig.encoderType = EncoderType::MediaCodec;
config.encoderConfig.encoderSurface = mediaCodecSurface;
#endif

// iOS VideoToolbox
#ifdef __APPLE__
int32_t setupPixelBufferOutput(void* pixelBufferPool = nullptr);
#endif
```

### 回调输出

```cpp
// Lambda 回调
int32_t setupCallbackOutput(
    [](FramePacketPtr frame) {
        // 获取纹理
        auto texture = frame->getTexture();
        
        // 获取 CPU 数据
        const uint8_t* data = frame->getCpuBuffer();
        
        // 处理...
    },
    OutputDataFormat::RGBA8
);

// 函数回调
void onFrameCallback(FramePacketPtr frame) {
    // 处理帧
}

int32_t callbackId = pipeline->setupCallbackOutput(
    onFrameCallback, 
    OutputDataFormat::NV12
);
```

### 文件输出

```cpp
// 保存为图片
int32_t setupFileOutput(const std::string& filePath);

// 高级配置
FileOutputConfig fileConfig;
fileConfig.filePath = "/path/to/output.png";
fileConfig.fileFormat = "png";  // png, jpg, raw
fileConfig.quality = 95;
fileConfig.appendTimestamp = true;
```

### 纹理输出

```cpp
// 用于后续处理链
int32_t setupTextureOutput(bool shareTexture = true);

// 获取输出纹理
auto texture = pipeline->getOutputTexture();
```

---

## 滤镜和效果

### 美颜滤镜

```cpp
// 参数：磨皮级别 [0.0, 1.0], 美白级别 [0.0, 1.0]
EntityId beautyId = pipeline->addBeautyFilter(0.7f, 0.3f);
```

### 颜色滤镜

```cpp
// 预设滤镜：vintage, warm, cool, cinema, black_white 等
EntityId filterId = pipeline->addColorFilter("vintage", 0.8f);
```

### 锐化滤镜

```cpp
// 参数：锐化强度 [0.0, 2.0]
EntityId sharpenId = pipeline->addSharpenFilter(0.5f);
```

### 模糊滤镜

```cpp
// 参数：模糊半径（像素）
EntityId blurId = pipeline->addBlurFilter(5.0f);
```

### 自定义 Entity

```cpp
class MyCustomEntity : public GPUEntity {
public:
    bool process(const std::vector<FramePacketPtr>& inputs,
                std::vector<FramePacketPtr>& outputs,
                PipelineContext& context) override {
        // 自定义处理
        return true;
    }
};

auto customEntity = std::make_shared<MyCustomEntity>();
EntityId entityId = pipeline->addCustomEntity(customEntity);
```

---

## 渲染配置

```cpp
// 设置输出分辨率
pipeline->setOutputResolution(1920, 1080);

// 旋转（0, 90, 180, 270）
pipeline->setRotation(90);

// 镜像
pipeline->setMirror(true, false);  // 水平镜像

// 裁剪区域（归一化坐标 [0.0, 1.0]）
pipeline->setCropRect(0.1f, 0.1f, 0.8f, 0.8f);

// 帧率限制
pipeline->setFrameRateLimit(30);
```

---

## 生命周期管理

```cpp
// 初始化
bool initialize();

// 启动
bool start();

// 暂停
void pause();

// 恢复
void resume();

// 停止
void stop();

// 销毁
void destroy();

// 状态查询
PipelineState getState() const;
bool isRunning() const;
```

---

## 回调设置

### 帧处理完成回调

```cpp
pipeline->setFrameProcessedCallback([](FramePacketPtr frame) {
    LOG_INFO("Frame %llu processed", frame->getFrameId());
});
```

### 错误回调

```cpp
pipeline->setErrorCallback([](const std::string& error) {
    LOG_ERROR("Pipeline error: %s", error.c_str());
});
```

### 状态变更回调

```cpp
pipeline->setStateCallback([](PipelineState state) {
    switch (state) {
        case PipelineState::Running:
            LOG_INFO("Pipeline started");
            break;
        case PipelineState::Stopped:
            LOG_INFO("Pipeline stopped");
            break;
        case PipelineState::Error:
            LOG_ERROR("Pipeline error state");
            break;
    }
});
```

### 统一回调配置

```cpp
PipelineCallbacks callbacks;
callbacks.onFrameProcessed = [](FramePacketPtr frame) { /* ... */ };
callbacks.onFrameDropped = [](FramePacketPtr frame) { /* ... */ };
callbacks.onStateChanged = [](PipelineState state) { /* ... */ };
callbacks.onError = [](const std::string& error) { /* ... */ };
callbacks.onStatsUpdate = [](const ExecutionStats& stats) { /* ... */ };

pipeline->setCallbacks(callbacks);
```

---

## 性能统计

### 获取统计信息

```cpp
// 执行统计
ExecutionStats stats = pipeline->getStats();
LOG_INFO("Total frames: %llu", stats.totalFrames);
LOG_INFO("Average FPS: %.2f", stats.averageFPS);
LOG_INFO("Average latency: %.2fms", stats.averageLatency);

// 输出统计
OutputEntityExt::OutputStats outputStats = pipeline->getOutputStats();
LOG_INFO("Dropped frames: %llu", outputStats.droppedFrames);

// 重置统计
pipeline->resetStats();
```

### 性能指标

```cpp
// 当前 FPS
double fps = pipeline->getCurrentFPS();

// 平均处理时间
double avgTime = pipeline->getAverageProcessTime();
```

---

## 输出目标管理

### 启用/禁用输出

```cpp
// 禁用某个输出（如停止录制时禁用编码器）
pipeline->setOutputTargetEnabled(encoderId, false);

// 重新启用
pipeline->setOutputTargetEnabled(encoderId, true);
```

### 移除输出

```cpp
// 移除输出目标
bool removed = pipeline->removeOutputTarget(encoderId);
```

### Entity 管理

```cpp
// 禁用某个效果
pipeline->setEntityEnabled(beautyId, false);

// 移除效果
pipeline->removeEntity(filterId);
```

---

## 高级功能

### 获取底层对象

```cpp
// 获取 PipelineManager（高级操作）
auto manager = pipeline->getPipelineManager();

// 获取平台上下文
PlatformContext* platformContext = pipeline->getPlatformContext();

// 获取渲染上下文
LRRenderContext* renderContext = pipeline->getRenderContext();
```

### 导出管线图

```cpp
// 导出为 DOT 格式（用于 Graphviz 可视化）
std::string dotGraph = pipeline->exportGraph();

// 保存到文件
std::ofstream file("pipeline_graph.dot");
file << dotGraph;
file.close();

// 使用 Graphviz 生成图片：
// dot -Tpng pipeline_graph.dot -o pipeline.png
```

### 配置持久化

```cpp
// 保存配置
pipeline->saveConfig("/path/to/config.json");

// 加载配置
pipeline->loadConfig("/path/to/config.json");
```

---

## 预设类型

```cpp
enum class PipelinePreset : uint8_t {
    CameraPreview,   // 相机预览（实时显示）
    CameraRecord,    // 相机录制（编码输出）
    ImageProcess,    // 图像处理（文件输入输出）
    LiveStream,      // 直播推流
    VideoPlayback,   // 视频播放
    Custom           // 自定义
};
```

### 获取推荐配置

```cpp
// 自动获取推荐配置
PipelineFacadeConfig config = getRecommendedConfig(
    PipelinePreset::CameraPreview,
    PlatformType::Android
);

auto pipeline = PipelineFacade::create(config);
```

---

## 完整示例

### Android 相机预览

```cpp
#include <pipeline/PipelineFacade.h>

// 1. 配置
PipelineFacadeConfig config;
config.preset = PipelinePreset::CameraPreview;
config.platformConfig.platform = PlatformType::Android;
config.platformConfig.androidConfig.sharedContext = cameraEGLContext;

// 2. 创建
auto pipeline = PipelineFacade::create(config);
pipeline->initialize();

// 3. 配置输出
pipeline->setupDisplayOutput(surface, 1920, 1080);

// 4. 添加效果
pipeline->addBeautyFilter(0.7f, 0.3f);
pipeline->addColorFilter("warm", 0.5f);

// 5. 设置回调
pipeline->setErrorCallback([](const std::string& error) {
    LOG_ERROR("Error: %s", error.c_str());
});

// 6. 启动
pipeline->start();

// 7. 相机回调中输入帧
void onCameraFrame(int oesTextureId, int w, int h, const float* matrix) {
    pipeline->feedOES(oesTextureId, w, h, matrix);
}

// 8. 停止
pipeline->stop();
```

### iOS 相机录制

```objc
#import <pipeline/PipelineFacade.h>

// 1. 配置
PipelineFacadeConfig config;
config.preset = PipelinePreset::CameraRecord;
config.platformConfig.platform = PlatformType::iOS;
config.platformConfig.iosConfig.metalDevice = MTLCreateSystemDefaultDevice();

// 2. 创建
auto pipeline = PipelineFacade::create(config);
pipeline->initialize();

// 3. 配置输出
pipeline->setupDisplayOutput((__bridge void*)metalLayer, 1920, 1080);
int encoderId = pipeline->setupPixelBufferOutput(pixelBufferPool);

// 4. 添加滤镜
pipeline->addColorFilter("vintage");

// 5. 启动
pipeline->start();

// 6. 相机回调
- (void)captureOutput:(AVCaptureOutput*)output 
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer 
    fromConnection:(AVCaptureConnection*)connection {
    
    CVPixelBufferRef pixelBuffer = 
        CMSampleBufferGetImageBuffer(sampleBuffer);
    pipeline->feedPixelBuffer(pixelBuffer);
}

// 7. 停止录制
pipeline->setOutputTargetEnabled(encoderId, false);
pipeline->stop();
```

---

## 错误处理

```cpp
// 初始化失败
if (!pipeline->initialize()) {
    LOG_ERROR("Failed to initialize pipeline");
    return;
}

// 启动失败
if (!pipeline->start()) {
    LOG_ERROR("Failed to start pipeline");
    return;
}

// 检查状态
if (pipeline->getState() == PipelineState::Error) {
    LOG_ERROR("Pipeline in error state");
}

// 错误回调
pipeline->setErrorCallback([](const std::string& error) {
    // 处理错误
    LOG_ERROR("Pipeline error: %s", error.c_str());
});
```

---

## 性能优化建议

1. **使用纹理输入**：避免 CPU → GPU 数据传输
2. **启用异步处理**：`config.enableAsync = true`
3. **合理设置队列大小**：`config.maxQueueSize = 3`
4. **禁用不需要的输出**：`setOutputTargetEnabled(id, false)`
5. **使用共享纹理**：`setupTextureOutput(true)`
6. **平台原生格式**：Android OES / iOS CVPixelBuffer

---

## 常见问题

### Q: 如何实现零延迟预览？
A: 使用纹理输入 + 共享纹理输出 + 异步处理：
```cpp
config.enableAsync = true;
pipeline->setupTextureOutput(true);  // 共享纹理
```

### Q: 如何同时录制和预览？
A: 添加多个输出目标：
```cpp
pipeline->setupDisplayOutput(previewSurface, 1920, 1080);
pipeline->setupEncoderOutput(encoderSurface);
```

### Q: 如何处理不同分辨率？
A: 设置输出分辨率：
```cpp
pipeline->setOutputResolution(1920, 1080);
```

### Q: 如何降低 CPU 占用？
A: 启用 GPU 优化 + 减少 CPU 回调：
```cpp
config.enableGPUOptimization = true;
// 避免频繁的 getCpuBuffer() 调用
```

---

**文档版本**: v1.0  
**最后更新**: 2026-01-22
