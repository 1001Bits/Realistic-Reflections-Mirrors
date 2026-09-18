#include "MirrorOcclusionQueries.h"
#include "MirrorPaneRenderer.h"
#include "EngineDeviceIdentity.h"
#include <cstring>
#include <limits>

namespace MirrorOcclusion
{
    namespace
    {
#include "MirrorOcclusionVertexShader.inc"
        HRESULT ReadResult(ID3D11DeviceContext* context,ID3D11Query* query,UINT64* samples) noexcept
        {
#if defined(RR_MIRROR_OCCLUSION_TEST_HOOKS)
            if (Testing::result==Testing::Result::Pending) return S_FALSE;
            if (Testing::result==Testing::Result::Failed) return E_FAIL;
#endif
            __try { return context->GetData(query,samples,sizeof(*samples),D3D11_ASYNC_GETDATA_DONOTFLUSH); }
            __except(EXCEPTION_EXECUTE_HANDLER) { return E_FAIL; }
        }
        bool MatchesDevice(ID3D11DeviceChild* object,ID3D11Device* engine)
        {
            Microsoft::WRL::ComPtr<ID3D11Device> actual;
            object->GetDevice(actual.GetAddressOf());
            return EngineDeviceIdentityPolicy::OwnedByEngineDevice(
                EngineDeviceIdentity::Current(reinterpret_cast<std::uintptr_t>(engine)),
                reinterpret_cast<std::uintptr_t>(actual.Get()));
        }
        void WriteVertices(ID3D11DeviceContext* context,ID3D11Buffer* buffer,void* destination,
            const void* source,std::size_t bytes) noexcept
        {
            __try { std::memcpy(destination,source,bytes); }
            __finally { context->Unmap(buffer,0); }
        }
        void DrawQuery(ID3D11DeviceContext* context,ID3D11Query* query,UINT first) noexcept
        {
            context->Begin(query);
            __try { context->Draw(6,first); }
            __finally { context->End(query); }
        }
    }

