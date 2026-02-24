/**
 * @file PipelineManager.h
 * @brief 管线管理器 - 对外统一接口
 */

#pragma once

#include "pipeline/data/EntityTypes.h"
#include "PipelineConfig.h"
#include "PipelineGraph.h"
#include "PipelineExecutor.h"
#include "pipeline/output/OutputConfig.h"
#include <memory>
#include <functional>
#include <map>
#include <atomic>

// 前向声明
namespace lrengine {
namespace render {
class LRRenderContext;
} // namespace render
} // namespace lrengine

namespace pipeline {

// 前向声明
namespace input {
class InputEntity;
class InputConfig;
namespace ios {
class PixelBufferInputStrategy;
} // namespace ios
namespace android {
class OESTextureInputStrategy;
} // namespace android
} // namespace input

namespace output {
class DisplaySurface;
class OutputTarget;
class OutputEntity;
} // namespace output

/**
 * @brief 管线状态
 */
enum class PipelineState : uint8_t {
    Created,     // 已创建
    Initialized, // 已初始化
    Running,     // 运行中
    Paused,      // 已暂停
    Stopped,     // 已停止
    Error        // 错误状态
};

/**
 * @brief 管线管理器
 * 
 * 对外的统一接口，封装Pipeline的创建、配置和执行。
 * 
 * 使用流程：
 * 1. create() 创建实例
 * 2. configure() 配置参数
 * 3. 添加Entity并建立连接
 * 4. start() 启动管线
 * 5. processFrame() 处理帧数据
 * 6. stop() 停止管线
 * 7. destroy() 销毁资源
 */
class PipelineManager : public std::enable_shared_from_this<PipelineManager> {
public:
    /**
     * @brief 创建管线管理器
     * @param renderContext 渲染上下文
     * @param config 配置参数
     * @return 管线管理器实例
     */
    static std::shared_ptr<PipelineManager> create(
        lrengine::render::LRRenderContext* renderContext,
        const PipelineConfig& config = PipelineConfig());
    
    ~PipelineManager();
    
    // 禁止拷贝
    PipelineManager(const PipelineManager&) = delete;
    PipelineManager& operator=(const PipelineManager&) = delete;
    
    // ==========================================================================
    // 生命周期
    // ==========================================================================
    
    /**
     * @brief 初始化管线
     * @return 是否成功
     */
    bool initialize();
    
    /**
     * @brief 启动管线
     * @return 是否成功
     */
    bool start();
    
    /**
     * @brief 暂停管线
     */
    void pause();
    
    /**
     * @brief 恢复管线
     */
    void resume();
    
    /**
     * @brief 停止管线
     */
    void stop();
    
    /**
     * @brief 销毁管线
     */
    void destroy();
    
    /**
     * @brief 获取管线状态
     */
    PipelineState getState() const { return mState; }
    
    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const { return mState == PipelineState::Running; }
    
    // ==========================================================================
    // Entity管理
    // ==========================================================================
    
    /**
     * @brief 添加Entity
     * @param entity Entity智能指针
     * @return Entity ID
     */
    EntityId addEntity(ProcessEntityPtr entity);
    
    /**
     * @brief 创建并添加Entity
     * @tparam T Entity类型
     * @tparam Args 构造函数参数类型
     * @param args 构造函数参数
     * @return Entity ID
     */
    template<typename T, typename... Args>
    EntityId createEntity(Args&&... args) {
        auto entity = std::make_shared<T>(std::forward<Args>(args)...);
        return addEntity(entity);
    }
    
    /**
     * @brief 移除Entity
     * @param entityId Entity ID
     * @return 是否成功
     */
    bool removeEntity(EntityId entityId);
    
    /**
     * @brief 获取Entity
     * @param entityId Entity ID
     * @return Entity指针
     */
    ProcessEntityPtr getEntity(EntityId entityId) const;
    
    /**
     * @brief 按名称获取Entity
     */
    ProcessEntityPtr getEntityByName(const std::string& name) const;
    
    /**
     * @brief 获取所有Entity
     */
    std::vector<ProcessEntityPtr> getAllEntities() const;
    
    // ==========================================================================
    // 连接管理
    // ==========================================================================
    
