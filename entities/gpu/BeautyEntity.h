/**
 * @file BeautyEntity.h
 * @brief 美颜Entity - 磨皮、美白等美颜效果
 */

#pragma once

#include "pipeline/entity/GPUEntity.h"

namespace pipeline {

/**
 * @brief 美颜算法类型
 */
enum class BeautyAlgorithm : uint8_t {
    Bilateral,       // 双边滤波
    Gaussian,        // 高斯模糊
    Surface,         // 表面模糊
    HighPass         // 高通滤波
};

/**
 * @brief 美颜Entity
 * 
 * 提供磨皮、美白、红润等美颜效果。
 * 
 * 算法原理：
 * 1. 磨皮：使用双边滤波或表面模糊保留边缘的同时平滑皮肤
 * 2. 美白：调整亮度和对比度
 * 3. 红润：调整肤色色调
 * 
 * 使用方式：
 * 1. 创建BeautyEntity
 * 2. 设置美颜强度
 * 3. 可选：提供人脸检测结果用于局部美颜
 */
class BeautyEntity : public GPUEntity {
public:
    /**
     * @brief 构造函数
     * @param name Entity名称
     */
    explicit BeautyEntity(const std::string& name = "BeautyEntity");
    
    ~BeautyEntity() override;
    
    // ==========================================================================
    // 磨皮参数
    // ==========================================================================
    
    /**
     * @brief 设置磨皮强度
     * @param level 强度（0-1，0为不磨皮）
     */
    void setSmoothLevel(float level);
    
    /**
     * @brief 获取磨皮强度
     */
    float getSmoothLevel() const { return mSmoothLevel; }
    
    /**
     * @brief 设置磨皮算法
     */
    void setSmoothAlgorithm(BeautyAlgorithm algorithm);
    
    /**
     * @brief 获取磨皮算法
     */
    BeautyAlgorithm getSmoothAlgorithm() const { return mSmoothAlgorithm; }
    
    /**
     * @brief 设置磨皮半径
     * @param radius 半径（像素）
     */
    void setSmoothRadius(float radius);
    
    // ==========================================================================
    // 美白参数
    // ==========================================================================
    
    /**
     * @brief 设置美白强度
     * @param level 强度（0-1，0为不美白）
     */
    void setWhitenLevel(float level);
    
    /**
     * @brief 获取美白强度
     */
    float getWhitenLevel() const { return mWhitenLevel; }
    
    // ==========================================================================
    // 红润参数
    // ==========================================================================
    
    /**
     * @brief 设置红润强度
     * @param level 强度（0-1，0为不红润）
     */
    void setRuddyLevel(float level);
    
    /**
     * @brief 获取红润强度
     */
    float getRuddyLevel() const { return mRuddyLevel; }
    
    // ==========================================================================
    // 锐化参数
    // ==========================================================================
    
    /**
     * @brief 设置锐化强度
     * @param level 强度（0-1，0为不锐化）
     */
    void setSharpenLevel(float level);
    
    /**
     * @brief 获取锐化强度
     */
    float getSharpenLevel() const { return mSharpenLevel; }
    
    // ==========================================================================
    // 大眼瘦脸参数
    // ==========================================================================
    
    /**
     * @brief 设置大眼强度
     * @param level 强度（0-1）
     */
    void setEyeEnlargeLevel(float level);
    
    /**
     * @brief 设置瘦脸强度
     * @param level 强度（0-1）
     */
    void setFaceSlimLevel(float level);
    
    // ==========================================================================
    // 人脸信息
    // ==========================================================================
    
    /**
     * @brief 设置是否使用人脸信息
     * 
     * 如果启用，将从FramePacket的metadata中读取人脸检测结果，
     * 只对人脸区域应用美颜效果。
     */
    void setUseFaceDetection(bool use) { mUseFaceDetection = use; }
    
    /**
     * @brief 获取是否使用人脸信息
     */
    bool getUseFaceDetection() const { return mUseFaceDetection; }
    
    /**
     * @brief 设置人脸区域元数据键名
     */
    void setFaceMetadataKey(const std::string& key) { mFaceMetadataKey = key; }
    
    // ==========================================================================
    // 预设
    // ==========================================================================
    
    /**
     * @brief 设置预设
     * @param presetName 预设名称（"natural", "clear", "goddess"等）
     */
    void setPreset(const std::string& presetName);
    
    /**
     * @brief 重置所有参数到默认值
     */
    void reset();
    
protected:
    bool setupShader() override;
    void setUniforms(FramePacket* input) override;
    bool processGPU(const std::vector<FramePacketPtr>& inputs, 
                   FramePacketPtr output) override;
    
    void onParameterChanged(const std::string& key) override;
    
private:
    /**
     * @brief 创建模糊纹理
     */
    bool createBlurTextures();
    
    /**
     * @brief 执行双边滤波
     */
    void performBilateralFilter(std::shared_ptr<lrengine::render::LRTexture> input,
                               std::shared_ptr<lrengine::render::LRTexture> output);
    
    /**
     * @brief 生成高斯权重
     */
    void generateGaussianWeights(float sigma, std::vector<float>& weights);
    
    /**
     * @brief 从metadata读取人脸信息
     */
    bool readFaceInfo(FramePacket* packet);
    
private:
    // 磨皮参数
    float mSmoothLevel = 0.5f;
    float mSmoothRadius = 7.0f;
    BeautyAlgorithm mSmoothAlgorithm = BeautyAlgorithm::Bilateral;
    
    // 美白参数
    float mWhitenLevel = 0.3f;
    
    // 红润参数
    float mRuddyLevel = 0.2f;
    
    // 锐化参数
    float mSharpenLevel = 0.0f;
    
    // 大眼瘦脸参数
    float mEyeEnlargeLevel = 0.0f;
    float mFaceSlimLevel = 0.0f;
    
    // 人脸检测
    bool mUseFaceDetection = false;
    std::string mFaceMetadataKey = "face_landmarks";
    
    // 当前人脸信息
    struct FaceInfo {
        float boundingBox[4] = {0, 0, 1, 1}; // x, y, width, height (normalized)
        bool valid = false;
    };
    FaceInfo mCurrentFace;
    
    // 中间纹理
    std::shared_ptr<lrengine::render::LRTexture> mBlurTexture1;
    std::shared_ptr<lrengine::render::LRTexture> mBlurTexture2;
    std::shared_ptr<lrengine::render::LRFrameBuffer> mBlurFBO1;
    std::shared_ptr<lrengine::render::LRFrameBuffer> mBlurFBO2;
    
    // 着色器（多Pass）
    std::shared_ptr<lrengine::render::LRShaderProgram> mBilateralShader;
    std::shared_ptr<lrengine::render::LRShaderProgram> mSharpenShader;
    std::shared_ptr<lrengine::render::LRShaderProgram> mBeautyBlendShader;
    
    // Uniform位置
    int32_t mSmoothLevelLocation = -1;
    int32_t mWhitenLevelLocation = -1;
    int32_t mRuddyLevelLocation = -1;
    int32_t mSharpenLevelLocation = -1;
    int32_t mTexelSizeLocation = -1;
    int32_t mFaceBoundsLocation = -1;
};

} // namespace pipeline
