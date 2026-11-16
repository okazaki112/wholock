#pragma once
// infrastructure/win32：设备路径↔盘符映射 + 路径绝对化校验（normalizePathKey 的注入依赖）
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "../../domain/LockInfo.h"

namespace who::win32 {

class WindowsPathMap final : public IPathMap {
public:
    WindowsPathMap(); // 枚举盘符 + QueryDosDeviceW 建 \Device\X → 盘符 缓存
    std::optional<std::wstring> toDosPath(const std::wstring& devicePath) const override;

    std::wstring dosFromVolumeGuid(const std::wstring& guidPath) const; // \\?\Volume{...}\ → 盘符（v1.0 F1 实装：枚举反查）
    const std::vector<std::wstring>& driveLetters() const { return drives_; }

    // GetFullPathNameW + 存在性/目录校验；失败给中文原因（存在但不含设备路径还原）。
    // outIdentity best-effort 填 FileIdInfo（被 share=0 独占打开失败时保持 invalid，调用方回落字符串比对）。
    static std::optional<std::wstring> absolutizeExistingFile(const std::wstring& raw, std::wstring* errOut,
                                                              FileIdentity* outIdentity = nullptr);

private:
    std::vector<std::pair<std::wstring, std::wstring>> devToDrive_; // 长前缀优先（已按长度排序）
    std::vector<std::wstring> drives_; // L"C:" 形式
};

} // namespace who::win32
