#include "peripheral_recovery.hpp"
#include "peripheral_dlaa.hpp"
#include "mock_ngx_parameters.hpp"
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <stdexcept>
#include <iostream>

namespace {
using namespace cheeky::foveated_dlss;
using Microsoft::WRL::ComPtr;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void check(HRESULT result) { require(SUCCEEDED(result), "Peripheral recovery GPU fixture failure"); }
unsigned creates{}, releases{};
NgxResult create(ID3D12GraphicsCommandList*, unsigned, NgxParameters*, NgxHandle** handle) {
    *handle = reinterpret_cast<NgxHandle*>(static_cast<uintptr_t>(++creates)); return 1;
}
NgxResult evaluate(ID3D12GraphicsCommandList*, const NgxHandle*, const NgxParameters*, NgxProgressCallback) { return 1; }
NgxResult release_feature(NgxHandle*) { ++releases; return 1; }

void policy_test() {
    PeripheralStartupRecovery policy;
    auto begin = [&](bool eligible = true, bool compatible = true, unsigned session = 1, unsigned eye = 0) {
        return policy.begin(eligible, session, eye, 5, 0x49, compatible);
    };
    for (unsigned i = 0; i < 240; ++i) {
        require(!begin(false), "Uncalibrated/non-UEVR work armed recovery"); policy.completed(true);
    }
    for (unsigned i = 0; i < 119; ++i) { require(!begin(), "Early rebuild"); policy.completed(true); }
    policy.completed(false);
    require(!begin() && policy.successes == 0, "Failure did not interrupt stabilization");
    policy.completed(true);
    require(!begin(true, false) && policy.successes == 0, "Resize did not interrupt stabilization");
    policy.completed(true);
    require(!begin(true, true, 2) && policy.successes == 0, "Session change retained progress");
    policy.completed(true);
    require(!begin(true, true, 2, 1) && policy.successes == 0, "Eye remap retained progress");
    for (unsigned i = 0; i < 120; ++i) { require(!begin(), "Early rebuild after remap"); policy.completed(true); }
    require(begin(), "Stable successful view did not rebuild");
    for (unsigned i = 0; i < 500; ++i) {
        require(!begin(i % 3 != 0, i % 5 != 0, i, i % 2), "Recovery rearmed within view lifetime");
        policy.completed(true);
    }
}
}

void run_peripheral_recovery_tests() {
    policy_test();
    creates = releases = 0;
    ComPtr<IDXGIFactory4> factory; check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> adapter; check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device; check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queue_desc{}; check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator; check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;
    check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
    ComPtr<ID3D12Fence> fence; check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    UINT64 fence_value{};
    const auto flush = [&] {
        check(list->Close());
        note_peripheral_dlaa_submission(queue.Get(), list.Get());
        ID3D12CommandList* submitted[]{list.Get()}; queue->ExecuteCommandLists(1, submitted);
        check(queue->Signal(fence.Get(), ++fence_value));
        const auto event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        require(event != nullptr, "Create completion event");
        check(fence->SetEventOnCompletion(fence_value, event));
        const auto waited = WaitForSingleObject(event, 10000); CloseHandle(event);
        require(waited == WAIT_OBJECT_0, "Peripheral GPU completion timeout");
        collect_peripheral_dlaa_resources();
        check(allocator->Reset()); check(list->Reset(allocator.Get(), nullptr));
    };
    ComPtr<ID3D12Resource> textures[4];
    const DXGI_FORMAT formats[]{DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT};
    for (unsigned i = 0; i < 4; ++i) {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = desc.Height = 64; desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.Format = formats[i]; desc.SampleDesc.Count = 1; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&textures[i])));
    }
    MockNgxParameters parameters;
    parameters.Set("DLSS.Feature.Create.Flags", 0x4bU); parameters.Set("Reset", 0);
    PeripheralDlaaRequest request{};
    request.command_list = list.Get(); request.color = textures[0].Get(); request.depth = textures[1].Get();
    request.motion_vectors = textures[2].Get(); request.output_template = textures[3].Get();
    request.render_width = request.render_height = request.source_output_width = request.source_output_height = 64;
    request.scale = .61F; request.create_flags = 0x4b;
    request.parameters = &parameters; request.callbacks = {create, evaluate, release_feature};
    request.startup_recovery_session = 1; request.startup_recovery_eligible = true;
    ID3D12Resource* original[2]{};
    ID3D12Resource* rebuilt[2]{};
    unsigned width{}, height{};
    const auto frame = [&](unsigned index) {
        for (unsigned eye = 0; eye < 2; ++eye) {
            request.view_id = 9000 + eye; request.startup_recovery_eye = eye;
            PeripheralDlaaResources resources; NgxResult result{};
            require(evaluate_peripheral_dlaa_ngx(request, resources, result), "Peripheral evaluation failed");
            if (index == 0) { original[eye] = resources.output; width = resources.working_width; height = resources.working_height; }
            if (index < 120) require(resources.output == original[eye], "Rebuilt before 120 successful evaluations");
            if (index == 120) {
                rebuilt[eye] = resources.output;
                require(rebuilt[eye] != original[eye] && resources.working_width == width && resources.working_height == height,
                    "Recovery did not replace resources at identical dimensions");
            }
            if (index > 120) require(resources.output == rebuilt[eye], "Recovery repeated");
        }
    };
    for (unsigned i = 0; i < 119; ++i) { frame(i); flush(); }
    frame(119); // Deliberately leave old GPU uses recorded but not submitted.
    frame(120);
    require(creates == 4 && releases == 0, "Private feature not recreated or released before old submission");
    collect_peripheral_dlaa_resources();
    require(releases == 0, "Collector released an unsubmitted generation");
    flush();
    // The collector signals its own retirement fence after submission.
    for (unsigned i = 0; i < 10 && releases != 2; ++i) { Sleep(1); collect_peripheral_dlaa_resources(); }
    require(releases == 2, "Completed old generations were not released exactly once");
    for (unsigned i = 121; i < 126; ++i) { frame(i); flush(); }
    require(creates == 4 && releases == 2, "Stable views recreated features again");
    check(list->Close());
    release_peripheral_dlaa_resources();
    require(releases == 4, "Final active feature cleanup mismatch");
    std::cout << "PASS: peripheral startup recovery policy, both eyes, same-size textures/features, deferred GPU retirement, no repeat\n";
}
