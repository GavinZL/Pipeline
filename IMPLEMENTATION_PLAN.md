# Pipeline 架构设计实现计划

## 概述

本文档记录了 Pipeline 多平台兼容性架构的实现计划和当前状态。

---

## ✅ 已完成的设计工作

### 1. 架构设计文档
- [x] 完整的架构设计文档（ARCHITECTURE_DESIGN.md）
- [x] API 快速参考文档（API_QUICK_REFERENCE.md）
- [x] 模块划分和依赖关系设计
- [x] 多平台兼容性方案设计

### 2. 接口定义（头文件）

#### 平台抽象层
- [x] `PlatformContext.h` - 平台上下文统一接口
  - [x] `AndroidEGLContextManager` - Android EGL 上下文管理
  - [x] `IOSMetalContextManager` - iOS Metal 上下文管理
  - [x] 跨平台统一接口设计

#### 扩展实体
- [x] `OutputEntityExt.h` - 扩展输出实体
  - [x] 多输出目标支持（Display/Encoder/Callback/Texture/File）
  - [x] 平台特定输出（CVPixelBuffer/SurfaceTexture）
  - [x] 硬件编码器集成接口

#### 外观接口
- [x] `PipelineFacade.h` - 外观模式统一接口
  - [x] 简化 API 设计
  - [x] 预设管线配置
  - [x] 平台特定输入接口
  - [x] 效果添加接口

### 3. 构建系统
- [x] 更新 `CMakeLists.txt`
  - [x] 集成 LREngine submodule
  - [x] 集成 TaskQueue submodule
  - [x] 平台特定编译配置
  - [x] Objective-C++ 文件处理（iOS）

### 4. 文档完善
- [x] 更新主头文件 `Pipeline.h`
- [x] 添加使用示例和注释
- [x] 创建实现计划文档（本文件）

---

## 📋 待实现的功能

### Phase 1: 平台抽象层实现（核心）

#### 1.1 Android 平台实现
- [x] `src/platform/AndroidEGLContextManager.cpp`
  - [x] EGL 上下文创建和初始化
  - [x] 共享上下文创建（与相机线程共享）
  - [x] 上下文切换（makeCurrent/releaseCurrent）
  - [x] 离屏渲染支持（PBuffer Surface）
  - [x] 错误处理和日志

- [ ] `src/platform/PlatformContext_Android.cpp`
  - [ ] Android 特定初始化
  - [ ] OES 纹理转换为标准 2D 纹理
  - [ ] createTextureFromOES 实现
  - [ ] SurfaceTexture 互操作

#### 1.2 iOS 平台实现
- [x] `src/platform/IOSMetalContextManager.mm`
  - [x] CVMetalTextureCache 创建和管理
  - [x] CVPixelBuffer → Metal 纹理转换
  - [ ] Metal 纹理 → CVPixelBuffer 转换
  - [x] 纹理缓存管理和回收

- [ ] `src/platform/PlatformContext_iOS.mm`
  - [ ] iOS 特定初始化
  - [ ] createTextureFromPixelBuffer 实现
  - [ ] copyTextureToPixelBuffer 实现

#### 1.3 通用实现
- [x] `src/platform/PlatformContext.cpp`
  - [x] 平台检测和自动配置
  - [x] 条件编译处理
  - [x] 通用接口实现

### Phase 2: 扩展输出实体实现

#### 2.1 核心输出功能
- [ ] `src/entity/OutputEntityExt.cpp`
  - [ ] 输出目标管理（添加/移除/更新）
  - [ ] 多输出目标并行处理
  - [ ] process() 主处理函数实现

#### 2.2 各类型输出实现
- [ ] **显示输出**（renderToDisplay）
  - [ ] 视口计算和缩放模式
  - [ ] 渲染着色器（显示用）
  - [ ] 双缓冲和 VSync 控制

- [ ] **编码器输出**（outputToEncoder）
  - [ ] Android MediaCodec Surface 渲染
  - [ ] iOS CVPixelBuffer 输出
  - [ ] 格式转换（RGBA → NV12）

- [ ] **回调输出**（executeCallback）
  - [ ] 异步回调机制
  - [ ] CPU 数据读取（GPU → CPU）
  - [ ] 格式转换支持

- [ ] **纹理输出**（outputTexture）
  - [ ] 纹理共享优化
  - [ ] 零拷贝实现

- [ ] **文件输出**（saveToFile）
  - [ ] PNG/JPEG 编码
  - [ ] 文件名时间戳
  - [ ] 批量保存管理

- [ ] **平台特定输出**（outputToPlatform）
  - [ ] Android SurfaceTexture 输出
  - [ ] iOS CVPixelBuffer 输出

