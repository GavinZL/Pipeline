/**
 * @file MergeEntity.cpp
 * @brief MergeEntity 实现
 */

#include "pipeline/entity/MergeEntity.h"
#include "pipeline/data/FramePacket.h"
#include "pipeline/core/PipelineConfig.h"

namespace pipeline {

// =============================================================================
// 构造与析构
// =============================================================================

MergeEntity::MergeEntity(const std::string& name)
    : ProcessEntity(name) {
    initializePorts();
    initializeSynchronizer();
}

MergeEntity::~MergeEntity() = default;

// =============================================================================
// 端口初始化
// =============================================================================

void MergeEntity::initializePorts() {
    // 双路输入端口
    addInputPort(MERGE_GPU_INPUT_PORT);
    addInputPort(MERGE_CPU_INPUT_PORT);
    
    // 合并后输出端口
    addOutputPort(MERGE_OUTPUT_PORT);
}

void MergeEntity::initializeSynchronizer() {
    mSynchronizer = std::make_shared<input::FrameSynchronizer>();
    
    // 配置同步器
    input::FrameSyncConfig syncConfig;
    syncConfig.policy = input::SyncPolicy::WaitBoth;
    syncConfig.maxWaitTimeMs = mConfig.maxWaitTimeMs;
    syncConfig.timestampToleranceUs = mConfig.timestampToleranceUs;
    mSynchronizer->configure(syncConfig);
}

// =============================================================================
// 配置
// =============================================================================

void MergeEntity::configure(const MergeConfig& config) {
    std::lock_guard<std::mutex> lock(mMergeMutex);
    mConfig = config;
    
    // 更新同步器配置
    if (mSynchronizer) {
        input::FrameSyncConfig syncConfig;
        
        switch (config.strategy) {
            case MergeStrategy::WaitBoth:
                syncConfig.policy = input::SyncPolicy::WaitBoth;
                break;
            case MergeStrategy::GPUPriority:
                syncConfig.policy = input::SyncPolicy::GPUFirst;
                break;
            case MergeStrategy::CPUPriority:
                syncConfig.policy = input::SyncPolicy::CPUFirst;
                break;
            case MergeStrategy::Latest:
                syncConfig.policy = input::SyncPolicy::DropOld;
                break;
        }
        
        syncConfig.maxWaitTimeMs = config.maxWaitTimeMs;
        syncConfig.timestampToleranceUs = config.timestampToleranceUs;
        mSynchronizer->configure(syncConfig);
    }
}

void MergeEntity::setMergeCallback(MergeCallback callback) {
    std::lock_guard<std::mutex> lock(mMergeMutex);
    mMergeCallback = std::move(callback);
}

// =============================================================================
// ProcessEntity 生命周期
// =============================================================================

bool MergeEntity::prepare(PipelineContext& context) {
    // 重置同步器
    if (mSynchronizer) {
        mSynchronizer->reset();
    }
    return true;
}

bool MergeEntity::process(const std::vector<FramePacketPtr>& inputs,
                          std::vector<FramePacketPtr>& outputs,
                          PipelineContext& context) {
    // 🔥 关键设计: MergeEntity不直接等待
    // 而是检查FrameSynchronizer是否有已同步的帧
    
    if (!mSynchronizer) {
        return false;
    }
    
    // 尝试获取已同步的帧 (非阻塞)
    auto syncedFrame = mSynchronizer->tryGetSyncedFrame();
    
    if (!syncedFrame) {
        // 没有已同步的帧,说明还在等待其他路
        // 🔥 关键: 返回false,不生成输出
        // PipelineExecutor会知道此Entity未完成,不投递下游任务
        return false;
    }
    
    // 有已同步的帧,创建合并输出
    MergedFrame merged;
    merged.gpuResult = syncedFrame->gpuFrame;
    merged.cpuResult = syncedFrame->cpuFrame;
    merged.timestamp = syncedFrame->timestamp;
    merged.hasGPU = syncedFrame->hasGPU;
    merged.hasCPU = syncedFrame->hasCPU;
    
    // 创建合并后的输出包
    auto outputPacket = createMergedPacket(merged);
    if (outputPacket) {
        outputs.push_back(outputPacket);
        
        // 设置到输出端口
        auto* outPort = getOutputPort(MERGE_OUTPUT_PORT);
        if (outPort) {
            outPort->setPacket(outputPacket);
        }
    }
    
    // 触发回调
    if (mMergeCallback) {
        auto mergedPtr = std::make_shared<MergedFrame>(merged);
        mMergeCallback(mergedPtr);
    }
    
    ++mMergedFrameCount;
    return true;
}

void MergeEntity::finalize(PipelineContext& context) {
    sendOutputs();
}

void MergeEntity::resetForNextFrame() {
    ProcessEntity::resetForNextFrame();
    // 异步任务链架构: 不需要重置临时存储
    // FrameSynchronizer自动管理帧数据
}

// =============================================================================
// 内部方法
// =============================================================================

void MergeEntity::processGPUInput(FramePacketPtr packet) {
    // 🔥 异步任务链架构: 此方法已废弃
    // 上游Entity应直接调用 getSynchronizer()->pushGPUFrame()
    
    if (!packet || !mSynchronizer) {
        return;
    }
    
    ++mGPUFrameCount;
    
    int64_t timestamp = packet->getTimestamp();
    mSynchronizer->pushGPUFrame(packet, timestamp);
}

void MergeEntity::processCPUInput(FramePacketPtr packet) {
    // 🔥 异步任务链架构: 此方法已废弃
    // 上游Entity应直接调用 getSynchronizer()->pushCPUFrame()
    
    if (!packet || !mSynchronizer) {
        return;
    }
    
    ++mCPUFrameCount;
    
    int64_t timestamp = packet->getTimestamp();
    mSynchronizer->pushCPUFrame(packet, timestamp);
}

FramePacketPtr MergeEntity::createMergedPacket(const MergedFrame& frame) {
    auto packet = std::make_shared<FramePacket>();
    packet->setTimestamp(frame.timestamp);
    
    // 优先使用 GPU 结果的纹理
    if (frame.hasGPU && frame.gpuResult) {
        packet->setTexture(frame.gpuResult->getTexture());
        packet->setSize(frame.gpuResult->getWidth(), frame.gpuResult->getHeight());
        packet->setFormat(frame.gpuResult->getFormat());
    }
    
    // 合并 CPU 数据
    if (frame.hasCPU && frame.cpuResult) {
        const uint8_t* cpuData = frame.cpuResult->getCpuBufferNoLoad();
        if (cpuData && mConfig.copyCPUData) {
            // 复制 CPU 数据到输出包
            // packet->setCpuBuffer(cpuData, size);
        }
        
        // 如果没有 GPU 数据，使用 CPU 数据的尺寸
        if (!frame.hasGPU) {
            packet->setSize(frame.cpuResult->getWidth(), frame.cpuResult->getHeight());
            packet->setFormat(frame.cpuResult->getFormat());
        }
    }
    
    // 添加元数据标记
    packet->setMetadata("merged", true);
    packet->setMetadata("hasGPU", frame.hasGPU);
    packet->setMetadata("hasCPU", frame.hasCPU);
    
    return packet;
}

} // namespace pipeline
