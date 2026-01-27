/**
 * @file InputEntity.h
 * @brief 输入实体 - Pipeline 输入数据源的统一抽象
 * 
 * InputEntity 实现双路输出架构：
 * - GPU 路径：输出纹理数据，供美颜/特效等 GPU 处理使用
 * - CPU 路径：输出 YUV/像素数据，供 AI 检测等 CPU 算法使用
 */

#pragma once

#include "pipeline/entity/ProcessEntity.h"
#include "pipeline/input/InputFormat.h"
#include <memory>
#include <functional>
#include <queue>
#include <condition_variable>

// 前向声明 LREngine 类型
namespace lrengine {
namespace render {
class LRTexture;
class LRPlanarTexture;
class LRRenderContext;
} // namespace render
using LRTexturePtr = std::shared_ptr<render::LRTexture>;
using LRPlanarTexturePtr = std::shared_ptr<render::LRPlanarTexture>;
using LRRenderContextPtr = render::LRRenderContext*;
} // namespace lrengine

namespace pipeline {
namespace input {

// =============================================================================
// 常量定义
// =============================================================================

/// GPU 输出端口名称
static constexpr const char* GPU_OUTPUT_PORT = "gpu_out";

/// CPU 输出端口名称  
static constexpr const char* CPU_OUTPUT_PORT = "cpu_out";

// =============================================================================
// 输入数据结构
// =============================================================================

/**
 * @brief CPU 端输入数据
 */
struct CPUInputData {
    const uint8_t* data = nullptr;      ///< 数据指针
    size_t dataSize = 0;                ///< 数据大小（字节）
    uint32_t width = 0;                 ///< 宽度
    uint32_t height = 0;                ///< 高度
    uint32_t stride = 0;                ///< 行跨度
    InputFormat format = InputFormat::RGBA;
    int64_t timestamp = 0;              ///< 时间戳（微秒）
    
    // YUV 多平面支持
    const uint8_t* planeY = nullptr;    ///< Y 平面
    const uint8_t* planeU = nullptr;    ///< U 平面（或 UV 交织）
    const uint8_t* planeV = nullptr;    ///< V 平面
    uint32_t strideY = 0;
    uint32_t strideU = 0;
    uint32_t strideV = 0;
};

/**
 * @brief GPU 端输入数据
 */
struct GPUInputData {
    uint32_t textureId = 0;             ///< OpenGL/GLES 纹理 ID
    void* metalTexture = nullptr;       ///< Metal 纹理对象
    uint32_t width = 0;
    uint32_t height = 0;
    InputFormat format = InputFormat::Texture;
    int64_t timestamp = 0;
    bool isOESTexture = false;          ///< 是否为 Android OES 纹理
    
    // 纹理变换矩阵（用于 OES 纹理）
    float transformMatrix[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
};

/**
 * @brief 统一输入数据包装
 */
struct InputData {
    CPUInputData cpu;
    GPUInputData gpu;
    InputDataType dataType = InputDataType::CPUBuffer;
};

// =============================================================================
// 输入策略接口
// =============================================================================

/**
 * @brief 输入处理策略基类
 * 
 * 不同平台实现不同的输入策略：
 * - Android: OESTextureInputStrategy
 * - iOS: PixelBufferInputStrategy
 * - 通用: RawBufferInputStrategy
 */
class InputStrategy {
public:
    virtual ~InputStrategy() = default;
    
    /**
     * @brief 初始化策略
     * @param context 渲染上下文
     * @return 是否成功
     */
    virtual bool initialize(lrengine::render::LRRenderContext* context) = 0;
    
    /**
     * @brief 处理输入数据，生成 GPU 纹理
     * @param input 输入数据
     * @param outputTexture 输出纹理
     * @return 是否成功
     */
    virtual bool processToGPU(const InputData& input,
                              lrengine::LRTexturePtr& outputTexture) = 0;
    
    /**
     * @brief 处理输入数据，生成 CPU 数据
     * @param input 输入数据
     * @param outputBuffer 输出缓冲区
     * @param outputSize 输出大小
     * @return 是否成功
     */
    virtual bool processToCPU(const InputData& input,
                              uint8_t* outputBuffer,
                              size_t& outputSize) = 0;
    
