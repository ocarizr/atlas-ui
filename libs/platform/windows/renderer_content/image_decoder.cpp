/** @file @brief WIC decoding from bounded memory, never paths or file handles. */
#include <Windows.h>
#include <atlas/renderer_content/images.hpp>
#include <limits>
#include <stdexcept>
#include <wincodec.h>
#include <wrl/client.h>

namespace atlas::renderer_content
{
namespace
{
/// @brief Convert native failures without exposing SDK types in portable headers.
void check(HRESULT result)
{
    if (FAILED(result))
    {
        throw std::runtime_error("WIC image decoding failed");
    }
}

/// @brief Balance this job's COM reference even on decoder failure or cancellation.
class apartment
{
  public:
    apartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
    {
        if (result_ != RPC_E_CHANGED_MODE)
        {
            check(result_);
        }
    } ///< Reuse an existing caller apartment without changing its policy.

    ~apartment()
    {
        if (SUCCEEDED(result_))
        {
            CoUninitialize();
        }
    } ///< Release only our reference.
  private:
    HRESULT result_; ///< S_FALSE also increments the COM initialization count.
};

/// @brief Each invocation owns its COM objects; shared decoder instances are reentrant.
class wic_decoder final : public image_decoder
{
  public:
    std::shared_ptr<const image_data> decode(std::span<const std::uint8_t> encoded,
                                             image_limits limits,
                                             std::shared_ptr<std::pmr::memory_resource> memory,
                                             std::stop_token stop) const override
    {
        limits.validate();
        if (stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        if (encoded.empty() || encoded.size() > std::numeric_limits<DWORD>::max())
        {
            throw std::invalid_argument("invalid encoded image size");
        }

        const apartment com;
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;
        check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&factory)));
        // WIC's memory stream API requires mutable storage; never cast away input constness.
        std::vector<std::uint8_t> storage(encoded.begin(), encoded.end());
        ComPtr<IWICStream> stream;
        check(factory->CreateStream(&stream));
        check(stream->InitializeFromMemory(storage.data(), static_cast<DWORD>(storage.size())));
        ComPtr<IWICBitmapDecoder> decoder;
        check(factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                               WICDecodeMetadataCacheOnDemand, &decoder));
        GUID container{};
        check(decoder->GetContainerFormat(&container));
        if (container != GUID_ContainerFormatPng && container != GUID_ContainerFormatBmp &&
            container != GUID_ContainerFormatJpeg)
        {
            throw std::invalid_argument("only PNG, BMP and JPEG images are supported");
        }

        UINT frames = 0;
        check(decoder->GetFrameCount(&frames));
        if (frames != 1)
        {
            throw std::invalid_argument("image must contain exactly one frame");
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        check(decoder->GetFrame(0, &frame));
        auto image = std::make_shared<image_data>(std::move(memory));
        check(frame->GetSize(&image->width, &image->height));
        const auto size = std::uint64_t{image->width} * image->height * 4;
        if (image->width == 0 || image->height == 0 || image->width > limits.max_dimension ||
            image->height > limits.max_dimension || size > limits.max_decoded_bytes ||
            size > std::numeric_limits<UINT>::max())
        {
            throw std::length_error("decoded image exceeds output limits");
        }

        if (stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        ComPtr<IWICFormatConverter> converted;
        check(factory->CreateFormatConverter(&converted));
        check(converted->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                    WICBitmapDitherTypeNone, nullptr, 0,
                                    WICBitmapPaletteTypeCustom));
        image->pixels.resize(static_cast<std::size_t>(size));
        check(converted->CopyPixels(nullptr, image->width * 4, static_cast<UINT>(size),
                                    reinterpret_cast<BYTE*>(image->pixels.data())));
        if (stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        return image;
    } ///< Normalize the first static frame to tight top-down RGBA8 without color management.
};
} // namespace

/// @brief Select the Windows implementation without leaking native objects to callers.
std::shared_ptr<const image_decoder> make_image_decoder()
{
    return std::make_shared<wic_decoder>();
}
} // namespace atlas::renderer_content
