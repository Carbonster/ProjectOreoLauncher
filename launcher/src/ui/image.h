// image.h - loads a mod thumbnail into a D3D11 texture using Windows Imaging Component.
// WIC ships with Windows, so PNG/JPG support costs no third party dependency.
#pragma once

#include <d3d11.h>

#include <string>

namespace oreo {

struct Texture {
    ID3D11ShaderResourceView* view = nullptr;
    int width = 0;
    int height = 0;
    bool valid() const { return view != nullptr; }
    void release();
};

Texture load_texture(ID3D11Device* device, const std::string& path);

}  // namespace oreo
