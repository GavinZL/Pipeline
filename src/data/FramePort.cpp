/**
 * @file FramePort.cpp
 * @brief FramePort实现
 */

#include "pipeline/data/FramePort.h"
#include "backend/Consumable.h"
#include <algorithm>

namespace pipeline {

// =============================================================================
// FramePort
// =============================================================================

FramePort::FramePort(const std::string& name)
    : mName(name)
{
}

// =============================================================================
// InputPort
// =============================================================================

InputPort::InputPort(const std::string& name)
    : FramePort(name)
{
}

InputPort::~InputPort() = default;

bool InputPort::isConnected() const {
    return mSourceEntityId != InvalidEntityId;
}

void InputPort::setSource(EntityId entityId, const std::string& portName) {
    mSourceEntityId = entityId;
    mSourcePortName = portName;
}

void InputPort::disconnect() {
    mSourceEntityId = InvalidEntityId;
    mSourcePortName.clear();
    reset();
}

void InputPort::setPacket(FramePacketPtr packet) {
    std::lock_guard<std::mutex> lock(mPacketMutex);
    mPacket = std::move(packet);
}

FramePacketPtr InputPort::getPacket() const {
    std::lock_guard<std::mutex> lock(mPacketMutex);
    return mPacket;
}

bool InputPort::waitReady(int64_t timeoutMs) {
    std::unique_lock<std::mutex> lock(mWaitMutex);
    
    if (mReady.load()) {
        return true;
    }
    
    if (timeoutMs < 0) {
        // 无限等待
        mWaitCond.wait(lock, [this] { return mReady.load(); });
        return true;
    } else {
        // 带超时等待
        return mWaitCond.wait_for(lock, 
            std::chrono::milliseconds(timeoutMs),
            [this] { return mReady.load(); });
    }
}

void InputPort::markReady() {
    {
        std::lock_guard<std::mutex> lock(mWaitMutex);
        mReady.store(true);
    }
    mWaitCond.notify_all();
}

void InputPort::reset() {
    {
        std::lock_guard<std::mutex> lock(mPacketMutex);
        mPacket.reset();
    }
    
    {
        std::lock_guard<std::mutex> lock(mWaitMutex);
        mReady.store(false);
    }
}

void InputPort::setReadySignal(std::shared_ptr<task::Consumable> signal) {
    mReadySignal = std::move(signal);
}

// =============================================================================
// OutputPort
// =============================================================================

OutputPort::OutputPort(const std::string& name)
    : FramePort(name)
{
}

OutputPort::~OutputPort() = default;

bool OutputPort::isConnected() const {
    std::lock_guard<std::mutex> lock(mConnectionsMutex);
    return !mConnectedInputs.empty();
}

void OutputPort::addConnection(InputPort* input) {
    if (!input) return;
    
    std::lock_guard<std::mutex> lock(mConnectionsMutex);
    
    // 检查是否已存在
    auto it = std::find(mConnectedInputs.begin(), mConnectedInputs.end(), input);
    if (it == mConnectedInputs.end()) {
        mConnectedInputs.push_back(input);
    }
}

void OutputPort::removeConnection(InputPort* input) {
    std::lock_guard<std::mutex> lock(mConnectionsMutex);
    
    auto it = std::find(mConnectedInputs.begin(), mConnectedInputs.end(), input);
    if (it != mConnectedInputs.end()) {
        mConnectedInputs.erase(it);
    }
}

void OutputPort::disconnectAll() {
    std::lock_guard<std::mutex> lock(mConnectionsMutex);
    
    for (auto* input : mConnectedInputs) {
        if (input) {
            input->disconnect();
        }
    }
    mConnectedInputs.clear();
}

void OutputPort::setPacket(FramePacketPtr packet) {
    std::lock_guard<std::mutex> lock(mPacketMutex);
    mPacket = std::move(packet);
}

FramePacketPtr OutputPort::getPacket() const {
    std::lock_guard<std::mutex> lock(mPacketMutex);
    return mPacket;
}

void OutputPort::send() {
    FramePacketPtr packetToSend;
    {
        std::lock_guard<std::mutex> lock(mPacketMutex);
        packetToSend = mPacket;
    }
    
    if (!packetToSend) {
        return;
    }
    
    std::vector<InputPort*> connections;
    {
        std::lock_guard<std::mutex> lock(mConnectionsMutex);
        connections = mConnectedInputs;
    }
    
    // 发送到所有连接的输入端口
    for (auto* input : connections) {
        if (input) {
            input->setPacket(packetToSend);
            input->markReady();
        }
    }
    
    mSent.store(true);
    
    // 发出完成信号
    signalCompletion();
}

void OutputPort::reset() {
    {
        std::lock_guard<std::mutex> lock(mPacketMutex);
        mPacket.reset();
    }
    mSent.store(false);
}

void OutputPort::setCompletionSignal(std::shared_ptr<task::Consumable> signal) {
    mCompletionSignal = std::move(signal);
}

void OutputPort::signalCompletion() {
    if (mCompletionSignal) {
        mCompletionSignal->release();
    }
}

} // namespace pipeline
