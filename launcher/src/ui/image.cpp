#include "image.h"

#include <windows.h>
#include <wincodec.h>

#include <vector>

#include "../core/log.h"
#include "../core/util.h"

#pragma comment(lib, "windowscodecs.lib")

namespace oreo {

void Texture::release() {
    if (view) {
        view->Release();
        view = nullptr;
    }
    width = height = 0;
}

Texture load_texture(ID3D11Device* device, const std::string& path) {
    Texture texture;
    if (!device || path.empty() || !file_exists(path)) return texture;

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        return texture;
    }

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    std::vector<unsigned char> pixels;
    UINT width = 0, height = 0;

    bool ok = SUCCEEDED(factory->CreateDecoderFromFilename(to_wide(path).c_str(), nullptr, GENERIC_READ,
                                                           WICDecodeMetadataCacheOnDemand, &decoder)) &&
              SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
              SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone,
                                              nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
              SUCCEEDED(converter->GetSize(&width, &height));
    if (ok && width && height) {
        pixels.resize((size_t)width * height * 4);
        ok = SUCCEEDED(converter->CopyPixels(nullptr, width * 4, (UINT)pixels.size(), pixels.data()));
    } else {
        ok = false;
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    factory->Release();

    if (!ok) {
        Log::warn("could not read the image " + path);
        return texture;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = pixels.data();
    data.SysMemPitch = width * 4;

    ID3D11Texture2D* d3d_texture = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, &data, &d3d_texture)) || !d3d_texture) return texture;

    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{};
    view_desc.Format = desc.Format;
    view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view_desc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(d3d_texture, &view_desc, &texture.view);
    d3d_texture->Release();

    texture.width = (int)width;
    texture.height = (int)height;
    return texture;
}

}  // namespace oreo
