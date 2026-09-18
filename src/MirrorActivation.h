#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

namespace MirrorActivation
{
	inline constexpr std::uint32_t kPolicyVersion = 1;

	enum class Runtime : std::uint8_t
	{
		kUnknown = 0,
		kSkyrimSE1597 = 1,
		kSkyrimAE161170 = 2,
		kSkyrimVR1415 = 3
	};

	enum class Product : std::uint32_t
	{
		kNone = 0,
		kMirrorsOfSkyrim = 1u << 0
	};

	enum class Feature : std::uint32_t
	{
		kNone = 0,
		kSharedSecondView = 1u << 0,
		kSharedPostSceneListFence = 1u << 1,
		kSharedCameraOverride = 1u << 2,
		kMirrorPlayerInclusion = 1u << 3,
		kMirrorPaneDelivery = 1u << 4,
		kMirrorPaneDraw = 1u << 5
	};

	enum class Origin : std::uint64_t
	{
		kNone = 0,
		kPublicMirrorsOfSkyrim = 1ull << 0,
		kLegacySharedSecondView = 1ull << 4
	};

	enum class DataReadiness : std::uint8_t
	{
		kUnknown = 0,
		kNotRequired = 1,
		kReady = 2,
		kMissing = 3,
		kConflict = 4
	};

	enum class Diagnostic : std::uint32_t
	{
		kNone = 0,
		kUnknownRuntime = 1u << 0,
		kUnsupportedRuntime = 1u << 1,
		kDataPending = 1u << 2,
		kDataMissing = 1u << 3,
		kDataConflict = 1u << 4,
		kPrepareRepeated = 1u << 5,
		kCommitBeforePrepare = 1u << 6,
		kCommitRepeated = 1u << 7
	};

	enum class Conflict : std::uint32_t
	{
		kNone = 0,
		kData = 1u << 0,
		kPrepareRepeated = 1u << 1,
		kCommitBeforePrepare = 1u << 2,
		kCommitRepeated = 1u << 3
	};

	using ProductMask = std::uint32_t;
	using FeatureMask = std::uint32_t;
	using OriginMask = std::uint64_t;
	using DiagnosticMask = std::uint32_t;
	using ConflictMask = std::uint32_t;

	[[nodiscard]] constexpr ProductMask Mask(Product value) noexcept
	{
		return static_cast<ProductMask>(value);
	}

	[[nodiscard]] constexpr FeatureMask Mask(Feature value) noexcept
	{
		return static_cast<FeatureMask>(value);
	}

	[[nodiscard]] constexpr OriginMask Mask(Origin value) noexcept
	{
		return static_cast<OriginMask>(value);
	}

	[[nodiscard]] constexpr DiagnosticMask Mask(Diagnostic value) noexcept
	{
		return static_cast<DiagnosticMask>(value);
	}

	[[nodiscard]] constexpr ConflictMask Mask(Conflict value) noexcept
	{
		return static_cast<ConflictMask>(value);
	}

	[[nodiscard]] constexpr bool Contains(ProductMask mask, Product value) noexcept
	{
		return (mask & Mask(value)) != 0;
	}

	[[nodiscard]] constexpr bool Contains(FeatureMask mask, Feature value) noexcept
	{
		return (mask & Mask(value)) != 0;
	}

	[[nodiscard]] constexpr bool Contains(OriginMask mask, Origin value) noexcept
	{
		return (mask & Mask(value)) != 0;
	}

	[[nodiscard]] constexpr bool Contains(
		DiagnosticMask mask,
		Diagnostic value) noexcept
	{
		return (mask & Mask(value)) != 0;
	}

	[[nodiscard]] constexpr bool Contains(
		ConflictMask mask,
		Conflict value) noexcept
	{
		return (mask & Mask(value)) != 0;
	}

	[[nodiscard]] constexpr bool ContainsAll(
		FeatureMask mask,
		FeatureMask required) noexcept
	{
		return (mask & required) == required;
	}

	inline constexpr ProductMask kMirrorProducts =
		Mask(Product::kMirrorsOfSkyrim);
	inline constexpr FeatureMask kSharedFeatures =
		Mask(Feature::kSharedSecondView) |
		Mask(Feature::kSharedPostSceneListFence) |
		Mask(Feature::kSharedCameraOverride);
	inline constexpr FeatureMask kMirrorFeatures =
		kSharedFeatures |
		Mask(Feature::kMirrorPlayerInclusion) |
		Mask(Feature::kMirrorPaneDelivery) |
		Mask(Feature::kMirrorPaneDraw);

	struct MirrorCorrectnessProfile
	{
		bool exactMainSceneLists{ true };
		bool supplementalActorCycle{ true };
		bool supplementalNearReferences{ true };
		bool stableAdmission{ true };
		bool cameraVariantGuard{ true };
		bool taggedDeferredMainViewPair{ true };
		bool projectionContinuityLease{ true };
		bool legacyPreviousDeliveryPair{ false };
		bool ownerExclusion{ false };
		bool shadowMapBypass{ false };
		bool supplementalWorldRoot{ false };
	};

	inline constexpr MirrorCorrectnessProfile kMirrorCorrectnessProfile{};

