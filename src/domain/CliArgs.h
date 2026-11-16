#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "LockInfo.h"

namespace who {

struct CliOptions {
    std::vector<std::wstring> paths; // v1.0 F5：多路径（文件/目录混合，位置参数全收）
    std::uint8_t engineMask = EngineRM | EngineHT;
    bool json = false;
    bool usageRequested = false; // -h / -?
    bool versionRequested = false; // --version（v1.0 F2）
    // v1.0 F6/F7 内部模式（GUI 的 DeepScanRunner 自调用协议，不对用户文档暴露）
    enum class Mode : std::uint8_t { Normal, InternalHtfull, InternalProcscan };
    Mode mode = Mode::Normal;
    std::uint32_t internalPid = 0;
    std::uint32_t budgetMs = 25000;
};

bool isCliMode(const std::vector<std::wstring>& args); // 含 --cli 或首参为文件路径
// argv[0]=程序名；非法返回 false 且 errOut 说明。usage 不算错误（out.usageRequested）
bool parseCli(const std::vector<std::wstring>& args, CliOptions& out, std::wstring& errOut);

std::wstring engineMaskName(std::uint8_t mask); // "both|rm|ht|stub" 展示用

} // namespace who
