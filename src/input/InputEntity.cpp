/**
 * @file InputEntity.cpp
 * @brief InputEntity 实现
 */

#include "pipeline/input/InputEntity.h"
#include "pipeline/data/FramePacket.h"
#include "pipeline/core/PipelineConfig.h"
#include "pipeline/core/PipelineExecutor.h"

// libyuv 头文件
#include "libyuv.h"

namespace pipeline {
namespace input {

// =============================================================================
// 构造与析构
// =============================================================================

InputEntity::InputEntity(const std::string& name)
    : ProcessEntity(name) {
    initializePorts();
}

InputEntity::~InputEntity() {
    // 释放策略资源
    if (mStrategy) {
        mStrategy->release();
    }
}

// =============================================================================
// 端口初始化
// =============================================================================

void InputEntity::initializePorts() {
    // InputEntity 没有输入端口，只有输出端口
    addOutputPort(GPU_OUTPUT_PORT);
    addOutputPort(CPU_OUTPUT_PORT);
}

// =============================================================================
// 配置
// =============================================================================

void InputEntity::configure(const InputConfig& config) {
    // 配置通常在初始化阶段调用，不需要锁保护
    mConfig = config;
    
    // 根据配置预分配 CPU 缓冲区
    if (config.enableDualOutput || config.dataType == InputDataType::CPUBuffer) {
        size_t bufferSize = config.width * config.height * 4; // RGBA
        mCPUOutputBuffer.resize(bufferSize);
    }
}

void InputEntity::setRenderContext(lrengine::render::LRRenderContext* context) {
    mRenderContext = context;
    
    // 初始化策略
    if (mStrategy && mRenderContext) {
        mStrategy->initialize(mRenderContext);
    }
}

void InputEntity::setInputStrategy(InputStrategyPtr strategy) {
    mStrategy = std::move(strategy);
    
    if (mStrategy && mRenderContext) {
        mStrategy->initialize(mRenderContext);
    }
}

// =============================================================================
// 数据提交接口
// =============================================================================

bool InputEntity::submitCPUData(const CPUInputData& data) {
    InputData inputData;
    inputData.cpu = data;
    inputData.dataType = InputDataType::CPUBuffer;
    return submitData(inputData);
}

bool InputEntity::submitGPUData(const GPUInputData& data) {
    InputData inputData;
    inputData.gpu = data;
    inputData.dataType = InputDataType::GPUTexture;
    return submitData(inputData);
}

bool InputEntity::submitData(const InputData& data) {
    std::unique_lock<std::mutex> lock(mQueueMutex);
    
    // 检查队列是否满
    if (mInputQueue.size() >= mMaxQueueSize) {
        if (mDropOldestOnFull) {
            mInputQueue.pop();  // 丢弃最旧帧
            // TODO: 使用PIPELINE_LOGW记录
        } else {
            // 丢弃新帧
            return false;
        }
    }
    
    // 入队
    mInputQueue.push(data);
    
    // 🔥 关键: 唤醒等待的process任务
    mDataAvailableCV.notify_one();
    
    return true;
}

// =============================================================================
// 便捷提交方法
// =============================================================================

bool InputEntity::submitRGBA(const uint8_t* data, uint32_t width, uint32_t height,
                              int64_t timestamp) {
    CPUInputData cpuData;
    cpuData.data = data;
    cpuData.dataSize = width * height * 4;
    cpuData.width = width;
    cpuData.height = height;
    cpuData.stride = width * 4;
    cpuData.format = InputFormat::RGBA;
    cpuData.timestamp = timestamp;
    return submitCPUData(cpuData);
}

bool InputEntity::submitNV21(const uint8_t* data, uint32_t width, uint32_t height,
                              int64_t timestamp) {
    CPUInputData cpuData;
    cpuData.data = data;
    cpuData.dataSize = width * height * 3 / 2;
    cpuData.width = width;
    cpuData.height = height;
    cpuData.format = InputFormat::NV21;
    cpuData.timestamp = timestamp;
    
    // NV21: Y 平面 + VU 交织平面
    cpuData.planeY = data;
    cpuData.planeU = data + width * height; // VU 交织
    cpuData.strideY = width;
    cpuData.strideU = width;
    
    return submitCPUData(cpuData);
}

bool InputEntity::submitNV12(const uint8_t* data, uint32_t width, uint32_t height,
                              int64_t timestamp) {
    CPUInputData cpuData;
    cpuData.data = data;
    cpuData.dataSize = width * height * 3 / 2;
    cpuData.width = width;
    cpuData.height = height;
    cpuData.format = InputFormat::NV12;
    cpuData.timestamp = timestamp;
    
    // NV12: Y 平面 + UV 交织平面
    cpuData.planeY = data;
    cpuData.planeU = data + width * height; // UV 交织
    cpuData.strideY = width;
    cpuData.strideU = width;
    
    return submitCPUData(cpuData);
}

bool InputEntity::submitYUV420P(const uint8_t* yPlane, const uint8_t* uPlane,
                                 const uint8_t* vPlane,
                                 uint32_t width, uint32_t height,
                                 uint32_t yStride, uint32_t uStride, uint32_t vStride,
                                 int64_t timestamp) {
    CPUInputData cpuData;
    cpuData.width = width;
    cpuData.height = height;
    cpuData.format = InputFormat::YUV420;
    cpuData.timestamp = timestamp;
    
    cpuData.planeY = yPlane;
    cpuData.planeU = uPlane;
    cpuData.planeV = vPlane;
    cpuData.strideY = yStride;
    cpuData.strideU = uStride;
    cpuData.strideV = vStride;
    cpuData.dataSize = yStride * height + uStride * height / 2 + vStride * height / 2;
    
    return submitCPUData(cpuData);
}

bool InputEntity::submitTexture(uint32_t textureId, uint32_t width, uint32_t height,
                                 int64_t timestamp) {
    GPUInputData gpuData;
    gpuData.textureId = textureId;
    gpuData.width = width;
    gpuData.height = height;
    gpuData.format = InputFormat::Texture;
    gpuData.timestamp = timestamp;
    gpuData.isOESTexture = false;
    return submitGPUData(gpuData);
}

bool InputEntity::submitOESTexture(uint32_t textureId, uint32_t width, uint32_t height,
                                    const float* transformMatrix, int64_t timestamp) {
    GPUInputData gpuData;
    gpuData.textureId = textureId;
    gpuData.width = width;
    gpuData.height = height;
    gpuData.format = InputFormat::OES;
    gpuData.timestamp = timestamp;
    gpuData.isOESTexture = true;
    
    if (transformMatrix) {
        std::memcpy(gpuData.transformMatrix, transformMatrix, sizeof(float) * 16);
    }
    
    return submitGPUData(gpuData);
}

// =============================================================================
// 状态查询
// =============================================================================

bool InputEntity::isGPUOutputEnabled() const {
    return mConfig.enableDualOutput || 
           mConfig.dataType == InputDataType::GPUTexture ||
           mConfig.dataType == InputDataType::Both;
}

bool InputEntity::isCPUOutputEnabled() const {
    return mConfig.enableDualOutput ||
           mConfig.dataType == InputDataType::CPUBuffer ||
           mConfig.dataType == InputDataType::Both;
}

// =============================================================================
// ProcessEntity 生命周期
// =============================================================================

bool InputEntity::prepare(PipelineContext& context) {
    // 获取渲染上下文（如果尚未设置）
    if (!mRenderContext) {
        // 从 context 获取 LRRenderContext
        // mRenderContext = context.getRenderContext();
    }
    
    return true;
}

bool InputEntity::process(const std::vector<FramePacketPtr>& inputs,
                          std::vector<FramePacketPtr>& outputs,
                          PipelineContext& context) {
    InputData inputData;
    
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        
        // 🔥 关键改进: 先检查队列是否有数据
        if (!mInputQueue.empty()) {
            // 队列有数据,立即处理,不等待
            inputData = mInputQueue.front();
            mInputQueue.pop();
            // TODO: 使用PIPELINE_LOGD记录
        } else {
            // 队列为空,等待数据到达
            mWaitingForData.store(true);
            mDataAvailableCV.wait(lock, [this] { 
                return !mInputQueue.empty() || !mTaskRunning.load(); 
            });
            mWaitingForData.store(false);
            
            // 检查任务是否被取消
            if (!mTaskRunning.load()) {
                return false;
            }
            
            // 再次检查队列
            if (mInputQueue.empty()) {
                // TODO: 使用PIPELINE_LOGW记录
                return false;
            }
            
            // 出队
            inputData = mInputQueue.front();
            mInputQueue.pop();
        }
    }
    
