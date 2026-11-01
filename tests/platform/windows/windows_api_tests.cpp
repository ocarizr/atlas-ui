/**
 * @file
 * @brief Behavioral coverage for windows api with isolated scenario ownership and cleanup.
 */

#include <Windows.h>
#include <oaidl.h>
#include <unknwn.h>

#include <gtest/gtest.h>

#include <d3d12.h>
#include <directx-dxc/dxcapi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <xaudio2.h>

/// @brief Verify creates the directx shader compiler utilities.
TEST(windows_api_tests, creates_the_directx_shader_compiler_utilities)
{
    Microsoft::WRL::ComPtr<IDxcUtils> utilities;
    const auto result = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utilities));
    EXPECT_GE(result, 0);
    EXPECT_NE(utilities.Get(), nullptr);
}

/// @brief Verify creates a direct3 d12 device through warp.
TEST(windows_api_tests, creates_a_direct3_d12_device_through_warp)
{
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    ASSERT_GE(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), 0);
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    ASSERT_GE(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), 0);
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    EXPECT_GE(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), 0);
}

/// @brief Verify creates an x audio2 engine without requiring an output voice.
TEST(windows_api_tests, creates_an_x_audio2_engine_without_requiring_an_output_voice)
{
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ASSERT_GE(initialized, 0);

    /// @brief Scenario state for apartment cleanup, isolated per test and released through RAII.
    struct apartment_cleanup
    {
        /// @brief Release apartment cleanup resources and complete its scoped cleanup.
        ~apartment_cleanup()
        {
            CoUninitialize();
        }
    } cleanup;

    Microsoft::WRL::ComPtr<IXAudio2> engine;
    EXPECT_GE(XAudio2Create(engine.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR), 0);
}
