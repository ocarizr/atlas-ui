#include <Windows.h>

#include <gtest/gtest.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <xaudio2.h>

TEST(WindowsApiTests, CreatesADirect3D12DeviceThroughWarp)
{
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    ASSERT_GE(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), 0);
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    ASSERT_GE(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), 0);
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    EXPECT_GE(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), 0);
}

TEST(WindowsApiTests, CreatesAnXAudio2EngineWithoutRequiringAnOutputVoice)
{
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ASSERT_GE(initialized, 0);
    struct ApartmentCleanup
    {
        ~ApartmentCleanup()
        {
            CoUninitialize();
        }
    } cleanup;
    Microsoft::WRL::ComPtr<IXAudio2> engine;
    EXPECT_GE(XAudio2Create(engine.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR), 0);
}