    // 处理数据
    if (!processInputData(inputData)) {
        return false;
    }
    
    // 获取时间戳 (根据数据类型)
    int64_t timestamp = (inputData.dataType == InputDataType::GPUTexture) 
                        ? inputData.gpu.timestamp 
                        : inputData.cpu.timestamp;
    
    // 创建输出数据包
    if (isGPUOutputEnabled()) {
        auto gpuPacket = createGPUOutputPacket(timestamp);
        if (gpuPacket) {
            outputs.push_back(gpuPacket);
            
            // 发送到 GPU 输出端口
            auto* gpuPort = getOutputPort(GPU_OUTPUT_PORT);
            if (gpuPort) {
                gpuPort->setPacket(gpuPacket);
            }
        }
    }
    
    if (isCPUOutputEnabled()) {
        auto cpuPacket = createCPUOutputPacket(timestamp);
        if (cpuPacket) {
            outputs.push_back(cpuPacket);
            
            // 发送到 CPU 输出端口
            auto* cpuPort = getOutputPort(CPU_OUTPUT_PORT);
            if (cpuPort) {
                cpuPort->setPacket(cpuPacket);
            }
        }
    }
    
    ++mFrameCount;
    return true;
}

void InputEntity::finalize(PipelineContext& context) {
    // 发送输出到下游
    sendOutputs();
}

// =============================================================================
// 内部处理
// =============================================================================

bool InputEntity::processInputData(const InputData& data) {
    // 使用策略处理（如果有）
    if (mStrategy) {
        if (isGPUOutputEnabled()) {
            if (!mStrategy->processToGPU(data, mGPUOutputTexture)) {
                return false;
            }
        }
        
        if (isCPUOutputEnabled()) {
            size_t outputSize = mCPUOutputBuffer.size();
            if (!mStrategy->processToCPU(data, mCPUOutputBuffer.data(), outputSize)) {
                return false;
            }
        }
        return true;
    }
    
    // 默认处理：格式转换
    if (data.dataType == InputDataType::CPUBuffer) {
        // CPU 数据需要上传到 GPU（如果启用 GPU 输出）
        if (isGPUOutputEnabled() && isCPUOutputEnabled()) {
            // 双路输出：转换为 RGBA 供 GPU 使用
            if (!convertToRGBA(data.cpu, mCPUOutputBuffer.data())) {
                return false;
            }
        }
    }
    
    return true;
}

FramePacketPtr InputEntity::createGPUOutputPacket(int64_t timestamp) {
    auto packet = std::make_shared<FramePacket>();
    packet->setTimestamp(timestamp);
    packet->setFormat(PixelFormat::RGBA8);
    packet->setSize(mConfig.width, mConfig.height);
    
    // 设置纹理
    if (mGPUOutputTexture) {
        packet->setTexture(mGPUOutputTexture);
    }
    
    return packet;
}

FramePacketPtr InputEntity::createCPUOutputPacket(int64_t timestamp) {
    auto packet = std::make_shared<FramePacket>();
    packet->setTimestamp(timestamp);
    packet->setSize(mConfig.width, mConfig.height);
    
    // 设置像素格式（根据配置）
    switch (mConfig.format) {
        case InputFormat::YUV420:
        case InputFormat::NV12:
        case InputFormat::NV21:
            packet->setFormat(PixelFormat::YUV420);
            break;
        default:
            packet->setFormat(PixelFormat::RGBA8);
            break;
    }
    
    // 设置 CPU 数据
    if (!mCPUOutputBuffer.empty()) {
        packet->setCpuBuffer(mCPUOutputBuffer.data(), mCPUOutputBuffer.size());
    }
    
    return packet;
}

// =============================================================================
// 格式转换
// =============================================================================

bool InputEntity::convertToRGBA(const CPUInputData& input, uint8_t* output) {
    if (!input.data && !input.planeY) {
        return false;
    }
    
    int width = static_cast<int>(input.width);
    int height = static_cast<int>(input.height);
    int dstStride = width * 4;
    
    switch (input.format) {
        case InputFormat::RGBA:
            // 直接复制
            if (input.stride == static_cast<uint32_t>(dstStride)) {
                std::memcpy(output, input.data, width * height * 4);
            } else {
                // 逐行复制
                for (int y = 0; y < height; ++y) {
                    std::memcpy(output + y * dstStride,
                               input.data + y * input.stride,
                               width * 4);
                }
            }
            return true;
            
        case InputFormat::BGRA:
            // BGRA -> RGBA
            libyuv::ARGBToABGR(input.data, input.stride,
                               output, dstStride,
                               width, height);
            return true;
            
        case InputFormat::RGB:
            // RGB -> RGBA
            libyuv::RAWToARGB(input.data, input.stride,
                              output, dstStride,
                              width, height);
            return true;
            
        case InputFormat::NV12:
            // NV12 -> RGBA
            libyuv::NV12ToARGB(input.planeY, input.strideY,
                               input.planeU, input.strideU,
                               output, dstStride,
                               width, height);
            return true;
            
        case InputFormat::NV21:
            // NV21 -> RGBA
            libyuv::NV21ToARGB(input.planeY, input.strideY,
                               input.planeU, input.strideU,
                               output, dstStride,
                               width, height);
            return true;
            
        case InputFormat::YUV420:
            // I420 -> RGBA
            libyuv::I420ToARGB(input.planeY, input.strideY,
                               input.planeU, input.strideU,
                               input.planeV, input.strideV,
                               output, dstStride,
                               width, height);
            return true;
            
        default:
            return false;
    }
}

bool InputEntity::convertToYUV420P(const CPUInputData& input,
                                    uint8_t* yOut, uint8_t* uOut, uint8_t* vOut) {
    if (!input.data && !input.planeY) {
        return false;
    }
    
    int width = static_cast<int>(input.width);
    int height = static_cast<int>(input.height);
    int yStride = width;
    int uvStride = width / 2;
    
    switch (input.format) {
        case InputFormat::RGBA:
            // RGBA -> I420
            libyuv::ARGBToI420(input.data, input.stride,
                               yOut, yStride,
                               uOut, uvStride,
                               vOut, uvStride,
                               width, height);
            return true;
            
        case InputFormat::BGRA:
            // BGRA -> I420
            libyuv::ARGBToI420(input.data, input.stride,
                               yOut, yStride,
                               uOut, uvStride,
                               vOut, uvStride,
                               width, height);
            return true;
            
        case InputFormat::NV12:
            // NV12 -> I420
            libyuv::NV12ToI420(input.planeY, input.strideY,
                               input.planeU, input.strideU,
                               yOut, yStride,
                               uOut, uvStride,
                               vOut, uvStride,
                               width, height);
            return true;
            
        case InputFormat::NV21:
            // NV21 -> I420
            libyuv::NV21ToI420(input.planeY, input.strideY,
                               input.planeU, input.strideU,
                               yOut, yStride,
                               uOut, uvStride,
                               vOut, uvStride,
                               width, height);
            return true;
            
        case InputFormat::YUV420:
            // 直接复制
            std::memcpy(yOut, input.planeY, width * height);
            std::memcpy(uOut, input.planeU, width * height / 4);
            std::memcpy(vOut, input.planeV, width * height / 4);
            return true;
            
        default:
            return false;
    }
}

// =============================================================================
// 异步任务链实现 (新增)
// =============================================================================

void InputEntity::startProcessingLoop() {
    mTaskRunning.store(true);
    
    // 将process任务投递到TaskQueue (通过PipelineExecutor)
    if (mExecutor) {
        mExecutor->submitEntityTask(this->getId());
    }
    // TODO: 使用PIPELINE_LOGI记录
}

void InputEntity::stopProcessingLoop() {
    mTaskRunning.store(false);
    mDataAvailableCV.notify_all();  // 唤醒所有等待的任务
    // TODO: 使用PIPELINE_LOGI记录
}

} // namespace input
} // namespace pipeline
