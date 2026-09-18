#pragma once

#include "MirrorOcclusionPolicy.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <unordered_map>

namespace MirrorOcclusion
{
#if defined(RR_MIRROR_OCCLUSION_TEST_HOOKS)
    namespace Testing { enum class Result { Native, Pending, Failed }; inline Result result{Result::Native}; }
#endif
    struct Probe { MultiMirrorPolicy::Identity identity{}; Bounds bounds{}; };
    struct Counters
    {
        std::uint64_t issued{}, visible{}, hidden{}, pending{}, unavailable{}, rejected{}, skipped{}, discarded{}, queryCreations{};
    };
    // Render-thread owned; no engine pointers, synchronous readback, or global mirror cap.
    class Queries
    {
    public:
        void Reconcile(std::span<const Probe> probes);
        void Poll(ID3D11DeviceContext* context) noexcept;
        // Revoke completed and in-flight decisions without releasing reusable GPU
        // resources. Outstanding queries still drain before their slots are reused.
        void InvalidateEvidence() noexcept;
        bool Occluded(const Probe& probe, const Pose& pose, std::uint64_t frame, std::uint64_t now) noexcept;
        bool RetainedImage(MultiMirrorPolicy::Identity id, std::uint64_t frame) const noexcept;
        bool Submit(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11DepthStencilView* depth,
            const D3D11_VIEWPORT& viewport, const DirectX::XMFLOAT4X4& matrix, const Pose& pose,
            std::uint64_t frame, std::uint64_t now, std::span<const Probe> probes);
        const Counters& Stats() const noexcept { return counters_; }
        std::size_t Size() const noexcept { return entries_.size(); }
        bool Faulted() const noexcept { return failed_; }
    private:
        template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
        struct Slot { Ptr<ID3D11Query> query{}; Evidence ticket{}; bool pending{}; };
        struct Entry
        {
            MultiMirrorPolicy::Identity identity{};
            std::array<Slot,kQuerySlots> slots{};
            Evidence result{};
            std::uint64_t retainedEpoch{}, lastCulled{};
        };
        static void InvalidateEntry(Entry& entry) noexcept;
        struct Draw { Slot* slot{}; UINT first{}; };
        struct Batch { Queries* owner{}; const D3D11_VIEWPORT* viewport{}; std::span<const Draw> draws{}; };
        static bool DrawBatch(ID3D11DeviceContext*,bool,void*) noexcept;
        bool Initialize(ID3D11Device*,ID3D11DeviceContext*,ID3D11DepthStencilView*,const D3D11_VIEWPORT&);
        std::unordered_map<std::uint32_t,Entry> entries_{};
        std::uint64_t epoch_{};
        Ptr<ID3D11Device> device_{};
        Ptr<ID3D11DeviceContext> context_{};
        Ptr<ID3D11DepthStencilView> sourceDepth_{}, readDepth_{};
        Ptr<ID3D11VertexShader> vertexShader_{};
        Ptr<ID3D11InputLayout> layout_{};
        Ptr<ID3D11Buffer> vertices_{};
        Ptr<ID3D11RasterizerState> rasterizer_{};
        Ptr<ID3D11DepthStencilState> depthState_{};
        UINT capacity_{};
        bool failed_{};
        std::vector<DirectX::XMFLOAT4> vertexData_{};
        std::vector<Draw> draws_{};
        Counters counters_{};
    };
}
