#pragma once

#include "MirrorAuthorCatalogParser.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MirrorAuthorCatalogDiscoveryPolicy
{
	// Engine-independent value policy used by the default-off filesystem/manifest
	// diagnostic.  No function here opens a path, queries Skyrim, grants authority,
	// or enables recognition/delivery itself.
	inline constexpr bool kFilesystemRuntimeAdapterWired = true;
	inline constexpr bool kManifestLoaderWired = true;
	inline constexpr bool kRuntimeIntegrationWired = false;
	inline constexpr bool kRuntimeTESAuthorityWired = false;
	inline constexpr bool kRecognitionWired = false;
	inline constexpr bool kDeliveryWired = false;
	inline constexpr bool kSupportedRuntimeFeature = false;
	inline constexpr bool kDigestsGrantAuthority = false;

	inline constexpr std::size_t kMaximumDirectoryEntries = 64;
	inline constexpr std::size_t kMaximumCatalogFiles = 64;
	inline constexpr std::size_t kMaximumCatalogFileBytes = 128U * 1024U;
	inline constexpr std::size_t kMaximumAggregateBytes = 1024U * 1024U;
	inline constexpr std::size_t kMaximumRawRegistrations = 1024;
	inline constexpr std::size_t kMaximumUniqueBases = 256;
	inline constexpr std::size_t kMaximumProviderIdentifierBytes = 64;
	inline constexpr std::size_t kDigestBytes = 32;
	inline constexpr std::string_view kDigestAlgorithm = "SHA-256";
	inline constexpr std::string_view kCatalogLeafSuffix =
		".mirror-author.catalog.json";
	inline constexpr std::size_t kMaximumCatalogLeafBytes =
		kMaximumProviderIdentifierBytes + kCatalogLeafSuffix.size();
	inline constexpr std::string_view kInputCohortDigestDomain =
		"RR.MirrorAuthorCatalog.InputCohort.v1";

	static_assert(kMaximumCatalogFileBytes ==
		MirrorAuthorCatalogParser::kMaximumCatalogBytes);
	static_assert(kMaximumUniqueBases ==
		MirrorAuthorCatalogParser::kMaximumCatalogEntries);
	static_assert(kMaximumDirectoryEntries == kMaximumCatalogFiles);

	enum class DirectoryState : std::uint8_t
	{
		kUnobserved,
		kMissing,
		kDirectory,
		kExistingNonDirectory,
		kReadFault
	};

	enum class EnumerationScope : std::uint8_t
	{
		kUnknown,
		kExactImmediateDirectory,
		kRecursiveOrOther
	};

	enum class EntryKind : std::uint8_t
	{
		kRegularFile,
		kDirectory,
		kOther
	};

	struct DirectoryEntryObservation
	{
		std::string_view leaf{};
		EntryKind kind{ EntryKind::kOther };
		bool reparsePoint{ false };
	};

	struct DirectoryObservation
	{
		DirectoryState state{ DirectoryState::kUnobserved };
		EnumerationScope scope{ EnumerationScope::kUnknown };
		bool reparsePoint{ false };
		std::span<const DirectoryEntryObservation> entries{};
	};

	struct Digest256
	{
		std::array<std::byte, kDigestBytes> bytes{};

		[[nodiscard]] friend constexpr bool operator==(
			const Digest256&,
			const Digest256&) noexcept = default;
	};

	[[nodiscard]] constexpr bool OrdinalLeafLess(
		const std::string_view left,
		const std::string_view right) noexcept
	{
		const auto common = left.size() < right.size() ?
			left.size() : right.size();
		for (std::size_t index = 0; index < common; ++index) {
			const auto leftByte = static_cast<unsigned char>(left[index]);
			const auto rightByte = static_cast<unsigned char>(right[index]);
			if (leftByte < rightByte)
				return true;
			if (leftByte > rightByte)
				return false;
		}
		return left.size() < right.size();
	}

	[[nodiscard]] constexpr bool IsProviderContinuationByte(
		const char value) noexcept
	{
		return (value >= 'a' && value <= 'z') ||
		       (value >= '0' && value <= '9') || value == '_' || value == '-';
	}

	[[nodiscard]] constexpr bool IsProviderFirstByte(
		const char value) noexcept
	{
		return (value >= 'a' && value <= 'z') ||
		       (value >= '0' && value <= '9');
	}

	[[nodiscard]] constexpr bool IsDOSDeviceStem(
		const std::string_view provider) noexcept
	{
		if (provider == "con" || provider == "prn" || provider == "aux" ||
			provider == "nul") {
			return true;
		}
		if (provider.size() != 4)
			return false;
		const auto prefix = provider.substr(0, 3);
		return (prefix == "com" || prefix == "lpt") &&
		       provider[3] >= '1' && provider[3] <= '9';
	}

	[[nodiscard]] constexpr bool IsRecognizedCatalogLeaf(
		const std::string_view leaf) noexcept
	{
		if (leaf.size() <= kCatalogLeafSuffix.size() ||
			leaf.substr(leaf.size() - kCatalogLeafSuffix.size()) !=
				kCatalogLeafSuffix) {
			return false;
		}
		const auto provider = leaf.substr(
			0, leaf.size() - kCatalogLeafSuffix.size());
		if (provider.empty() ||
			provider.size() > kMaximumProviderIdentifierBytes ||
			!IsProviderFirstByte(provider.front())) {
			return false;
		}
		for (std::size_t index = 1; index < provider.size(); ++index) {
			if (!IsProviderContinuationByte(provider[index]))
				return false;
		}
		return !IsDOSDeviceStem(provider);
	}

	struct OwnedCatalogLeaf
	{
		std::array<char, kMaximumCatalogLeafBytes + 1> bytes{};
		std::uint32_t length{ 0 };

		[[nodiscard]] constexpr std::string_view View() const noexcept
		{
			return length <= kMaximumCatalogLeafBytes ?
				std::string_view{ bytes.data(), length } : std::string_view{};
		}
	};

	[[nodiscard]] constexpr OwnedCatalogLeaf CopyCatalogLeaf(
		const std::string_view leaf) noexcept
	{
		OwnedCatalogLeaf output{};
		if (leaf.size() > kMaximumCatalogLeafBytes)
			return output;
		for (std::size_t index = 0; index < leaf.size(); ++index)
			output.bytes[index] = leaf[index];
		output.length = static_cast<std::uint32_t>(leaf.size());
		return output;
	}

	enum class DiscoveryIssue : std::uint8_t
	{
		kNone,
		kObservationInvalid,
		kExistingPathNotDirectory,
		kDirectoryReadFault,
		kEnumerationScopeInvalid,
		kReparsePoint,
		kEntryCapacityExceeded,
		kSubdirectory,
		kUnrecognizedEntry,
		kDuplicateLeaf,
		kFileReadFault,
		kFileCapacityExceeded,
		kAggregateCapacityExceeded
	};

	enum class SnapshotDisposition : std::uint8_t
	{
		kAccepted,
		kAborted
	};

	struct DirectoryPlan
	{
		SnapshotDisposition disposition{ SnapshotDisposition::kAborted };
		DiscoveryIssue issue{ DiscoveryIssue::kObservationInvalid };
		bool directoryMissing{ false };
		std::uint32_t fileCount{ 0 };
		std::array<OwnedCatalogLeaf, kMaximumCatalogFiles> leaves{};

		[[nodiscard]] constexpr bool Accepted() const noexcept
		{
			return disposition == SnapshotDisposition::kAccepted &&
			       issue == DiscoveryIssue::kNone;
		}

		[[nodiscard]] constexpr std::span<const OwnedCatalogLeaf>
		ExpectedLeaves() const noexcept
		{
			return fileCount <= leaves.size() ?
				std::span<const OwnedCatalogLeaf>{ leaves.data(), fileCount } :
				std::span<const OwnedCatalogLeaf>{};
		}
	};

	/** Validate exactly one non-recursive enumeration before any file is read. */
	[[nodiscard]] inline DirectoryPlan BuildDirectoryPlan(
		const DirectoryObservation& directory)
	{
		if (directory.state == DirectoryState::kMissing) {
			if (directory.reparsePoint || !directory.entries.empty())
				return {};
			DirectoryPlan plan{};
			plan.disposition = SnapshotDisposition::kAccepted;
			plan.issue = DiscoveryIssue::kNone;
			plan.directoryMissing = true;
			return plan;
		}
		if (directory.state == DirectoryState::kExistingNonDirectory) {
			DirectoryPlan plan{};
			plan.issue = DiscoveryIssue::kExistingPathNotDirectory;
			return plan;
		}
		if (directory.state == DirectoryState::kReadFault) {
			DirectoryPlan plan{};
			plan.issue = DiscoveryIssue::kDirectoryReadFault;
			return plan;
		}
		if (directory.state != DirectoryState::kDirectory)
			return {};
		if (directory.reparsePoint) {
			DirectoryPlan plan{};
			plan.issue = DiscoveryIssue::kReparsePoint;
			return plan;
		}
		if (directory.scope != EnumerationScope::kExactImmediateDirectory) {
			DirectoryPlan plan{};
			plan.issue = DiscoveryIssue::kEnumerationScopeInvalid;
			return plan;
		}
		if (directory.entries.size() > kMaximumDirectoryEntries) {
			DirectoryPlan plan{};
			plan.issue = DiscoveryIssue::kEntryCapacityExceeded;
			return plan;
		}

		std::array<DirectoryEntryObservation, kMaximumDirectoryEntries> ordered{};
		std::copy(directory.entries.begin(), directory.entries.end(), ordered.begin());
		std::sort(ordered.begin(), ordered.begin() + directory.entries.size(),
			[](const DirectoryEntryObservation& left,
				const DirectoryEntryObservation& right) noexcept {
				return OrdinalLeafLess(left.leaf, right.leaf);
			});

		DirectoryPlan plan{};
		for (std::size_t index = 0; index < directory.entries.size(); ++index) {
			const auto& entry = ordered[index];
			if (entry.reparsePoint) {
				plan.issue = DiscoveryIssue::kReparsePoint;
				return plan;
			}
			if (entry.kind == EntryKind::kDirectory) {
				plan.issue = DiscoveryIssue::kSubdirectory;
				return plan;
			}
			if (entry.kind != EntryKind::kRegularFile ||
				!IsRecognizedCatalogLeaf(entry.leaf)) {
				plan.issue = DiscoveryIssue::kUnrecognizedEntry;
				return plan;
			}
			if (index != 0 && entry.leaf == ordered[index - 1].leaf) {
				plan.issue = DiscoveryIssue::kDuplicateLeaf;
				return plan;
			}
			plan.leaves[index] = CopyCatalogLeaf(entry.leaf);
		}
		plan.fileCount = static_cast<std::uint32_t>(directory.entries.size());
		plan.disposition = SnapshotDisposition::kAccepted;
		plan.issue = DiscoveryIssue::kNone;
		return plan;
	}

	[[nodiscard]] constexpr DiscoveryIssue ValidateDirectoryPlan(
		const DirectoryPlan& plan) noexcept
	{
		if (!plan.Accepted()) {
			return plan.issue != DiscoveryIssue::kNone ?
				plan.issue : DiscoveryIssue::kObservationInvalid;
		}
		if (plan.fileCount > kMaximumCatalogFiles ||
			(plan.directoryMissing && plan.fileCount != 0)) {
			return DiscoveryIssue::kObservationInvalid;
		}
		for (std::size_t index = 0; index < plan.fileCount; ++index) {
			const auto& owned = plan.leaves[index];
			const auto leaf = owned.View();
			if (!IsRecognizedCatalogLeaf(leaf) ||
				CopyCatalogLeaf(leaf).bytes != owned.bytes) {
				return DiscoveryIssue::kUnrecognizedEntry;
			}
			if (index != 0) {
				const auto previous = plan.leaves[index - 1].View();
				if (leaf == previous)
					return DiscoveryIssue::kDuplicateLeaf;
				if (!OrdinalLeafLess(previous, leaf))
					return DiscoveryIssue::kObservationInvalid;
			}
		}
		for (std::size_t index = plan.fileCount; index < plan.leaves.size(); ++index) {
			if (plan.leaves[index].length != 0 ||
				plan.leaves[index].bytes != OwnedCatalogLeaf{}.bytes) {
				return DiscoveryIssue::kObservationInvalid;
			}
		}
		return DiscoveryIssue::kNone;
	}

	enum class SnapshotReadStatus : std::uint8_t
	{
		kFault,
		kSuccess
	};

	// contentDigest is SHA-256 over exactly bytes, with no domain prefix or
	// length framing.  The future adapter computes it during the same one-read
	// transaction; this policy carries it opaquely and never treats it as trust.
	struct SnapshotReadResult
	{
		SnapshotReadStatus status{ SnapshotReadStatus::kFault };
		std::vector<std::byte> bytes{};
		Digest256 contentDigest{};
	};

	class OwnedFileSnapshot final
	{
	public:
		OwnedFileSnapshot(
			std::string leaf,
			std::vector<std::byte> bytes,
			const Digest256 contentDigest) :
			leaf_(std::move(leaf)),
			bytes_(std::move(bytes)),
			contentDigest_(contentDigest)
		{}

		[[nodiscard]] std::string_view Leaf() const noexcept
		{
			return leaf_;
		}

		[[nodiscard]] std::size_t Length() const noexcept
		{
			return bytes_.size();
		}

		[[nodiscard]] std::span<const std::byte> Bytes() const noexcept
		{
			return { bytes_.data(), bytes_.size() };
		}

		[[nodiscard]] const Digest256& ContentDigest() const noexcept
		{
			return contentDigest_;
		}

	private:
		std::string leaf_{};
		std::vector<std::byte> bytes_{};
		Digest256 contentDigest_{};
	};

	struct SnapshotResult
	{
		SnapshotDisposition disposition{ SnapshotDisposition::kAborted };
		DiscoveryIssue issue{ DiscoveryIssue::kObservationInvalid };
		DirectoryPlan plan{};
		std::size_t aggregateLength{ 0 };
		std::vector<OwnedFileSnapshot> files{};

		[[nodiscard]] bool Accepted() const noexcept
		{
			return disposition == SnapshotDisposition::kAccepted &&
			       issue == DiscoveryIssue::kNone && plan.Accepted();
		}
	};

	/**
	 * Invoke the injected reader exactly once per already-validated ordinal leaf.
	 * Raw bytes live only in this transient result.  Every failure destroys the
	 * partial prefix instead of exposing an enumeration-order winner.
	 */
	template <class ReadOne>
	[[nodiscard]] SnapshotResult DiscoverAndSnapshot(
		const DirectoryObservation& directory,
		ReadOne&& readOne)
	{
		const auto plan = BuildDirectoryPlan(directory);
		if (!plan.Accepted()) {
			SnapshotResult result{};
			result.issue = plan.issue;
			return result;
		}

		SnapshotResult result{};
		result.disposition = SnapshotDisposition::kAccepted;
		result.issue = DiscoveryIssue::kNone;
		result.plan = plan;
		result.files.reserve(plan.fileCount);
		for (const auto& leaf : plan.ExpectedLeaves()) {
			SnapshotReadResult read{};
			try {
				read = std::invoke(readOne, leaf.View());
			} catch (...) {
				SnapshotResult failure{};
				failure.issue = DiscoveryIssue::kFileReadFault;
				return failure;
			}
			if (read.status != SnapshotReadStatus::kSuccess) {
				SnapshotResult failure{};
				failure.issue = DiscoveryIssue::kFileReadFault;
				return failure;
			}
			if (read.bytes.size() > kMaximumCatalogFileBytes) {
				SnapshotResult failure{};
				failure.issue = DiscoveryIssue::kFileCapacityExceeded;
				return failure;
			}
			if (read.bytes.size() >
				kMaximumAggregateBytes - result.aggregateLength) {
				SnapshotResult failure{};
				failure.issue = DiscoveryIssue::kAggregateCapacityExceeded;
				return failure;
			}
			result.aggregateLength += read.bytes.size();
			result.files.emplace_back(
				std::string(leaf.View()), std::move(read.bytes), read.contentDigest);
		}
		return result;
	}

	enum class CohortDisposition : std::uint8_t
	{
		kAccepted,
		kAborted,
		kQuarantined
	};

	enum class MergeIssue : std::uint8_t
	{
		kNone,
		kCatalogShapeInvalid,
		kRawRegistrationCapacityExceeded,
		kConflictingDeclaration,
		kUniqueBaseCapacityExceeded
	};

	struct MergeResult
	{
		CohortDisposition disposition{ CohortDisposition::kAborted };
		MergeIssue issue{ MergeIssue::kCatalogShapeInvalid };
		std::size_t rawRegistrationCount{ 0 };
		MirrorAuthorCatalogParser::CanonicalCatalog catalog{};

		[[nodiscard]] constexpr bool Accepted() const noexcept
		{
			return disposition == CohortDisposition::kAccepted &&
			       issue == MergeIssue::kNone;
		}
	};

	/**
	 * Value-only exact identity/semantics merge.  Callers which receive catalogs
	 * from bytes validate each one through the parser before reaching this seam.
	 */
	[[nodiscard]] inline MergeResult MergeCanonicalCatalogs(
		const std::span<const MirrorAuthorCatalogParser::CanonicalCatalog> catalogs)
	{
		using MirrorAuthorCatalogParser::CanonicalDeclaration;
		std::vector<CanonicalDeclaration> raw{};
		raw.reserve(kMaximumRawRegistrations);
		std::size_t rawCount = 0;
		for (const auto& catalog : catalogs) {
			if (catalog.entryCount > catalog.registrations.size()) {
				return {
					.disposition = CohortDisposition::kAborted,
					.issue = MergeIssue::kCatalogShapeInvalid,
					.rawRegistrationCount = rawCount
				};
			}
			const auto count = static_cast<std::size_t>(catalog.entryCount);
			if (count > kMaximumRawRegistrations - rawCount) {
				return {
					.disposition = CohortDisposition::kAborted,
					.issue = MergeIssue::kRawRegistrationCapacityExceeded,
					.rawRegistrationCount = rawCount + count
				};
			}
			raw.insert(
				raw.end(), catalog.registrations.begin(),
				catalog.registrations.begin() + catalog.entryCount);
			rawCount += count;
		}

		std::sort(raw.begin(), raw.end(),
			[](const CanonicalDeclaration& left,
				const CanonicalDeclaration& right) noexcept {
				return MirrorAuthorCatalogParser::CompareIdentity(left, right) < 0;
			});

		MergeResult result{};
		result.disposition = CohortDisposition::kAccepted;
		result.issue = MergeIssue::kNone;
		result.rawRegistrationCount = rawCount;
		for (const auto& declaration : raw) {
			if (result.catalog.entryCount != 0) {
				const auto& previous = result.catalog.registrations[
					result.catalog.entryCount - 1];
				if (MirrorAuthorCatalogParser::SameIdentity(
						previous, declaration)) {
					if (MirrorAuthorCatalogParser::SameDeclaration(
							previous, declaration)) {
						continue;
					}
					return {
						.disposition = CohortDisposition::kQuarantined,
						.issue = MergeIssue::kConflictingDeclaration,
						.rawRegistrationCount = rawCount
					};
				}
			}
			if (result.catalog.entryCount >= kMaximumUniqueBases) {
				return {
					.disposition = CohortDisposition::kQuarantined,
					.issue = MergeIssue::kUniqueBaseCapacityExceeded,
					.rawRegistrationCount = rawCount
				};
			}
			result.catalog.registrations[result.catalog.entryCount++] = declaration;
		}
		return result;
	}

	struct FileSummary
	{
		OwnedCatalogLeaf leaf{};
		std::uint64_t byteCount{ 0 };
		Digest256 contentDigest{};
	};

	enum class CohortPhase : std::uint8_t
	{
		kDormant,
		kAdding,
		kClosed,
		kAborted,
		kQuarantined
	};

	enum class BuilderIssue : std::uint8_t
	{
		kNone,
		kWrongPhase,
		kDirectoryRejected,
		kUnexpectedFile,
		kLeafOrderMismatch,
		kFileCapacityExceeded,
		kAggregateCapacityExceeded,
		kCatalogRejected,
		kByteCountMismatch,
		kRawRegistrationCapacityExceeded,
		kConflictingDeclaration,
		kUniqueBaseCapacityExceeded,
		kIncompleteCohort
	};

	struct CohortResult
	{
		CohortDisposition disposition{ CohortDisposition::kAborted };
		BuilderIssue builderIssue{ BuilderIssue::kWrongPhase };
		DiscoveryIssue discoveryIssue{ DiscoveryIssue::kNone };
		MirrorAuthorCatalogParser::ParseIssue parseIssue{
			MirrorAuthorCatalogParser::ParseIssue::kNone };
		std::size_t parseErrorOffset{ 0 };
		MergeIssue mergeIssue{ MergeIssue::kNone };
		OwnedCatalogLeaf failureLeaf{};
		bool directoryMissing{ false };
		std::uint32_t fileCount{ 0 };
		std::uint64_t aggregateLength{ 0 };
		std::size_t rawRegistrationCount{ 0 };
		std::array<FileSummary, kMaximumCatalogFiles> files{};
		MirrorAuthorCatalogParser::CanonicalCatalog catalog{};

		[[nodiscard]] constexpr bool Accepted() const noexcept
		{
			return disposition == CohortDisposition::kAccepted &&
			       builderIssue == BuilderIssue::kNone &&
			       discoveryIssue == DiscoveryIssue::kNone &&
			       parseIssue == MirrorAuthorCatalogParser::ParseIssue::kNone &&
			       mergeIssue == MergeIssue::kNone;
		}

		[[nodiscard]] constexpr std::span<const FileSummary>
		FileSummaries() const noexcept
		{
			return fileCount <= files.size() ?
				std::span<const FileSummary>{ files.data(), fileCount } :
				std::span<const FileSummary>{};
		}
	};

	/**
	 * Fixed-capacity incremental seam for a future adapter:
	 * Begin(directory plan) -> AddFile(ordinal leaf, exact byte count, raw-file
	 * SHA-256, parser-validated catalog) -> Close().  It never receives or retains
	 * a file byte span.  Every terminal failure scrubs summaries and declarations.
	 */
	class CohortBuilder final
	{
	public:
		[[nodiscard]] bool Begin(const DirectoryObservation& directory)
		{
			return Begin(BuildDirectoryPlan(directory));
		}

		[[nodiscard]] bool Begin(const DirectoryPlan& plan)
		{
			if (IsTerminalPhase(phase_))
				return false;
			if (phase_ != CohortPhase::kDormant)
				return Fail(BuilderIssue::kWrongPhase);
			const auto planIssue = ValidateDirectoryPlan(plan);
			if (planIssue != DiscoveryIssue::kNone) {
				return Fail(
					BuilderIssue::kDirectoryRejected, planIssue,
					MirrorAuthorCatalogParser::ParseIssue::kNone,
					MergeIssue::kNone, false);
			}
			expectedFileCount_ = plan.fileCount;
			expectedLeaves_ = plan.leaves;
			directoryMissing_ = plan.directoryMissing;
			phase_ = CohortPhase::kAdding;
			builderIssue_ = BuilderIssue::kNone;
			discoveryIssue_ = DiscoveryIssue::kNone;
			return true;
		}

		[[nodiscard]] bool AddFile(
			const std::string_view providerLeaf,
			const std::uint64_t byteCount,
			const Digest256& contentDigest,
			const MirrorAuthorCatalogParser::CanonicalCatalog& parsedCatalog)
		{
			if (!CheckExpectedLeaf(providerLeaf))
				return false;
			if (byteCount > kMaximumCatalogFileBytes) {
				return Fail(
					BuilderIssue::kFileCapacityExceeded,
					DiscoveryIssue::kFileCapacityExceeded);
			}
			if (byteCount > kMaximumAggregateBytes - aggregateLength_) {
				return Fail(
					BuilderIssue::kAggregateCapacityExceeded,
					DiscoveryIssue::kAggregateCapacityExceeded);
			}

			const auto validation =
				MirrorAuthorCatalogParser::ValidateCanonicalCatalog(parsedCatalog);
			if (validation != MirrorAuthorCatalogParser::ParseIssue::kNone) {
				return Fail(
					BuilderIssue::kCatalogRejected, DiscoveryIssue::kNone,
					validation);
			}
			const auto canonicalByteCount =
				MirrorAuthorCatalogParser::CanonicalByteSize(parsedCatalog);
			if (canonicalByteCount == 0 ||
				byteCount != static_cast<std::uint64_t>(canonicalByteCount)) {
				return Fail(BuilderIssue::kByteCountMismatch);
			}
			const auto addedRaw =
				static_cast<std::size_t>(parsedCatalog.entryCount);
			if (addedRaw > kMaximumRawRegistrations - rawRegistrationCount_) {
				return Fail(
					BuilderIssue::kRawRegistrationCapacityExceeded,
					DiscoveryIssue::kNone,
					MirrorAuthorCatalogParser::ParseIssue::kNone,
					MergeIssue::kRawRegistrationCapacityExceeded);
			}

			if (!MergeParsedCatalog(parsedCatalog))
				return false;

			auto& summary = fileSummaries_[filesAdded_];
			summary.leaf = expectedLeaves_[filesAdded_];
			summary.byteCount = byteCount;
			summary.contentDigest = contentDigest;
			++filesAdded_;
			aggregateLength_ += byteCount;
			rawRegistrationCount_ += addedRaw;
			failureLeaf_ = {};
			return true;
		}

		[[nodiscard]] bool RejectParsedFile(
			const std::string_view providerLeaf,
			const MirrorAuthorCatalogParser::ParseIssue issue,
			const std::size_t errorOffset)
		{
			if (!CheckExpectedLeaf(providerLeaf))
				return false;
			parseErrorOffset_ = errorOffset;
			return Fail(
				BuilderIssue::kCatalogRejected, DiscoveryIssue::kNone, issue);
		}

		[[nodiscard]] bool Close()
		{
			if (IsTerminalPhase(phase_))
				return false;
			if (phase_ != CohortPhase::kAdding)
				return Fail(BuilderIssue::kWrongPhase);
			if (filesAdded_ != expectedFileCount_)
				return Fail(BuilderIssue::kIncompleteCohort);
			phase_ = CohortPhase::kClosed;
			return true;
		}

		[[nodiscard]] constexpr CohortPhase Phase() const noexcept
		{
			return phase_;
		}

		[[nodiscard]] constexpr std::string_view NextExpectedLeaf() const noexcept
		{
			return phase_ == CohortPhase::kAdding &&
				filesAdded_ < expectedFileCount_ ?
				expectedLeaves_[filesAdded_].View() : std::string_view{};
		}

		[[nodiscard]] constexpr CohortResult Result() const noexcept
		{
			CohortResult result{};
			result.disposition = phase_ == CohortPhase::kClosed ?
				CohortDisposition::kAccepted :
				phase_ == CohortPhase::kQuarantined ?
					CohortDisposition::kQuarantined : CohortDisposition::kAborted;
			result.builderIssue = builderIssue_;
			result.discoveryIssue = discoveryIssue_;
			result.parseIssue = parseIssue_;
			result.parseErrorOffset = parseErrorOffset_;
			result.mergeIssue = mergeIssue_;
			result.failureLeaf = failureLeaf_;
			if (phase_ == CohortPhase::kClosed) {
				result.directoryMissing = directoryMissing_;
				result.fileCount = filesAdded_;
				result.aggregateLength = aggregateLength_;
				result.rawRegistrationCount = rawRegistrationCount_;
				result.files = fileSummaries_;
				result.catalog = mergedCatalog_;
			}
			return result;
		}

	private:
		[[nodiscard]] static constexpr bool IsTerminalPhase(
			const CohortPhase phase) noexcept
		{
			return phase == CohortPhase::kClosed ||
			       phase == CohortPhase::kAborted ||
			       phase == CohortPhase::kQuarantined;
		}

		[[nodiscard]] bool CheckExpectedLeaf(
			const std::string_view providerLeaf)
		{
			if (IsTerminalPhase(phase_))
				return false;
			if (phase_ != CohortPhase::kAdding)
				return Fail(BuilderIssue::kWrongPhase);
			if (filesAdded_ >= expectedFileCount_)
				return Fail(BuilderIssue::kUnexpectedFile);
			if (providerLeaf != expectedLeaves_[filesAdded_].View()) {
				failureLeaf_ = IsRecognizedCatalogLeaf(providerLeaf) ?
					CopyCatalogLeaf(providerLeaf) : OwnedCatalogLeaf{};
				return Fail(BuilderIssue::kLeafOrderMismatch);
			}
			failureLeaf_ = expectedLeaves_[filesAdded_];
			return true;
		}

		[[nodiscard]] bool MergeParsedCatalog(
			const MirrorAuthorCatalogParser::CanonicalCatalog& parsedCatalog)
		{
			for (const auto& declaration : parsedCatalog.Entries()) {
				std::size_t insertion = 0;
				while (insertion < mergedCatalog_.entryCount &&
					MirrorAuthorCatalogParser::CompareIdentity(
						mergedCatalog_.registrations[insertion], declaration) < 0) {
					++insertion;
				}
				if (insertion < mergedCatalog_.entryCount &&
					MirrorAuthorCatalogParser::SameIdentity(
						mergedCatalog_.registrations[insertion], declaration)) {
					if (MirrorAuthorCatalogParser::SameDeclaration(
							mergedCatalog_.registrations[insertion], declaration)) {
						continue;
					}
					return Fail(
						BuilderIssue::kConflictingDeclaration,
						DiscoveryIssue::kNone,
						MirrorAuthorCatalogParser::ParseIssue::kNone,
						MergeIssue::kConflictingDeclaration, true);
				}
				if (mergedCatalog_.entryCount >= kMaximumUniqueBases) {
					return Fail(
						BuilderIssue::kUniqueBaseCapacityExceeded,
						DiscoveryIssue::kNone,
						MirrorAuthorCatalogParser::ParseIssue::kNone,
						MergeIssue::kUniqueBaseCapacityExceeded, true);
				}
				for (std::size_t index = mergedCatalog_.entryCount;
					index > insertion; --index) {
					mergedCatalog_.registrations[index] =
						mergedCatalog_.registrations[index - 1];
				}
				mergedCatalog_.registrations[insertion] = declaration;
				++mergedCatalog_.entryCount;
			}
			return true;
		}

		[[nodiscard]] bool Fail(
			const BuilderIssue builderIssue,
			const DiscoveryIssue discoveryIssue = DiscoveryIssue::kNone,
			const MirrorAuthorCatalogParser::ParseIssue parseIssue =
				MirrorAuthorCatalogParser::ParseIssue::kNone,
			const MergeIssue mergeIssue = MergeIssue::kNone,
			const bool quarantine = false)
		{
			builderIssue_ = builderIssue;
			discoveryIssue_ = discoveryIssue;
			parseIssue_ = parseIssue;
			mergeIssue_ = mergeIssue;
			phase_ = quarantine ?
				CohortPhase::kQuarantined : CohortPhase::kAborted;
			// Terminal failure never leaves a provider summary or declaration live.
			expectedLeaves_ = {};
			expectedFileCount_ = 0;
			fileSummaries_ = {};
			filesAdded_ = 0;
			aggregateLength_ = 0;
			rawRegistrationCount_ = 0;
			mergedCatalog_ = {};
			directoryMissing_ = false;
			return false;
		}

		CohortPhase phase_{ CohortPhase::kDormant };
		BuilderIssue builderIssue_{ BuilderIssue::kWrongPhase };
		DiscoveryIssue discoveryIssue_{ DiscoveryIssue::kNone };
		MirrorAuthorCatalogParser::ParseIssue parseIssue_{
			MirrorAuthorCatalogParser::ParseIssue::kNone };
		std::size_t parseErrorOffset_{ 0 };
		MergeIssue mergeIssue_{ MergeIssue::kNone };
		OwnedCatalogLeaf failureLeaf_{};
		bool directoryMissing_{ false };
		std::uint32_t expectedFileCount_{ 0 };
		std::array<OwnedCatalogLeaf, kMaximumCatalogFiles> expectedLeaves_{};
		std::uint32_t filesAdded_{ 0 };
		std::array<FileSummary, kMaximumCatalogFiles> fileSummaries_{};
		std::uint64_t aggregateLength_{ 0 };
		std::size_t rawRegistrationCount_{ 0 };
		MirrorAuthorCatalogParser::CanonicalCatalog mergedCatalog_{};
	};

	[[nodiscard]] inline CohortResult ParseAndMergeSnapshots(
		SnapshotResult snapshots)
	{
		if (!snapshots.Accepted()) {
			CohortResult result{};
			result.builderIssue = BuilderIssue::kDirectoryRejected;
			result.discoveryIssue = snapshots.issue;
			return result;
		}

		CohortBuilder builder{};
		if (!builder.Begin(snapshots.plan))
			return builder.Result();
		for (const auto& file : snapshots.files) {
			const auto parse = MirrorAuthorCatalogParser::ParseCanonicalCatalog(
				file.Bytes());
			if (!parse.Succeeded()) {
				(void)builder.RejectParsedFile(
					file.Leaf(), parse.issue, parse.errorOffset);
				return builder.Result();
			}
			if (!builder.AddFile(
					file.Leaf(), static_cast<std::uint64_t>(file.Length()),
					file.ContentDigest(), parse.catalog)) {
				return builder.Result();
			}
		}
		(void)builder.Close();
		return builder.Result();
	}

	template <class ReadOne>
	[[nodiscard]] CohortResult DiscoverSnapshotAndMerge(
		const DirectoryObservation& directory,
		ReadOne&& readOne)
	{
		return ParseAndMergeSnapshots(DiscoverAndSnapshot(
			directory, std::forward<ReadOne>(readOne)));
	}

	enum class DigestPurpose : std::uint8_t
	{
		kRawSnapshotSHA256Input,
		kInputCohortSHA256Input,
		kMergedSemanticSHA256Input
	};

	struct DigestInput
	{
		DigestPurpose purpose{ DigestPurpose::kRawSnapshotSHA256Input };
		std::vector<std::byte> material{};

		[[nodiscard]] friend bool operator==(
			const DigestInput&,
			const DigestInput&) = default;
	};

	namespace detail
	{
		inline void AppendASCII(
			std::vector<std::byte>& output,
			const std::string_view value)
		{
			for (const char byte : value) {
				output.push_back(static_cast<std::byte>(
					static_cast<unsigned char>(byte)));
			}
		}

		inline void AppendU32LE(
			std::vector<std::byte>& output,
			const std::uint32_t value)
		{
			for (unsigned int shift = 0; shift < 32; shift += 8) {
				output.push_back(static_cast<std::byte>(
					static_cast<unsigned char>((value >> shift) & 0xFFU)));
			}
		}

		inline void AppendU64LE(
			std::vector<std::byte>& output,
			const std::uint64_t value)
		{
			for (unsigned int shift = 0; shift < 64; shift += 8) {
				output.push_back(static_cast<std::byte>(
					static_cast<unsigned char>((value >> shift) & 0xFFU)));
			}
		}
	}

	// Exact SHA-256 input for FileSummary::contentDigest: raw snapshot bytes,
	// without a prefix, filename, or framing.
	[[nodiscard]] inline DigestInput BuildRawSnapshotDigestInput(
		const OwnedFileSnapshot& snapshot)
	{
		DigestInput input{};
		input.purpose = DigestPurpose::kRawSnapshotSHA256Input;
		const auto bytes = snapshot.Bytes();
		input.material.assign(bytes.begin(), bytes.end());
		return input;
	}

	/**
	 * Exact input-cohort SHA-256 preimage:
	 *   ASCII domain, NUL, u32-LE file count, then for each ordinal leaf:
	 *   u32-LE leaf length, leaf bytes, u64-LE byte count, raw SHA-256[32].
	 */
	[[nodiscard]] inline DigestInput BuildInputCohortDigestInput(
		const std::span<const FileSummary> summaries)
	{
		std::vector<const FileSummary*> ordered{};
		ordered.reserve(summaries.size());
		for (const auto& summary : summaries)
			ordered.push_back(&summary);
		std::sort(ordered.begin(), ordered.end(),
			[](const FileSummary* left, const FileSummary* right) noexcept {
				return OrdinalLeafLess(left->leaf.View(), right->leaf.View());
			});

		DigestInput input{};
		input.purpose = DigestPurpose::kInputCohortSHA256Input;
		detail::AppendASCII(input.material, kInputCohortDigestDomain);
		input.material.push_back(std::byte{ 0 });
		detail::AppendU32LE(
			input.material, static_cast<std::uint32_t>(ordered.size()));
		for (const auto* summary : ordered) {
			const auto leaf = summary->leaf.View();
			detail::AppendU32LE(
				input.material, static_cast<std::uint32_t>(leaf.size()));
			detail::AppendASCII(input.material, leaf);
			detail::AppendU64LE(input.material, summary->byteCount);
			input.material.insert(
				input.material.end(), summary->contentDigest.bytes.begin(),
				summary->contentDigest.bytes.end());
		}
		return input;
	}

	// Exact semantic SHA-256 preimage: the parser's canonical merged catalog
	// bytes, including its final LF.  No separate domain bytes are prepended.
	[[nodiscard]] inline DigestInput BuildMergedSemanticDigestInput(
		const MirrorAuthorCatalogParser::CanonicalCatalog& catalog)
	{
		DigestInput input{};
		input.purpose = DigestPurpose::kMergedSemanticSHA256Input;
		const auto byteCount =
			MirrorAuthorCatalogParser::CanonicalByteSize(catalog);
		if (byteCount == 0)
			return input;
		input.material.resize(byteCount);
		std::size_t written = 0;
		if (!MirrorAuthorCatalogParser::TryEmitCanonicalCatalog(
				catalog,
				std::span<std::byte>{ input.material.data(), input.material.size() },
				written) || written != byteCount) {
			input.material.clear();
		}
		return input;
	}
}
