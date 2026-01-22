/**
 * @file CPUEntity.h
 * @brief CPU处理节点 - 执行CPU算法计算
 */

#pragma once

#include "ProcessEntity.h"

namespace pipeline {

/**
 * @brief CPU处理节点
 * 
 * 执行算法计算，如人脸检测、AI推理、图像分析等。
 * 
 * 特点：
 * - 在CPU并行队列执行
 * - 自动从GPU纹理读取数据到CPU缓冲
 * - 计算结果存入FramePacket.metadata
 * - 支持多线程并行计算
 * 
 * 子类需要实现：
 * - processOnCPU(): CPU处理逻辑
 * 
 * 可选重写：
 * - getRequiredFormat(): 指定所需的像素格式
 */
class CPUEntity : public ProcessEntity {
public:
    /**
     * @brief 构造函数
     * @param name Entity名称
     */
    explicit CPUEntity(const std::string& name = "CPUEntity");
    
    ~CPUEntity() override;
    
    // ==========================================================================
    // 类型信息
    // ==========================================================================
    
    EntityType getType() const override { return EntityType::CPU; }
    ExecutionQueue getExecutionQueue() const override { return ExecutionQueue::CPUParallel; }
    
    // ==========================================================================
    // 配置
    // ==========================================================================
    
    /**
     * @brief 设置是否需要写回纹理
     * 
     * 如果为true，处理后的CPU数据会上传回GPU纹理。
     * 默认为false（仅更新metadata）。
     */
    void setWriteBackTexture(bool writeBack) { mWriteBackTexture = writeBack; }
    
    /**
     * @brief 获取是否需要写回纹理
     */
    bool getWriteBackTexture() const { return mWriteBackTexture; }
    
    /**
     * @brief 设置处理分辨率缩放
     * 
     * 用于降低CPU处理负载，如人脸检测可使用1/4分辨率。
     * @param scale 缩放比例（0.0-1.0）
     */
    void setProcessingScale(float scale);
    
    /**
     * @brief 获取处理分辨率缩放
     */
    float getProcessingScale() const { return mProcessingScale; }
    
    /**
     * @brief 设置是否透传输入
     * 
     * 如果为true，输出数据包直接使用输入的纹理（仅添加metadata）。
     * 默认为true。
     */
    void setPassthroughInput(bool passthrough) { mPassthroughInput = passthrough; }
    
    /**
     * @brief 获取是否透传输入
     */
    bool getPassthroughInput() const { return mPassthroughInput; }
    
protected:
    // ==========================================================================
    // 子类实现接口
    // ==========================================================================
    
    /**
     * @brief 核心处理逻辑
     */
    bool process(const std::vector<FramePacketPtr>& inputs,
                std::vector<FramePacketPtr>& outputs,
                PipelineContext& context) override;
    
    /**
     * @brief CPU处理逻辑（子类必须实现）
     * 
     * @param data 像素数据指针
     * @param width 图像宽度
     * @param height 图像高度
     * @param stride 行步长
     * @param format 像素格式
     * @param metadata 元数据映射（用于存储计算结果）
     * @return 是否成功
     */
    virtual bool processOnCPU(const uint8_t* data,
                             uint32_t width,
                             uint32_t height,
                             uint32_t stride,
                             PixelFormat format,
                             std::unordered_map<std::string, std::any>& metadata) = 0;
    
    /**
     * @brief 获取所需的像素格式
     * 
     * 子类可重写以指定所需格式，默认为RGBA8。
     * 如果返回Unknown，则使用输入的原始格式。
     */
    virtual PixelFormat getRequiredFormat() const { return PixelFormat::RGBA8; }
    
    /**
     * @brief CPU处理完成后的回调（可选重写）
     * 
     * 在processOnCPU完成后调用，可用于后处理。
     * @param input 输入数据包
     * @param output 输出数据包
     */
    virtual void onProcessComplete(FramePacketPtr input, FramePacketPtr output) {}
    
    // ==========================================================================
    // 辅助方法
    // ==========================================================================
    
    /**
     * @brief 确保CPU缓冲可用
     * @param packet 数据包
     * @return 是否成功
     */
    bool ensureCpuBuffer(FramePacketPtr packet);
    
    /**
     * @brief 缩放图像
     * @param src 源数据
     * @param srcWidth 源宽度
     * @param srcHeight 源高度
     * @param dstWidth 目标宽度
     * @param dstHeight 目标高度
     * @param format 像素格式
     * @return 缩放后的数据
     */
    std::shared_ptr<uint8_t> scaleImage(const uint8_t* src,
                                        uint32_t srcWidth, uint32_t srcHeight,
                                        uint32_t dstWidth, uint32_t dstHeight,
                                        PixelFormat format);
    
protected:
    // 配置
    bool mWriteBackTexture = false;
    bool mPassthroughInput = true;
    float mProcessingScale = 1.0f;
    
    // 临时缓冲
    std::shared_ptr<uint8_t> mScaledBuffer;
    size_t mScaledBufferSize = 0;
};

} // namespace pipeline
