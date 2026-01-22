/**
 * @file GPUEntity.h
 * @brief GPU处理节点 - 执行GPU渲染操作
 */

#pragma once

#include "ProcessEntity.h"

// 前向声明LREngine类型
namespace lrengine {
namespace render {
class LRRenderContext;
class LRShader;
class LRShaderProgram;
class LRFrameBuffer;
class LRPipelineState;
class LRVertexBuffer;
class LRTexture;
} // namespace render
} // namespace lrengine

namespace pipeline {

/**
 * @brief GPU处理节点
 * 
 * 封装LREngine渲染操作，执行滤镜、美颜、色彩调整等GPU任务。
 * 
 * 特点：
 * - 在GPU串行队列执行
 * - 管理着色器程序和FBO
 * - 支持多纹理输入
 * - 自动处理OpenGL/Metal上下文
 * 
 * 子类需要实现：
 * - setupShader(): 设置着色器程序
 * - setUniforms(): 设置着色器参数
 * - processGPU(): GPU处理逻辑（可选，默认为全屏绘制）
 */
class GPUEntity : public ProcessEntity {
public:
    /**
     * @brief 构造函数
     * @param name Entity名称
     */
    explicit GPUEntity(const std::string& name = "GPUEntity");
    
    ~GPUEntity() override;
    
    // ==========================================================================
    // 类型信息
    // ==========================================================================
    
    EntityType getType() const override { return EntityType::GPU; }
    ExecutionQueue getExecutionQueue() const override { return ExecutionQueue::GPU; }
    
    // ==========================================================================
    // 渲染上下文
    // ==========================================================================
    
    /**
     * @brief 设置渲染上下文
     */
    void setRenderContext(lrengine::render::LRRenderContext* context);
    
    /**
     * @brief 获取渲染上下文
     */
    lrengine::render::LRRenderContext* getRenderContext() const { return mRenderContext; }
    
    // ==========================================================================
    // 着色器管理
    // ==========================================================================
    
    /**
     * @brief 获取着色器程序
     */
    std::shared_ptr<lrengine::render::LRShaderProgram> getShaderProgram() const { return mShaderProgram; }
    
    /**
     * @brief 设置顶点着色器源码
     */
    void setVertexShaderSource(const std::string& source);
    
    /**
     * @brief 设置片段着色器源码
     */
    void setFragmentShaderSource(const std::string& source);
    
    /**
     * @brief 从文件加载着色器
     */
    bool loadShaderFromFile(const std::string& vertexPath, const std::string& fragmentPath);
    
    // ==========================================================================
    // 输出配置
    // ==========================================================================
    
    /**
     * @brief 设置输出尺寸
     * @param width 宽度（0表示使用输入尺寸）
     * @param height 高度（0表示使用输入尺寸）
     */
    void setOutputSize(uint32_t width, uint32_t height);
    
    /**
     * @brief 设置输出格式
     */
    void setOutputFormat(PixelFormat format);
    
    /**
     * @brief 获取输出宽度
     */
    uint32_t getOutputWidth() const { return mOutputWidth; }
    
    /**
     * @brief 获取输出高度
     */
    uint32_t getOutputHeight() const { return mOutputHeight; }
    
    /**
     * @brief 获取输出格式
     */
    PixelFormat getOutputFormat() const { return mOutputFormat; }
    
protected:
    // ==========================================================================
    // 子类实现接口
    // ==========================================================================
    
    /**
     * @brief 准备阶段 - 初始化GPU资源
     */
    bool prepare(PipelineContext& context) override;
    
    /**
     * @brief 核心处理逻辑
     */
    bool process(const std::vector<FramePacketPtr>& inputs,
                std::vector<FramePacketPtr>& outputs,
                PipelineContext& context) override;
    
    /**
     * @brief 完成阶段 - 清理
     */
    void finalize(PipelineContext& context) override;
    
    /**
     * @brief 设置着色器（子类实现）
     * 
     * 在此方法中创建和编译着色器程序。
     * @return 是否成功
     */
    virtual bool setupShader();
    
    /**
     * @brief 设置着色器uniform参数（子类实现）
     * 
     * 在每帧渲染前调用，用于设置滤镜参数等。
     * @param input 输入数据包
     */
    virtual void setUniforms(FramePacket* input) {}
    
    /**
     * @brief GPU处理逻辑（子类可选重写）
     * 
     * 默认实现为全屏四边形绘制。
     * @param inputs 输入数据包列表
     * @param output 输出数据包
     * @return 是否成功
     */
    virtual bool processGPU(const std::vector<FramePacketPtr>& inputs, 
                           FramePacketPtr output);
    
    /**
     * @brief 创建/更新FrameBuffer
     */
    bool ensureFrameBuffer(uint32_t width, uint32_t height);
    
    /**
     * @brief 创建全屏顶点缓冲
     */
    bool createFullscreenQuad();
    
    /**
     * @brief 绘制全屏四边形
     */
    void drawFullscreenQuad();
    
    /**
     * @brief 绑定输入纹理
     * @param inputs 输入数据包列表
     * @param startSlot 起始纹理槽位
     */
    void bindInputTextures(const std::vector<FramePacketPtr>& inputs, uint32_t startSlot = 0);
    
    /**
     * @brief 解绑输入纹理
     * @param count 纹理数量
     * @param startSlot 起始纹理槽位
     */
    void unbindInputTextures(size_t count, uint32_t startSlot = 0);
    
protected:
    // 渲染上下文
    lrengine::render::LRRenderContext* mRenderContext = nullptr;
    
    // 着色器
    std::shared_ptr<lrengine::render::LRShaderProgram> mShaderProgram;
    std::string mVertexShaderSource;
    std::string mFragmentShaderSource;
    bool mShaderNeedsRebuild = true;
    bool mNeedsShaderUpdate = false;
    
    // FrameBuffer
    std::shared_ptr<lrengine::render::LRFrameBuffer> mFrameBuffer;
    std::shared_ptr<lrengine::render::LRTexture> mOutputTexture;
    
    // 管线状态
    std::shared_ptr<lrengine::render::LRPipelineState> mPipelineState;
    
    // 全屏顶点缓冲
    std::shared_ptr<lrengine::render::LRVertexBuffer> mFullscreenQuad;
    
    // 输出配置
    uint32_t mOutputWidth = 0;   // 0表示使用输入尺寸
    uint32_t mOutputHeight = 0;
    PixelFormat mOutputFormat = PixelFormat::RGBA8;
    
    // 默认着色器源码
    static const char* sDefaultVertexShader;
    static const char* sDefaultFragmentShader;
};

} // namespace pipeline
