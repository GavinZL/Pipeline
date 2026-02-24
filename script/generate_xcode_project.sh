#!/bin/bash

################################################################################
# Pipeline Xcode 项目生成脚本
#
# 功能：
# - 为 Pipeline 工程生成 Xcode 项目文件
# - 便于使用 Xcode 进行开发、调试和代码浏览
# - 支持 macOS 平台开发
#
# 使用方法：
#   ./generate_xcode_project.sh [选项]
#
# 选项：
#   -o, --output <路径>     输出目录（默认：./build/xcode）
#   -p, --platform <macos|ios>  目标平台（默认：macos）
#   -c, --config <Debug|Release> 默认构建配置（默认：Debug）
#   -o, --open              生成后自动打开 Xcode
#   -h, --help              显示帮助信息
#
# 示例：
#   ./generate_xcode_project.sh                   # 生成 macOS Xcode 项目
#   ./generate_xcode_project.sh -p ios            # 生成 iOS Xcode 项目
#   ./generate_xcode_project.sh -o                # 生成并打开 Xcode
#   ./generate_xcode_project.sh -p ios -o         # 生成 iOS 项目并打开
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
PLATFORM="macos"
BUILD_CONFIG="Debug"
OPEN_XCODE=false
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="${PROJECT_ROOT}/build/xcode"
LIBRARY_NAME="Pipeline"
VERSION="1.0.0"

# iOS 部署目标版本
IOS_DEPLOYMENT_TARGET="13.0"

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
Pipeline Xcode 项目生成脚本

用法: $0 [选项]

选项:
  -o, --output <路径>              输出目录（默认：./build/xcode）
  -p, --platform <macos|ios>       目标平台（默认：macos）
  -c, --config <Debug|Release>     默认构建配置（默认：Debug）
  -O, --open                       生成后自动打开 Xcode
  -h, --help                       显示此帮助信息

示例:
  $0                               # 生成 macOS Xcode 项目
  $0 -p ios                        # 生成 iOS Xcode 项目
  $0 -O                            # 生成并打开 Xcode
  $0 -p ios -O                     # 生成 iOS 项目并打开
  $0 -o ~/Desktop/PipelineXcode    # 指定输出目录

生成后使用:
  1. 打开生成的 .xcodeproj 文件:
     open ${OUTPUT_DIR}/Pipeline.xcodeproj

  2. 或在 Xcode 中:
     File -> Open -> 选择 Pipeline.xcodeproj

注意事项:
  - macOS 项目用于 Mac 桌面应用开发
  - iOS 项目用于 iPhone/iPad 应用开发
  - 生成后可使用 Xcode 进行代码浏览、调试和编译
EOF
}

################################################################################
# 参数解析
################################################################################

while [[ $# -gt 0 ]]; do
    case $1 in
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -p|--platform)
            PLATFORM="$2"
            shift 2
            ;;
        -c|--config)
            BUILD_CONFIG="$2"
            shift 2
            ;;
        -O|--open)
            OPEN_XCODE=true
            shift
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

# 验证平台
if [[ "$PLATFORM" != "macos" && "$PLATFORM" != "ios" ]]; then
    print_error "无效的平台: $PLATFORM (必须是 macos 或 ios)"
    exit 1
fi

# 验证构建配置
if [[ "$BUILD_CONFIG" != "Debug" && "$BUILD_CONFIG" != "Release" ]]; then
    print_error "无效的构建配置: $BUILD_CONFIG (必须是 Debug 或 Release)"
    exit 1
fi

# 根据平台调整输出目录
if [[ "$PLATFORM" == "ios" ]]; then
    OUTPUT_DIR="${PROJECT_ROOT}/build/xcode_ios"
fi

################################################################################
# 环境检查
################################################################################

check_environment() {
    print_header "检查构建环境"

    # 检查是否在 macOS 上运行
    if [[ "$OSTYPE" != "darwin"* ]]; then
        print_error "此脚本只能在 macOS 上运行"
        exit 1
    fi
    print_success "操作系统: macOS"

    # 检查 CMake
    if ! command -v cmake &> /dev/null; then
        print_error "未找到 CMake，请先安装 (brew install cmake)"
        exit 1
    fi
    print_success "CMake 版本: $(cmake --version | head -n1)"

    # 检查 Xcode 命令行工具
    if ! command -v xcodebuild &> /dev/null; then
        print_error "未找到 Xcode 命令行工具"
        print_info "请运行: xcode-select --install"
        exit 1
    fi
    print_success "Xcode 版本: $(xcodebuild -version | head -n1)"

    # iOS 平台需要检查 iOS SDK
    if [[ "$PLATFORM" == "ios" ]]; then
        local ios_sdk_path
        ios_sdk_path=$(xcrun --sdk iphoneos --show-sdk-path 2>/dev/null || echo "")
        if [[ -z "$ios_sdk_path" ]]; then
            print_error "未找到 iOS SDK"
            print_info "请确保已安装 Xcode 及 iOS 开发组件"
            exit 1
        fi
        print_success "iOS SDK 路径: $ios_sdk_path"
    fi

    # 检查第三方库目录
    if [[ ! -d "$LRENGINE_DIR" ]]; then
        print_warning "未找到 LREngine 目录: $LRENGINE_DIR"
        print_info "请确保已初始化 git submodule: git submodule update --init"
    else
        print_success "LREngine: $LRENGINE_DIR"
    fi

    if [[ ! -d "$TASKQUEUE_DIR" ]]; then
        print_warning "未找到 TaskQueue 目录: $TASKQUEUE_DIR"
        print_info "请确保已初始化 git submodule: git submodule update --init"
    else
        print_success "TaskQueue: $TASKQUEUE_DIR"
    fi

    echo ""
}