#### 2.3 格式转换
- [ ] `convertFormat()` 实现
  - [ ] RGBA ↔ BGRA
  - [ ] RGBA → NV12
  - [ ] RGBA → YUV420P

### Phase 3: 外观接口实现

#### 3.1 核心功能
- [ ] `src/PipelineFacade.cpp`
  - [ ] 生命周期管理（initialize/start/stop）
  - [ ] 预设管线创建
  - [ ] 平台自动检测

#### 3.2 输入输出接口
- [ ] 输入接口实现
  - [ ] feedFrame（统一入口）
  - [ ] feedRGBA/feedYUV420/feedNV12
  - [ ] feedTexture
  - [ ] feedOES（Android）
  - [ ] feedPixelBuffer（iOS）

- [ ] 输出配置接口
  - [ ] setupDisplayOutput
  - [ ] setupEncoderOutput
  - [ ] setupCallbackOutput
  - [ ] setupFileOutput

#### 3.3 效果添加
- [ ] addBeautyFilter 实现
- [ ] addColorFilter 实现
- [ ] addSharpenFilter 实现
- [ ] addBlurFilter 实现
- [ ] addCustomEntity 实现

#### 3.4 高级功能
- [ ] 配置保存/加载（JSON）
- [ ] 管线图导出（DOT）
- [ ] 统计信息收集

### Phase 4: LREngine 扩展（平台支持）

#### 4.1 Android GLES 扩展
- [ ] 扩展 `TextureGLES.h/.cpp`
  - [ ] 从 OES 纹理创建
  - [ ] OES 转换着色器

#### 4.2 iOS Metal 扩展
- [ ] 扩展 `TextureMTL.h/.mm`
  - [ ] CreateFromPixelBuffer 静态方法
  - [ ] syncToPixelBuffer 方法
  - [ ] CVMetalTextureCache 集成

### Phase 5: 测试和验证

#### 5.1 单元测试
- [x] PlatformContext 测试
  - [x] Android EGL 上下文创建
  - [x] iOS Metal 纹理缓存
  - [x] 跨平台接口一致性

- [ ] OutputEntityExt 测试
  - [ ] 多输出目标
  - [ ] 格式转换
  - [ ] 性能基准测试

- [ ] PipelineFacade 测试
  - [ ] 预设管线
  - [ ] 完整流程测试

#### 5.2 集成测试
- [ ] Android 平台测试
  - [ ] 与 AndroidCameraFramework 集成
  - [ ] OES 纹理输入测试
  - [ ] MediaCodec 编码测试

- [ ] iOS 平台测试
  - [ ] 与 IOSCameraFramework 集成
  - [ ] CVPixelBuffer 输入测试
  - [ ] VideoToolbox 编码测试

#### 5.3 性能测试
- [ ] 1080p@30fps 稳定性测试
- [ ] 4K@60fps 压力测试
- [ ] 内存泄漏检测
- [ ] GPU 使用率分析

### Phase 6: 示例和文档

#### 6.1 示例代码
- [ ] `examples/android_camera_preview/` - Android 相机预览
- [ ] `examples/android_camera_record/` - Android 相机录制
- [ ] `examples/ios_camera_preview/` - iOS 相机预览
- [ ] `examples/ios_camera_record/` - iOS 相机录制
- [ ] `examples/image_batch_process/` - 图像批处理

#### 6.2 平台集成指南
- [ ] `PLATFORM_GUIDE_ANDROID.md` - Android 集成指南
- [ ] `PLATFORM_GUIDE_IOS.md` - iOS 集成指南
- [ ] `INTEGRATION_GUIDE.md` - 通用集成指南

#### 6.3 API 文档
- [ ] Doxygen 配置
- [ ] API 参考文档生成
- [ ] 代码注释完善

---

## 🚀 实施路线图

### Sprint 1（2 周）：平台抽象层实现
**目标**：完成 Android 和 iOS 平台上下文管理

**任务**：
1. 实现 AndroidEGLContextManager
2. 实现 IOSMetalContextManager
3. 实现 PlatformContext 通用接口
4. 编写单元测试

**交付物**：
- 可编译的平台抽象层
- 通过的单元测试
- 集成测试 Demo

### Sprint 2（2 周）：扩展输出实体实现
**目标**：完成多输出目标功能

**任务**：
1. 实现 OutputEntityExt 核心框架
2. 实现各类型输出（Display/Encoder/Callback）
3. 实现格式转换
4. 编写单元测试

**交付物**：
- 可工作的多输出系统
- 通过的测试用例
- 性能基准数据

### Sprint 3（2 周）：外观接口实现
**目标**：完成易用的对外 API