    /**
     * @brief 释放资源
     */
    virtual void release() = 0;
    
    /**
     * @brief 获取策略名称
     */
    virtual const char* getName() const = 0;
};

using InputStrategyPtr = std::shared_ptr<InputStrategy>;

// =============================================================================
// InputEntity
// =============================================================================

/**
 * @brief 输入实体类
 * 
 * 作为 Pipeline 的数据入口，负责：
 * 1. 接收外部输入（相机/视频/图像）
 * 2. 格式转换（使用 libyuv）
 * 3. 双路分发（GPU 路径 + CPU 路径）
 * 
 * 使用示例：
 * @code
 * auto inputEntity = std::make_shared<InputEntity>("camera_input");
 * inputEntity->configure(InputConfig{
 *     .format = InputFormat::NV21,
 *     .dataType = InputDataType::Both,
 *     .width = 1920,
 *     .height = 1080,
 *     .enableDualOutput = true
 * });
 * 
 * // 在每帧回调中
 * inputEntity->submitCPUData(yuvData, dataSize, timestamp);
 * // 或
 * inputEntity->submitGPUData(textureId, timestamp);
 * @endcode
 */
class InputEntity : public ProcessEntity {
public:
    /**
     * @brief 构造函数
     * @param name Entity 名称
     */
    explicit InputEntity(const std::string& name = "InputEntity");
    
    ~InputEntity() override;
    
    // ==========================================================================
    // ProcessEntity 接口实现
    // ==========================================================================
    
    EntityType getType() const override { return EntityType::Input; }
    
    ExecutionQueue getExecutionQueue() const override { return ExecutionQueue::IO; }
    
    // ==========================================================================
    // 配置
    // ==========================================================================
    
    /**
     * @brief 配置输入参数
     * @param config 输入配置
     */
    void configure(const InputConfig& config);
    
    /**
     * @brief 获取当前配置
     */
    const InputConfig& getInputConfig() const { return mConfig; }
    
    /**
     * @brief 设置渲染上下文
     * @param context LREngine 渲染上下文
     */
    void setRenderContext(lrengine::render::LRRenderContext* context);
    
    /**
     * @brief 设置输入策略
     * @param strategy 输入处理策略
     */
    void setInputStrategy(InputStrategyPtr strategy);
    
    // ==========================================================================
    // 数据提交接口
    // ==========================================================================
    
    /**
     * @brief 提交 CPU 数据（如 YUV buffer）
     * @param data CPU 输入数据
     * @return 是否成功
     */
    bool submitCPUData(const CPUInputData& data);
    
    /**
     * @brief 提交 GPU 数据（如纹理）
     * @param data GPU 输入数据
     * @return 是否成功
     */
    bool submitGPUData(const GPUInputData& data);
    
    /**
     * @brief 提交双路数据
     * @param data 包含CPU和GPU的输入数据
     * @return 是否成功
     */
    bool submitData(const InputData& data);
        
    // ==========================================================================
    // 异步任务链接口 (新增)
    // ==========================================================================
        
    /**
     * @brief 设置PipelineExecutor引用
     * 
     * 用于在处理完成后投递下游任务。
     * 
     * @param executor PipelineExecutor指针
     */
    void setExecutor(class PipelineExecutor* executor) { mExecutor = executor; }
        
    /**
     * @brief 启动处理循环
     * 
     * 将InputEntity的process任务投递到TaskQueue，
     * 进入等待数据状态。
     */
    void startProcessingLoop();
        
    /**
     * @brief 停止处理循环
     */
    void stopProcessingLoop();
        
    /**
     * @brief 检查是否正在等待数据
     */
    bool isWaitingForData() const { return mWaitingForData.load(); }
    
    // ==========================================================================
    // 便捷提交方法
    // ==========================================================================
    
    /**
     * @brief 提交 RGBA 数据
     */
    bool submitRGBA(const uint8_t* data, uint32_t width, uint32_t height,
                    int64_t timestamp = 0);
    
