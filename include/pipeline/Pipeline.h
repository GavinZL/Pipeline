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
#include "entity/IOEntity.h"

// 核心层
#include "core/PipelineConfig.h"
#include "core/PipelineGraph.h"
#include "core/PipelineExecutor.h"
#include "core/PipelineManager.h"

// 资源池
#include "pool/TexturePool.h"
#include "pool/FramePacketPool.h"

/**
 * @namespace pipeline
 * @brief 图像处理管线命名空间
 * 
 * Pipeline系统提供了一个灵活的图像处理框架，支持：
 * - GPU/CPU混合处理
 * - 并行分支和合成
 * - 动态Entity添加/移除
 * - 与AndroidCamera/TaskQueue/LREngine集成
 * 
 * 基本使用流程：
 * @code
 * // 1. 创建管线管理器
 * auto pipeline = PipelineManager::create(renderContext);
 * 
 * // 2. 添加Entity
 * auto inputId = pipeline->createEntity<InputEntity>("input");
 * auto beautyId = pipeline->createEntity<BeautyEntity>("beauty");
 * auto filterId = pipeline->createEntity<FilterEntity>("filter");
 * auto outputId = pipeline->createEntity<OutputEntity>("output");
 * 
 * // 3. 建立连接
 * pipeline->connect(inputId, beautyId);
 * pipeline->connect(beautyId, filterId);
 * pipeline->connect(filterId, outputId);
 * 
 * // 4. 配置Entity
 * auto beauty = std::dynamic_pointer_cast<BeautyEntity>(pipeline->getEntity(beautyId));
 * beauty->setSmoothLevel(0.7f);
 * beauty->setWhitenLevel(0.3f);
 * 
 * // 5. 启动管线
 * pipeline->start();
 * 
 * // 6. 处理帧数据
 * pipeline->feedRGBA(data, width, height, stride, timestamp);
 * 
 * // 7. 停止和销毁
 * pipeline->stop();
 * pipeline->destroy();
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
