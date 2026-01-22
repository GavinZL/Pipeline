/**
 * @file FaceDetectionEntity.h
 * @brief 人脸检测Entity - CPU端人脸检测算法
 */

#pragma once

#include "pipeline/entity/CPUEntity.h"
#include <vector>

namespace pipeline {

/**
 * @brief 人脸关键点
 */
struct FaceLandmark {
    float x;
    float y;
};

/**
 * @brief 人脸信息
 */
struct FaceInfo {
    // 边界框（归一化坐标0-1）
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
    
    // 置信度
    float confidence = 0;
    
    // 关键点（可选）
    std::vector<FaceLandmark> landmarks;
    
    // 人脸角度（可选）
    float roll = 0;    // 横滚角
    float pitch = 0;   // 俯仰角
    float yaw = 0;     // 偏航角
};

/**
 * @brief 人脸检测结果
 */
struct FaceDetectionResult {
    std::vector<FaceInfo> faces;
    uint64_t timestamp = 0;
    uint32_t imageWidth = 0;
    uint32_t imageHeight = 0;
};

/**
 * @brief 人脸检测算法后端
 */
enum class FaceDetectorBackend : uint8_t {
    OpenCV,      // OpenCV Haar/LBP级联
    NCNN,        // NCNN神经网络推理
    TFLite,      // TensorFlow Lite
    Custom       // 自定义实现
};

/**
 * @brief 人脸检测Entity
 * 
 * 在CPU端执行人脸检测算法，将检测结果存入FramePacket的metadata。
 * 
 * 特点：
 * - 支持多种后端（OpenCV, NCNN, TFLite）
 * - 支持降采样处理以提高性能
 * - 检测结果可供下游美颜/贴纸Entity使用
 * 
 * 使用方式：
 * 1. 创建FaceDetectionEntity
 * 2. 配置检测参数
 * 3. 可选：加载自定义模型
 */
class FaceDetectionEntity : public CPUEntity {
public:
    /**
     * @brief 构造函数
     * @param name Entity名称
     */
    explicit FaceDetectionEntity(const std::string& name = "FaceDetectionEntity");
    
    ~FaceDetectionEntity() override;
    
    // ==========================================================================
    // 模型配置
    // ==========================================================================
    
    /**
     * @brief 设置检测后端
     */
    void setBackend(FaceDetectorBackend backend);
    
    /**
     * @brief 获取检测后端
     */
    FaceDetectorBackend getBackend() const { return mBackend; }
    
    /**
     * @brief 加载检测模型
     * @param modelPath 模型文件路径
     * @return 是否成功
     */
    bool loadModel(const std::string& modelPath);
    
    /**
     * @brief 加载关键点检测模型
     * @param modelPath 模型文件路径
     * @return 是否成功
     */
    bool loadLandmarkModel(const std::string& modelPath);
    
    // ==========================================================================
    // 检测参数
    // ==========================================================================
    
    /**
     * @brief 设置最小人脸尺寸（相对于图像短边的比例）
     * @param minSize 最小尺寸（0-1，如0.1表示图像短边的10%）
     */
    void setMinFaceSize(float minSize);
    
    /**
     * @brief 获取最小人脸尺寸
     */
    float getMinFaceSize() const { return mMinFaceSize; }
    
    /**
     * @brief 设置最大检测人脸数
     */
    void setMaxFaces(uint32_t maxFaces);
    
    /**
     * @brief 获取最大检测人脸数
     */
    uint32_t getMaxFaces() const { return mMaxFaces; }
    
    /**
     * @brief 设置检测置信度阈值
     * @param threshold 阈值（0-1）
     */
    void setConfidenceThreshold(float threshold);
    
    /**
     * @brief 获取检测置信度阈值
     */
    float getConfidenceThreshold() const { return mConfidenceThreshold; }
    
    /**
     * @brief 设置是否检测关键点
     */
    void setDetectLandmarks(bool detect);
    
