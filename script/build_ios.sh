#!/bin/bash

################################################################################
# Pipeline iOS 构建脚本
# 
# 功能：
# - 为iOS设备（arm64）编译Pipeline库及其依赖（LREngine、TaskQueue）
# - 生成静态库（.a）和Framework（.framework）
# - 支持Debug和Release配置
# - 自动配置Metal后端
#
# 使用方法：
#   ./build_ios.sh [选项]
#
# 选项：
#   -c, --config <Debug|Release>  构建配置（默认：Release）
#   -t, --type <static|framework|all>  输出类型（默认：static）
#   -o, --output <路径>           输出目录（默认：./build/ios）
#   -h, --help                    显示帮助信息
#
# 示例：
#   ./build_ios.sh                          # 构建Release版本的静态库
#   ./build_ios.sh -c Debug                 # 构建Debug版本
#   ./build_ios.sh -t framework             # 构建Framework
################################################################################

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 默认参数
BUILD_CONFIG="Release"
BUILD_TYPE="static"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="${PROJECT_ROOT}/build/ios"
LIBRARY_NAME="Pipeline"
VERSION="1.0.0"

# iOS部署目标版本
IOS_DEPLOYMENT_TARGET="13.0"

# 支持的架构
DEVICE_ARCHS="arm64"

# 第三方库路径
LRENGINE_DIR="${PROJECT_ROOT}/third_party/LREngine"
TASKQUEUE_DIR="${PROJECT_ROOT}/third_party/TaskQueue"

################################################################################
# 辅助函数
################################################################################

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}$1${NC}"
    echo -e "${GREEN}========================================${NC}"
}

print_subheader() {
    echo ""
    echo -e "${CYAN}----------------------------------------${NC}"
    echo -e "${CYAN}$1${NC}"
    echo -e "${CYAN}----------------------------------------${NC}"
}

show_help() {
    cat << EOF
Pipeline iOS 构建脚本

用法: $0 [选项]

选项:
  -c, --config <Debug|Release>        构建配置（默认：Release）
  -t, --type <static|framework|all>   输出类型（默认：static）
  -o, --output <路径>                 输出目录（默认：./build/ios）
  -h, --help                          显示此帮助信息

构建配置说明:
  Debug     - 包含调试符号，未优化，启用调试日志
  Release   - 优化构建，移除调试符号

输出类型说明:
  static    - 只生成静态库（.a文件）
  framework - 只生成Framework（.framework包）
  all       - 同时生成静态库和Framework

示例:
  $0                                  # 构建Release版本的静态库
  $0 -c Debug                         # 构建Debug版本
  $0 -t framework                     # 只构建Framework
  $0 -t all -c Debug                  # 构建Debug版本的所有输出
  $0 -o ~/Desktop/Pipeline            # 输出到指定目录

输出文件结构:
  <输出目录>/
    ├── lib/
    │   ├── libPipeline.a             # Pipeline静态库
    │   ├── liblrengine.a             # LREngine静态库
    │   └── libTaskQueue.a            # TaskQueue静态库
    ├── framework/
    │   └── Pipeline.framework/       # Framework包（包含所有依赖）
    └── include/
        ├── pipeline/                 # Pipeline头文件
        ├── lrengine/                 # LREngine头文件
        └── TaskQueue/                # TaskQueue头文件

EOF
}

################################################################################
# 参数解析
################################################################################

while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--config)
            BUILD_CONFIG="$2"
            shift 2
            ;;
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            print_error "未知选项: $1"
            show_help
            exit 1
            ;;
    esac
done

# 验证构建配置
if [[ "$BUILD_CONFIG" != "Debug" && "$BUILD_CONFIG" != "Release" ]]; then
    print_error "无效的构建配置: $BUILD_CONFIG (必须是 Debug 或 Release)"
    exit 1
fi

# 验证构建类型
if [[ "$BUILD_TYPE" != "static" && "$BUILD_TYPE" != "framework" && "$BUILD_TYPE" != "all" ]]; then
    print_error "无效的构建类型: $BUILD_TYPE (必须是 static、framework 或 all)"
    exit 1
fi

################################################################################
# 环境检查
################################################################################