    /**
     * @brief 连接两个Entity
     * @param srcId 源Entity ID
     * @param srcPort 源输出端口名称
     * @param dstId 目标Entity ID
     * @param dstPort 目标输入端口名称
     * @return 是否成功
     */
    bool connect(EntityId srcId, const std::string& srcPort,
                 EntityId dstId, const std::string& dstPort);
    
    /**
     * @brief 连接两个Entity（使用默认端口）
     */
    bool connect(EntityId srcId, EntityId dstId);
    
    /**
     * @brief 断开连接
     */
    bool disconnect(EntityId srcId, EntityId dstId);
    
    /**
     * @brief 验证图结构
     */
    ValidationResult validate() const;
    
    // ==========================================================================
    // 帧处理
    // ==========================================================================
    
    /**
     * @brief 处理一帧（同步）
     * @param input 输入数据包
     * @return 输出数据包
     */
    FramePacketPtr processFrame(FramePacketPtr input);
    
    /**
     * @brief 处理一帧（异步）
     * @param input 输入数据包
     * @param callback 完成回调
     * @return 是否成功提交
     */
    bool processFrameAsync(FramePacketPtr input,
                          std::function<void(FramePacketPtr)> callback);
    
    /**
     * @brief 等待所有帧处理完成
     */
    bool flush(int64_t timeoutMs = -1);
    
    // ==========================================================================
    // 输入输出快捷接口
    // ==========================================================================
    
    /**
     * @brief 获取输入Entity
     */
    input::InputEntity* getInputEntity() const;
    
    /**
     * @brief 获取输出Entity
     */
    output::OutputEntity* getOutputEntity() const;
    
    /**
     * @brief 设置输入Entity
     */
    void setInputEntity(EntityId entityId);
    
    /**
     * @brief 设置输出Entity
     */
    void setOutputEntity(EntityId entityId);
    
    // ==========================================================================
    // 输入配置(新增)
    // ==========================================================================
    
    /**
     * @brief 设置输入配置，创建并添加输入实体（通用方式）
     * @param config 输入配置
     * @return 输入实体 ID，失败返回 InvalidEntityId
     */
    EntityId setupInput(const input::InputConfig& config);
    
#if defined(__APPLE__)
    /**
     * @brief 设置 PixelBuffer 输入（iOS/macOS）
     * @param width 宽度
     * @param height 高度
     * @param metalManager Metal 上下文管理器（可选）
     * @return 输入实体 ID
     */
    EntityId setupPixelBufferInput(uint32_t width, uint32_t height, void* metalManager = nullptr);
#endif
    
#if defined(__ANDROID__)
    /**
     * @brief 设置 OES 纹理输入（Android 相机）
     * @param width 宽度
     * @param height 高度
     * @return 输入实体 ID
     */
    EntityId setupOESInput(uint32_t width, uint32_t height);
#endif
    
    /**
     * @brief 设置 RGBA 输入
     * @param width 宽度
     * @param height 高度
     * @return 输入实体 ID
     */
    EntityId setupRGBAInput(uint32_t width, uint32_t height);
    
    /**
     * @brief 设置 YUV 输入
     * @param width 宽度
     * @param height 高度
     * @return 输入实体 ID
     */
    EntityId setupYUVInput(uint32_t width, uint32_t height);
    
    // ==========================================================================
    // 输出配置(扩展)
    // ==========================================================================
    
    /**
     * @brief 设置显示输出(完整配置)
     * @param surface 平台 Surface (CAMetalLayer/ANativeWindow)
     * @param width 显示宽度
     * @param height 显示高度
     * @param metalManager Metal 上下文管理器（iOS/macOS 可选）
     * @return 输出目标 ID,失败返回 -1
     */
    int32_t setupDisplayOutput(void* surface, uint32_t width, uint32_t height, void* metalManager = nullptr);
    
    /**
     * @brief 设置回调输出
     * @param callback 帧回调函数
     * @param dataFormat 数据格式
     * @return 目标 ID
     */
    int32_t setupCallbackOutput(
        std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, 
                          output::OutputFormat, int64_t)> callback,
        output::OutputFormat dataFormat);
    
    /**
     * @brief 设置编码器输出
     * @param encoderSurface 编码器 Surface
     * @param encoderType 编码器类型
     * @return 目标 ID
     */
    int32_t setupEncoderOutput(void* encoderSurface, output::EncoderType encoderType);
    