################################################################################
# 生成 Xcode 项目
################################################################################

generate_xcode_project() {
    print_header "生成 ${PLATFORM} Xcode 项目"

    local build_dir="${OUTPUT_DIR}"

    # 清理旧的构建目录
    if [[ -d "$build_dir" ]]; then
        print_info "清理旧的 Xcode 项目目录: $build_dir"
        rm -rf "$build_dir"
    fi
    mkdir -p "$build_dir"

    print_info "配置 CMake 生成 Xcode 项目..."

    if [[ "$PLATFORM" == "ios" ]]; then
        # iOS 平台配置
        print_subheader "iOS 平台配置"
        print_info "  部署目标: iOS ${IOS_DEPLOYMENT_TARGET}"
        print_info "  架构: arm64"

        cmake -S "$PROJECT_ROOT" -B "$build_dir" \
            -G Xcode \
            -DCMAKE_SYSTEM_NAME=iOS \
            -DCMAKE_OSX_DEPLOYMENT_TARGET=${IOS_DEPLOYMENT_TARGET} \
            -DCMAKE_OSX_ARCHITECTURES="arm64" \
            -DCMAKE_BUILD_TYPE=${BUILD_CONFIG} \
            -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO \
            -DCMAKE_IOS_INSTALL_COMBINED=YES \
            -DPIPELINE_BUILD_EXAMPLES=OFF \
            -DPIPELINE_BUILD_TESTS=OFF \
            -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
            -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO

    else
        # macOS 平台配置
        print_subheader "macOS 平台配置"

        cmake -S "$PROJECT_ROOT" -B "$build_dir" \
            -G Xcode \
            -DCMAKE_SYSTEM_NAME=Darwin \
            -DCMAKE_BUILD_TYPE=${BUILD_CONFIG} \
            -DPIPELINE_BUILD_EXAMPLES=OFF \
            -DPIPELINE_BUILD_TESTS=OFF \
            -DCMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT="dwarf-with-dsym" \
            -DCMAKE_XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS[variant=Debug]="YES" \
            -DCMAKE_XCODE_ATTRIBUTE_GCC_OPTIMIZATION_LEVEL[variant=Debug]="0" \
            -DCMAKE_XCODE_ATTRIBUTE_STRIP_INSTALLED_PRODUCT[variant=Debug]="NO"
    fi

    print_success "Xcode 项目生成完成"

    # 查找生成的 .xcodeproj 文件
    local xcode_proj=$(find "$build_dir" -name "*.xcodeproj" -maxdepth 1 | head -n1)
    
    if [[ -n "$xcode_proj" && -d "$xcode_proj" ]]; then
        print_success "项目文件: $xcode_proj"
    else
        print_warning "未找到 .xcodeproj 文件，可能 CMake 配置有问题"
    fi

    echo ""
}

################################################################################
# 打开 Xcode
################################################################################

open_xcode() {
    local xcode_proj=$(find "$OUTPUT_DIR" -name "*.xcodeproj" -maxdepth 1 | head -n1)
    
    if [[ -z "$xcode_proj" ]]; then
        print_error "未找到 Xcode 项目文件"
        exit 1
    fi

    print_info "打开 Xcode 项目: $xcode_proj"
    open "$xcode_proj"
    print_success "Xcode 已启动"
}

################################################################################
# 打印使用说明
################################################################################

print_usage_info() {
    print_header "使用说明"

    local xcode_proj=$(find "$OUTPUT_DIR" -name "*.xcodeproj" -maxdepth 1 | head -n1)
    
    echo "Xcode 项目已生成在:"
    echo "  ${OUTPUT_DIR}"
    echo ""
    echo "打开项目的方式:"
    echo ""
    echo "  方式 1: 命令行打开"
    echo "    open ${xcode_proj}"
    echo ""
    echo "  方式 2: Xcode 中打开"
    echo "    File -> Open -> 选择 ${xcode_proj}"
    echo ""
    echo "构建配置:"
    echo "  - 默认配置: ${BUILD_CONFIG}"
    echo "  - 可在 Xcode 中切换 Debug/Release"
    echo ""
    echo "开发建议:"
    echo "  1. 使用 Xcode 的代码导航功能浏览代码"
    echo "  2. 使用 ⌘+B 构建项目"
    echo "  3. 使用 ⌘+Click 跳转到定义"
    echo "  4. 使用 Find Navigator (⌘+3) 搜索代码"
    echo ""

    if [[ "$PLATFORM" == "ios" ]]; then
        echo "iOS 开发注意:"
        echo "  - 确保在 Xcode 中选择正确的 Team 用于签名"
        echo "  - 选择目标设备进行调试"
        echo ""
    fi
}

################################################################################
# 主流程
################################################################################

main() {
    print_header "Pipeline Xcode 项目生成脚本"

    echo "项目路径: $PROJECT_ROOT"
    echo "输出目录: $OUTPUT_DIR"
    echo "目标平台: $PLATFORM"
    echo "构建配置: $BUILD_CONFIG"
    echo "自动打开: $OPEN_XCODE"
    echo ""

    # 检查环境
    check_environment

    # 生成 Xcode 项目
    generate_xcode_project

    # 打印使用说明
    print_usage_info

    # 是否自动打开 Xcode
    if [[ "$OPEN_XCODE" == true ]]; then
        open_xcode
    fi

    print_header "完成！"
}

# 执行主函数
main