check_environment() {
    print_header "检查构建环境"
    
    # 检查是否在macOS上运行
    if [[ "$OSTYPE" != "darwin"* ]]; then
        print_error "此脚本只能在macOS上运行"
        exit 1
    fi
    print_success "操作系统: macOS"
    
    # 检查CMake
    if ! command -v cmake &> /dev/null; then
        print_error "未找到CMake，请先安装CMake"
        print_info "可以使用 Homebrew 安装: brew install cmake"
        exit 1
    fi
    print_success "CMake 版本: $(cmake --version | head -n1)"
    
    # 检查Xcode命令行工具
    if ! command -v xcodebuild &> /dev/null; then
        print_error "未找到Xcode命令行工具"
        print_info "请运行: xcode-select --install"
        exit 1
    fi
    print_success "Xcode 版本: $(xcodebuild -version | head -n1)"
    
    # 检查iOS SDK
    IOS_SDK_PATH=$(xcrun --sdk iphoneos --show-sdk-path 2>/dev/null || echo "")
    if [[ -z "$IOS_SDK_PATH" ]]; then
        print_error "未找到iOS SDK"
        print_info "请确保已安装Xcode及iOS开发组件"
        exit 1
    fi
    print_success "iOS SDK 路径: $IOS_SDK_PATH"
    
    # 检查第三方库
    if [[ ! -d "$LRENGINE_DIR" ]]; then
        print_error "未找到LREngine: $LRENGINE_DIR"
        print_info "请确保已初始化git submodule"
        exit 1
    fi
    print_success "LREngine: $LRENGINE_DIR"
    
    if [[ ! -d "$TASKQUEUE_DIR" ]]; then
        print_error "未找到TaskQueue: $TASKQUEUE_DIR"
        print_info "请确保已初始化git submodule"
        exit 1
    fi
    print_success "TaskQueue: $TASKQUEUE_DIR"
    
    echo ""
}

################################################################################
# 清理函数
################################################################################

clean_build_dir() {
    local build_dir=$1
    if [[ -d "$build_dir" ]]; then
        print_info "清理旧的构建目录: $build_dir"
        rm -rf "$build_dir"
    fi
}

################################################################################
# 构建静态库
################################################################################

build_static_library() {
    print_header "构建iOS静态库 (${BUILD_CONFIG})"
    
    local build_dir="${OUTPUT_DIR}/build_static_${BUILD_CONFIG}"
    clean_build_dir "$build_dir"
    mkdir -p "$build_dir"
    
    print_info "配置CMake..."
    
    cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=${IOS_DEPLOYMENT_TARGET} \
        -DCMAKE_OSX_ARCHITECTURES="${DEVICE_ARCHS}" \
        -DCMAKE_BUILD_TYPE=${BUILD_CONFIG} \
        -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO \
        -DCMAKE_IOS_INSTALL_COMBINED=YES \
        -DPIPELINE_PLATFORM_IOS=ON \
        -DPIPELINE_BUILD_TESTS=OFF \
        -DPIPELINE_BUILD_EXAMPLES=OFF \
        -DLRENGINE_ENABLE_METAL=ON \
        -DLRENGINE_ENABLE_OPENGL=OFF \
        -DLRENGINE_ENABLE_OPENGLES=OFF \
        -DLRENGINE_BUILD_EXAMPLES=OFF \
        -DLRENGINE_BUILD_TESTS=OFF \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO
    
    print_info "编译Pipeline库..."
    cmake --build "$build_dir" --config ${BUILD_CONFIG} --target Pipeline -- -quiet
    
    # 复制静态库到输出目录
    local lib_output="${OUTPUT_DIR}/lib"
    mkdir -p "$lib_output"
    
    # 查找并复制Pipeline库
    local pipeline_lib=$(find "$build_dir" -name "libPipeline.a" -path "*/${BUILD_CONFIG}/*" | head -n1)
    if [[ -z "$pipeline_lib" ]]; then
        pipeline_lib=$(find "$build_dir" -name "libPipeline.a" | head -n1)
    fi
    
    if [[ -f "$pipeline_lib" ]]; then
        cp "$pipeline_lib" "$lib_output/libPipeline.a"
        print_success "Pipeline库: $lib_output/libPipeline.a"
        print_info "  大小: $(du -h "$lib_output/libPipeline.a" | awk '{print $1}')"
        lipo -info "$lib_output/libPipeline.a"
    else
        print_error "未找到Pipeline静态库"
        exit 1
    fi
    
    # 查找并复制LREngine库
    local lrengine_lib=$(find "$build_dir" -name "liblrengine.a" -path "*/${BUILD_CONFIG}/*" | head -n1)
    if [[ -z "$lrengine_lib" ]]; then
        lrengine_lib=$(find "$build_dir" -name "liblrengine.a" | head -n1)
    fi
    
    if [[ -f "$lrengine_lib" ]]; then
        cp "$lrengine_lib" "$lib_output/liblrengine.a"
        print_success "LREngine库: $lib_output/liblrengine.a"
        print_info "  大小: $(du -h "$lib_output/liblrengine.a" | awk '{print $1}')"
    fi
    
    # 查找并复制TaskQueue库 (实际名为 dispatch_queue)
    local taskqueue_lib=$(find "$build_dir" -name "libdispatch_queue.a" -path "*/${BUILD_CONFIG}/*" | head -n1)
    if [[ -z "$taskqueue_lib" ]]; then
        taskqueue_lib=$(find "$build_dir" -name "libdispatch_queue.a" | head -n1)
    fi
    
    if [[ -f "$taskqueue_lib" ]]; then
        cp "$taskqueue_lib" "$lib_output/libdispatch_queue.a"
        print_success "TaskQueue库: $lib_output/libdispatch_queue.a"
        print_info "  大小: $(du -h "$lib_output/libdispatch_queue.a" | awk '{print $1}')"
    fi
    
    echo ""
}