    /**
     * @brief 移除输出目标
     * @param targetId 目标 ID
     * @return 是否成功
     */
    bool removeOutputTarget(int32_t targetId);
    
    /**
     * @brief 更新显示输出尺寸
     * @param targetId 目标 ID
     * @param width 新宽度
     * @param height 新高度
     * @return 是否成功
     */
    bool updateDisplayOutputSize(int32_t targetId, uint32_t width, uint32_t height);
    
    // ==========================================================================
    // 配置
    // ==========================================================================
    
    /**
     * @brief 获取配置
     */
    const PipelineConfig& getConfig() const ;
    
    /**
     * @brief 更新配置
     */
    void updateConfig(const PipelineConfig& config);
    
    /**
     * @brief 获取渲染上下文
     */
    lrengine::render::LRRenderContext* getRenderContext() const { return mRenderContext; }
    
    /**
     * @brief 获取管线图
     */
    PipelineGraph* getGraph() const { return mGraph.get(); }
    
    /**
     * @brief 获取执行器
     */
    PipelineExecutor* getExecutor() const { return mExecutor.get(); }
    
    /**
     * @brief 获取上下文
     */
    std::shared_ptr<PipelineContext> getContext() const;
    
    // ==========================================================================
    // 回调
    // ==========================================================================
    
    /**
     * @brief 设置帧完成回调
     */
    void setFrameCompleteCallback(std::function<void(FramePacketPtr)> callback);
    
    /**
     * @brief 设置帧丢弃回调
     */
    void setFrameDroppedCallback(std::function<void(FramePacketPtr)> callback);
    
    /**
     * @brief 设置错误回调
     */
    void setErrorCallback(std::function<void(EntityId, const std::string&)> callback);
    
    /**
     * @brief 设置状态变更回调
     */
    void setStateCallback(std::function<void(PipelineState)> callback);
    
    // ==========================================================================
    // 统计和调试
    // ==========================================================================
    
    /**
     * @brief 获取执行统计
     */
    ExecutionStats getStats() const;
    
    /**
     * @brief 重置统计
     */
    void resetStats();
    
    /**
     * @brief 导出图为DOT格式
     */
    std::string exportGraphToDot() const;
    
    /**
     * @brief 导出图为JSON格式
     */
    std::string exportGraphToJson() const;
    
private:
    /**
     * @brief 私有构造函数
     */
    PipelineManager(lrengine::render::LRRenderContext* renderContext,
                   const PipelineConfig& config);
    
    /**
     * @brief 创建资源池
     */
    bool createResourcePools();
    
    /**
     * @brief 设置状态
     */
    void setState(PipelineState state);
    
    /**
     * @brief 初始化GPU资源
     */
    bool initializeGPUResources();
    
private:
    // 配置
    lrengine::render::LRRenderContext* mRenderContext;
    
    // 状态
    PipelineState mState = PipelineState::Created;
    std::function<void(PipelineState)> mStateCallback;
    
    // 核心组件
    std::unique_ptr<PipelineGraph> mGraph;
    std::unique_ptr<PipelineExecutor> mExecutor;
    std::shared_ptr<PipelineContext> mContext;
    
    // 资源池
    std::shared_ptr<TexturePool> mTexturePool;
    std::shared_ptr<FramePacketPool> mFramePacketPool;
    
    // 特殊Entity引用
    EntityId mInputEntityId = InvalidEntityId;
    EntityId mOutputEntityId = InvalidEntityId;
    
    // 回调
    std::function<void(FramePacketPtr)> mFrameCompleteCallback;
    std::function<void(FramePacketPtr)> mFrameDroppedCallback;
    std::function<void(EntityId, const std::string&)> mErrorCallback;
    
    // 输出目标管理
    std::map<int32_t, std::shared_ptr<output::OutputTarget>> mOutputTargets;
    std::atomic<int32_t> mNextTargetId{0};
    
    // 输入实体管理
    std::shared_ptr<input::InputEntity> mInputEntity;
    
    // 平台特定输入策略
#if defined(__APPLE__)
    std::shared_ptr<input::ios::PixelBufferInputStrategy> mPixelBufferStrategy;
#endif
#if defined(__ANDROID__)
    std::shared_ptr<input::android::OESTextureInputStrategy> mOESStrategy;
#endif
};

} // namespace pipeline