    void Queries::Reconcile(std::span<const Probe> probes)
    {
        ++epoch_;
        for (const auto& probe:probes) {
            if (!probe.identity.Valid()) continue;
            auto& entry=entries_[probe.identity.formID];
            if (entry.identity!=probe.identity) { entry={}; entry.identity=probe.identity; }
            entry.retainedEpoch=epoch_;
        }
        std::erase_if(entries_,[this](const auto& item){return item.second.retainedEpoch!=epoch_;});
    }
    void Queries::InvalidateEntry(Entry& entry) noexcept
    {
        entry.result={}; entry.lastCulled=0;
        for (auto& slot:entry.slots) slot.ticket.valid=false;
    }
    void Queries::InvalidateEvidence() noexcept
    {
        for (auto& [id,entry]:entries_) { (void)id; InvalidateEntry(entry); }
    }
    void Queries::Poll(ID3D11DeviceContext* context) noexcept
    {
        if (!context || context!=context_.Get()) {
            InvalidateEvidence();
            return;
        }
        for (auto& [id,e]:entries_) {
            (void)id;
            for (auto& slot:e.slots) {
                if (!slot.pending) continue;
                UINT64 samples=0;
                const HRESULT hr=ReadResult(context,slot.query.Get(),&samples);
                if (hr==S_FALSE) { ++counters_.pending; continue; }
                slot.pending=false;
                if (hr!=S_OK) {
                    ++counters_.unavailable;
                    failed_=true;
                    // A failed query cannot certify hidden pixels or be reused.
                    if (slot.ticket.frame>=e.result.frame) {
                        e.result={}; e.result.frame=slot.ticket.frame;
                    }
                    slot.query.Reset(); continue;
                }
                if (!slot.ticket.valid) { ++counters_.discarded; continue; }
                slot.ticket.samples=samples;
                if (slot.ticket.frame>e.result.frame) e.result=slot.ticket;
                samples ? ++counters_.visible : ++counters_.hidden;
            }
        }
    }
    bool Queries::Occluded(const Probe& probe,const Pose& pose,std::uint64_t frame,std::uint64_t now) noexcept
    {
        if (failed_) return false;
        const auto it=entries_.find(probe.identity.formID);
        if (it==entries_.end() || it->second.identity!=probe.identity) return false;
        auto& e=it->second;
        const bool hidden=e.result.Occluded(probe.bounds,pose,frame,now);
        if (hidden) { e.lastCulled=frame; ++counters_.skipped; }
        return hidden;
    }
    bool Queries::RetainedImage(MultiMirrorPolicy::Identity id,std::uint64_t frame) const noexcept
    {
        if (failed_) return false;
        const auto it=entries_.find(id.formID);
        if (it==entries_.end() || it->second.identity!=id) return false;
        const auto last=it->second.lastCulled;
        // While fully hidden, keep presenting the retained image without consuming
        // the normal three-frame return lease. A newly revealed pane bridges only
        // three frames while its fresh capture is produced. All delivery proofs remain.
        return last && frame>=last && frame-last<3;
    }
    bool Queries::Initialize(ID3D11Device* device,ID3D11DeviceContext* context,
        ID3D11DepthStencilView* depth,const D3D11_VIEWPORT& viewport)
    {
        if (!device || !context || !depth || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE ||
            !MatchesDevice(context,device) || !MatchesDevice(depth,device)) return false;
        if (device_.Get()!=device || context_.Get()!=context) {
            device_=device; context_=context; sourceDepth_.Reset(); readDepth_.Reset();
            vertexShader_.Reset(); layout_.Reset(); vertices_.Reset(); rasterizer_.Reset(); depthState_.Reset(); capacity_=0;
            for (auto& [id,e]:entries_) { (void)id; e.slots={}; e.result={}; e.lastCulled=0; }
        }
        D3D11_DEPTH_STENCIL_VIEW_DESC view{}; depth->GetDesc(&view);
        // A one-slice array view of a one-slice flat target is equivalent to 2D.
        // Stereo targets/reverse Z are deliberately not inferred.
        const bool flatView=(view.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D && view.Texture2D.MipSlice==0) ||
            (view.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DARRAY && view.Texture2DArray.MipSlice==0 &&
             view.Texture2DArray.FirstArraySlice==0 && view.Texture2DArray.ArraySize==1);
        if (!flatView ||
            (view.Format!=DXGI_FORMAT_D32_FLOAT && view.Format!=DXGI_FORMAT_D24_UNORM_S8_UINT &&
             view.Format!=DXGI_FORMAT_D32_FLOAT_S8X24_UINT && view.Format!=DXGI_FORMAT_D16_UNORM)) return false;
        Ptr<ID3D11Resource> resource; depth->GetResource(resource.GetAddressOf());
        Ptr<ID3D11Texture2D> texture;
        if (!resource || FAILED(resource.As(&texture))) return false;
        D3D11_TEXTURE2D_DESC description{}; texture->GetDesc(&description);
        if (!Finite(viewport.TopLeftX) || !Finite(viewport.TopLeftY) || !Finite(viewport.Width) || !Finite(viewport.Height) ||
            viewport.TopLeftX<0 || viewport.TopLeftY<0 || viewport.Width<1 || viewport.Height<1 ||
            viewport.TopLeftX+viewport.Width>static_cast<float>(description.Width) ||
            viewport.TopLeftY+viewport.Height>static_cast<float>(description.Height) ||
            viewport.MinDepth!=0 || viewport.MaxDepth!=1 || description.SampleDesc.Count!=1 || description.ArraySize!=1)
            return false;
        if (sourceDepth_.Get()!=depth) {
            readDepth_.Reset();
            view.Flags=D3D11_DSV_READ_ONLY_DEPTH;
            if (view.Format==DXGI_FORMAT_D24_UNORM_S8_UINT || view.Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
                view.Flags|=D3D11_DSV_READ_ONLY_STENCIL;
            if (FAILED(device->CreateDepthStencilView(resource.Get(),&view,readDepth_.GetAddressOf()))) return false;
            sourceDepth_=depth;
            // Pending results belong to the previous depth allocation/view, too.
            for (auto& [id,e]:entries_) { (void)id; e.slots={}; e.result={}; e.lastCulled=0; }
        }
        if (vertexShader_ && layout_ && rasterizer_ && depthState_) return true;
        vertexShader_.Reset(); layout_.Reset(); rasterizer_.Reset(); depthState_.Reset();
        if (FAILED(device->CreateVertexShader(kMirrorOcclusionVS,sizeof(kMirrorOcclusionVS),nullptr,vertexShader_.GetAddressOf()))) return false;
        const D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        if (FAILED(device->CreateInputLayout(&element,1,kMirrorOcclusionVS,sizeof(kMirrorOcclusionVS),layout_.GetAddressOf()))) return false;
        D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
        D3D11_DEPTH_STENCIL_DESC state{}; state.DepthEnable=TRUE; state.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO; state.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        return SUCCEEDED(device->CreateRasterizerState(&raster,rasterizer_.GetAddressOf())) &&
            SUCCEEDED(device->CreateDepthStencilState(&state,depthState_.GetAddressOf()));
    }
    bool Queries::DrawBatch(ID3D11DeviceContext* context,bool touchTessellation,void* opaque) noexcept
    {
        const auto& batch=*static_cast<const Batch*>(opaque); auto& self=*batch.owner;
        context->SetPredication(nullptr,FALSE);
        context->OMSetRenderTargets(0,nullptr,self.readDepth_.Get()); // no colour target, no depth/stencil writes
        context->OMSetDepthStencilState(self.depthState_.Get(),0);
        context->OMSetBlendState(nullptr,nullptr,0xFFFFFFFFu);
        context->RSSetViewports(1,batch.viewport); context->RSSetState(self.rasterizer_.Get());
        context->IASetInputLayout(self.layout_.Get());
        const UINT stride=sizeof(DirectX::XMFLOAT4),offset=0;
        ID3D11Buffer* buffer=self.vertices_.Get();
        context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(self.vertexShader_.Get(),nullptr,0); context->PSSetShader(nullptr,nullptr,0);
        context->GSSetShader(nullptr,nullptr,0);
        if (touchTessellation) { context->HSSetShader(nullptr,nullptr,0); context->DSSetShader(nullptr,nullptr,0); }
        for (const auto& draw:batch.draws) {
            DrawQuery(context,draw.slot->query.Get(),draw.first);
            draw.slot->pending=true;
        }
        return true;
    }
    bool Queries::Submit(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11DepthStencilView* depth,
        const D3D11_VIEWPORT& viewport,const DirectX::XMFLOAT4X4& matrix,const Pose& pose,
        std::uint64_t frame,std::uint64_t now,std::span<const Probe> probes)
    {
        struct Attempt {
            Queries& owner; bool completed{};
            ~Attempt() { if (!completed) owner.InvalidateEvidence(); }
        } attempt{*this};
        if (!frame || !Initialize(device,context,depth,viewport)) { ++counters_.rejected; return false; }
        Poll(context);
        if (failed_) return false;
        draws_.clear(); vertexData_.clear();
        for (const auto& probe:probes) {
            const auto it=entries_.find(probe.identity.formID);
            if (it==entries_.end() || it->second.identity!=probe.identity) continue;
            auto& e=it->second;
            const auto rect=Project(probe.bounds,pose,matrix,viewport.Width,viewport.Height);
            if (!rect.valid) { InvalidateEntry(e); continue; }
            const Evidence ticket{frame,now,0,pose,probe.bounds,true};
            if (rect.outside) { e.result=ticket; continue; }
            auto slot=std::find_if(e.slots.begin(),e.slots.end(),[](const Slot& s){return !s.pending;});
            if (slot==e.slots.end()) { ++counters_.unavailable; continue; } // never overwrite/wait on an outstanding query
            if (!slot->query) {
                const D3D11_QUERY_DESC query{D3D11_QUERY_OCCLUSION,0};
                if (FAILED(device->CreateQuery(&query,slot->query.GetAddressOf()))) {
                    ++counters_.unavailable; InvalidateEntry(e); continue;
                }
                ++counters_.queryCreations;
            }
            if (vertexData_.size()>(std::numeric_limits<UINT>::max)()/sizeof(DirectX::XMFLOAT4)-6) return false;
            const auto first=static_cast<UINT>(vertexData_.size());
            const DirectX::XMFLOAT4 a{rect.left,rect.bottom,rect.depth,1},b{rect.left,rect.top,rect.depth,1},
                c{rect.right,rect.bottom,rect.depth,1},d{rect.right,rect.top,rect.depth,1};
            vertexData_.insert(vertexData_.end(),{a,b,c,c,b,d});
            slot->ticket=ticket; draws_.push_back({&*slot,first});
        }
        if (draws_.empty()) { attempt.completed=true; return true; }
        const auto bytes=static_cast<UINT>(vertexData_.size()*sizeof(DirectX::XMFLOAT4));
        if (capacity_<bytes) {
            Ptr<ID3D11Buffer> buffer;
            D3D11_BUFFER_DESC description{}; description.ByteWidth=bytes; description.Usage=D3D11_USAGE_DYNAMIC;
            description.BindFlags=D3D11_BIND_VERTEX_BUFFER; description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device->CreateBuffer(&description,nullptr,buffer.GetAddressOf()))) return false;
            vertices_=std::move(buffer); capacity_=bytes;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(vertices_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
        WriteVertices(context,vertices_.Get(),mapped.pData,vertexData_.data(),bytes);
        Batch batch{this,&viewport,draws_};
        const auto status=MirrorPaneRenderer::RunDepthProbeWithPreservedState(context,DrawBatch,&batch);
        if (!MirrorPaneRenderer::IsSuccessfulDraw(status)) {
            ++counters_.rejected;
            failed_=status==MirrorPaneRenderer::DrawStatus::kContextStateRestoreFailed ||
                status==MirrorPaneRenderer::DrawStatus::kContextStateCaptureFailed;
            return false;
        }
        counters_.issued+=draws_.size();
        attempt.completed=true;
        return true;
    }
}
