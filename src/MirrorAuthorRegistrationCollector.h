#pragma once

#include "MirrorAuthorRegistrationPolicy.h"
#include "RealisticReflections_MirrorAuthoringAPI.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace MirrorAuthorRegistrationCollector
{
	// The marker-gated diagnostic exposes this fixed collector through either the
	// synchronous public C ABI, the canonical manifest adapter, or their merge.
	// Those transport facts remain separate from TES authority or product support.
	inline constexpr bool kDiagnosticCABITransportWired = true;
	inline constexpr bool kDiagnosticPostLoadDispatchWired = true;
	inline constexpr bool kPublicAPIExportWired = true;
	inline constexpr bool kRuntimeIntegrationWired = false;
	inline constexpr bool kRuntimeTESAuthorityWired = false;
	inline constexpr bool kManifestRuntimeLoaderWired = true;
	inline constexpr bool kRecognitionWired = false;
	inline constexpr bool kDeliveryWired = false;
	inline constexpr bool kSupportedRuntimeFeature = false;

	inline constexpr std::size_t kMaximumRegisteredBases =
		RR_MIRROR_AUTHOR_MAX_REGISTERED_BASES;
	inline constexpr std::size_t kPluginStorageBytes =
		RR_MIRROR_AUTHOR_MAX_PLUGIN_FILENAME_BYTES + 1U;
	static_assert(kMaximumRegisteredBases ==
		MirrorAuthorRegistrationPolicy::kMaximumRegisteredBases);
	static_assert(kPluginStorageBytes == 256);

	enum class Phase : std::uint8_t
	{
		kDormant,
		kCollecting,
		kFrozen,
		kAborted
	};

	enum class Transport : std::uint8_t
	{
		kPublicCABI = 0x01,
		kManifest = 0x02
	};

	struct ManifestDeclaration
	{
		std::string_view sourcePlugin{};
		std::uint32_t localFormID{ 0 };
		std::uint32_t mirrorSchema{ 0 };
		std::uint32_t flags{ 0 };
	};

	struct OwnedDeclaration
	{
		std::array<char, kPluginStorageBytes> sourcePlugin{};
		std::uint32_t sourcePluginLength{ 0 };
		std::uint32_t localFormID{ 0 };
		std::uint32_t mirrorSchema{ 0 };
		std::uint32_t flags{ 0 };
		std::uint8_t transportMask{ 0 };

		[[nodiscard]] constexpr std::string_view PluginName() const noexcept
		{
			return { sourcePlugin.data(), sourcePluginLength };
		}
	};

	struct TransportProof
	{
		std::uint8_t maskUnion{ 0 };
		std::size_t cabiContributionCount{ 0 };
		std::size_t manifestContributionCount{ 0 };
		std::size_t declarationCount{ 0 };
	};

	[[nodiscard]] constexpr char LowerASCII(const char value) noexcept
	{
		return value >= 'A' && value <= 'Z' ?
			static_cast<char>(value + ('a' - 'A')) : value;
	}

	[[nodiscard]] constexpr bool FilenameEquals(
		const std::string_view left,
		const std::string_view right) noexcept
	{
		if (left.size() != right.size())
			return false;
		for (std::size_t index = 0; index < left.size(); ++index) {
			if (LowerASCII(left[index]) != LowerASCII(right[index]))
				return false;
		}
		return true;
	}

	[[nodiscard]] constexpr bool IsValidPluginBasename(
		const std::string_view name) noexcept
	{
		if (name.empty() ||
			name.size() > RR_MIRROR_AUTHOR_MAX_PLUGIN_FILENAME_BYTES) {
			return false;
		}
		if (name.front() == ' ' || name.front() == '.' ||
			name.back() == ' ' || name.back() == '.') {
			return false;
		}
		for (const char raw : name) {
			const auto byte = static_cast<unsigned char>(raw);
			if (byte < 0x20U || byte > 0x7EU || raw == '<' || raw == '>' ||
				raw == ':' || raw == '"' || raw == '/' || raw == '\\' ||
				raw == '|' || raw == '?' || raw == '*') {
				return false;
			}
		}
		if (name.size() < 5)
			return false;
		const auto extension = name.substr(name.size() - 4);
		return FilenameEquals(extension, ".esp") ||
		       FilenameEquals(extension, ".esm") ||
		       FilenameEquals(extension, ".esl");
	}

	[[nodiscard]] constexpr bool HasExplicitESLExtension(
		const std::string_view name) noexcept
	{
		return name.size() >= 4 &&
		       FilenameEquals(name.substr(name.size() - 4), ".esl");
	}

	[[nodiscard]] constexpr std::uint32_t MapDeclarationIssue(
		const MirrorAuthorRegistrationPolicy::DeclarationIssue issue) noexcept
	{
		using Issue = MirrorAuthorRegistrationPolicy::DeclarationIssue;
		switch (issue) {
		case Issue::kNone:
			return RR_MIRROR_AUTHOR_RESULT_SUCCESS;
		case Issue::kCollectionWindowClosed:
			return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
		case Issue::kTransportUnavailable:
			return RR_MIRROR_AUTHOR_RESULT_INVALID_ARGUMENT;
		case Issue::kPluginFilenameMissing:
		case Issue::kPluginFilenameTooLong:
		case Issue::kPluginFilenameInvalid:
		case Issue::kPluginExtensionInvalid:
			return RR_MIRROR_AUTHOR_RESULT_INVALID_PLUGIN_NAME;
		case Issue::kLocalFormIDInvalid:
			return RR_MIRROR_AUTHOR_RESULT_INVALID_LOCAL_FORM_ID;
		case Issue::kSchemaUnsupported:
			return RR_MIRROR_AUTHOR_RESULT_UNSUPPORTED_SCHEMA;
		case Issue::kFlagsUnsupported:
			return RR_MIRROR_AUTHOR_RESULT_UNSUPPORTED_FLAGS;
		case Issue::kFirstPartyNamespaceReserved:
			return RR_MIRROR_AUTHOR_RESULT_FIRST_PARTY_NAMESPACE_RESERVED;
		}
		return RR_MIRROR_AUTHOR_RESULT_INVALID_ARGUMENT;
	}

	class Collector
	{
	public:
		[[nodiscard]] std::uint32_t BeginLifecycle(
			const std::uint64_t lifecycleToken) noexcept
		{
			if (lifecycleToken == 0 ||
				phase_ == Phase::kCollecting ||
				(lifecycleToken_ != 0 && lifecycleToken <= lifecycleToken_)) {
				return RR_MIRROR_AUTHOR_RESULT_LIFECYCLE_MISMATCH;
			}
			entries_ = {};
			entryCount_ = 0;
			lifecycleToken_ = lifecycleToken;
			phase_ = Phase::kCollecting;
			quarantined_ = false;
			quarantineCause_ = RR_MIRROR_AUTHOR_RESULT_SUCCESS;
			return RR_MIRROR_AUTHOR_RESULT_SUCCESS;
		}

		[[nodiscard]] std::uint32_t RegisterCABI(
			const std::uint64_t expectedLifecycleToken,
			const RR_MirrorAuthorBaseV1* declaration) noexcept
		{
			if (!declaration)
				return RR_MIRROR_AUTHOR_RESULT_INVALID_ARGUMENT;
			if (expectedLifecycleToken == 0 ||
				expectedLifecycleToken != lifecycleToken_) {
				return RR_MIRROR_AUTHOR_RESULT_LIFECYCLE_MISMATCH;
			}
			if (phase_ != Phase::kCollecting)
				return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
			if (quarantined_)
				return RR_MIRROR_AUTHOR_RESULT_CATALOG_QUARANTINED;
			if (declaration->struct_size != sizeof(RR_MirrorAuthorBaseV1) ||
				declaration->struct_version != RR_MIRROR_AUTHOR_BASE_VERSION_1) {
				return RR_MIRROR_AUTHOR_RESULT_INCOMPATIBLE_STRUCT;
			}
			if (!ReservedFieldsAreZero(*declaration))
				return RR_MIRROR_AUTHOR_RESULT_INVALID_ARGUMENT;
			if (declaration->source_plugin_length == 0 ||
				declaration->source_plugin_length >
					RR_MIRROR_AUTHOR_MAX_PLUGIN_FILENAME_BYTES) {
				return RR_MIRROR_AUTHOR_RESULT_INVALID_PLUGIN_NAME;
			}
			const auto length =
				static_cast<std::size_t>(declaration->source_plugin_length);
			if (declaration->source_plugin[length] != '\0')
				return RR_MIRROR_AUTHOR_RESULT_INVALID_PLUGIN_NAME;
			for (std::size_t index = 0; index < length; ++index) {
				if (declaration->source_plugin[index] == '\0')
					return RR_MIRROR_AUTHOR_RESULT_INVALID_PLUGIN_NAME;
			}
			for (std::size_t index = length + 1;
				index < kPluginStorageBytes; ++index) {
				if (declaration->source_plugin[index] != '\0')
					return RR_MIRROR_AUTHOR_RESULT_INVALID_ARGUMENT;
			}

			return RegisterCommon(
				Transport::kPublicCABI,
				{ declaration->source_plugin, length },
				declaration->local_form_id,
				declaration->mirror_schema,
				declaration->flags);
		}

		[[nodiscard]] std::uint32_t RegisterManifest(
			const std::uint64_t expectedLifecycleToken,
			const ManifestDeclaration& declaration) noexcept
		{
			if (expectedLifecycleToken == 0 ||
				expectedLifecycleToken != lifecycleToken_) {
				return RR_MIRROR_AUTHOR_RESULT_LIFECYCLE_MISMATCH;
			}
			if (phase_ != Phase::kCollecting)
				return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
			if (quarantined_)
				return RR_MIRROR_AUTHOR_RESULT_CATALOG_QUARANTINED;
			return RegisterCommon(
				Transport::kManifest,
				declaration.sourcePlugin,
				declaration.localFormID,
				declaration.mirrorSchema,
				declaration.flags);
		}

		[[nodiscard]] std::uint32_t Freeze(
			const std::uint64_t lifecycleToken) noexcept
		{
			if (lifecycleToken == 0 || lifecycleToken != lifecycleToken_)
				return RR_MIRROR_AUTHOR_RESULT_LIFECYCLE_MISMATCH;
			if (phase_ != Phase::kCollecting)
				return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
			if (quarantined_) {
				(void)AbortAndScrub();
				return RR_MIRROR_AUTHOR_RESULT_CATALOG_QUARANTINED;
			}

			// The fixed cohort is sorted without the standard-library sort machinery.
			// This insertion sort has no allocation and every operation is a bounded
			// copy/compare of owned values, so Freeze remains a truthful no-throw seam.
			for (std::size_t index = 1; index < entryCount_; ++index) {
				const auto value = entries_[index];
				auto insertion = index;
				while (insertion != 0 &&
					DeclarationLess(value, entries_[insertion - 1])) {
					entries_[insertion] = entries_[insertion - 1];
					--insertion;
				}
				entries_[insertion] = value;
			}
			phase_ = Phase::kFrozen;
			return RR_MIRROR_AUTHOR_RESULT_SUCCESS;
		}

		[[nodiscard]] std::uint32_t Abort(
			const std::uint64_t lifecycleToken) noexcept
		{
			if (lifecycleToken == 0 || lifecycleToken != lifecycleToken_)
				return RR_MIRROR_AUTHOR_RESULT_LIFECYCLE_MISMATCH;
			if (phase_ != Phase::kCollecting)
				return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;

			if (!quarantined_) {
				quarantined_ = true;
				quarantineCause_ = RR_MIRROR_AUTHOR_RESULT_CATALOG_QUARANTINED;
			}
			return AbortAndScrub();
		}

		[[nodiscard]] constexpr Phase CurrentPhase() const noexcept
		{
			return phase_;
		}

		[[nodiscard]] constexpr std::uint64_t LifecycleToken() const noexcept
		{
			return lifecycleToken_;
		}

		[[nodiscard]] constexpr bool IsQuarantined() const noexcept
		{
			return quarantined_;
		}

		[[nodiscard]] constexpr std::uint32_t QuarantineCause() const noexcept
		{
			return quarantineCause_;
		}

		[[nodiscard]] constexpr std::size_t StoredCountForDiagnostics() const noexcept
		{
			return entryCount_;
		}

		// Produce the exact owned per-entry transport proof while collection is
		// still abortable.  The window validates and records this proof before it
		// commits Freeze, so no fallible receipt construction remains afterward.
		[[nodiscard]] constexpr bool TryBuildTransportProof(
			const std::uint64_t expectedLifecycleToken,
			const std::uint8_t allowedTransportMask,
			TransportProof& output) const noexcept
		{
			output = {};
			constexpr auto knownMask = static_cast<std::uint8_t>(
				static_cast<std::uint8_t>(Transport::kPublicCABI) |
				static_cast<std::uint8_t>(Transport::kManifest));
			if (phase_ != Phase::kCollecting || quarantined_ ||
				expectedLifecycleToken == 0 ||
				expectedLifecycleToken != lifecycleToken_ ||
				allowedTransportMask == 0 ||
				(allowedTransportMask &
				 static_cast<std::uint8_t>(~knownMask)) != 0) {
				return false;
			}

			for (std::size_t index = 0; index < entryCount_; ++index) {
				const auto mask = entries_[index].transportMask;
				if (mask == 0 ||
					(mask & static_cast<std::uint8_t>(~allowedTransportMask)) != 0) {
					output = {};
					return false;
				}
				output.maskUnion |= mask;
				if ((mask & static_cast<std::uint8_t>(Transport::kPublicCABI)) != 0)
					++output.cabiContributionCount;
				if ((mask & static_cast<std::uint8_t>(Transport::kManifest)) != 0)
					++output.manifestContributionCount;
			}
			output.declarationCount = entryCount_;
			return true;
		}

		[[nodiscard]] constexpr std::size_t FrozenCount(
			const std::uint64_t expectedLifecycleToken) const noexcept
		{
			return phase_ == Phase::kFrozen && !quarantined_ &&
				expectedLifecycleToken != 0 &&
				expectedLifecycleToken == lifecycleToken_ ?
				entryCount_ : 0;
		}

		// Copy-out is token-bound; no caller can retain a pointer into storage that
		// a later lifecycle clears and reuses. Failure also clears output so a
		// stale caller cannot accidentally keep using a previous successful read.
		[[nodiscard]] constexpr bool TryCopyFrozen(
			const std::uint64_t expectedLifecycleToken,
			const std::size_t index,
			OwnedDeclaration& output) const noexcept
		{
			output = {};
			if (phase_ != Phase::kFrozen || quarantined_ ||
				expectedLifecycleToken == 0 ||
				expectedLifecycleToken != lifecycleToken_ ||
				index >= entryCount_) {
				return false;
			}
			output = entries_[index];
			return true;
		}

	private:
		[[nodiscard]] static constexpr bool DeclarationLess(
			const OwnedDeclaration& left,
			const OwnedDeclaration& right) noexcept
		{
			const auto leftName = left.PluginName();
			const auto rightName = right.PluginName();
			const auto common = leftName.size() < rightName.size() ?
				leftName.size() : rightName.size();
			for (std::size_t index = 0; index < common; ++index) {
				const auto leftByte =
					static_cast<unsigned char>(leftName[index]);
				const auto rightByte =
					static_cast<unsigned char>(rightName[index]);
				if (leftByte != rightByte)
					return leftByte < rightByte;
			}
			if (leftName.size() != rightName.size())
				return leftName.size() < rightName.size();
			return left.localFormID < right.localFormID;
		}

		[[nodiscard]] std::uint32_t AbortAndScrub() noexcept
		{
			// Every failed transaction has one terminal representation.  Erase every
			// partial value before publishing kAborted so no diagnostic copy path can
			// expose source-order-dependent or pre-quarantine state.
			entries_ = {};
			entryCount_ = 0;
			phase_ = Phase::kAborted;
			return quarantineCause_;
		}

		[[nodiscard]] static constexpr bool ReservedFieldsAreZero(
			const RR_MirrorAuthorBaseV1& declaration) noexcept
		{
			for (const auto value : declaration.reserved_u32) {
				if (value != 0)
					return false;
			}
			for (const auto value : declaration.reserved) {
				if (value != 0)
					return false;
			}
			return true;
		}

		[[nodiscard]] std::uint32_t RegisterCommon(
			const Transport transport,
			const std::string_view sourcePlugin,
			const std::uint32_t localFormID,
			const std::uint32_t mirrorSchema,
			const std::uint32_t flags) noexcept
		{
			if (!IsValidPluginBasename(sourcePlugin))
				return RR_MIRROR_AUTHOR_RESULT_INVALID_PLUGIN_NAME;
			if (HasExplicitESLExtension(sourcePlugin) &&
				(localFormID <
					MirrorAuthorRegistrationPolicy::kMinimumLightPluginOwnedLocalFormID ||
					localFormID >
					MirrorAuthorRegistrationPolicy::kMaximumLightPluginLocalFormID)) {
				return RR_MIRROR_AUTHOR_RESULT_INVALID_LOCAL_FORM_ID;
			}

			using namespace MirrorAuthorRegistrationPolicy;
			const Declaration policyDeclaration{
				.transport = transport == Transport::kPublicCABI ?
					MirrorAuthorRegistrationPolicy::Transport::kPublicCABI :
					MirrorAuthorRegistrationPolicy::Transport::kManifest,
				.sourcePlugin = sourcePlugin,
				.localFormID = localFormID,
				.mirrorSchema = mirrorSchema,
				.flags = flags
			};
			if (const auto issue = ValidateDeclaration(
					CollectionPhase::kCollecting, policyDeclaration);
				issue != DeclarationIssue::kNone) {
				return MapDeclarationIssue(issue);
			}

			for (std::size_t index = 0; index < entryCount_; ++index) {
				auto& existing = entries_[index];
				if (!FilenameEquals(existing.PluginName(), sourcePlugin) ||
					existing.localFormID != localFormID) {
					continue;
				}
				if (existing.mirrorSchema != mirrorSchema ||
					existing.flags != flags) {
					return Quarantine(
						RR_MIRROR_AUTHOR_RESULT_CONFLICTING_DECLARATION);
				}
				existing.transportMask |= static_cast<std::uint8_t>(transport);
				return RR_MIRROR_AUTHOR_RESULT_IDEMPOTENT_DUPLICATE;
			}

			if (entryCount_ >= kMaximumRegisteredBases)
				return Quarantine(RR_MIRROR_AUTHOR_RESULT_CAPACITY_EXCEEDED);

			auto& output = entries_[entryCount_++];
			output = {};
			output.sourcePluginLength =
				static_cast<std::uint32_t>(sourcePlugin.size());
			for (std::size_t index = 0; index < sourcePlugin.size(); ++index)
				output.sourcePlugin[index] = LowerASCII(sourcePlugin[index]);
			output.localFormID = localFormID;
			output.mirrorSchema = mirrorSchema;
			output.flags = flags;
			output.transportMask = static_cast<std::uint8_t>(transport);
			return RR_MIRROR_AUTHOR_RESULT_SUCCESS;
		}

		[[nodiscard]] std::uint32_t Quarantine(
			const std::uint32_t cause) noexcept
		{
			quarantined_ = true;
			quarantineCause_ = cause;
			return cause;
		}

		std::array<OwnedDeclaration, kMaximumRegisteredBases> entries_{};
		std::size_t entryCount_{ 0 };
		std::uint64_t lifecycleToken_{ 0 };
		Phase phase_{ Phase::kDormant };
		bool quarantined_{ false };
		std::uint32_t quarantineCause_{ RR_MIRROR_AUTHOR_RESULT_SUCCESS };
	};
}
