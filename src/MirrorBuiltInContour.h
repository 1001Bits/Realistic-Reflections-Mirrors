#pragma once
#include <DirectXMath.h>
#include <array>
#include <span>

namespace MirrorBuiltInContour
{
	// mirror02.nif: pane y=[-52,52], z=[10,182]. The timber spandrels
	// cover z >= 188 - 6*abs(y)/7. Trim those corners from the custom draw
	// itself, so distant depth precision cannot expose rectangular triangles.
	inline constexpr float kShoulder = (188.0F - 6.0F * 52.0F / 7.0F - 96.0F) / 86.0F;
	inline constexpr float kCrownHalfWidth = 7.0F / 52.0F;
	inline const std::array<DirectX::XMFLOAT2, 12> kWhiterunArch{
		DirectX::XMFLOAT2{-1,-1}, {1,-1}, {1,kShoulder},
		{-1,-1}, {1,kShoulder}, {kCrownHalfWidth,1},
		{-1,-1}, {kCrownHalfWidth,1}, {-kCrownHalfWidth,1},
		{-1,-1}, {-kCrownHalfWidth,1}, {-1,kShoulder}
	};
}
