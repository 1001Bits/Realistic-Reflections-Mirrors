#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <algorithm>
#include <cmath>
#include <DirectXMath.h>

// A shadow caster need not be visible. It must, however, be able to send a ray
// into the receiver frustum. Extrude that frustum towards the light, retaining
// a conservative half-space hull (not a main-camera visibility test).
struct MirrorShadowCasterVolume
{
    using Point = DirectX::XMFLOAT3;
    struct Plane { double x{},y{},z{},w{}; };
    // The six receiver planes and six map-box planes, swept towards the sun.
    std::array<Plane,12> planes{};
    unsigned planeCount{12};
    std::array<Point,96> vertices{}; // bounded receiver vertices and their upstream ends
    unsigned vertexCount{};
    Point origin{};
    double filterGuard{};
    // Spare coverage permits neighbouring mirrors to share a current-frame map.
    // Reuse validates the new receiver's entire upstream hull, including PCF.
    static constexpr double ReuseGuard=128;
    bool valid{};

    static bool Finite(float x) noexcept
    { return (std::bit_cast<std::uint32_t>(x)&0x7F800000u)!=0x7F800000u; }
    static bool Finite(double x) noexcept
    { return (std::bit_cast<std::uint64_t>(x)&0x7FF0000000000000ull)!=0x7FF0000000000000ull; }
    static bool Finite(const Point& p) noexcept
    { return Finite(p.x) && Finite(p.y) && Finite(p.z); }
    double Distance(const Plane& p,const Point& v) const noexcept
    { return p.x*(double(v.x)-origin.x)+p.y*(double(v.y)-origin.y)+p.z*(double(v.z)-origin.z)+p.w; }

    bool Intersects(const Point& center,float radius) const noexcept
    {
        // Unknown/degenerate structural bounds never justify discarding a tree.
        if(!valid || !Finite(center) || !Finite(radius) || radius<=1) return true;
        for(unsigned i=0;i<planeCount;++i)
            if(Distance(planes[i],center)<-(double(radius)+filterGuard+ReuseGuard)) return false;
        return true;
    }
    bool Covers(const MirrorShadowCasterVolume& requested) const noexcept
    {
        if(!valid) return true; // an unpruned map covers every receiver
        if(!requested.valid) return false;
        // Linear planes attain their minima at vertices of the swept frustum.
        // Account for the requested filter footprint, not its optional reuse pad.
        if (requested.vertexCount>requested.vertices.size()) return false;
        for(unsigned v=0;v<requested.vertexCount;++v) {
            if (!Finite(requested.vertices[v])) return false;
            for(unsigned i=0;i<planeCount;++i)
                if(Distance(planes[i],requested.vertices[v])<requested.filterGuard-filterGuard-ReuseGuard) return false;
        }
        return true;
    }

    // Intersect the receiver frustum with the existing shadow map before
    // extrusion. Huge game far planes otherwise prevent useful sharing after
    // tiny camera changes, even though those far corners receive no map pixels.
    static MirrorShadowCasterVolume BuildClipped(const DirectX::XMFLOAT4X4& vp,
        const Point& eye,const DirectX::XMFLOAT4X4& worldToTexture,
        const Point& light,float travel,float guard,float mapPadding=0) noexcept
    {
        MirrorShadowCasterVolume out;out.origin=eye;out.filterGuard=guard;
        out.planeCount=12;out.vertexCount=0;
        if (!Finite(eye) || !Finite(light) || !Finite(travel) || travel<=0 || !Finite(guard) || guard<0 ||
            !Finite(mapPadding) || mapPadding<0) return {};
        const double length=std::sqrt(double(light.x)*light.x+double(light.y)*light.y+double(light.z)*light.z);
        if (!Finite(length) || length<1.e-8) return {};
        const double ray[3]{light.x/length,light.y/length,light.z/length};
        const auto plane=[&](unsigned index,const DirectX::XMFLOAT4X4& matrix,bool texture) {
            for (const auto& row:matrix.m) for (float x:row) if (!Finite(x)) return false;
            for (unsigned side=0;side<6;++side) {
                double p[4]{};
                for (unsigned row=0;row<4;++row)
                    p[row]=texture ? (side%2 ? double(matrix.m[row][3])-matrix.m[row][side/2] : matrix.m[row][side/2]) :
                        (side==4 ? matrix.m[row][2] : double(matrix.m[row][3])+(side%2?-1:1)*double(matrix.m[row][side/2]));
                const double n=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
                if (!Finite(n) || n<1.e-12) return false;
                auto& dest=out.planes[index+side];dest={p[0]/n,p[1]/n,p[2]/n,p[3]/n};
                if (texture) {
                    dest.w+=dest.x*eye.x+dest.y*eye.y+dest.z*eye.z;
                    // A compatible shared map may move by the existing map
                    // coverage guard. Its newly exposed edges also need casters.
                    dest.w+=mapPadding;
                }
            }
            return true;
        };
        if (!plane(0,vp,false) || !plane(6,worldToTexture,true)) return {};
        const auto cross=[](const Plane& a,const Plane& b) {
            return std::array<double,3>{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
        };
        for (unsigned i=0;i<12;++i) for (unsigned j=i+1;j<12;++j) for (unsigned k=j+1;k<12;++k) {
            const auto& a=out.planes[i];const auto& b=out.planes[j];const auto& c=out.planes[k];
            const auto bc=cross(b,c),ca=cross(c,a),ab=cross(a,b);
            const double det=a.x*bc[0]+a.y*bc[1]+a.z*bc[2];
            if (!Finite(det) || std::abs(det)<1.e-10) continue;
            double point[3]{};for (unsigned d=0;d<3;++d) point[d]=(-a.w*bc[d]-b.w*ca[d]-c.w*ab[d])/det;
            if (!Finite(point[0]) || !Finite(point[1]) || !Finite(point[2])) return {};
            bool inside=true;
            for (const auto& p:std::span<const Plane>(out.planes.data(),12))
                if (p.x*point[0]+p.y*point[1]+p.z*point[2]+p.w<-.001) { inside=false;break; }
            if (!inside) continue;
            Point vertex{float(point[0]+eye.x),float(point[1]+eye.y),float(point[2]+eye.z)};
            bool duplicate=false;
            for (unsigned v=0;v<out.vertexCount;v+=2) {
                const auto& old=out.vertices[v];
                if (std::abs(old.x-vertex.x)<.01f && std::abs(old.y-vertex.y)<.01f && std::abs(old.z-vertex.z)<.01f) duplicate=true;
            }
            if (duplicate) continue;
            if (out.vertexCount+2>out.vertices.size()) return {}; // numerical degeneracy: retain full collection
            out.vertices[out.vertexCount++]=vertex;
            out.vertices[out.vertexCount++]={float(vertex.x-ray[0]*travel),float(vertex.y-ray[1]*travel),float(vertex.z-ray[2]*travel)};
            if (!Finite(vertex) || !Finite(out.vertices[out.vertexCount-1])) return {};
        }
        if (out.vertexCount<8) return {};
        for (unsigned i=0;i<12;++i) {
            auto& p=out.planes[i];p.w+=(std::max)(0.,p.x*ray[0]+p.y*ray[1]+p.z*ray[2])*travel;
        }
        out.valid=true;if (!out.Covers(out)) return {};return out;
    }
};
