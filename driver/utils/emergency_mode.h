#pragma once

#include "driver/platform/platform.h"

#include <cstdlib>
#include <string>

namespace emergency_mode {

// MS-ACCESS緊急モード：最小限のメモリ使用
inline bool isEmergencyMode() {
    const char* env_val = std::getenv("CLICKHOUSE_ODBC_EMERGENCY_MODE");
    return (env_val != nullptr && std::string(env_val) == "1");
}

// 緊急モード用の極小設定
namespace emergency_settings {
    constexpr std::size_t MEMORY_POOL_SIZE = 500;     // 500個のみ
    constexpr std::size_t PREFETCH_ROWS = 1;          // 1行のみ
    constexpr std::size_t BUFFER_INCREMENT = 32;      // 32バイトずつ
    constexpr std::size_t MAX_BUFFER_SIZE = 8192;     // 8KB制限
    constexpr std::size_t MAX_INPUT_SIZE = 4096;      // 4KB制限
    constexpr std::size_t MAX_ITERATIONS = 20;        // 20回制限
}

// 緊急モード用の安全な値を取得
inline std::size_t getSafeMemoryPoolSize() {
    return isEmergencyMode() ? emergency_settings::MEMORY_POOL_SIZE : 1000;
}

inline std::size_t getSafePrefetchRows() {
    return isEmergencyMode() ? emergency_settings::PREFETCH_ROWS : 1;
}

inline std::size_t getSafeBufferIncrement() {
    return isEmergencyMode() ? emergency_settings::BUFFER_INCREMENT : 64;
}

inline std::size_t getSafeMaxBufferSize() {
    return isEmergencyMode() ? emergency_settings::MAX_BUFFER_SIZE : 32768;
}

inline std::size_t getSafeMaxInputSize() {
    return isEmergencyMode() ? emergency_settings::MAX_INPUT_SIZE : 16384;
}

inline std::size_t getSafeMaxIterations() {
    return isEmergencyMode() ? emergency_settings::MAX_ITERATIONS : 50;
}

} // namespace emergency_mode
