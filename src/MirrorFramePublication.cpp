#if defined(RR_PLANAR_PUBLICATION_STANDALONE) || defined(MIRRORS_OF_SKYRIM_FLEET_TEST)
#	include "NoDestructor.h"

#	include <cmath>
#	include <windows.h>
#else
#	include "PCH.h"
#endif

#include "MirrorFramePublication.h"
#include "PlanarMipChainPolicy.h"

#include <cstddef>
#include <atomic>
#include <limits>
#include <utility>
#include <unordered_map>

namespace MirrorFramePublication
{
	namespace
	{
		SRWLOCK g_lock = SRWLOCK_INIT;
		stl::no_destructor<SnapshotSet> g_published{};
		// Indices borrow identities only; the snapshots retain the COM owners.
		// All three stores are changed under g_lock in one transaction.
		stl::no_destructor<std::unordered_map<std::uint32_t, std::size_t>> g_byForm{};
		stl::no_destructor<std::unordered_map<std::uint64_t, std::size_t>> g_byResource{};
		std::atomic_bool g_multiMirror{ false };
		AttemptToken g_activeAttempt{};
		std::uint64_t g_attemptSequence{ 0 };
		std::uint64_t g_captureSequence{ 0 };
		std::uint64_t g_nativePublicationSequence{ 0 };
		bool g_tokenExhausted{ false };

		class ExclusiveLock
		{
		public:
			ExclusiveLock() noexcept { AcquireSRWLockExclusive(&g_lock); }
			~ExclusiveLock() { ReleaseSRWLockExclusive(&g_lock); }
		};

		class SharedLock
		{
		public:
			SharedLock() noexcept { AcquireSRWLockShared(&g_lock); }
			~SharedLock() { ReleaseSRWLockShared(&g_lock); }
		};

		[[nodiscard]] std::uint64_t AdvanceNonZero(std::uint64_t& value) noexcept
		{
			if (g_tokenExhausted ||
				value == (std::numeric_limits<std::uint64_t>::max)()) {
				g_tokenExhausted = true;
				return 0;
			}
			++value;
			return value;
		}

		std::atomic<std::uint32_t> g_invalidateLogBudget{ 60 };

		void LogInvalidationNoexcept(const Snapshot& published, const char* reason) noexcept
		{
#if !defined(RR_PLANAR_PUBLICATION_STANDALONE) && !defined(MIRRORS_OF_SKYRIM_FLEET_TEST)
			if (!published.valid)
				return;
			auto budget = g_invalidateLogBudget.load(std::memory_order_acquire);
			if (budget == 0 || !g_invalidateLogBudget.compare_exchange_strong(
					budget, budget - 1, std::memory_order_acq_rel)) {
				return;
			}
			try {
				logger::info(
					"[RR][M3][wall-invalidate] reason={} candidate={:08X} generation={} capture={} lease={}",
					reason ? reason : "unspecified", published.candidateFormID,
					published.candidateGeneration, published.captureSequence,
					published.bridgeIdentity.ownerLeaseSequence);
			} catch (...) {
			}
#else
			(void)published;
			(void)reason;
#endif
		}

		void RemoveIndicesLocked(const Snapshot& old, const Snapshot* next = nullptr) noexcept
		{
			if (!old.valid) return;
			if (!next || old.candidateFormID != next->candidateFormID)
				g_byForm.get().erase(old.candidateFormID);
			for (const auto resource : {old.wallKey.colorResourceIdentity, old.wallKey.depthResourceIdentity}) {
				if (resource && (!next || (resource != next->wallKey.colorResourceIdentity &&
					resource != next->wallKey.depthResourceIdentity)))
					g_byResource.get().erase(resource);
			}
		}

		[[nodiscard]] bool PrepareIndicesLocked(const Snapshot& next) noexcept
		{
			const auto color = next.wallKey.colorResourceIdentity;
			const auto depth = next.wallKey.depthResourceIdentity;
			bool insertedForm = false, insertedColor = false, insertedDepth = false;
			try {
				insertedForm = g_byForm.get().try_emplace(next.candidateFormID, next.slot).second;
				insertedColor = g_byResource.get().try_emplace(color, next.slot).second;
				if (depth)
					insertedDepth = g_byResource.get().try_emplace(depth, next.slot).second;
				return true;
			} catch (...) {
				// Preserve the previous publication and its indices on any OOM.
				if (insertedForm) g_byForm.get().erase(next.candidateFormID);
				if (insertedColor) g_byResource.get().erase(color);
				if (insertedDepth) g_byResource.get().erase(depth);
				return false;
			}
		}