    /**
     * @brief 获取是否检测关键点
     */
    bool getDetectLandmarks() const { return mDetectLandmarks; }
    
    /**
     * @brief 设置关键点数量（68, 98, 106等）
     */
    void setLandmarkCount(uint32_t count);
    
    // ==========================================================================
    // 性能配置
    // ==========================================================================
    
    /**
     * @brief 设置检测间隔帧数
     * 
     * 不是每帧都执行检测，而是间隔N帧检测一次，
     * 中间帧使用跟踪算法或直接复用上次结果。
     * @param interval 间隔帧数（1表示每帧检测）
     */
    void setDetectionInterval(uint32_t interval);
    
    /**
     * @brief 获取检测间隔
     */
    uint32_t getDetectionInterval() const { return mDetectionInterval; }
    
    /**
     * @brief 设置是否启用跟踪
     * 
     * 启用后，在非检测帧使用轻量级跟踪算法更新人脸位置。
     */
    void setTrackingEnabled(bool enabled);
    
    /**
     * @brief 获取是否启用跟踪
     */
    bool isTrackingEnabled() const { return mTrackingEnabled; }
    
    // ==========================================================================
    // 结果获取
    // ==========================================================================
    
    /**
     * @brief 获取最后一次检测结果
     */
    const FaceDetectionResult& getLastResult() const { return mLastResult; }
    
    /**
     * @brief 设置结果存储的metadata键名
     */
    void setResultMetadataKey(const std::string& key) { mResultMetadataKey = key; }
    
    /**
     * @brief 获取结果存储的metadata键名
     */
    const std::string& getResultMetadataKey() const { return mResultMetadataKey; }
    
protected:
    bool processOnCPU(const uint8_t* data,
                     uint32_t width,
                     uint32_t height,
                     uint32_t stride,
                     PixelFormat format,
                     std::unordered_map<std::string, std::any>& metadata) override;
    
    PixelFormat getRequiredFormat() const override { return PixelFormat::RGBA8; }
    
private:
    /**
     * @brief 执行人脸检测
     */
    bool detectFaces(const uint8_t* data,
                    uint32_t width, uint32_t height,
                    uint32_t stride,
                    std::vector<FaceInfo>& faces);
    
    /**
     * @brief 检测关键点
     */
    bool detectLandmarks(const uint8_t* data,
                        uint32_t width, uint32_t height,
                        uint32_t stride,
                        FaceInfo& face);
    
    /**
     * @brief 跟踪人脸
     */
    bool trackFaces(const uint8_t* data,
                   uint32_t width, uint32_t height,
                   uint32_t stride,
                   std::vector<FaceInfo>& faces);
    
    /**
     * @brief 转换为灰度图
     */
    void convertToGray(const uint8_t* rgba,
                      uint32_t width, uint32_t height,
                      uint32_t stride,
                      std::vector<uint8_t>& gray);
    
private:
    // 后端
    FaceDetectorBackend mBackend = FaceDetectorBackend::OpenCV;
    
    // 模型路径
    std::string mModelPath;
    std::string mLandmarkModelPath;
    bool mModelLoaded = false;
    
    // 检测参数
    float mMinFaceSize = 0.1f;
    uint32_t mMaxFaces = 5;
    float mConfidenceThreshold = 0.7f;
    bool mDetectLandmarks = false;
    uint32_t mLandmarkCount = 68;
    
    // 性能参数
    uint32_t mDetectionInterval = 3;
    bool mTrackingEnabled = true;
    
    // 状态
    uint32_t mFrameCounter = 0;
    FaceDetectionResult mLastResult;
    std::string mResultMetadataKey = "face_detection";
    
    // 临时缓冲
    std::vector<uint8_t> mGrayBuffer;
    std::vector<uint8_t> mScaledBuffer;
    
    // 后端实现（使用PIMPL模式隐藏具体实现）
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace pipeline
