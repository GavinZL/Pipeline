/**
 * @file ProcessEntity.h
 * @brief 处理节点基类 - 管线中的核心处理单元
 */

#pragma once

#include "pipeline/data/EntityTypes.h"
#include "pipeline/data/FramePort.h"
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <functional>
#include <unordered_map>

namespace pipeline {

// 前向声明
class PipelineContext;

/**
 * @brief Entity配置参数
 */
struct EntityConfig {
    std::string name;                          // Entity名称
    bool enabled = true;                       // 是否启用
    int32_t priority = 0;                      // 执行优先级
    std::unordered_map<std::string, std::any> params; // 自定义参数
};

/**
 * @brief 处理节点基类
 * 
 * ProcessEntity是管线中的核心处理单元抽象，负责：
 * - 管理输入/输出端口
 * - 维护执行状态机
 * - 提供生命周期回调
 * - 处理依赖关系
 * 
 * 子类需要实现：
 * - process(): 核心处理逻辑
 * - getType(): 返回Entity类型
 * 
 * 可选重写：
 * - prepare(): 准备阶段（获取资源）
 * - finalize(): 完成阶段（清理）
 * - validate(): 配置验证
 */
class ProcessEntity : public std::enable_shared_from_this<ProcessEntity> {
public:
    /**
     * @brief 构造函数
     * @param name Entity名称
     */
    explicit ProcessEntity(const std::string& name = "");
    
    /**
     * @brief 虚析构函数
     */
    virtual ~ProcessEntity();
    
    // 禁止拷贝
    ProcessEntity(const ProcessEntity&) = delete;
    ProcessEntity& operator=(const ProcessEntity&) = delete;
    
    // ==========================================================================
    // 身份信息
    // ==========================================================================
    
    /**
     * @brief 获取Entity唯一ID
     */
    EntityId getId() const { return mId; }
    
    /**
     * @brief 获取Entity名称
     */
    const std::string& getName() const { return mName; }
    
    /**
     * @brief 设置Entity名称
     */
    void setName(const std::string& name) { mName = name; }
    
    /**
     * @brief 获取Entity类型
     */
    virtual EntityType getType() const = 0;
    
    /**
     * @brief 获取执行队列类型
     */
    virtual ExecutionQueue getExecutionQueue() const { return ExecutionQueue::GPU; }
    
    // ==========================================================================
    // 状态管理
    // ==========================================================================
    
    /**
     * @brief 获取当前状态
     */
    EntityState getState() const { return mState.load(); }
    
    /**
     * @brief 检查是否空闲
     */
    bool isIdle() const { return mState.load() == EntityState::Idle; }
    
    /**
     * @brief 检查是否就绪
     */
    bool isReady() const { return mState.load() == EntityState::Ready; }
    
    /**
     * @brief 检查是否正在处理
     */
    bool isProcessing() const { return mState.load() == EntityState::Processing; }
    
    /**
     * @brief 检查是否已完成
     */
    bool isCompleted() const { return mState.load() == EntityState::Completed; }
    
    /**
     * @brief 检查是否出错
     */
    bool hasError() const { return mState.load() == EntityState::Error; }
    
    /**
     * @brief 检查是否启用
     */
    bool isEnabled() const { return mEnabled.load(); }
    
    /**
     * @brief 设置启用状态
     */
    void setEnabled(bool enabled) { mEnabled.store(enabled); }
    
    // ==========================================================================
    // 端口管理
    // ==========================================================================
    
    /**
     * @brief 添加输入端口
     * @param name 端口名称
     * @return 添加的输入端口
     */
    InputPort* addInputPort(const std::string& name);
    
    /**
     * @brief 添加输出端口
     * @param name 端口名称
     * @return 添加的输出端口
     */
    OutputPort* addOutputPort(const std::string& name);
    
    /**
     * @brief 获取输入端口
     * @param index 索引
     */
    InputPort* getInputPort(size_t index) const;
    
    /**
     * @brief 获取输入端口（按名称）
     * @param name 端口名称
     */
    InputPort* getInputPort(const std::string& name) const;
    
    /**
     * @brief 获取输出端口
     * @param index 索引
     */
    OutputPort* getOutputPort(size_t index) const;
    
    /**
     * @brief 获取输出端口（按名称）
     * @param name 端口名称
     */
    OutputPort* getOutputPort(const std::string& name) const;
    
    /**
     * @brief 获取输入端口数量
     */
    size_t getInputPortCount() const { return mInputPorts.size(); }
    
    /**
     * @brief 获取输出端口数量
     */
    size_t getOutputPortCount() const { return mOutputPorts.size(); }
    
    /**
     * @brief 获取所有输入端口
     */
    const std::vector<std::unique_ptr<InputPort>>& getInputPorts() const { return mInputPorts; }
    
    /**
     * @brief 获取所有输出端口
     */
    const std::vector<std::unique_ptr<OutputPort>>& getOutputPorts() const { return mOutputPorts; }
    
    // ==========================================================================
    // 依赖管理
    // ==========================================================================
    
    /**
     * @brief 检查所有输入是否就绪
     */
    bool areInputsReady() const;
    
    /**
     * @brief 等待所有输入就绪
     * @param timeoutMs 超时时间（毫秒）
     * @return 是否成功等待
     */
    bool waitInputsReady(int64_t timeoutMs = -1);
    
