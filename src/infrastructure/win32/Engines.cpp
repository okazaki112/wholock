#include "Engines.h"
#include "RmScanner.h"
#include "HandleTableScanner.h"
#include "DeepScanner.h"

namespace who::win32 {

std::vector<std::unique_ptr<ILockScanner>> createEngines(std::uint8_t mask, const WindowsPathMap* pathmap) {
    std::vector<std::unique_ptr<ILockScanner>> v;
    if (mask & EngineRM) v.push_back(std::make_unique<RmScanner>());
    if (mask & EngineHT) v.push_back(std::make_unique<HandleTableScanner>(pathmap));
    if (mask & EngineDeep) v.push_back(std::make_unique<DeepScanner>()); // F6 子进程隔离
    return v;
}

} // namespace who::win32