		void InvalidateSlotLocked(std::size_t slot, const char* reason) noexcept
		{
			if (slot >= g_published.get().size())
				return;
			auto& published = g_published.get()[slot];
			LogInvalidationNoexcept(published, reason);
			RemoveIndicesLocked(published);
			published = {};
			if (g_activeAttempt && g_activeAttempt.slot == slot) {
				(void)AdvanceNonZero(g_attemptSequence);
				g_activeAttempt = {};
			}
		}

		void InvalidateLocked(const char* reason = "internal") noexcept
		{
			for (const auto& published : g_published.get())
				LogInvalidationNoexcept(published, reason);
			(void) AdvanceNonZero(g_attemptSequence);
			g_activeAttempt = {};
			g_published.get() = {};
			g_byForm.get().clear();
			g_byResource.get().clear();
		}

		void RevokeAttemptLocked() noexcept
		{
			(void) AdvanceNonZero(g_attemptSequence);
			g_activeAttempt = {};
		}

		[[nodiscard]] bool IsFinite(const DirectX::XMFLOAT3& value) noexcept
		{
			return std::isfinite(value.x) && std::isfinite(value.y) &&
			       std::isfinite(value.z);
		}

		[[nodiscard]] bool IsFinite(const DirectX::XMFLOAT4X4& value) noexcept
		{
			const auto* elements = &value.m[0][0];
			for (std::size_t index = 0; index < 16; ++index) {
				if (!std::isfinite(elements[index]))
					return false;
			}
			return true;
		}

		[[nodiscard]] bool IsValidCapturePlane(
			const PlanarMirrorMath::Plane& value) noexcept
		{
			if (!IsFinite(value.normal) || !std::isfinite(value.distance))
				return false;

			const float lengthSquared = value.normal.x * value.normal.x +
				value.normal.y * value.normal.y + value.normal.z * value.normal.z;
			return std::isfinite(lengthSquared) &&
			       std::abs(lengthSquared - 1.0f) <= 1.0e-3f;
		}

		[[nodiscard]] bool IsReflectedOriginBehindPlane(
			const DirectX::XMFLOAT3& origin,
			const PlanarMirrorMath::Plane& plane) noexcept
		{
			const float side = plane.normal.x * origin.x +
				plane.normal.y * origin.y + plane.normal.z * origin.z - plane.distance;
			return std::isfinite(side) && side < -1.0e-4f;
		}