	struct ActivationMasks
	{
		FeatureMask requested{};
		FeatureMask active{};
		FeatureMask hookRequest{};
		FeatureMask runtimeEnable{};
		ProductMask requestedProducts{};
		ProductMask activeProducts{};

		[[nodiscard]] constexpr FeatureMask Requested() const noexcept
		{
			return requested;
		}

		[[nodiscard]] constexpr FeatureMask Active() const noexcept
		{
			return active;
		}

		[[nodiscard]] constexpr FeatureMask HookRequest() const noexcept
		{
			return hookRequest;
		}

		[[nodiscard]] constexpr FeatureMask RuntimeEnable() const noexcept
		{
			return runtimeEnable;
		}

		[[nodiscard]] constexpr bool MirrorsOfSkyrimActive() const noexcept
		{
			return (activeProducts & Mask(Product::kMirrorsOfSkyrim)) != 0;
		}
	};

	struct ProductDataReadiness
	{
		DataReadiness mirrorsOfSkyrim{ DataReadiness::kUnknown };

		[[nodiscard]] constexpr DataReadiness Get(Product product) const noexcept
		{
			return product == Product::kMirrorsOfSkyrim ?
				mirrorsOfSkyrim : DataReadiness::kUnknown;
		}

		constexpr void Set(Product product, DataReadiness value) noexcept
		{
			if (product == Product::kMirrorsOfSkyrim)
				mirrorsOfSkyrim = value;
		}
	};

	class PreparedState
	{
	public:
		PreparedState() = default;

		[[nodiscard]] std::uint32_t PolicyVersion() const noexcept
		{
			return policyVersion_;
		}

		[[nodiscard]] Runtime RuntimeVersion() const noexcept
		{
			return runtime_;
		}

		[[nodiscard]] OriginMask Origins() const noexcept
		{
			return origins_;
		}

		[[nodiscard]] OriginMask OriginsFor(Feature feature) const noexcept;

		[[nodiscard]] ProductMask SupportedProductMask() const noexcept
		{
			return supportedProducts_;
		}

		[[nodiscard]] DiagnosticMask Diagnostics() const noexcept
		{
			return diagnostics_;
		}

		[[nodiscard]] ConflictMask Conflicts() const noexcept
		{
			return conflicts_;
		}

		[[nodiscard]] bool IsConflicted() const noexcept
		{
			return conflicts_ != 0;
		}

		[[nodiscard]] const ActivationMasks& Masks() const noexcept
		{
			return masks_;
		}

	private:
		friend PreparedState Prepare(Runtime) noexcept;
		friend class Lifecycle;

		std::uint32_t policyVersion_{ kPolicyVersion };
		Runtime runtime_{ Runtime::kUnknown };
		OriginMask origins_{};
		ProductMask supportedProducts_{};
		DiagnosticMask diagnostics_{};
		ConflictMask conflicts_{};
		ActivationMasks masks_{};
	};

	class CommittedState
	{
	public:
		CommittedState() = default;

		[[nodiscard]] const PreparedState& Prepared() const noexcept
		{
			return prepared_;
		}

		[[nodiscard]] const ProductDataReadiness& Readiness() const noexcept
		{
			return readiness_;
		}

		[[nodiscard]] DiagnosticMask Diagnostics() const noexcept
		{
			return diagnostics_;
		}

		[[nodiscard]] ConflictMask Conflicts() const noexcept
		{
			return conflicts_;
		}

		[[nodiscard]] bool IsConflicted() const noexcept
		{
			return conflicts_ != 0;
		}

		[[nodiscard]] const ActivationMasks& Masks() const noexcept
		{
			return masks_;
		}

	private:
		friend CommittedState Commit(
			const PreparedState&,
			const ProductDataReadiness&) noexcept;
		friend class Lifecycle;

		PreparedState prepared_;
		ProductDataReadiness readiness_;
		DiagnosticMask diagnostics_{};
		ConflictMask conflicts_{};
		ActivationMasks masks_{};
	};

	[[nodiscard]] PreparedState Prepare(Runtime runtime) noexcept;
	[[nodiscard]] CommittedState Commit(
		const PreparedState& prepared,
		const ProductDataReadiness& readiness) noexcept;

	enum class LifecycleStage : std::uint8_t
	{
		kCold = 0,
		kPrepared = 1,
		kCommitted = 2,
		kConflict = 3
	};

	class Lifecycle final
	{
	public:
		[[nodiscard]] bool PrepareAtInputLoaded(Runtime runtime) noexcept;
		[[nodiscard]] bool CommitDataReadiness(
			const ProductDataReadiness& readiness) noexcept;

		[[nodiscard]] LifecycleStage Stage() const noexcept;
		[[nodiscard]] std::shared_ptr<const PreparedState> Prepared() const noexcept;
		[[nodiscard]] std::shared_ptr<const CommittedState> Committed() const noexcept;

	private:
		mutable std::mutex mutex_;
		LifecycleStage stage_{ LifecycleStage::kCold };
		std::shared_ptr<const PreparedState> prepared_;
		std::shared_ptr<const CommittedState> committed_;
	};

	[[nodiscard]] Lifecycle& GlobalLifecycle() noexcept;
}
