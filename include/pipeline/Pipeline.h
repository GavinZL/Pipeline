/**
 * @file Pipeline.h
 * @brief Pipeline主头文件 - 包含所有公共接口
 */

#pragma once

// 数据层
#include "data/EntityTypes.h"
#include "data/FramePacket.h"
#include "data/FramePort.h"

// Entity层
#include "entity/ProcessEntity.h"
#include "entity/GPUEntity.h"
#include "entity/CPUEntity.h"
#include "entity/CompositeEntity.h"

// 核心层
#include "core/PipelineConfig.h"
#include "core/PipelineGraph.h"
#include "core/PipelineExecutor.h"
#include "core/PipelineManager.h"

// 资源池
#include "pool/TexturePool.h"
#include "pool/FramePacketPool.h"

// 平台抽象层
#include "platform/PlatformContext.h"

// 外观接口（推荐使用）
#include "PipelineFacade.h"

/**
 * @namespace pipeline
 * @brief 图像处理管线命名空间
 * 
 * Pipeline系统提供了一个灵活的图像处理框架，支持：
 * - GPU/CPU混合处理
 * - 并行分支和合成
 * - 动态Entity添加/移除
 * - 与AndroidCamera/TaskQueue/LREngine集成
 * - 多平台兼容（Android GLES / iOS Metal）
 * - 多输出目标（Display / Encoder / Callback）
 * 
 * 推荐使用 PipelineFacade 进行快速开发：
 * @code
 * // 使用外观接口（推荐）
 * PipelineFacadeConfig config;
 * config.preset = PipelinePreset::CameraPreview;
 * config.platformConfig.platform = PlatformType::Android;
 * 
 * auto pipeline = PipelineFacade::create(config);
 * pipeline->setupDisplayOutput(surface, 1920, 1080);
 * pipeline->addBeautyFilter(0.7f, 0.3f);
 * pipeline->start();
 * 
 * // 输入帧数据
 * pipeline->feedFrame(data, width, height, InputFormat::NV12);
 * @endcode
 * 
 * 高级用户可直接使用 PipelineManager：
 * @code
 * // 使用管理器接口（高级）
 * auto pipeline = PipelineManager::create(renderContext);
 * 
 * // 配置输入输出
 * auto inputId = pipeline->setupPixelBufferInput(1920, 1080);
 * auto outputId = pipeline->setupDisplayOutput(surface, 1920, 1080);
 * 
 * // 添加处理Entity
 * auto beautyId = pipeline->createEntity<BeautyEntity>("beauty");
 * auto filterId = pipeline->createEntity<FilterEntity>("filter");
 * 
 * // 建立连接
 * pipeline->connect(inputId, beautyId);
 * pipeline->connect(beautyId, filterId);
 * pipeline->connect(filterId, outputId);
 * 
 * // 启动管线
 * pipeline->start();
 * @endcode
 */
namespace pipeline {

/**
 * @brief 获取Pipeline版本号
 */
inline const char* getVersion() {
    return "1.0.0";
}

/**
 * @brief 获取Pipeline构建信息
 */
inline const char* getBuildInfo() {
#ifdef NDEBUG
    return "Release";
#else
    return "Debug";
#endif
}

} // namespace pipeline