    /**
     * @brief 获取未就绪的输入端口数量
     */
    size_t getPendingInputCount() const;
    
    // ==========================================================================
    // 执行流程
    // ==========================================================================
    
    /**
     * @brief 执行Entity
     * 
     * 完整的执行流程：
     * 1. 检查依赖是否满足
     * 2. 调用prepare()准备资源
     * 3. 调用process()执行处理
     * 4. 调用finalize()完成清理
     * 5. 发送输出到下游
     * 
     * @param context 管线上下文
     * @return 是否执行成功
     */
    bool execute(PipelineContext& context);
    
    /**
     * @brief 取消执行
     */
    virtual void cancel();
    
    /**
     * @brief 重置状态（为下一帧准备）
     */
    virtual void resetForNextFrame();
    
    // ==========================================================================
    // 配置
    // ==========================================================================
    
    /**
     * @brief 应用配置
     * @param config 配置参数
     */
    virtual void configure(const EntityConfig& config);
    
    /**
     * @brief 设置参数
     * @param key 参数名
     * @param value 参数值
     */
    template<typename T>
    void setParameter(const std::string& key, T&& value) {
        std::lock_guard<std::mutex> lock(mParamsMutex);
        mParams[key] = std::forward<T>(value);
        onParameterChanged(key);
    }
    
    /**
     * @brief 获取参数
     * @param key 参数名
     * @return 参数值，如果不存在返回空optional
     */
    template<typename T>
    std::optional<T> getParameter(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mParamsMutex);
        auto it = mParams.find(key);
        if (it != mParams.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (const std::bad_any_cast&) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }
    
    /**
     * @brief 验证配置
     * @return 是否有效
     */
    virtual bool validate() const { return true; }
    
    // ==========================================================================
    // 回调
    // ==========================================================================
    
    /**
     * @brief 设置状态变更回调
     */
    void setStateCallback(EntityCallback callback) { mStateCallback = std::move(callback); }
    
    /**
     * @brief 设置错误回调
     */
    void setErrorCallback(ErrorCallback callback) { mErrorCallback = std::move(callback); }
    
    // ==========================================================================
    // 性能统计
    // ==========================================================================
    
    /**
     * @brief 获取上次处理耗时（微秒）
     */
    uint64_t getLastProcessDuration() const { return mLastProcessDuration; }
    
    /**
     * @brief 获取平均处理耗时（微秒）
     */
    uint64_t getAverageProcessDuration() const;
    
    /**
     * @brief 重置统计数据
     */
    void resetStatistics();
    
protected:
    // ==========================================================================
    // 子类实现接口
    // ==========================================================================
    
    /**
     * @brief 准备阶段（获取资源、初始化）
     * @param context 管线上下文
     * @return 是否成功
     */
    virtual bool prepare(PipelineContext& context) { return true; }
    
    /**
     * @brief 核心处理逻辑（子类必须实现）
     * @param inputs 输入数据包列表
     * @param outputs 输出数据包列表（由子类填充）
     * @param context 管线上下文
     * @return 是否成功
     */
    virtual bool process(const std::vector<FramePacketPtr>& inputs,
                        std::vector<FramePacketPtr>& outputs,
                        PipelineContext& context) = 0;
    
    /**
     * @brief 完成阶段（清理、发送）
     * @param context 管线上下文
     */
    virtual void finalize(PipelineContext& context) {}
    
    /**
     * @brief 参数变更回调
     * @param key 变更的参数名
     */
    virtual void onParameterChanged(const std::string& key) {}
    
    /**
     * @brief 状态变更回调
     * @param oldState 旧状态
     * @param newState 新状态
     */
    virtual void onStateChanged(EntityState oldState, EntityState newState) {}
    
    // ==========================================================================
    // 辅助方法
    // ==========================================================================
    
    /**
     * @brief 设置状态
     */
    void setState(EntityState state);
    
    /**
     * @brief 设置错误状态并记录消息
     */
    void setError(const std::string& message);
    
    /**
     * @brief 获取默认输入端口的数据包
     */
    FramePacketPtr getDefaultInput() const;
    
    /**
     * @brief 设置默认输出端口的数据包
     */
    void setDefaultOutput(FramePacketPtr packet);
    
    /**
     * @brief 收集所有输入数据包
     */
    std::vector<FramePacketPtr> collectInputs() const;
    
    /**
     * @brief 发送所有输出
     */
    void sendOutputs();
    
private:
    // 身份信息
    EntityId mId;
    std::string mName;
    static std::atomic<EntityId> sNextId;
    
    // 状态
    std::atomic<EntityState> mState{EntityState::Idle};
    std::atomic<bool> mEnabled{true};
    std::atomic<bool> mCancelled{false};
    std::string mErrorMessage;
    
    // 端口
    std::vector<std::unique_ptr<InputPort>> mInputPorts;
    std::vector<std::unique_ptr<OutputPort>> mOutputPorts;
    mutable std::mutex mPortsMutex;
    
    // 参数
    mutable std::mutex mParamsMutex;
    std::unordered_map<std::string, std::any> mParams;
    
    // 回调
    EntityCallback mStateCallback;
    ErrorCallback mErrorCallback;
    
    // 性能统计
    std::atomic<uint64_t> mLastProcessDuration{0};
    std::atomic<uint64_t> mTotalProcessDuration{0};
    std::atomic<uint32_t> mProcessCount{0};
};

} // namespace pipeline
