/** @file @brief Adapter selection, queue ownership, and bounded GPU completion. */
#include "native_renderer.hpp"
#include <chrono>
#include <system_error>

namespace atlas::renderer::detail
{
/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
void check(HRESULT result, const char* operation)
{
    if (FAILED(result))
    {
        throw std::system_error(static_cast<int>(result), std::system_category(), operation);
    }
}

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
native_device::native_device(device_options policy,
                             std::shared_ptr<std::pmr::memory_resource> resource)
    : options(policy), memory(std::move(resource))
{
    if (!memory)
    {
        memory = std::shared_ptr<std::pmr::memory_resource>(std::pmr::new_delete_resource(),
                                                            [](auto*)
                                                            {
                                                            });
    }

    com_ptr<ID3D12Debug> debug;
    if (options.debug != debug_validation::disabled)
    {
        const auto result = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
        if (options.debug == debug_validation::required)
        {
            check(result, "D3D12 debug layer required");
        }

        if (SUCCEEDED(result))
        {
            debug->EnableDebugLayer();
            information.debug_layer_active = true;
        }
    }

    check(CreateDXGIFactory2(information.debug_layer_active ? DXGI_CREATE_FACTORY_DEBUG : 0,
                             IID_PPV_ARGS(&factory)),
          "create DXGI factory");
    com_ptr<IDXGIAdapter1> selected;
    if (options.adapter == adapter_preference::prefer_hardware)
    {
        for (UINT index = 0;; ++index)
        {
            com_ptr<IDXGIAdapter1> candidate;
            const auto result = factory->EnumAdapterByGpuPreference(
                index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
            if (result == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            check(result, "enumerate graphics adapters");
            DXGI_ADAPTER_DESC1 description{};
            check(candidate->GetDesc1(&description), "query adapter");
            if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                SUCCEEDED(
                    D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu))))
            {
                selected = candidate;
                break;
            }
        }
    }

    if (!selected)
    {
        check(factory->EnumWarpAdapter(IID_PPV_ARGS(&selected)), "select WARP");
        check(D3D12CreateDevice(selected.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu)),
              "create D3D12 device");
    }

    DXGI_ADAPTER_DESC1 description{};
    check(selected->GetDesc1(&description), "query selected adapter");
    information.software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, nullptr, 0, nullptr, nullptr);
    std::string name(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name.data(), length, nullptr,
                        nullptr);
    if (!name.empty())
    {
        name.pop_back();
    }

    information.adapter_name = std::move(name);
    D3D12_COMMAND_QUEUE_DESC queue_description{};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    check(gpu->CreateCommandQueue(&queue_description, IID_PPV_ARGS(&queue)),
          "create graphics queue");
    check(gpu->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
          "create graphics fence");
    create_triangle_pipeline(*this);
    event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr)
    {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "create fence event");
    }
}

/// @brief Release retained resources on the owner thread after outstanding work completes.
native_device::~native_device()
{
    drain_noexcept();
    if (event != nullptr)
    {
        CloseHandle(event);
    }
}

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
std::uint64_t native_device::signal()
{
    const auto value = ++next_fence;
    check(queue->Signal(fence.Get(), value), "signal graphics fence");
    return value;
}

/// @brief Wait for the requested fence value with a bounded coordinator deadline.
void native_device::wait(std::uint64_t value)
{
    check_owner();
    const auto completed = fence->GetCompletedValue();
    if (completed == UINT64_MAX)
    {
        check(gpu->GetDeviceRemovedReason(), "graphics device removed");
    }

    if (completed >= value)
    {
        return;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(options.gpu_wait_timeout_ms);
    check(fence->SetEventOnCompletion(value, event), "register fence completion");
    for (;;)
    {
        const auto current = fence->GetCompletedValue();
        if (current == UINT64_MAX)
        {
            check(gpu->GetDeviceRemovedReason(), "graphics device removed");
        }

        if (current >= value)
        {
            return;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        if (remaining <= 0 ||
            WaitForSingleObject(event, static_cast<DWORD>(remaining)) != WAIT_OBJECT_0)
        {
            throw std::runtime_error("GPU completion wait failed or timed out");
        }
    }
}

/// @brief Signal after all previous submissions and wait for their completion.
void native_device::wait_idle()
{
    check_owner();
    wait(signal());
}

/// @brief Drain safely; terminate rather than release resources still used by a live GPU.
void native_device::drain_noexcept() noexcept
{
    if (owner != std::this_thread::get_id())
    {
        std::terminate();
    }

    try
    {
        wait_idle();
    }
    catch (...)
    {
        if (SUCCEEDED(gpu->GetDeviceRemovedReason()))
        {
            std::terminate();
        }
    }
}

/// @brief Count D3D12 corruption and error diagnostics when validation is enabled.
std::uint64_t native_device::errors() const
{
    com_ptr<ID3D12InfoQueue> messages;
    if (FAILED(gpu.As(&messages)))
    {
        return 0;
    }

    std::uint64_t count = 0;
    for (UINT64 index = 0; index < messages->GetNumStoredMessages(); ++index)
    {
        SIZE_T length = 0;
        check(messages->GetMessage(index, nullptr, &length), "query validation message size");
        /// @brief Own aligned allocator storage for one variable-length native diagnostic.
        std::vector<std::byte> storage(length);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        check(messages->GetMessage(index, message, &length), "query validation message");
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
        {
            ++count;
        }
    }

    return count;
}

/// @brief Construct the selected backend while retaining the CPU allocation resource.
std::shared_ptr<device_backend> make_device(device_options options,
                                            std::shared_ptr<std::pmr::memory_resource> memory)
{
    return std::make_shared<native_device>(options, std::move(memory));
}

/// @brief Retain the device and native window when creating a presentation surface.
std::unique_ptr<surface_backend>
make_surface(std::shared_ptr<device_backend> device,
             std::shared_ptr<window::detail::window_backend> target, surface_options options)
{
    return std::make_unique<native_surface>(
        std::static_pointer_cast<native_device>(std::move(device)), std::move(target), options);
}
} // namespace atlas::renderer::detail

namespace atlas::renderer
{
/// @brief Report native backend availability without allocating graphics resources.
bool backend_available() noexcept
{
    return true;
}
} // namespace atlas::renderer
