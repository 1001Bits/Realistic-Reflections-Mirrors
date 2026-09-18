#pragma once

#include "MirrorAuthorCatalogDiscoveryPolicy.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MirrorAuthorCatalogRuntimeLoader
{
	// The Win32 adapter is called only by the exact-marker default-off diagnostic.
	// Catalog values and digests remain non-authorizing diagnostic facts.
	inline constexpr bool kDiagnosticFilesystemAdapterImplemented = true;
	inline constexpr bool kDefaultOffRuntimeCallWired = true;
	inline constexpr bool kRuntimeIntegrationWired = false;
	inline constexpr bool kRuntimeTESAuthorityWired = false;
	inline constexpr bool kRecognitionWired = false;
	inline constexpr bool kDeliveryWired = false;
	inline constexpr bool kSupportedRuntimeFeature = false;
	inline constexpr bool kDigestsGrantAuthority = false;

	inline constexpr std::wstring_view kCatalogDirectoryRelativePath =
		L"SKSE\\Plugins\\RealisticReflections\\MirrorAuthorCatalogs";

	enum class LoaderIssue : std::uint8_t
	{
		kNone,
		kInvalidDataDirectory,
		kDirectoryOpenFault,
		kDirectoryMetadataFault,
		kExistingPathNotDirectory,
		kDirectoryReparsePoint,
		kDirectoryEnumerationFault,
		kDirectoryPlanRejected,
		kFileOpenFault,
		kFileMetadataFault,
		kFileIdentityMismatch,
		kFileNotRegularDiskFile,
		kFileReparsePoint,
		kFileSizeInvalid,
		kFileReadFault,
		kFileDigestFault,
		kCatalogParseRejected,
		kCohortRejected,
		kOutputDigestFault,
		kUnexpectedException
	};

	enum class SourceStatus : std::uint8_t
	{
		kSuccess,
		kMissing,
		kFault
	};

	struct SourceDirectoryEntry
	{
		std::string leaf{};
		MirrorAuthorCatalogDiscoveryPolicy::EntryKind kind{
			MirrorAuthorCatalogDiscoveryPolicy::EntryKind::kOther };
		bool reparsePoint{ false };
	};

	struct SourceDirectoryResult
	{
		SourceStatus status{ SourceStatus::kFault };
		LoaderIssue issue{ LoaderIssue::kDirectoryEnumerationFault };
		std::uint32_t nativeError{ 0 };
		std::vector<SourceDirectoryEntry> entries{};
	};

	struct SourceFileResult
	{
		SourceStatus status{ SourceStatus::kFault };
		LoaderIssue issue{ LoaderIssue::kFileReadFault };
		std::uint32_t nativeError{ 0 };
		std::vector<std::byte> bytes{};
		MirrorAuthorCatalogDiscoveryPolicy::Digest256 contentDigest{};
	};

	/**
	 * Injectable source seam used by the coordinator tests.  The production
	 * implementation holds one non-reparse directory handle from enumeration
	 * through every sorted ReadOne call.  Each ReadOne performs one CreateFileW,
	 * one bounded ReadFile, and raw SHA-256 over those returned bytes.
	 */
	class CatalogSource
	{
	public:
		virtual ~CatalogSource() = default;
		[[nodiscard]] virtual SourceDirectoryResult EnumerateImmediate() = 0;
		[[nodiscard]] virtual SourceFileResult ReadOne(
			std::string_view ordinalProviderLeaf) = 0;
	};

	using DigestFunction = bool (*)(
		void* context,
		std::span<const std::byte> input,
		MirrorAuthorCatalogDiscoveryPolicy::Digest256& output,
		std::uint32_t& nativeError) noexcept;

	struct LoadResult
	{
		LoaderIssue issue{ LoaderIssue::kUnexpectedException };
		std::uint32_t nativeError{ 0 };
		MirrorAuthorCatalogDiscoveryPolicy::DiscoveryIssue discoveryIssue{
			MirrorAuthorCatalogDiscoveryPolicy::DiscoveryIssue::kNone };
		MirrorAuthorCatalogParser::ParseIssue parseIssue{
			MirrorAuthorCatalogParser::ParseIssue::kNone };
		std::size_t parseErrorOffset{ 0 };
		MirrorAuthorCatalogDiscoveryPolicy::BuilderIssue builderIssue{
			MirrorAuthorCatalogDiscoveryPolicy::BuilderIssue::kNone };
		MirrorAuthorCatalogDiscoveryPolicy::MergeIssue mergeIssue{
			MirrorAuthorCatalogDiscoveryPolicy::MergeIssue::kNone };
		std::size_t sourceFileRequests{ 0 };
		MirrorAuthorCatalogDiscoveryPolicy::CohortResult cohort{};
		MirrorAuthorCatalogDiscoveryPolicy::Digest256 inputCohortDigest{};
		MirrorAuthorCatalogDiscoveryPolicy::Digest256 mergedSemanticDigest{};
		bool outputDigestsValid{ false };

		[[nodiscard]] constexpr bool Succeeded() const noexcept
		{
			return issue == LoaderIssue::kNone && cohort.Accepted() &&
			       outputDigestsValid;
		}
	};

	/** Pure coordinator over an injected one-read source and digest primitive. */
	void LoadFromSourceInto(
		CatalogSource& source,
		DigestFunction digestFunction,
		void* digestContext,
		LoadResult& output) noexcept;

	/** Convenience value-returning wrapper over LoadFromSourceInto. */
	[[nodiscard]] LoadResult LoadFromSource(
		CatalogSource& source,
		DigestFunction digestFunction,
		void* digestContext = nullptr) noexcept;

	/**
	 * Append the exact relative catalog directory to the caller-supplied Data
	 * directory and execute the Win32/CNG source.  The caller, not this adapter,
	 * derives the executable's Data directory.
	 */
	void LoadFromDataDirectoryInto(
		std::wstring_view executableDerivedDataDirectory,
		LoadResult& output) noexcept;

	/** Convenience wrapper; runtime integration uses the output-parameter API. */
	[[nodiscard]] LoadResult LoadFromDataDirectory(
		std::wstring_view executableDerivedDataDirectory) noexcept;
}
