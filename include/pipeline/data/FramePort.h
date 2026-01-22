/**
 * @file FramePort.h
 * @brief 数据端口 - Entity的输入输出接口
 */

#pragma once

#include "EntityTypes.h"
#include "FramePacket.h"
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>

// 前向声明TaskQueue的Consumable
namespace task {
class Consumable;
} // namespace task

namespace pipeline {

/**
 * @brief 端口基类
 */
class FramePort {
public:
    /**
     * @brief 构造函数
     * @param name 端口名称
     */
    explicit FramePort(const std::string& name);
    
    virtual ~FramePort() = default;
    
    /**
     * @brief 获取端口名称
     */
    const std::string& getName() const { return mName; }
    
    /**
     * @brief 获取所属Entity ID
     */
    EntityId getOwnerId() const { return mOwnerId; }
    
    /**
     * @brief 设置所属Entity ID
     */
    void setOwnerId(EntityId id) { mOwnerId = id; }
    
    /**
     * @brief 检查端口是否已连接
     */
    virtual bool isConnected() const = 0;
    
    /**
     * @brief 重置端口状态
     */
    virtual void reset() = 0;
    
protected:
    std::string mName;
    EntityId mOwnerId = InvalidEntityId;
};

/**
 * @brief 输入端口
 * 
 * 接收上游Entity的数据，支持等待数据就绪。
 */
class InputPort : public FramePort {
public:
    /**
     * @brief 构造函数
     * @param name 端口名称
     */
    explicit InputPort(const std::string& name);
    
    ~InputPort() override;
    
    // ==========================================================================
    // 连接管理
    // ==========================================================================
    
    /**
     * @brief 检查是否已连接
     */
    bool isConnected() const override;
    
    /**
     * @brief 获取连接的源Entity ID
     */
    EntityId getSourceEntityId() const { return mSourceEntityId; }
    
    /**
     * @brief 获取连接的源端口名称
     */
    const std::string& getSourcePortName() const { return mSourcePortName; }
    
    /**
     * @brief 设置连接源
     * @param entityId 源Entity ID
     * @param portName 源端口名称
     */
    void setSource(EntityId entityId, const std::string& portName);
    
    /**
     * @brief 断开连接
     */
    void disconnect();
    
    // ==========================================================================
    // 数据接收
    // ==========================================================================
    
    /**
     * @brief 设置接收的数据包
     * @param packet 数据包
     */
    void setPacket(FramePacketPtr packet);
    
    /**
     * @brief 获取数据包
     */
    FramePacketPtr getPacket() const;
    
    /**
     * @brief 检查数据是否就绪
     */
    bool isReady() const { return mReady.load(); }
    
    /**
     * @brief 等待数据就绪
     * @param timeoutMs 超时时间（毫秒），-1表示无限等待
     * @return 是否成功等待
     */
    bool waitReady(int64_t timeoutMs = -1);
    
    /**
     * @brief 标记数据就绪
     */
    void markReady();
    
    /**
     * @brief 重置端口状态
     */
    void reset() override;
    
    // ==========================================================================
    // 同步信号
    // ==========================================================================
    
    /**
     * @brief 设置就绪信号（Consumable）
     */
    void setReadySignal(std::shared_ptr<task::Consumable> signal);
    
    /**
     * @brief 获取就绪信号
     */
    std::shared_ptr<task::Consumable> getReadySignal() const { return mReadySignal; }
    
private:
    // 连接信息
    EntityId mSourceEntityId = InvalidEntityId;
    std::string mSourcePortName;
    
    // 数据
    mutable std::mutex mPacketMutex;
    FramePacketPtr mPacket;
    
    // 就绪状态
    std::atomic<bool> mReady{false};
    std::mutex mWaitMutex;
    std::condition_variable mWaitCond;
    
    // 同步信号
    std::shared_ptr<task::Consumable> mReadySignal;
};

/**
 * @brief 输出端口
 * 
 * 向下游Entity发送数据，支持多个连接。
 */
class OutputPort : public FramePort {
public:
    /**
     * @brief 构造函数
     * @param name 端口名称
     */
    explicit OutputPort(const std::string& name);
    
    ~OutputPort() override;
    
    // ==========================================================================
    // 连接管理
    // ==========================================================================
    
    /**
     * @brief 检查是否已连接
     */
    bool isConnected() const override;
    
    /**
     * @brief 添加连接的输入端口
     * @param input 输入端口指针
     */
    void addConnection(InputPort* input);
    
    /**
     * @brief 移除连接
     * @param input 输入端口指针
     */
    void removeConnection(InputPort* input);
    
    /**
     * @brief 获取所有连接的输入端口
     */
    const std::vector<InputPort*>& getConnections() const { return mConnectedInputs; }
    
    /**
     * @brief 获取连接数量
     */
    size_t getConnectionCount() const { return mConnectedInputs.size(); }
    
    /**
     * @brief 断开所有连接
     */
    void disconnectAll();
    
    // ==========================================================================
    // 数据发送
    // ==========================================================================
    
    /**
     * @brief 设置输出数据包
     * @param packet 数据包
     */
    void setPacket(FramePacketPtr packet);
    
    /**
     * @brief 获取输出数据包
     */
    FramePacketPtr getPacket() const;
    
    /**
     * @brief 发送数据到所有连接的输入端口
     * 
     * 将数据包发送到所有已连接的下游InputPort，
     * 并标记它们为就绪状态。
     */
    void send();
    
    /**
     * @brief 检查是否已发送
     */
    bool isSent() const { return mSent.load(); }
    
    /**
     * @brief 重置端口状态
     */
    void reset() override;
    
    // ==========================================================================
    // 同步信号
    // ==========================================================================
    
    /**
     * @brief 设置完成信号（Consumable）
     */
    void setCompletionSignal(std::shared_ptr<task::Consumable> signal);
    
    /**
     * @brief 获取完成信号
     */
    std::shared_ptr<task::Consumable> getCompletionSignal() const { return mCompletionSignal; }
    
    /**
     * @brief 发出完成信号
     */
    void signalCompletion();
    
private:
    // 连接的输入端口
    mutable std::mutex mConnectionsMutex;
    std::vector<InputPort*> mConnectedInputs;
    
    // 数据
    mutable std::mutex mPacketMutex;
    FramePacketPtr mPacket;
    
    // 状态
    std::atomic<bool> mSent{false};
    
    // 同步信号
    std::shared_ptr<task::Consumable> mCompletionSignal;
};

} // namespace pipeline
