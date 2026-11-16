#pragma once
// domain 层：纯数据与接口。禁止 include <windows.h>/网络/磁盘 —— R8-a 依赖方向
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace who {

enum EngineBits : std::uint8_t {
    EngineRM    = std::uint8_t{1} << 0, // RestartManager 主通道
    EngineHT    = std::uint8_t{1} << 1, // 句柄表补盲通道（候选门）
    EngineStub  = std::uint8_t{1} << 2, // 测试桩（可插拔第二实现）
    EngineDeep  = std::uint8_t{1} << 3, // v1.0 F6：子进程隔离全表深扫
};

enum class LockKind : std::uint8_t {
    FileHandle,      // 文件句柄（可删除性占用）
    MemoryMap,       // 内存映射视图
    ServiceModule,   // 服务持有模块
    ProcessModule,   // 作为可执行映像/模块被加载
    Unknown,
};

// 文件身份（NTFS 卷序列号+文件ID64）：8.3/硬链/软链等价类的权威判据（v1.0 F1）。
// 纯 POD，win32 侧由 GetFileInformationByHandleEx(FileIdInfo) 填充。
struct FileIdentity {
    std::uint64_t volumeSerial = 0;
    std::uint64_t fileId = 0;
    bool valid = false;
};

inline bool identityEqual(const FileIdentity& a, const FileIdentity& b) noexcept {
    return a.valid && b.valid && a.volumeSerial == b.volumeSerial && a.fileId == b.fileId;
}

// 单个扫描目标（v1.0 F4/F5：多目标与目录展开后的原子单位）
struct TargetRef {
    std::wstring originalPath;   // 绝对常规路径（RM 注册用）
    std::wstring normalizedPath; // 归一化比对键
    FileIdentity identity;       // best-effort 身份（absolutize 填充）
};

struct ScanQuery {
    std::vector<TargetRef> targets;          // 多目标：CLI 多路径 / 目录展开 / GUI 多行
    std::vector<std::uint32_t> candidateHints; // 编排器累积回喂 HT（候选门）
};

struct LockInfo {
    std::uint32_t pid = 0;
    std::wstring  name;       // 进程映像基本名，小写
    std::wstring  imagePath;  // 可执行文件完整路径（可空）
    LockKind      kind = LockKind::Unknown;
    std::uint8_t  engines = 0;            // EngineBits 位或：来源引擎集合
    bool          seenByRm = false;       // RM 可见（保留字段，UI 摘要用）
    std::wstring  occupiedFile;           // v1.0 多目标：命中的目标文件（单目标=目标本身）
    std::uint32_t occupiedMore = 0;       // 同进程命中多文件时，除 occupiedFile 外的数量
};

// v1.0 F7：某进程句柄明细行（详情面板数据；纯 POD，win32 填充）
struct HandleRow {
    std::uint64_t handleValue = 0;
    std::uint32_t grantedAccess = 0;
    std::uint8_t  type = 0;   // 0=未知 1=磁盘 2=管道 3=其他
    std::wstring  path;       // 未能解析时为空
};

struct ScanOutcome {
    bool ok = false;
    std::vector<LockInfo> locks;
    std::wstring error; // ok=false 时给出错误说明
};

class ILockScanner {
public:
    virtual ~ILockScanner() = default;
    virtual std::uint8_t engineBit() const = 0;
    virtual std::wstring engineName() const = 0;
    virtual ScanOutcome scan(const ScanQuery& q) = 0;
    // true=本轮结果可能不完整（如缺 SeDebugPrivilege），UI 必须黄字提示降级
    virtual bool adminRecommended() const = 0;
};

// 设备路径→盘符注入点（真实现 Win32 在 infrastructure；单测用 Fake）
class IPathMap {
public:
    virtual ~IPathMap() = default;
    // devicePath 形如 \Device\HarddiskVolume3\a.txt；失败返回 nullopt
    virtual std::optional<std::wstring> toDosPath(const std::wstring& devicePath) const = 0;
};

} // namespace who
