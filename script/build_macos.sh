#!/bin/bash

################################################################################
# Pipeline macOS 构建脚本
#
# 功能：
# - 为 macOS(当前主机) 编译 Pipeline 库及其依赖（LREngine、TaskQueue）
# - 生成静态库 libPipeline.a
# - 同步导出公共头文件，便于上层（如 MacCameraApp）集成
#
# 使用方法：
#   ./build_macos.sh [选项]
#
# 选项：
#   -c, --config <Debug|Release>   构建配置（默认：Release）
#   -o, --output <路径>            输出目录（默认：./build/macos）
#   -h, --help                     显示帮助信息
#
# 示例：
#   ./build_macos.sh                      # 构建 Release 版本
#   ./build_macos.sh -c Debug             # 构建 Debug 版本
#   ./build_macos.sh -o ~/Desktop/Pipeline-macos
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
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="${PROJECT_ROOT}/build/macos"
LIBRARY_NAME="Pipeline"
VERSION="1.0.0"

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

show_help() {
    cat << EOF
Pipeline macOS 构建脚本

用法: $0 [选项]

选项:
  -c, --config <Debug|Release>        构建配置（默认：Release）
  -o, --output <路径>                 输出目录（默认：./build/macos）
  -h, --help                          显示此帮助信息

示例:
  $0                                  # 构建 Release 版本
  $0 -c Debug                         # 构建 Debug 版本
  $0 -o ~/Desktop/Pipeline-macos      # 指定输出目录

输出文件结构:
  <输出目录>/
    ├── lib/
    │   └── libPipeline.a             # Pipeline 静态库
    └── include/
        ├── pipeline/                 # Pipeline 头文件
        ├── lrengine/                 # LREngine 头文件（若存在）
        └── TaskQueue/                # TaskQueue 头文件（若存在）
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

    # 检查构建工具（优先 Ninja，其次 Make）
    if command -v ninja &> /dev/null; then
        BUILD_GENERATOR="Ninja"
        GENERATOR_OPTION="-G Ninja"
        print_success "构建工具: Ninja"
    elif command -v make &> /dev/null; then
        BUILD_GENERATOR="Unix Makefiles"
        GENERATOR_OPTION="-G \"Unix Makefiles\""
        print_success "构建工具: Make (Unix Makefiles)"
    else
        print_error "未找到构建工具（Ninja 或 Make）"
        exit 1
    fi

    # 检查第三方库目录是否存在（用于包含头文件，如果缺失给出警告即可）
    if [[ ! -d "$LRENGINE_DIR" ]]; then
        print_warning "未找到 LREngine 目录: $LRENGINE_DIR (如无需直接使用 LREngine 头文件可忽略)"
    else
        print_success "LREngine: $LRENGINE_DIR"
    fi

    if [[ ! -d "$TASKQUEUE_DIR" ]]; then
        print_warning "未找到 TaskQueue 目录: $TASKQUEUE_DIR (如无需直接使用 TaskQueue 头文件可忽略)"
    else
        print_success "TaskQueue: $TASKQUEUE_DIR"
    fi

    echo ""
}

################################################################################
# 构建函数
################################################################################

