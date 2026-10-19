#pragma once
#include <Windows.h>
#include <array>
namespace cheeky::foveated_dlss {
struct NrRuntimeModule {
    HMODULE module{}; DWORD error{}; std::array<wchar_t,32768> directory{};
    std::array<char, 4096> path{};
    std::array<char, 64> version{};
};
NrRuntimeModule load_nr_runtime_module() noexcept;
}
