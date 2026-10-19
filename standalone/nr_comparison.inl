// Included after the overlay command helpers. No GPU hooks or persistent tuning.
namespace {
struct NrComparison {
    bool active{};
    unsigned stage{};
    ULONGLONG due{}, started{};
    HANDLE process{};
    std::filesystem::path root, helper, api;
    std::string status{"Idle"};
    Settings original{};
};
NrComparison nr_capture;
constexpr const char* nr_capture_labels[] = {
    "00-current", "01-NR-off", "02-both-legacy", "03-new-first-legacy-second",
    "04-legacy-first-new-second", "05-both-new", "06-current-repeat"
};
std::filesystem::path nr_capture_module() {
    HMODULE module{}; wchar_t path[32768]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&nr_capture_module), &module) || !GetModuleFileNameW(module, path, 32768))
        throw std::runtime_error("Cannot locate capture helper directory");
    return std::filesystem::path(path).parent_path();
}
void nr_capture_write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path); out << text; out.close();
    if (!out) throw std::runtime_error("Cannot save capture metadata");
}
void nr_capture_stop(OverlayUiState& r, const OverlayRuntime& runtime, const char* message) {
    auto& c = nr_capture;
    if (c.process) { TerminateProcess(c.process, 2); CloseHandle(c.process); c.process = nullptr; }
    const bool restored = command(r, runtime, "nr_comparison_end");
    c.active = false;
    c.status = std::string(message) + (restored ? "; original settings restored" : "; restore request failed (runtime timeout remains armed)");
    if (!c.root.empty()) { try { nr_capture_write(c.root / "result.txt", c.status); } catch (...) {} }
}
bool nr_capture_apply(OverlayUiState& r, const OverlayRuntime& runtime) {
    auto s = nr_capture.original;
    switch (nr_capture.stage) {
    case 1: s.nr_enabled = false; s.nr_second_pass = false; s.nr_third_pass = false; break;
    case 2: s.nr_color_pipeline = 0; s.nr_pass2_color_pipeline = 0; break;
    case 3: s.nr_color_pipeline = 1; s.nr_pass2_color_pipeline = 0; break;
    case 4: s.nr_color_pipeline = 0; s.nr_pass2_color_pipeline = 1; break;
    case 5: s.nr_color_pipeline = 1; s.nr_pass2_color_pipeline = 1; break;
    }
    const auto payload = "NrEnabled=" + std::to_string(s.nr_enabled) + "\nNrSecondPass=" + std::to_string(s.nr_second_pass) +
        "\nNrThirdPass=" + std::to_string(s.nr_third_pass) + "\nNrColorPipeline=" + std::to_string(s.nr_color_pipeline) +
        "\nNrPass2ColorPipeline=" + std::to_string(s.nr_pass2_color_pipeline);
    if (!command(r, runtime, "nr_comparison_set", payload)) return false;
    nr_capture_write(nr_capture.root / (std::string(nr_capture_labels[nr_capture.stage]) + "-applied.json"), r.snapshot);
    return true;
}
void nr_capture_start(OverlayUiState& r, const OverlayRuntime& runtime) {
    auto& c = nr_capture;
    try {
        refresh(r, runtime, true);
        if (!r.draft.nr_enabled || !r.draft.nr_second_pass || r.draft.nr_third_pass)
            throw std::runtime_error("Enable exactly two NR passes before starting");
        const auto dir = nr_capture_module();
        c.helper = dir / "cpvr_fov_image_audit.exe";
        std::ifstream apiFile(dir / "nr-capture-openvr.txt"); std::string api; std::getline(apiFile, api);
        if (!api.empty() && api.back() == '\r') api.pop_back();
        c.api = api;
        if (!std::filesystem::exists(c.helper) || !c.api.is_absolute() || !std::filesystem::exists(c.api))
            throw std::runtime_error("Capture helper or SteamVR path not installed");
        c.root = dir / "nr-captures" / (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        if (!std::filesystem::create_directories(c.root)) throw std::runtime_error("Capture directory already exists");
        c.original = r.draft;
        nr_capture_write(c.root / "original.json", r.snapshot);
        nr_capture_write(c.root / "README.txt", "Sequential SteamVR composited mirrors, not raw NR inputs or simultaneous eyes.\n"
            "Keep camera, gaze, scene, FOV, SR, resolution and frame generation unchanged. Prefer FG off.\n"
            "Seven stages: current, NR off, both Legacy, new/Legacy, Legacy/new, both new, current repeat.\n"
            "Model tuning, filters, strengths, pass placement and scaling are unchanged.\n"
            "Ten seconds settling per stage. Compare first/last to detect scene drift.\n"
            "Snapshots contain diagnostic counters, not a controlled GPU benchmark or proof of RenoDX parity.\n");
        if (!command(r, runtime, "nr_comparison_begin")) throw std::runtime_error("Runtime refused comparison");
        c.active = true; c.stage = 0; c.started = GetTickCount64(); c.due = c.started + 10000;
        c.status = "Close both menus; hold camera and gaze still. Seven captures, about 90 seconds.";
    } catch (const std::exception& e) { if (c.active) nr_capture_stop(r, runtime, e.what()); else c.status = e.what(); }
}
}
void tick_nr_comparison(OverlayUiState& r, const OverlayRuntime& runtime, bool menu_open) {
    auto& c = nr_capture; if (!c.active) return;
    try {
        const auto now = GetTickCount64();
        if (now - c.started > 150000) { nr_capture_stop(r, runtime, "Capture timed out"); return; }
        if (c.process) {
            if (menu_open) { nr_capture_stop(r, runtime, "Menu opened during image capture; last stage invalid"); return; }
            const auto wait = WaitForSingleObject(c.process, 0);
            if (wait == WAIT_TIMEOUT) {
                if (now > c.due) nr_capture_stop(r, runtime, "Image helper timed out");
                return;
            }
            DWORD code = 1; GetExitCodeProcess(c.process, &code); CloseHandle(c.process); c.process = nullptr;
            if (wait != WAIT_OBJECT_0 || code) { nr_capture_stop(r, runtime, "Image helper failed; see stage outputs"); return; }
            if (++c.stage == std::size(nr_capture_labels)) { nr_capture_stop(r, runtime, "Complete: seven stereo mirror captures saved"); return; }
            if (!nr_capture_apply(r, runtime)) { nr_capture_stop(r, runtime, "Runtime rejected stage"); return; }
            c.due = now + 10000;
        }
        if (menu_open) { c.due = now + 10000; return; }
        if (now < c.due) return;
        refresh(r, runtime, true);
        nr_capture_write(c.root / (std::string(nr_capture_labels[c.stage]) + "-capture.json"), r.snapshot);
        const auto folder = c.root / nr_capture_labels[c.stage];
        std::wstring args = L"\"" + c.helper.wstring() + L"\" --capture \"" + c.api.wstring() + L"\" " +
            std::to_wstring(GetCurrentProcessId()) + L" \"" + folder.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        if (!CreateProcessW(c.helper.c_str(), args.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, c.helper.parent_path().c_str(), &startup, &process)) throw std::runtime_error("Cannot start image helper");
        CloseHandle(process.hThread); c.process = process.hProcess; c.due = now + 12000;
        c.status = std::string("Capturing ") + nr_capture_labels[c.stage];
    } catch (const std::exception& e) { nr_capture_stop(r, runtime, e.what()); }
}
void draw_nr_comparison(OverlayUiState& r, const OverlayRuntime& runtime) {
    if (!ImGui::CollapsingHeader("NR automatic comparison capture")) return;
    ImGui::TextWrapped("Two-pass comparison including NR off. Keeps your tuning and FOV. Close F10 and this menu; hold camera and gaze still. Prefer frame generation off. About 90 seconds.");
    if (nr_capture.active) {
        if (ImGui::Button("Cancel comparison and restore")) nr_capture_stop(r, runtime, "Cancelled");
    } else if (ImGui::Button("Start seven-stage NR capture")) nr_capture_start(r, runtime);
    ImGui::TextWrapped("%s", nr_capture.status.c_str());
    if (!nr_capture.root.empty()) ImGui::TextWrapped("%s", nr_capture.root.string().c_str());
}