**任务**：
1. 实现 PipelineFacade 核心功能
2. 实现预设管线
3. 实现输入/输出快捷接口
4. 实现效果添加接口

**交付物**：
- 完整的外观接口
- 示例代码
- API 使用文档

### Sprint 4（1 周）：LREngine 扩展
**目标**：扩展 LREngine 平台支持

**任务**：
1. 扩展 TextureGLES（Android）
2. 扩展 TextureMTL（iOS）
3. 测试平台特定功能

**交付物**：
- 扩展的 LREngine
- 集成测试通过

### Sprint 5（2 周）：集成测试和优化
**目标**：端到端测试和性能优化

**任务**：
1. 与 AndroidCameraFramework 集成测试
2. 与 IOSCameraFramework 集成测试
3. 性能优化和调优
4. 内存泄漏检测

**交付物**：
- 稳定的 Pipeline 系统
- 性能测试报告
- Bug 修复列表

### Sprint 6（1 周）：文档和示例
**目标**：完善文档和示例代码

**任务**：
1. 编写平台集成指南
2. 创建示例代码
3. 生成 API 文档
4. 代码注释完善

**交付物**：
- 完整的文档体系
- 可运行的示例代码
- 发布就绪的 Pipeline v1.0

---

## 📦 依赖关系

### 内部依赖
```
PipelineFacade
    ↓
PipelineManager + PlatformContext + OutputEntityExt
    ↓
LREngine + TaskQueue (submodules)
```

### 外部依赖
- **LREngine**: 图形渲染引擎（submodule）
- **TaskQueue**: 任务调度队列（submodule）
- **AndroidCameraFramework**: Android 相机集成（外部）
- **IOSCameraFramework**: iOS 相机集成（外部）

---

## 📊 进度跟踪

| 阶段 | 状态 | 进度 | 预计完成时间 |
|------|------|------|------------|
| Phase 1: 平台抽象层 | 🔵 进行中 | 70% | Sprint 1 |
| Phase 2: 扩展输出实体 | 🟡 待开始 | 0% | Sprint 2 |
| Phase 3: 外观接口 | 🟡 待开始 | 0% | Sprint 3 |
| Phase 4: LREngine 扩展 | 🟡 待开始 | 0% | Sprint 4 |
| Phase 5: 测试验证 | 🔵 进行中 | 20% | Sprint 5 |
| Phase 6: 文档示例 | 🟡 待开始 | 0% | Sprint 6 |

**图例**：
- 🟢 已完成
- 🟡 待开始
- 🔵 进行中
- 🔴 受阻

---

## 🎯 下一步行动

### 立即行动（本周）
1. **创建平台实现文件骨架**
   ```bash
   mkdir -p src/platform
   touch src/platform/AndroidEGLContextManager.cpp
   touch src/platform/PlatformContext_Android.cpp
   touch src/platform/IOSMetalContextManager.mm
   touch src/platform/PlatformContext_iOS.mm
   touch src/platform/PlatformContext.cpp
   ```

2. **创建扩展实体文件**
   ```bash
   mkdir -p src/entity
   touch src/entity/OutputEntityExt.cpp
   ```

3. **创建外观接口文件**
   ```bash
   touch src/PipelineFacade.cpp
   ```

4. **编写第一个单元测试**
   ```bash
   mkdir -p tests/platform
   touch tests/platform/test_platform_context.cpp
   ```

### 短期目标（2 周内）
- 完成 AndroidEGLContextManager 实现
- 编写并通过基础单元测试
- 创建简单的集成测试 Demo

### 中期目标（1 个月内）
- 完成所有平台抽象层实现
- 完成扩展输出实体核心功能
- 通过 Android 平台集成测试

### 长期目标（2 个月内）
- 完成所有功能实现
- 通过完整测试套件
- 发布 Pipeline v1.0

---

## 📝 注意事项

### 技术风险
1. **EGL 上下文共享**：不同设备的 EGL 实现差异可能导致兼容性问题
   - 缓解：充分测试不同 Android 设备
   
2. **CVMetalTextureCache 性能**：纹理缓存管理不当可能影响性能
   - 缓解：实现合理的缓存策略和监控

3. **格式转换开销**：CPU/GPU 之间的数据传输可能成为瓶颈
   - 缓解：尽量使用零拷贝技术

### 开发规范
1. 所有平台特定代码使用条件编译隔离
2. 接口设计遵循 RAII 原则
3. 错误处理必须完善（日志 + 回调）
4. 性能关键路径需要基准测试

---

**文档版本**: v1.0  
**最后更新**: 2026-01-22  
**维护者**: Pipeline Team