    /**
     * @brief 提交 NV21 数据（Android 常用）
     */
    bool submitNV21(const uint8_t* data, uint32_t width, uint32_t height,
                    int64_t timestamp = 0);
    
    /**
     * @brief 提交 NV12 数据
     */
    bool submitNV12(const uint8_t* data, uint32_t width, uint32_t height,
                    int64_t timestamp = 0);
    
    /**
     * @brief 提交 YUV420P 数据
     */
    bool submitYUV420P(const uint8_t* yPlane, const uint8_t* uPlane,
                       const uint8_t* vPlane,
                       uint32_t width, uint32_t height,
                       uint32_t yStride, uint32_t uStride, uint32_t vStride,
                       int64_t timestamp = 0);
    
    /**
     * @brief 提交 OpenGL 纹理
     */
    bool submitTexture(uint32_t textureId, uint32_t width, uint32_t height,
                       int64_t timestamp = 0);
    
    /**
     * @brief 提交 OES 纹理（Android）
     * @param textureId OES 纹理 ID
     * @param transformMatrix 纹理变换矩阵
     */
    bool submitOESTexture(uint32_t textureId, uint32_t width, uint32_t height,
                          const float* transformMatrix, int64_t timestamp = 0);
    
    // ==========================================================================
    // 状态查询
    // ==========================================================================
    
    /**
     * @brief 获取已处理帧数
     */
    uint64_t getFrameCount() const { return mFrameCount; }
    
    /**
     * @brief 检查 GPU 输出是否启用
     */
    bool isGPUOutputEnabled() const;
    
    /**
     * @brief 检查 CPU 输出是否启用
     */
    bool isCPUOutputEnabled() const;
    
protected:
    // ==========================================================================
    // ProcessEntity 生命周期
    // ==========================================================================
    
    bool prepare(PipelineContext& context) override;
    
    bool process(const std::vector<FramePacketPtr>& inputs,
                std::vector<FramePacketPtr>& outputs,
                PipelineContext& context) override;
    
    void finalize(PipelineContext& context) override;
    
private:
    // 初始化输出端口
    void initializePorts();
    
    // 处理提交的数据
    bool processInputData(const InputData& data);
    
    // 创建输出数据包
    FramePacketPtr createGPUOutputPacket(int64_t timestamp);
    FramePacketPtr createCPUOutputPacket(int64_t timestamp);
    
    // 格式转换（使用 libyuv）
    bool convertToRGBA(const CPUInputData& input, uint8_t* output);
    bool convertToYUV420P(const CPUInputData& input, uint8_t* yOut,
                          uint8_t* uOut, uint8_t* vOut);
    
private:
    // 配置
    InputConfig mConfig;
    
    // 渲染上下文
    lrengine::render::LRRenderContext* mRenderContext = nullptr;
    
    // 输入策略
    InputStrategyPtr mStrategy;
    
    // 帧计数
    uint64_t mFrameCount = 0;
    
    // GPU 输出纹理
    lrengine::LRTexturePtr mGPUOutputTexture;
    
    // CPU 输出缓冲区
    std::vector<uint8_t> mCPUOutputBuffer;
    
    // ==========================================================================
    // 异步任务链数据 (新增)
    // ==========================================================================
    
    // 输入数据队列 (线程安全)
    std::queue<InputData> mInputQueue;
    std::mutex mQueueMutex;
    std::condition_variable mDataAvailableCV;
    
    // 任务控制
    std::atomic<bool> mTaskRunning{false};       // 任务是否在运行
    std::atomic<bool> mWaitingForData{false};    // 是否等待数据
    
    // PipelineExecutor 引用 (用于投递下游任务)
    class PipelineExecutor* mExecutor = nullptr;
    
    // 队列配置
    size_t mMaxQueueSize = 3;                    // 最大队列长度
    bool mDropOldestOnFull = true;               // 队列满时是否丢弃最旧帧
};

using InputEntityPtr = std::shared_ptr<InputEntity>;

} // namespace input
} // namespace pipeline