build_pipeline() {
    print_header "构建 macOS 静态库 (${BUILD_CONFIG})"

    local build_dir="${PROJECT_ROOT}/build/macos_build_${BUILD_CONFIG}"

    # 清理旧的构建目录
    if [[ -d "$build_dir" ]]; then
        print_info "清理旧的构建目录: $build_dir"
        rm -rf "$build_dir"
    fi
    mkdir -p "$build_dir"

    print_info "配置 CMake..."

    # 注意：GENERATOR_OPTION 可能包含空格，用 eval 处理
    eval cmake -S "$PROJECT_ROOT" -B "$build_dir" $GENERATOR_OPTION \
        -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
        -DPIPELINE_BUILD_EXAMPLES=OFF \
        -DPIPELINE_BUILD_TESTS=OFF

    print_info "开始编译 Pipeline (${BUILD_CONFIG})..."

    local jobs
    jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

    cmake --build "$build_dir" --config "$BUILD_CONFIG" --target "$LIBRARY_NAME" -j"$jobs"

    print_success "编译完成"

    # 找到生成的静态库
    print_info "查找生成的 libPipeline.a..."
    local lib_path
    lib_path=$(find "$build_dir" -maxdepth 5 -name "lib${LIBRARY_NAME}.a" -print | head -n1 || true)

    if [[ -z "$lib_path" ]]; then
        print_error "未找到生成的 lib${LIBRARY_NAME}.a，请检查 CMake 配置/输出"
        exit 1
    fi

    print_success "找到静态库: $lib_path"

    # 准备输出目录
    mkdir -p "${OUTPUT_DIR}/lib" "${OUTPUT_DIR}/include"

    print_info "复制静态库到输出目录..."
    cp "$lib_path" "${OUTPUT_DIR}/lib/"

    # 复制依赖静态库：LREngine、TaskQueue(dispatch_queue)、libyuv
    print_info "复制依赖静态库 (LREngine / TaskQueue / libyuv)..."

    # LREngine
    local lr_lib
    lr_lib=$(find "$build_dir" -maxdepth 5 -name "liblrengine.a" -print | head -n1 || true)
    if [[ -n "$lr_lib" ]]; then
        cp "$lr_lib" "${OUTPUT_DIR}/lib/"
        print_success "LREngine 库已复制: $lr_lib"
    else
        print_warning "未找到 liblrengine.a (如未启用 LREngine 可忽略)"
    fi

    # TaskQueue (dispatch_queue)
    local dq_lib
    dq_lib=$(find "$build_dir" -maxdepth 7 \( -name "libdispatch_queue.a" -o -name "libdispatch_queue.dylib" \) -print | head -n1 || true)
    if [[ -n "$dq_lib" ]]; then
        cp "$dq_lib" "${OUTPUT_DIR}/lib/"
        print_success "TaskQueue 库已复制: $dq_lib"
    else
        print_warning "未找到 libdispatch_queue.a/dylib (如未启用 TaskQueue 可忽略)"
    fi

    # libyuv
    local yuv_lib
    yuv_lib=$(find "$build_dir" -maxdepth 7 -path "*/third_party/libyuv/libyuv.a" -print | head -n1 || true)
    if [[ -n "$yuv_lib" ]]; then
        cp "$yuv_lib" "${OUTPUT_DIR}/lib/"
        print_success "libyuv.a 已复制: $yuv_lib"
    else
        print_warning "未找到 libyuv.a (如未启用 libyuv 可忽略)"
    fi

    print_info "复制 Pipeline 头文件..."
    if [[ -d "${PROJECT_ROOT}/include/pipeline" ]]; then
        rm -rf "${OUTPUT_DIR}/include/pipeline"
        cp -R "${PROJECT_ROOT}/include/pipeline" "${OUTPUT_DIR}/include/"
        print_success "Pipeline 头文件已复制"
    else
        print_warning "未找到 Pipeline 头文件目录: ${PROJECT_ROOT}/include/pipeline"
    fi

    # 可选：复制 LREngine / TaskQueue 头文件，便于上层直接使用
    if [[ -d "${LRENGINE_DIR}/include/lrengine" ]]; then
        rm -rf "${OUTPUT_DIR}/include/lrengine"
        cp -R "${LRENGINE_DIR}/include/lrengine" "${OUTPUT_DIR}/include/"
        print_success "LREngine 头文件已复制"
    fi

    if [[ -d "${TASKQUEUE_DIR}/TaskQueue" ]]; then
        rm -rf "${OUTPUT_DIR}/include/TaskQueue"
        cp -R "${TASKQUEUE_DIR}/TaskQueue" "${OUTPUT_DIR}/include/TaskQueue"
        print_success "TaskQueue 头文件已复制"
    fi

    echo ""
    print_success "macOS 构建完成，输出目录: ${OUTPUT_DIR}"
}

################################################################################
# 主流程
################################################################################

check_environment
build_pipeline