################################################################################
# 构建Framework
################################################################################

build_framework() {
    print_header "构建iOS Framework (${BUILD_CONFIG})"
    
    # 先确保静态库已构建
    local lib_output="${OUTPUT_DIR}/lib"
    if [[ ! -f "$lib_output/libPipeline.a" ]]; then
        print_info "先构建静态库..."
        build_static_library
    fi
    
    # 创建Framework结构
    local framework_dir="${OUTPUT_DIR}/framework/${LIBRARY_NAME}.framework"
    rm -rf "$framework_dir"
    mkdir -p "$framework_dir/Headers/pipeline"
    mkdir -p "$framework_dir/Headers/lrengine"
    mkdir -p "$framework_dir/Headers/TaskQueue"
    
    print_info "合并静态库..."
    
    # 合并所有静态库为一个
    local libs_to_merge=""
    [[ -f "$lib_output/libPipeline.a" ]] && libs_to_merge+=" $lib_output/libPipeline.a"
    [[ -f "$lib_output/liblrengine.a" ]] && libs_to_merge+=" $lib_output/liblrengine.a"
    [[ -f "$lib_output/libdispatch_queue.a" ]] && libs_to_merge+=" $lib_output/libdispatch_queue.a"
    
    libtool -static -o "$framework_dir/${LIBRARY_NAME}" $libs_to_merge
    
    # 复制头文件
    print_info "复制头文件..."
    cp -R "$PROJECT_ROOT/include/pipeline/"* "$framework_dir/Headers/pipeline/"
    cp -R "$LRENGINE_DIR/include/lrengine/"* "$framework_dir/Headers/lrengine/"
    
    if [[ -d "$TASKQUEUE_DIR/TaskQueue" ]]; then
        find "$TASKQUEUE_DIR/TaskQueue" -name "*.h" -exec cp {} "$framework_dir/Headers/TaskQueue/" \;
    fi
    
    # 创建模块映射文件
    cat > "$framework_dir/Headers/module.modulemap" << EOF
framework module ${LIBRARY_NAME} {
    umbrella header "${LIBRARY_NAME}.h"
    export *
    module * { export * }
}
EOF
    
    # 创建伞形头文件
    cat > "$framework_dir/Headers/${LIBRARY_NAME}.h" << 'EOF'
//
//  Pipeline.h
//  Pipeline iOS Framework
//
//  统一头文件
//

#ifndef PIPELINE_FRAMEWORK_H
#define PIPELINE_FRAMEWORK_H

// Pipeline 核心
#import <Pipeline/pipeline/Pipeline.h>
#import <Pipeline/pipeline/PipelineFacade.h>

// LREngine 渲染引擎
#import <Pipeline/lrengine/core/LRRenderContext.h>
#import <Pipeline/lrengine/core/LRTypes.h>

#endif /* PIPELINE_FRAMEWORK_H */
EOF
    
    # 创建Info.plist
    cat > "$framework_dir/Info.plist" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>${LIBRARY_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>com.pipeline.${LIBRARY_NAME}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>${LIBRARY_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>FMWK</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundleSupportedPlatforms</key>
    <array>
        <string>iPhoneOS</string>
    </array>
    <key>MinimumOSVersion</key>
    <string>${IOS_DEPLOYMENT_TARGET}</string>
</dict>
</plist>
EOF
    
    print_success "Framework已生成: $framework_dir"
    print_info "  大小: $(du -sh "$framework_dir" | awk '{print $1}')"
    lipo -info "$framework_dir/${LIBRARY_NAME}"
    
    echo ""
}

################################################################################
# 复制头文件
################################################################################

copy_headers() {
    print_info "复制公共头文件..."
    
    local include_dir="${OUTPUT_DIR}/include"
    rm -rf "$include_dir"
    mkdir -p "$include_dir/pipeline"
    mkdir -p "$include_dir/lrengine"
    mkdir -p "$include_dir/TaskQueue"
    
    # Pipeline 头文件
    cp -R "$PROJECT_ROOT/include/pipeline/"* "$include_dir/pipeline/"
    
    # LREngine 头文件
    cp -R "$LRENGINE_DIR/include/lrengine/"* "$include_dir/lrengine/"
    
    # TaskQueue 头文件
    if [[ -d "$TASKQUEUE_DIR/TaskQueue" ]]; then
        find "$TASKQUEUE_DIR/TaskQueue" -name "*.h" -exec cp {} "$include_dir/TaskQueue/" \;
    fi
    
    print_success "头文件已复制到: $include_dir"
}

################################################################################
# 生成使用说明
################################################################################

generate_readme() {
    local readme_file="${OUTPUT_DIR}/README.md"
    
    cat > "$readme_file" << EOF
# Pipeline iOS 库

Pipeline 是一个用于相机数据处理的跨平台框架。

## 构建信息

- 构建日期: $(date '+%Y-%m-%d %H:%M:%S')
- 构建配置: ${BUILD_CONFIG}
- iOS部署目标: ${IOS_DEPLOYMENT_TARGET}
- 架构: ${DEVICE_ARCHS}

## 包含内容

\`\`\`
ios/
├── lib/
│   ├── libPipeline.a         # Pipeline 静态库
│   ├── liblrengine.a         # LREngine 静态库
│   └── libTaskQueue.a        # TaskQueue 静态库
├── framework/
│   └── Pipeline.framework/   # 合并的 Framework
└── include/
    ├── pipeline/             # Pipeline 头文件
    ├── lrengine/             # LREngine 头文件
    └── TaskQueue/            # TaskQueue 头文件
\`\`\`

## 集成方式

### 使用静态库

1. 将 \`lib/\` 下的所有 .a 文件添加到项目
2. 将 \`include/\` 添加到 Header Search Paths
3. 链接系统框架：Metal, CoreVideo, CoreMedia, Foundation, UIKit

### 使用 Framework

1. 将 \`Pipeline.framework\` 添加到项目
2. 链接系统框架：Metal, CoreVideo, CoreMedia, Foundation, UIKit

## 使用示例

\`\`\`objc
#import "PipelineBridge.h"

// 创建 Pipeline
PipelineBridgeConfig *config = [PipelineBridgeConfig cameraPreviewConfig];
PipelineBridge *bridge = [[PipelineBridge alloc] initWithConfig:config];

// 初始化
[bridge initialize];

// 设置输出
[bridge setupDisplayOutputWithMetalLayer:metalLayer width:1920 height:1080];

// 启动
[bridge start];

// 输入相机帧
[bridge feedSampleBuffer:sampleBuffer];
\`\`\`

EOF
    
    print_success "README已生成: $readme_file"
}

################################################################################
# 主函数
################################################################################

main() {
    print_header "Pipeline iOS 构建脚本"
    
    echo "项目路径: $PROJECT_ROOT"
    echo "输出目录: $OUTPUT_DIR"
    echo "构建配置: $BUILD_CONFIG"
    echo "构建类型: $BUILD_TYPE"
    echo "iOS版本: $IOS_DEPLOYMENT_TARGET"
    echo "架构: $DEVICE_ARCHS"
    echo ""
    
    # 检查环境
    check_environment
    
    # 创建输出目录
    mkdir -p "$OUTPUT_DIR"
    
    # 根据构建类型执行
    if [[ "$BUILD_TYPE" == "static" || "$BUILD_TYPE" == "all" ]]; then
        build_static_library
        copy_headers
    fi
    
    if [[ "$BUILD_TYPE" == "framework" || "$BUILD_TYPE" == "all" ]]; then
        build_framework
    fi
    
    # 生成说明文档
    generate_readme
    
    # 完成
    print_header "构建完成！"
    
    echo "输出位置: $OUTPUT_DIR"
    if [[ "$BUILD_TYPE" == "static" || "$BUILD_TYPE" == "all" ]]; then
        echo "  静态库: ${OUTPUT_DIR}/lib/"
        echo "  头文件: ${OUTPUT_DIR}/include/"
    fi
    if [[ "$BUILD_TYPE" == "framework" || "$BUILD_TYPE" == "all" ]]; then
        echo "  Framework: ${OUTPUT_DIR}/framework/${LIBRARY_NAME}.framework"
    fi
    echo ""
    
    print_success "所有任务已完成！"
}

# 执行主函数
main
