/**
 * @file ProcessEntity.cpp
 * @brief ProcessEntity实现
 */

#include "pipeline/entity/ProcessEntity.h"
#include "pipeline/core/PipelineConfig.h"
#include <chrono>
#include <algorithm>

namespace pipeline {

// 静态成员初始化
std::atomic<EntityId> ProcessEntity::sNextId{1000};

ProcessEntity::ProcessEntity(const std::string& name)
    : mId(sNextId.fetch_add(1))
    , mName(name.empty() ? "Entity_" + std::to_string(mId) : name)
{
}

ProcessEntity::~ProcessEntity() = default;

// =============================================================================
// 端口管理
// =============================================================================

InputPort* ProcessEntity::addInputPort(const std::string& name) {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    
    // 检查是否已存在同名端口
    for (const auto& port : mInputPorts) {
        if (port->getName() == name) {
            return port.get();
        }
    }
    
    auto port = std::make_unique<InputPort>(name);
    port->setOwnerId(mId);
    InputPort* ptr = port.get();
    mInputPorts.push_back(std::move(port));
    return ptr;
}

OutputPort* ProcessEntity::addOutputPort(const std::string& name) {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    
    // 检查是否已存在同名端口
    for (const auto& port : mOutputPorts) {
        if (port->getName() == name) {
            return port.get();
        }
    }
    
    auto port = std::make_unique<OutputPort>(name);
    port->setOwnerId(mId);
    OutputPort* ptr = port.get();
    mOutputPorts.push_back(std::move(port));
    return ptr;
}

InputPort* ProcessEntity::getInputPort(size_t index) const {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    if (index < mInputPorts.size()) {
        return mInputPorts[index].get();
    }
    return nullptr;
}

InputPort* ProcessEntity::getInputPort(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    for (const auto& port : mInputPorts) {
        if (port->getName() == name) {
            return port.get();
        }
    }
    return nullptr;
}

OutputPort* ProcessEntity::getOutputPort(size_t index) const {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    if (index < mOutputPorts.size()) {
        return mOutputPorts[index].get();
    }
    return nullptr;
}

OutputPort* ProcessEntity::getOutputPort(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    for (const auto& port : mOutputPorts) {
        if (port->getName() == name) {
            return port.get();
        }
    }
    return nullptr;
}

// =============================================================================
// 依赖管理
// =============================================================================

bool ProcessEntity::areInputsReady() const {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    
    for (const auto& port : mInputPorts) {
        if (port->isConnected() && !port->isReady()) {
            return false;
        }
    }
    return true;
}

bool ProcessEntity::waitInputsReady(int64_t timeoutMs) {
    auto startTime = std::chrono::steady_clock::now();
    
    for (auto& port : mInputPorts) {
        if (!port->isConnected()) {
            continue;
        }
        
        int64_t remainingTime = timeoutMs;
        if (timeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            remainingTime = timeoutMs - elapsed;
            if (remainingTime <= 0) {
                return false;
            }
        }
        
        if (!port->waitReady(remainingTime)) {
            return false;
        }
    }
    
    return true;
}

size_t ProcessEntity::getPendingInputCount() const {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    
    size_t count = 0;
    for (const auto& port : mInputPorts) {
        if (port->isConnected() && !port->isReady()) {
            ++count;
        }
    }
    return count;
}

// =============================================================================
// 执行流程
// =============================================================================

bool ProcessEntity::execute(PipelineContext& context) {
    // 检查是否启用
    if (!mEnabled.load()) {
        setState(EntityState::Completed);
        return true;
    }
    
    // 检查是否被取消
    if (mCancelled.load()) {
        setState(EntityState::Error);
        return false;
    }
    
    // 🔥 异步任务链兼容: 不阻塞等待输入
    // InputPort的数据应该在上游Entity完成时已经ready
    if (!areInputsReady()) {
        setState(EntityState::Blocked);
        return false;  // 输入未就绪，返回false
    }
    
    // 准备阶段
    setState(EntityState::Ready);
    if (!prepare(context)) {
        setError("Prepare failed");
        return false;
    }
    
    // 处理阶段
    setState(EntityState::Processing);
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // 🔥 Step 1: 从InputPort收集输入
    auto inputs = collectInputs();
    std::vector<FramePacketPtr> outputs;
    
    // 🔥 Step 2: 调用子类的process
    bool success = process(inputs, outputs, context);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        endTime - startTime).count();
    
    // 更新统计
    mLastProcessDuration.store(duration);
    mTotalProcessDuration.fetch_add(duration);
    mProcessCount.fetch_add(1);
    
    if (!success) {
        setError("Process failed");
        return false;
    }
    
    // 🔥 Step 3: 将输出写入OutputPort
    {
        std::lock_guard<std::mutex> lock(mPortsMutex);
        for (size_t i = 0; i < outputs.size() && i < mOutputPorts.size(); ++i) {
            mOutputPorts[i]->setPacket(outputs[i]);
        }
    }
    
    // 完成阶段
    finalize(context);
    
    // 发送输出到下游InputPort
    sendOutputs();
    
    setState(EntityState::Completed);
    return true;
}

void ProcessEntity::cancel() {
    mCancelled.store(true);
}

void ProcessEntity::resetForNextFrame() {
    mCancelled.store(false);
    setState(EntityState::Idle);
    
    // 重置端口
    std::lock_guard<std::mutex> lock(mPortsMutex);
    for (auto& port : mInputPorts) {
        port->reset();
    }
    for (auto& port : mOutputPorts) {
        port->reset();
    }
}

// =============================================================================
// 配置
// =============================================================================

void ProcessEntity::configure(const EntityConfig& config) {
    setName(config.name);
    setEnabled(config.enabled);
    
    std::lock_guard<std::mutex> lock(mParamsMutex);
    for (const auto& [key, value] : config.params) {
        mParams[key] = value;
        onParameterChanged(key);
    }
}

// =============================================================================
// 统计
// =============================================================================

uint64_t ProcessEntity::getAverageProcessDuration() const {
    uint32_t count = mProcessCount.load();
    if (count == 0) {
        return 0;
    }
    return mTotalProcessDuration.load() / count;
}

void ProcessEntity::resetStatistics() {
    mLastProcessDuration.store(0);
    mTotalProcessDuration.store(0);
    mProcessCount.store(0);
}

// =============================================================================
// 辅助方法
// =============================================================================

void ProcessEntity::setState(EntityState state) {
    EntityState oldState = mState.exchange(state);
    if (oldState != state) {
        onStateChanged(oldState, state);
        if (mStateCallback) {
            mStateCallback(mId, state);
        }
    }
}

void ProcessEntity::setError(const std::string& message) {
    mErrorMessage = message;
    setState(EntityState::Error);
    if (mErrorCallback) {
        mErrorCallback(mId, message);
    }
}

FramePacketPtr ProcessEntity::getDefaultInput() const {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    if (!mInputPorts.empty()) {
        return mInputPorts[0]->getPacket();
    }
    return nullptr;
}

void ProcessEntity::setDefaultOutput(FramePacketPtr packet) {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    if (!mOutputPorts.empty()) {
        mOutputPorts[0]->setPacket(packet);
    }
}

std::vector<FramePacketPtr> ProcessEntity::collectInputs() const {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    
    std::vector<FramePacketPtr> inputs;
    inputs.reserve(mInputPorts.size());
    
    for (const auto& port : mInputPorts) {
        inputs.push_back(port->getPacket());
    }
    
    return inputs;
}

void ProcessEntity::sendOutputs() {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    
    for (auto& port : mOutputPorts) {
        port->send();
    }
}

} // namespace pipeline