		[[nodiscard]] bool IsMatchingColorView(
			ID3D11ShaderResourceView* view,
			std::uint32_t width,
			std::uint32_t height,
			DXGI_FORMAT format) noexcept
		{
			if (!view)
				return false;
			D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
			view->GetDesc(&viewDescription);
			if (viewDescription.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				viewDescription.Format != format ||
				viewDescription.Texture2D.MostDetailedMip != 0 ||
				viewDescription.Texture2D.MipLevels < 1)
				return false;

			Microsoft::WRL::ComPtr<ID3D11Resource> resource{};
			view->GetResource(resource.GetAddressOf());
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture{};
			if (!resource || FAILED(resource.As(&texture)) || !texture)
				return false;

			D3D11_TEXTURE2D_DESC textureDescription{};
			texture->GetDesc(&textureDescription);
			// The capture target carries a generated full mip chain for the pane's
			// minified sampling.  A single-mip target is not a lower-quality valid
			// publication: it aliases thin vegetation and changes appearance with
			// pane angle, so mirror delivery fails closed instead.
			const auto expectedMipLevels =
				PlanarMipChainPolicy::FullMipCount(width, height);
			return textureDescription.Width == width &&
			       textureDescription.Height == height &&
			       PlanarMipChainPolicy::IsCompleteMinificationChain(
				       width, height, textureDescription.MipLevels) &&
			       viewDescription.Texture2D.MipLevels == expectedMipLevels &&
			       textureDescription.ArraySize == 1 &&
			       textureDescription.Format == format &&
			       textureDescription.SampleDesc.Count == 1 &&
			       (textureDescription.BindFlags & D3D11_BIND_RENDER_TARGET) != 0 &&
			       (textureDescription.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 &&
			       (textureDescription.MiscFlags &
				       D3D11_RESOURCE_MISC_GENERATE_MIPS) != 0;
		}

		[[nodiscard]] std::uint64_t ResourceIdentity(
			ID3D11ShaderResourceView* view) noexcept
		{
			if (!view)
				return 0;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource{};
			view->GetResource(resource.GetAddressOf());
			return reinterpret_cast<std::uint64_t>(resource.Get());
		}

		[[nodiscard]] bool IsMatchingDepthView(
			ID3D11ShaderResourceView* view,
			std::uint32_t width,
			std::uint32_t height) noexcept
		{
			if (!view)
				return false;
			D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
			view->GetDesc(&viewDescription);
			if (viewDescription.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				viewDescription.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
				viewDescription.Texture2D.MostDetailedMip != 0 ||
				viewDescription.Texture2D.MipLevels != 1)
				return false;

			Microsoft::WRL::ComPtr<ID3D11Resource> resource{};
			view->GetResource(resource.GetAddressOf());
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture{};
			if (!resource || FAILED(resource.As(&texture)) || !texture)
				return false;

			D3D11_TEXTURE2D_DESC textureDescription{};
			texture->GetDesc(&textureDescription);
			return textureDescription.Width == width &&
			       textureDescription.Height == height &&
			       textureDescription.MipLevels == 1 &&
			       textureDescription.ArraySize == 1 &&
			       textureDescription.Format == DXGI_FORMAT_R24G8_TYPELESS &&
			       textureDescription.SampleDesc.Count == 1 &&
			       (textureDescription.BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0 &&
			       (textureDescription.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0;
		}

		[[nodiscard]] bool SameAttempt(
			const AttemptToken& left,
			const AttemptToken& right) noexcept
		{
			return left.candidateFormID == right.candidateFormID &&
			       left.candidateGeneration == right.candidateGeneration &&
			       left.attemptSequence == right.attemptSequence && left.slot == right.slot;
		}
	}

	AttemptToken BeginReflectedAttempt(
		std::uint32_t candidateFormID,
		std::uint64_t candidateGeneration,
		AttemptStartPolicy policy, std::size_t slot) noexcept
	{
		ExclusiveLock lock{};
		if (slot >= MultiMirrorPolicy::kHand ||
			(!g_multiMirror.load(std::memory_order_relaxed) && slot != 0))
			return {};
		if (candidateFormID == 0 || candidateGeneration == 0 ||
			g_tokenExhausted) {
			InvalidateSlotLocked(slot, "begin-attempt-invalid-identity");
			return {};
		}

		try {
			if (slot >= g_published.get().size())
				g_published.get().resize(slot + 1);
		} catch (...) {
			// A new slot must never revoke another mirror's completed frame.
			return {};
		}
		const auto& published = g_published.get()[slot];
		const bool samePublishedIdentity = !published.valid ||
			(published.candidateFormID == candidateFormID &&
				published.candidateGeneration == candidateGeneration);
		if (policy == AttemptStartPolicy::kPreserveCompletedFrame &&
			samePublishedIdentity) {
			RevokeAttemptLocked();
		} else {
			InvalidateSlotLocked(slot, policy == AttemptStartPolicy::kPreserveCompletedFrame ?
				"begin-attempt-identity-changed" : "begin-attempt-invalidate-policy");
			RevokeAttemptLocked();
		}

		if (g_attemptSequence == 0)
			return {};
		g_activeAttempt = {
			candidateFormID,
			candidateGeneration,
			g_attemptSequence, slot };
		return g_activeAttempt;
	}

	bool CancelReflectedAttempt(const AttemptToken& attempt) noexcept
	{
		if (!attempt)
			return false;
		ExclusiveLock lock{};
		if (!SameAttempt(g_activeAttempt, attempt))
			return false;
		RevokeAttemptLocked();
		return true;
	}

	void Invalidate() noexcept
	{
		Invalidate("external-invalidate");
	}

	void Invalidate(const char* reason) noexcept
	{
		ExclusiveLock lock{};
		InvalidateLocked(reason);
	}

	void ConfigureMultiMirror(bool enabled) noexcept
	{
		ExclusiveLock lock{};
		if (g_multiMirror.exchange(enabled, std::memory_order_acq_rel) != enabled)
			InvalidateLocked("multi-mirror-mode-change");
	}

	bool MultiMirrorEnabled() noexcept
	{
		return g_multiMirror.load(std::memory_order_acquire);
	}

	void InvalidateSlot(std::size_t slot, const char* reason) noexcept
	{
		ExclusiveLock lock{};
		InvalidateSlotLocked(slot, reason);
	}

	void InvalidateCandidate(std::uint32_t formID, const char* reason) noexcept
	{
		ExclusiveLock lock{};
		if (const auto found = g_byForm.get().find(formID); found != g_byForm.get().end())
			InvalidateSlotLocked(found->second, reason);
		if (g_activeAttempt && g_activeAttempt.candidateFormID == formID)
			InvalidateSlotLocked(g_activeAttempt.slot, reason);
	}

	bool InvalidateExact(const WallSnapshotKey& key) noexcept
	{
		if (!IsValidWallSnapshotKey(key))
			return false;
		ExclusiveLock lock{};
		if (const auto found = g_byForm.get().find(key.candidateFormID); found != g_byForm.get().end()) {
			const auto slot = found->second;
			const auto& current = g_published.get()[slot];
			if (current.valid && current.wallKey == key) {
				InvalidateSlotLocked(slot, "invalidate-exact");
				return true;
			}
		}
		return false;
	}

	bool Publish(
		const AttemptToken& attempt,
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorSRV,
		const DirectX::XMFLOAT4X4& reflectedViewProjection,
		const DirectX::XMFLOAT3& reflectedOrigin,
		const PlanarMirrorMath::Plane& capturePlane,
		std::uint32_t width,
		std::uint32_t height,
		DXGI_FORMAT colorFormat,
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSRV,
		const DirectX::XMFLOAT4X4* mainViewProjection,
		const DirectX::XMFLOAT3* mainOrigin,
		std::uint64_t sourceMainWorldFrame,
		const BridgePublicationContext& bridgeContext) noexcept
	{
		const bool hasBridgeContext =
			bridgeContext != BridgePublicationContext{};
		if (!attempt || !colorSRV || width == 0 || height == 0 ||
			colorFormat == DXGI_FORMAT_UNKNOWN ||
			!IsFinite(reflectedViewProjection) || !IsFinite(reflectedOrigin) ||
			!IsValidCapturePlane(capturePlane) ||
			!IsReflectedOriginBehindPlane(reflectedOrigin, capturePlane) ||
			!IsMatchingColorView(colorSRV.Get(), width, height, colorFormat) ||
			(depthSRV && !IsMatchingDepthView(depthSRV.Get(), width, height)) ||
			(sourceMainWorldFrame != 0 &&
				(!mainViewProjection || !mainOrigin ||
					!IsFinite(*mainViewProjection) || !IsFinite(*mainOrigin))) ||
			(hasBridgeContext &&
				(bridgeContext.sourceSequence == 0 ||
					bridgeContext.mainViewFrame == 0 ||
					bridgeContext.ownerLeaseSequence == 0 || !depthSRV ||
					!HandMirrorRuntimeBridgePolicy::IsValidPrivateTarget(
						bridgeContext.privateTarget) ||
					bridgeContext.privateTarget.colorResourceToken !=
						ResourceIdentity(colorSRV.Get()) ||
					bridgeContext.privateTarget.depthResourceToken !=
						ResourceIdentity(depthSRV.Get()))))
			return false;

		ExclusiveLock lock{};
		if (!SameAttempt(attempt, g_activeAttempt) || attempt.slot >= g_published.get().size())
			return false;
		const auto colorIdentity = ResourceIdentity(colorSRV.Get());
		const auto depthIdentity = ResourceIdentity(depthSRV.Get());
		const auto form = g_byForm.get().find(attempt.candidateFormID);
		const auto color = g_byResource.get().find(colorIdentity);
		const auto depth = g_byResource.get().find(depthIdentity);
		if (!colorIdentity || (depthIdentity && colorIdentity == depthIdentity) ||
			(form != g_byForm.get().end() && form->second != attempt.slot) ||
			(color != g_byResource.get().end() && color->second != attempt.slot) ||
			(depth != g_byResource.get().end() && depth->second != attempt.slot))
			return false;

		Snapshot next{};
		next.colorSRV = std::move(colorSRV);
		next.depthSRV = std::move(depthSRV);
		next.reflectedViewProjection = reflectedViewProjection;
		next.reflectedOrigin = reflectedOrigin;
		if (mainViewProjection && mainOrigin &&
			IsFinite(*mainViewProjection) && IsFinite(*mainOrigin)) {
			next.mainViewProjection = *mainViewProjection;
			next.mainOrigin = *mainOrigin;
			next.mainViewValid = true;
			next.sourceMainWorldFrame = sourceMainWorldFrame;
		}
		next.capturePlane = capturePlane;
		next.candidateFormID = attempt.candidateFormID;
		next.candidateGeneration = attempt.candidateGeneration;
		next.captureSequence = AdvanceNonZero(g_captureSequence);
		if (next.captureSequence == 0) {
			InvalidateLocked("publish-capture-sequence-exhausted");
			return false;
		}
		const auto nativePublicationToken =
			AdvanceNonZero(g_nativePublicationSequence);
		if (nativePublicationToken == 0) {
			InvalidateLocked("publish-native-token-exhausted");
			return false;
		}
		next.wallKey = {
			.candidateFormID = attempt.candidateFormID,
			.candidateGeneration = attempt.candidateGeneration,
			.captureSequence = next.captureSequence,
			.colorResourceIdentity = ResourceIdentity(next.colorSRV.Get()),
			.depthResourceIdentity = ResourceIdentity(next.depthSRV.Get())
		};
		if (hasBridgeContext) {
			next.bridgeIdentity = {
				.owner = HandMirrorRuntimeBridgePolicy::MakeWallOwner({
					attempt.candidateFormID, attempt.candidateGeneration }),
				.ownerLeaseSequence = bridgeContext.ownerLeaseSequence,
				.attemptSequence = attempt.attemptSequence,
				.sourceSequence = bridgeContext.sourceSequence,
				.mainViewFrame = bridgeContext.mainViewFrame,
				.privateTarget = bridgeContext.privateTarget,
				.captureSequence = next.captureSequence,
				.nativePublicationToken = nativePublicationToken
			};
			if (!HandMirrorRuntimeBridgePolicy::IsValidPriorPublication(
					next.bridgeIdentity)) {
				return false;
			}
		}
		next.width = width;
		next.height = height;
		next.colorFormat = colorFormat;
		next.valid = true;
		next.slot = attempt.slot;
		if (!PrepareIndicesLocked(next))
			return false;
#if !defined(RR_PLANAR_PUBLICATION_STANDALONE) && !defined(MIRRORS_OF_SKYRIM_FLEET_TEST)
		{
			static std::atomic<std::uint32_t> publishLogBudget{ 30 };
			auto budget = publishLogBudget.load(std::memory_order_acquire);
			if (budget != 0 && publishLogBudget.compare_exchange_strong(
					budget, budget - 1, std::memory_order_acq_rel)) {
				try {
					logger::info(
						"[RR][M3][wall-published] candidate={:08X} generation={} capture={} lease={} bridge={}",
						next.candidateFormID, next.candidateGeneration,
						next.captureSequence, next.bridgeIdentity.ownerLeaseSequence,
						hasBridgeContext);
				} catch (...) {
				}
			}
		}
#endif
		RemoveIndicesLocked(g_published.get()[attempt.slot], &next);
		g_published.get()[attempt.slot] = std::move(next);
		// An attempt token authorizes exactly one completed publication.  Keep the
		// frame, but consume the token so an accidental duplicate Publish cannot
		// advance the capture sequence or replace its metadata.
		g_activeAttempt = {};
		return true;
	}

	ExactRetirementStatus RetireExactForSuccessor(
		const HandMirrorRuntimeBridgePolicy::PriorPublicationIdentity& prior,
		const HandMirrorRuntimeBridgePolicy::MirrorOwnerIdentity& successor,
		const std::uint64_t successorOwnerLeaseSequence,
		const std::uint64_t retirementToken,
		const std::array<HandMirrorRuntimeBridgePolicy::PrivateTargetIdentity, 2>&
			successorTargets,
		HandMirrorRuntimeBridgePolicy::PublicationRetirementReceipt& output) noexcept
	{
		using namespace HandMirrorRuntimeBridgePolicy;
		output = {};
		if (!IsValidPriorPublication(prior) ||
			(!IsValidOwner(successor) && !IsDarkOwner(successor)) ||
			retirementToken == 0 ||
			(IsValidOwner(successor) != (successorOwnerLeaseSequence != 0)) ||
			!ArePrivateTargetsPhysicallyDisjoint(
				successorTargets[0], successorTargets[1]) ||
			!ArePrivateTargetsPhysicallyDisjoint(
				prior.privateTarget, successorTargets[0]) ||
			!ArePrivateTargetsPhysicallyDisjoint(
				prior.privateTarget, successorTargets[1])) {
			return ExactRetirementStatus::kInvalidEvidence;
		}
		ExclusiveLock lock{};
		std::size_t slot = MultiMirrorPolicy::kNone;
		for (const auto& current : g_published.get()) {
			if (current.valid && current.bridgeIdentity == prior)
				slot = current.slot;
		}
		if (slot == MultiMirrorPolicy::kNone)
			return ExactRetirementStatus::kExactPriorUnavailable;
		const PublicationRetirementReceipt receipt{
			.retiredPublication = prior,
			.successorOwner = successor,
			.successorOwnerLeaseSequence = successorOwnerLeaseSequence,
			.retirementToken = retirementToken,
			.invalidationCommitted = true,
			.retiredPublicationNoLongerAcquirable = true,
			.retiredResourcesUnavailableToSuccessor = true
		};
		if (!MatchesExactRetirementReceipt(
				prior, successor, successorOwnerLeaseSequence, receipt)) {
			return ExactRetirementStatus::kInvalidEvidence;
		}
		InvalidateSlotLocked(slot, "exact-retirement");
		output = receipt;
		return ExactRetirementStatus::kRetired;
	}

	bool TryGetSnapshot(Snapshot& output) noexcept
	{
		SharedLock lock{};
		output = {};
		for (const auto& current : g_published.get()) {
			if (current.valid && current.captureSequence > output.captureSequence)
				output = current;
		}
		return output.valid && output.colorSRV && output.candidateFormID != 0 &&
		       output.candidateGeneration != 0 && output.captureSequence != 0 &&
		       output.width != 0 && output.height != 0 &&
		       output.colorFormat != DXGI_FORMAT_UNKNOWN &&
		       IsValidCapturePlane(output.capturePlane);
	}

	bool TryGetSnapshot(std::uint32_t formID, std::uint64_t generation,
		Snapshot& output) noexcept
	{
		SharedLock lock{};
		output = {};
		if (const auto found = g_byForm.get().find(formID); found != g_byForm.get().end()) {
			const auto& current = g_published.get()[found->second];
			if (current.valid && current.candidateFormID == formID &&
				current.candidateGeneration == generation) {
				output = current;
				return true;
			}
		}
		return false;
	}

	std::size_t GetSnapshots(SnapshotSet& output) noexcept
	{
		SharedLock lock{};
		try {
			output = g_published.get();
		} catch (...) {
			output.clear();
			return 0;
		}
		std::size_t count = 0;
		for (const auto& current : output)
			count += current.valid ? 1 : 0;
		return count;
	}

	bool CaptureResourcesDisjoint(const void* color, const void* depth) noexcept
	{
		if (!color || !depth || color == depth)
			return false;
		const auto colorID = reinterpret_cast<std::uint64_t>(color);
		const auto depthID = reinterpret_cast<std::uint64_t>(depth);
		SharedLock lock{};
		return !g_byResource.get().contains(colorID) && !g_byResource.get().contains(depthID);
	}
}
