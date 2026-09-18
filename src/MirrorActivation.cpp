#include "MirrorActivation.h"

#include "MirrorVRReadinessPolicy.h"
#include "NoDestructor.h"

#include <utility>

namespace MirrorActivation
{
	namespace
	{
		[[nodiscard]] constexpr bool IsSupported(Runtime runtime) noexcept
		{
			if (runtime == Runtime::kSkyrimSE1597 ||
				runtime == Runtime::kSkyrimAE161170) {
				return true;
			}
			if (runtime == Runtime::kSkyrimVR1415) {
				return MirrorVRReadinessPolicy::
					kCurrentSkyrimVR1415Decision.Ready();
			}
			return false;
		}

		void FailClosed(ActivationMasks& masks) noexcept
		{
			masks.active = 0;
			masks.hookRequest = 0;
			masks.runtimeEnable = 0;
			masks.activeProducts = 0;
		}
	}

	OriginMask PreparedState::OriginsFor(Feature feature) const noexcept
	{
		return Contains(masks_.requested, feature) ? origins_ : 0;
	}

	PreparedState Prepare(Runtime runtime) noexcept
	{
		PreparedState result;
		result.runtime_ = runtime;
		if (runtime == Runtime::kUnknown) {
			result.diagnostics_ |= Mask(Diagnostic::kUnknownRuntime);
			return result;
		}
		if (!IsSupported(runtime)) {
			result.diagnostics_ |= Mask(Diagnostic::kUnsupportedRuntime);
			return result;
		}

		result.origins_ = Mask(Origin::kPublicMirrorsOfSkyrim);
		result.supportedProducts_ = kMirrorProducts;
		result.masks_.requested = kMirrorFeatures;
		result.masks_.hookRequest = kMirrorFeatures;
		result.masks_.runtimeEnable = kMirrorFeatures;
		result.masks_.requestedProducts = kMirrorProducts;
		return result;
	}

	CommittedState Commit(
		const PreparedState& prepared,
		const ProductDataReadiness& readiness) noexcept
	{
		CommittedState result;
		result.prepared_ = prepared;
		result.readiness_ = readiness;
		result.diagnostics_ = prepared.Diagnostics();
		result.conflicts_ = prepared.Conflicts();
		result.masks_ = prepared.Masks();

		if (prepared.IsConflicted()) {
			FailClosed(result.masks_);
			return result;
		}
		if (!Contains(
				prepared.Masks().requestedProducts,
				Product::kMirrorsOfSkyrim)) {
			result.masks_.active = 0;
			result.masks_.runtimeEnable = 0;
			return result;
		}

		switch (readiness.Get(Product::kMirrorsOfSkyrim)) {
		case DataReadiness::kReady:
			result.masks_.active = kMirrorFeatures;
			result.masks_.runtimeEnable = kMirrorFeatures;
			result.masks_.activeProducts = kMirrorProducts;
			break;
		case DataReadiness::kConflict:
			result.diagnostics_ |= Mask(Diagnostic::kDataConflict);
			result.conflicts_ |= Mask(Conflict::kData);
			FailClosed(result.masks_);
			break;
		case DataReadiness::kUnknown:
			result.diagnostics_ |= Mask(Diagnostic::kDataPending);
			result.masks_.active = 0;
			result.masks_.runtimeEnable = 0;
			break;
		case DataReadiness::kNotRequired:
		case DataReadiness::kMissing:
		default:
			result.diagnostics_ |= Mask(Diagnostic::kDataMissing);
			result.masks_.active = 0;
			result.masks_.runtimeEnable = 0;
			break;
		}
		return result;
	}

	bool Lifecycle::PrepareAtInputLoaded(Runtime runtime) noexcept
	{
		std::scoped_lock lock{ mutex_ };
		PreparedState next = MirrorActivation::Prepare(runtime);
		if (stage_ != LifecycleStage::kCold) {
			next.diagnostics_ |= Mask(Diagnostic::kPrepareRepeated);
			next.conflicts_ |= Mask(Conflict::kPrepareRepeated);
			FailClosed(next.masks_);
			try {
				prepared_ =
					std::make_shared<const PreparedState>(std::move(next));
				committed_.reset();
			} catch (...) {
				prepared_.reset();
				committed_.reset();
			}
			stage_ = LifecycleStage::kConflict;
			return false;
		}

		try {
			prepared_ = std::make_shared<const PreparedState>(std::move(next));
		} catch (...) {
			stage_ = LifecycleStage::kConflict;
			return false;
		}
		stage_ = prepared_->IsConflicted() ? LifecycleStage::kConflict :
			LifecycleStage::kPrepared;
		return stage_ == LifecycleStage::kPrepared;
	}

	bool Lifecycle::CommitDataReadiness(
		const ProductDataReadiness& readiness) noexcept
	{
		std::scoped_lock lock{ mutex_ };
		if (!prepared_) {
			PreparedState missing;
			missing.diagnostics_ |= Mask(Diagnostic::kCommitBeforePrepare);
			missing.conflicts_ |= Mask(Conflict::kCommitBeforePrepare);
			FailClosed(missing.masks_);
			CommittedState failed = MirrorActivation::Commit(missing, readiness);
			failed.diagnostics_ |= Mask(Diagnostic::kCommitBeforePrepare);
			failed.conflicts_ |= Mask(Conflict::kCommitBeforePrepare);
			FailClosed(failed.masks_);
			try {
				prepared_ =
					std::make_shared<const PreparedState>(std::move(missing));
				committed_ =
					std::make_shared<const CommittedState>(std::move(failed));
			} catch (...) {
				prepared_.reset();
				committed_.reset();
			}
			stage_ = LifecycleStage::kConflict;
			return false;
		}

		CommittedState next = MirrorActivation::Commit(*prepared_, readiness);
		if (committed_ || stage_ == LifecycleStage::kCommitted) {
			next.diagnostics_ |= Mask(Diagnostic::kCommitRepeated);
			next.conflicts_ |= Mask(Conflict::kCommitRepeated);
			FailClosed(next.masks_);
		}
		try {
			committed_ =
				std::make_shared<const CommittedState>(std::move(next));
		} catch (...) {
			stage_ = LifecycleStage::kConflict;
			return false;
		}
		stage_ = committed_->IsConflicted() ? LifecycleStage::kConflict :
			LifecycleStage::kCommitted;
		return stage_ == LifecycleStage::kCommitted;
	}

	LifecycleStage Lifecycle::Stage() const noexcept
	{
		std::scoped_lock lock{ mutex_ };
		return stage_;
	}

	std::shared_ptr<const PreparedState> Lifecycle::Prepared() const noexcept
	{
		std::scoped_lock lock{ mutex_ };
		return prepared_;
	}

	std::shared_ptr<const CommittedState> Lifecycle::Committed() const noexcept
	{
		std::scoped_lock lock{ mutex_ };
		return committed_;
	}

	Lifecycle& GlobalLifecycle() noexcept
	{
		static stl::no_destructor<Lifecycle> lifecycle;
		return lifecycle.get();
	}
}
