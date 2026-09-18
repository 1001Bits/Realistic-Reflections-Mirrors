#pragma once

#include "MirrorPaneRenderer.h"

#include <span>

namespace HandMirrorStereoPresentation
{
	struct Eye
	{
		MirrorPaneRenderer::MainView mainView{};
		D3D11_VIEWPORT viewport{};
	};

	// One completed reflection is projected onto the physical pane in each
	// native eye. This does not mint a second capture or binocular publication.
	[[nodiscard]] inline MirrorPaneRenderer::DrawStatus Draw(
		MirrorPaneRenderer::Renderer& renderer,
		ID3D11DeviceContext* context,
		const MirrorPaneRenderer::DrawRequest& request,
		const std::span<const Eye> eyes) noexcept
	{
		using namespace MirrorPaneRenderer;
		if (eyes.size() != 2)
			return DrawStatus::kInvalidArgument;
		// Reject a missing/invalid eye projection before writing either eye.
		for (const auto& eye : eyes) {
			const auto coverage = ClassifyProjectionCoverage(
				request.pane, eye.mainView, request.frame);
			if (coverage != DrawStatus::kDrawn)
				return coverage;
		}
		auto eyeRequest = request;
		// A flat-screen motion history cannot describe both native eye views.
		eyeRequest.targets.motionRTV = nullptr;
		eyeRequest.motionHistory = {};
		for (const auto& eye : eyes) {
			eyeRequest.mainView = eye.mainView;
			eyeRequest.targets.viewport = eye.viewport;
			const auto status = renderer.Draw(context, eyeRequest);
			if (status != DrawStatus::kDrawn)
				return status;
		}
		return DrawStatus::kDrawn;
	}
}
