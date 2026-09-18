#pragma once
#include "MirrorShadowCasterVolume.h"
#include <vector>

class MirrorShadowCasterSet
{
    std::vector<MirrorShadowCasterVolume> volumes_;
    bool unpruned_{true};
public:
    void Reset(const MirrorShadowCasterVolume& current) noexcept
    {
        volumes_.clear();unpruned_=!current.valid;
        if (!unpruned_ && !Add(current)) unpruned_=true;
    }
    bool Add(const MirrorShadowCasterVolume& volume) noexcept
    {
        if (unpruned_) return true;
        if (!volume.valid) { unpruned_=true;volumes_.clear();return true; }
        try { volumes_.push_back(volume);return true; } catch (...) { return false; }
    }
    bool Assign(const MirrorShadowCasterSet& source) noexcept
    {
        try { volumes_=source.volumes_;unpruned_=source.unpruned_;return true; }
        catch (...) { volumes_.clear();unpruned_=true;return false; }
    }
    bool Intersects(const DirectX::XMFLOAT3& center,float radius) const noexcept
    {
        return unpruned_ || std::any_of(volumes_.begin(),volumes_.end(),
            [&](const auto& v){return v.Intersects(center,radius);});
    }
    bool Covers(const MirrorShadowCasterVolume& request) const noexcept
    {
        return unpruned_ || (request.valid && std::any_of(volumes_.begin(),volumes_.end(),
            [&](const auto& v){return v.Covers(request);}));
    }
    bool Unpruned() const noexcept { return unpruned_; }
    std::size_t Size() const noexcept { return volumes_.size(); }
};
