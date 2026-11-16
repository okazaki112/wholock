#pragma once
#include <memory>
#include <vector>
#include "../../domain/LockInfo.h"

namespace who::win32 {

class WindowsPathMap;

// 引擎工厂（可插拔接线唯一入口；UI 与 CLI 共用）
// mask: EngineRM / EngineHT 位或。pathmap 注入 HT 做 Volume GUID 反查（v1.0 F1）。
std::vector<std::unique_ptr<ILockScanner>> createEngines(std::uint8_t mask, const WindowsPathMap* pathmap = nullptr);

} // namespace who::win32
