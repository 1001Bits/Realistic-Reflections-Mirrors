#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <vector>
#include <wrl/client.h>

// Diagnostic only: public D3D copies of the exact resources used by a draw.
// Never changes a binding, maps a live resource, flushes, or waits for the GPU.
namespace MirrorSunShadowReadback {
using Microsoft::WRL::ComPtr;
enum class Result { pending, ready, failed };
struct Item {
  ComPtr<ID3D11Resource> staging;
  std::array<std::vector<std::uint8_t>, 2> bytes;
  unsigned rows[2]{}, rowBytes[2]{}, count{}, completed{};

  bool Buffer(ID3D11DeviceContext *context, ID3D11Buffer *source) {
    if (staging || !context || !source ||
        context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
      return false;
    D3D11_BUFFER_DESC desc{};
    source->GetDesc(&desc);
    if (!desc.ByteWidth || desc.ByteWidth > 4096)
      return false;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Device> device;
    context->GetDevice(&device);
    ComPtr<ID3D11Buffer> copy;
    if (FAILED(device->CreateBuffer(&desc, nullptr, &copy)))
      return false;
    bytes[0].resize(desc.ByteWidth);
    rows[0] = count = 1;
    rowBytes[0] = desc.ByteWidth;
    staging = copy;
    context->CopyResource(staging.Get(), source);
    return true;
  }

  bool Moments(ID3D11DeviceContext *context, ID3D11Texture2D *source) {
    if (staging || !context || !source ||
        context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
      return false;
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    if (desc.Width != 512 || desc.Height != 512 || desc.MipLevels != 2 ||
        desc.ArraySize != 1 || desc.SampleDesc.Count != 1 ||
        desc.Format != DXGI_FORMAT_R16G16_UNORM)
      return false;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Device> device;
    context->GetDevice(&device);
    ComPtr<ID3D11Texture2D> copy;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &copy)))
      return false;
    count = 2;
    for (unsigned i = 0; i < count; ++i) {
      rows[i] = 512u >> i;
      rowBytes[i] = rows[i] * 4;
      bytes[i].resize(static_cast<std::size_t>(rows[i]) * rowBytes[i]);
    }
    staging = copy;
    context->CopyResource(staging.Get(), source);
    return true;
  }

  Result Poll(ID3D11DeviceContext *context) {
    if (!staging || !context ||
        context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
      return Result::failed;
    // At most two map calls per poll; a busy resource yields immediately.
    for (; completed < count; ++completed) {
      D3D11_MAPPED_SUBRESOURCE mapped{};
      const auto hr = context->Map(staging.Get(), completed, D3D11_MAP_READ,
                                   D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
      if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
        return Result::pending;
      if (FAILED(hr))
        return Result::failed;
      const bool valid =
          mapped.pData &&
          (rows[completed] == 1 || mapped.RowPitch >= rowBytes[completed]);
      if (valid)
        for (unsigned row = 0; row < rows[completed]; ++row)
          std::memcpy(bytes[completed].data() +
                          static_cast<std::size_t>(row) * rowBytes[completed],
                      static_cast<const std::uint8_t *>(mapped.pData) +
                          static_cast<std::size_t>(row) * mapped.RowPitch,
                      rowBytes[completed]);
      context->Unmap(staging.Get(), completed);
      if (!valid)
        return Result::failed;
    }
    return Result::ready;
  }
};
} // namespace MirrorSunShadowReadback
