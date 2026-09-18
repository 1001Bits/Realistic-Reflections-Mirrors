#include "MirrorAuthorCatalogRuntimeLoader.h"

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#pragma comment(lib, "bcrypt.lib")

namespace MirrorAuthorCatalogRuntimeLoader
{
	namespace
	{
		using MirrorAuthorCatalogDiscoveryPolicy::BuildDirectoryPlan;
		using MirrorAuthorCatalogDiscoveryPolicy::BuildInputCohortDigestInput;
		using MirrorAuthorCatalogDiscoveryPolicy::BuildMergedSemanticDigestInput;
		using MirrorAuthorCatalogDiscoveryPolicy::CohortBuilder;
		using MirrorAuthorCatalogDiscoveryPolicy::Digest256;
		using MirrorAuthorCatalogDiscoveryPolicy::DirectoryEntryObservation;
		using MirrorAuthorCatalogDiscoveryPolicy::DirectoryObservation;
		using MirrorAuthorCatalogDiscoveryPolicy::DirectoryPlan;
		using MirrorAuthorCatalogDiscoveryPolicy::DirectoryState;
		using MirrorAuthorCatalogDiscoveryPolicy::EntryKind;
		using MirrorAuthorCatalogDiscoveryPolicy::EnumerationScope;

		static_assert(std::is_trivially_copyable_v<CohortBuilder>);
		static_assert(std::is_trivially_copyable_v<
			MirrorAuthorCatalogParser::ParseResult>);
		static_assert(std::is_trivially_copyable_v<LoadResult>);

		class UniqueHandle final
		{
		public:
			UniqueHandle() = default;
			explicit UniqueHandle(const HANDLE value) noexcept : value_(value) {}

			UniqueHandle(const UniqueHandle&) = delete;
			UniqueHandle& operator=(const UniqueHandle&) = delete;

			UniqueHandle(UniqueHandle&& other) noexcept :
				value_(std::exchange(other.value_, INVALID_HANDLE_VALUE))
			{}

			UniqueHandle& operator=(UniqueHandle&& other) noexcept
			{
				if (this != std::addressof(other)) {
					Reset();
					value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
				}
				return *this;
			}

			~UniqueHandle()
			{
				Reset();
			}

			[[nodiscard]] HANDLE Get() const noexcept
			{
				return value_;
			}

			[[nodiscard]] bool Valid() const noexcept
			{
				return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
			}

			void Reset(const HANDLE replacement = INVALID_HANDLE_VALUE) noexcept
			{
				if (Valid())
					(void)CloseHandle(value_);
				value_ = replacement;
			}

		private:
			HANDLE value_{ INVALID_HANDLE_VALUE };
		};

		class UniqueAlgorithm final
		{
		public:
			UniqueAlgorithm() = default;
			UniqueAlgorithm(const UniqueAlgorithm&) = delete;
			UniqueAlgorithm& operator=(const UniqueAlgorithm&) = delete;

			~UniqueAlgorithm()
			{
				if (value_ != nullptr)
					(void)BCryptCloseAlgorithmProvider(value_, 0);
			}

			[[nodiscard]] BCRYPT_ALG_HANDLE* Put() noexcept
			{
				return std::addressof(value_);
			}

			[[nodiscard]] BCRYPT_ALG_HANDLE Get() const noexcept
			{
				return value_;
			}

		private:
			BCRYPT_ALG_HANDLE value_{ nullptr };
		};

		class UniqueHash final
		{
		public:
			UniqueHash() = default;
			UniqueHash(const UniqueHash&) = delete;
			UniqueHash& operator=(const UniqueHash&) = delete;

			~UniqueHash()
			{
				if (value_ != nullptr)
					(void)BCryptDestroyHash(value_);
			}

			[[nodiscard]] BCRYPT_HASH_HANDLE* Put() noexcept
			{
				return std::addressof(value_);
			}

			[[nodiscard]] BCRYPT_HASH_HANDLE Get() const noexcept
			{
				return value_;
			}

		private:
			BCRYPT_HASH_HANDLE value_{ nullptr };
		};

		template <class Value>
		void SecureScrubObject(Value& value) noexcept
		{
			static_assert(std::is_trivially_copyable_v<Value>);
			(void)SecureZeroMemory(std::addressof(value), sizeof(value));
		}

		void SecureScrubBytes(std::vector<std::byte>& bytes) noexcept
		{
			if (!bytes.empty())
				(void)SecureZeroMemory(bytes.data(), bytes.size());
			bytes.clear();
		}

		class VectorScrubGuard final
		{
		public:
			explicit VectorScrubGuard(std::vector<std::byte>& bytes) noexcept :
				bytes_(std::addressof(bytes))
			{}

			VectorScrubGuard(const VectorScrubGuard&) = delete;
			VectorScrubGuard& operator=(const VectorScrubGuard&) = delete;

			~VectorScrubGuard()
			{
				SecureScrubBytes(*bytes_);
			}

		private:
			std::vector<std::byte>* bytes_;
		};

		template <class Value>
		class ObjectScrubGuard final
		{
		public:
			explicit ObjectScrubGuard(Value& value) noexcept :
				value_(std::addressof(value))
			{}

			ObjectScrubGuard(const ObjectScrubGuard&) = delete;
			ObjectScrubGuard& operator=(const ObjectScrubGuard&) = delete;

			~ObjectScrubGuard()
			{
				SecureScrubObject(*value_);
			}

		private:
			Value* value_;
		};

		template <class Value>
		struct SecureDelete
		{
			void operator()(Value* value) const noexcept
			{
				if (value != nullptr) {
					SecureScrubObject(*value);
					delete value;
				}
			}
		};

		template <class Value>
		using SecureUniquePtr = std::unique_ptr<Value, SecureDelete<Value>>;

		[[nodiscard]] bool StatusSucceeded(const NTSTATUS status) noexcept
		{
			return status >= 0;
		}

		[[nodiscard]] bool CngSHA256(
			void*,
			const std::span<const std::byte> input,
			Digest256& output,
			std::uint32_t& nativeError) noexcept
		{
			output = {};
			nativeError = 0;
			try {
				UniqueAlgorithm algorithm{};
				auto status = BCryptOpenAlgorithmProvider(
					algorithm.Put(), BCRYPT_SHA256_ALGORITHM, nullptr, 0);
				if (!StatusSucceeded(status)) {
					nativeError = static_cast<std::uint32_t>(status);
					return false;
				}

				DWORD objectBytes = 0;
				DWORD copied = 0;
				status = BCryptGetProperty(
					algorithm.Get(), BCRYPT_OBJECT_LENGTH,
					reinterpret_cast<PUCHAR>(std::addressof(objectBytes)),
					sizeof(objectBytes), std::addressof(copied), 0);
				if (!StatusSucceeded(status) || copied != sizeof(objectBytes) ||
					objectBytes == 0) {
					nativeError = static_cast<std::uint32_t>(status);
					return false;
				}

				DWORD hashBytes = 0;
				copied = 0;
				status = BCryptGetProperty(
					algorithm.Get(), BCRYPT_HASH_LENGTH,
					reinterpret_cast<PUCHAR>(std::addressof(hashBytes)),
					sizeof(hashBytes), std::addressof(copied), 0);
				if (!StatusSucceeded(status) || copied != sizeof(hashBytes) ||
					hashBytes != output.bytes.size()) {
					nativeError = static_cast<std::uint32_t>(status);
					return false;
				}

				std::vector<std::byte> object(objectBytes);
				VectorScrubGuard objectGuard(object);
				UniqueHash hash{};
				status = BCryptCreateHash(
					algorithm.Get(), hash.Put(),
					reinterpret_cast<PUCHAR>(object.data()), objectBytes,
					nullptr, 0, 0);
				if (!StatusSucceeded(status)) {
					nativeError = static_cast<std::uint32_t>(status);
					return false;
				}
				if (input.size() > std::numeric_limits<ULONG>::max()) {
					nativeError = ERROR_FILE_TOO_LARGE;
					return false;
				}
				status = BCryptHashData(
					hash.Get(),
					reinterpret_cast<PUCHAR>(
						const_cast<std::byte*>(input.data())),
					static_cast<ULONG>(input.size()), 0);
				if (!StatusSucceeded(status)) {
					nativeError = static_cast<std::uint32_t>(status);
					return false;
				}
				status = BCryptFinishHash(
					hash.Get(), reinterpret_cast<PUCHAR>(output.bytes.data()),
					static_cast<ULONG>(output.bytes.size()), 0);
				if (!StatusSucceeded(status)) {
					nativeError = static_cast<std::uint32_t>(status);
					output = {};
					return false;
				}
				return true;
			} catch (...) {
				output = {};
				nativeError = ERROR_NOT_ENOUGH_MEMORY;
				return false;
			}
		}

		[[nodiscard]] bool IsMissingError(const DWORD error) noexcept
		{
			return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
		}

		[[nodiscard]] SourceDirectoryResult DirectoryFailure(
			const LoaderIssue issue,
			const DWORD nativeError) noexcept
		{
			SourceDirectoryResult result{};
			result.status = SourceStatus::kFault;
			result.issue = issue;
			result.nativeError = nativeError;
			return result;
		}

		[[nodiscard]] SourceFileResult FileFailure(
			const LoaderIssue issue,
			const DWORD nativeError) noexcept
		{
			SourceFileResult result{};
			result.status = SourceStatus::kFault;
			result.issue = issue;
			result.nativeError = nativeError;
			return result;
		}

		[[nodiscard]] std::string NarrowLeafForPolicy(
			const WCHAR* name,
			const std::size_t characterCount)
		{
			std::string output{};
			output.reserve(characterCount);
			for (std::size_t index = 0; index < characterCount; ++index) {
				const auto value = static_cast<std::uint32_t>(name[index]);
				output.push_back(value <= 0x7FU ?
					static_cast<char>(value) : static_cast<char>(0xFF));
			}
			return output;
		}

		[[nodiscard]] bool IsDotEntry(
			const WCHAR* name,
			const std::size_t characterCount) noexcept
		{
			return (characterCount == 1 && name[0] == L'.') ||
			       (characterCount == 2 && name[0] == L'.' && name[1] == L'.');
		}

		class Win32CatalogSource final : public CatalogSource
		{
			struct ExpectedFileIdentity
			{
				std::string leaf{};
				DWORD volumeSerial{ 0 };
				std::uint64_t fileID{ 0 };
			};

		public:
			explicit Win32CatalogSource(std::wstring root) : root_(std::move(root)) {}

			[[nodiscard]] SourceDirectoryResult EnumerateImmediate() override
			{
				if (enumerated_)
					return DirectoryFailure(LoaderIssue::kDirectoryEnumerationFault,
						ERROR_INVALID_STATE);
				enumerated_ = true;

				const auto raw = CreateFileW(
					root_.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
					FILE_SHARE_READ, nullptr, OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
					nullptr);
				if (raw == INVALID_HANDLE_VALUE) {
					const auto error = GetLastError();
					if (IsMissingError(error)) {
						SourceDirectoryResult missing{};
						missing.status = SourceStatus::kMissing;
						missing.issue = LoaderIssue::kNone;
						missing.nativeError = error;
						return missing;
					}
					return DirectoryFailure(LoaderIssue::kDirectoryOpenFault, error);
				}
				directory_.Reset(raw);

				SetLastError(ERROR_SUCCESS);
				const auto type = GetFileType(directory_.Get());
				if (type != FILE_TYPE_DISK) {
					const auto error = type == FILE_TYPE_UNKNOWN ? GetLastError() : 0;
					return DirectoryFailure(
						error != ERROR_SUCCESS ? LoaderIssue::kDirectoryMetadataFault :
							LoaderIssue::kExistingPathNotDirectory,
						error);
				}

				FILE_ATTRIBUTE_TAG_INFO attributes{};
				if (!GetFileInformationByHandleEx(
						directory_.Get(), FileAttributeTagInfo, std::addressof(attributes),
						sizeof(attributes))) {
					return DirectoryFailure(
						LoaderIssue::kDirectoryMetadataFault, GetLastError());
				}
				if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
					return DirectoryFailure(LoaderIssue::kDirectoryReparsePoint, 0);
				}

				FILE_STANDARD_INFO standard{};
				if (!GetFileInformationByHandleEx(
						directory_.Get(), FileStandardInfo, std::addressof(standard),
						sizeof(standard))) {
					return DirectoryFailure(
						LoaderIssue::kDirectoryMetadataFault, GetLastError());
				}
				const bool attributeDirectory =
					(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
				if (standard.Directory == FALSE || !attributeDirectory) {
					return DirectoryFailure(LoaderIssue::kExistingPathNotDirectory, 0);
				}
				BY_HANDLE_FILE_INFORMATION directoryIdentity{};
				if (!GetFileInformationByHandle(
						directory_.Get(), std::addressof(directoryIdentity))) {
					return DirectoryFailure(
						LoaderIssue::kDirectoryMetadataFault, GetLastError());
				}
				volumeSerial_ = directoryIdentity.dwVolumeSerialNumber;

					SourceDirectoryResult result{};
				result.status = SourceStatus::kSuccess;
				result.issue = LoaderIssue::kNone;
				try {
					constexpr std::size_t kEnumerationBufferBytes = 64U * 1024U;
					std::vector<std::uint64_t> storage(
						kEnumerationBufferBytes / sizeof(std::uint64_t));
					for (;;) {
						const auto succeeded = GetFileInformationByHandleEx(
							directory_.Get(), FileIdBothDirectoryInfo, storage.data(),
							static_cast<DWORD>(kEnumerationBufferBytes));
						if (!succeeded) {
							const auto error = GetLastError();
							if (error == ERROR_NO_MORE_FILES)
								break;
							return DirectoryFailure(
								LoaderIssue::kDirectoryEnumerationFault, error);
						}
						std::size_t offset = 0;
						for (;;) {
							if (offset > kEnumerationBufferBytes -
								sizeof(FILE_ID_BOTH_DIR_INFO)) {
								return DirectoryFailure(
									LoaderIssue::kDirectoryEnumerationFault,
									ERROR_INVALID_DATA);
							}
							const auto* info = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(
								reinterpret_cast<const std::byte*>(storage.data()) + offset);
							if ((info->FileNameLength % sizeof(WCHAR)) != 0) {
								return DirectoryFailure(
									LoaderIssue::kDirectoryEnumerationFault,
									ERROR_INVALID_DATA);
							}
							const auto nameCharacters = static_cast<std::size_t>(
								info->FileNameLength / sizeof(WCHAR));
							const auto fixedBytes = offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
							const auto recordBytes = info->NextEntryOffset != 0 ?
								static_cast<std::size_t>(info->NextEntryOffset) :
								kEnumerationBufferBytes - offset;
							if (recordBytes < fixedBytes ||
								info->FileNameLength > recordBytes - fixedBytes ||
								recordBytes > kEnumerationBufferBytes - offset) {
								return DirectoryFailure(
									LoaderIssue::kDirectoryEnumerationFault,
									ERROR_INVALID_DATA);
							}
							if (!IsDotEntry(info->FileName, nameCharacters)) {
								SourceDirectoryEntry entry{};
								entry.leaf = NarrowLeafForPolicy(
									info->FileName, nameCharacters);
								entry.reparsePoint =
									(info->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
								entry.kind =
									(info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ?
										EntryKind::kDirectory : EntryKind::kRegularFile;
								ExpectedFileIdentity identity{};
								identity.leaf = entry.leaf;
								identity.volumeSerial = volumeSerial_;
								identity.fileID = static_cast<std::uint64_t>(
									info->FileId.QuadPart);
								expectedIdentities_.push_back(std::move(identity));
								result.entries.push_back(std::move(entry));
								if (result.entries.size() >
									MirrorAuthorCatalogDiscoveryPolicy::kMaximumDirectoryEntries) {
									return result;
								}
							}
							if (info->NextEntryOffset == 0)
								break;
							if (info->NextEntryOffset < fixedBytes ||
								(info->NextEntryOffset %
									alignof(FILE_ID_BOTH_DIR_INFO)) != 0 ||
								info->NextEntryOffset > kEnumerationBufferBytes - offset) {
								return DirectoryFailure(
									LoaderIssue::kDirectoryEnumerationFault,
									ERROR_INVALID_DATA);
							}
							offset += info->NextEntryOffset;
						}
					}
				} catch (...) {
					return DirectoryFailure(
						LoaderIssue::kDirectoryEnumerationFault,
						ERROR_NOT_ENOUGH_MEMORY);
				}
				return result;
			}

			[[nodiscard]] SourceFileResult ReadOne(
				const std::string_view ordinalProviderLeaf) override
			{
				if (!enumerated_ || !directory_.Valid() ||
					!MirrorAuthorCatalogDiscoveryPolicy::IsRecognizedCatalogLeaf(
						ordinalProviderLeaf)) {
					return FileFailure(LoaderIssue::kFileOpenFault, ERROR_INVALID_STATE);
				}
				const auto expected = std::find_if(
					expectedIdentities_.begin(), expectedIdentities_.end(),
					[ordinalProviderLeaf](const ExpectedFileIdentity& identity) noexcept {
						return identity.leaf == ordinalProviderLeaf;
					});
				if (expected == expectedIdentities_.end()) {
					return FileFailure(
						LoaderIssue::kFileIdentityMismatch, ERROR_INVALID_DATA);
				}

				try {
					std::wstring path = root_;
					path.push_back(L'\\');
					for (const char byte : ordinalProviderLeaf)
						path.push_back(static_cast<wchar_t>(
							static_cast<unsigned char>(byte)));

					const auto raw = CreateFileW(
						path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
						OPEN_EXISTING,
						FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
						nullptr);
					if (raw == INVALID_HANDLE_VALUE)
						return FileFailure(LoaderIssue::kFileOpenFault, GetLastError());
					UniqueHandle file(raw);

					SetLastError(ERROR_SUCCESS);
					const auto type = GetFileType(file.Get());
					if (type != FILE_TYPE_DISK) {
						const auto error = type == FILE_TYPE_UNKNOWN ? GetLastError() : 0;
						return FileFailure(
							error != ERROR_SUCCESS ? LoaderIssue::kFileMetadataFault :
								LoaderIssue::kFileNotRegularDiskFile,
							error);
					}

					FILE_ATTRIBUTE_TAG_INFO attributes{};
					if (!GetFileInformationByHandleEx(
							file.Get(), FileAttributeTagInfo, std::addressof(attributes),
							sizeof(attributes))) {
						return FileFailure(
							LoaderIssue::kFileMetadataFault, GetLastError());
					}
					if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
						return FileFailure(LoaderIssue::kFileReparsePoint, 0);

					FILE_STANDARD_INFO standard{};
					if (!GetFileInformationByHandleEx(
							file.Get(), FileStandardInfo, std::addressof(standard),
							sizeof(standard))) {
						return FileFailure(
							LoaderIssue::kFileMetadataFault, GetLastError());
					}
					if (standard.Directory != FALSE ||
						(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
						standard.DeletePending != FALSE) {
						return FileFailure(LoaderIssue::kFileNotRegularDiskFile, 0);
					}
					BY_HANDLE_FILE_INFORMATION openedIdentity{};
					if (!GetFileInformationByHandle(
							file.Get(), std::addressof(openedIdentity))) {
						return FileFailure(
							LoaderIssue::kFileMetadataFault, GetLastError());
					}
					const auto openedFileID =
						(static_cast<std::uint64_t>(openedIdentity.nFileIndexHigh) << 32U) |
						static_cast<std::uint64_t>(openedIdentity.nFileIndexLow);
					if (openedIdentity.dwVolumeSerialNumber != expected->volumeSerial ||
						openedFileID != expected->fileID) {
						return FileFailure(LoaderIssue::kFileIdentityMismatch, 0);
					}
					if (standard.EndOfFile.QuadPart <= 0 ||
						static_cast<unsigned long long>(standard.EndOfFile.QuadPart) >
							MirrorAuthorCatalogDiscoveryPolicy::kMaximumCatalogFileBytes) {
						return FileFailure(LoaderIssue::kFileSizeInvalid, 0);
					}

					const auto byteCount = static_cast<std::size_t>(
						standard.EndOfFile.QuadPart);
					SourceFileResult result{};
					result.bytes.resize(byteCount);
					DWORD bytesRead = 0;
					SetLastError(ERROR_SUCCESS);
					if (!ReadFile(
							file.Get(), result.bytes.data(), static_cast<DWORD>(byteCount),
							std::addressof(bytesRead), nullptr) ||
						bytesRead != static_cast<DWORD>(byteCount)) {
						const auto error = GetLastError();
						SecureScrubBytes(result.bytes);
						return FileFailure(
							LoaderIssue::kFileReadFault,
							error != ERROR_SUCCESS ? error : ERROR_HANDLE_EOF);
					}

					std::uint32_t digestError = 0;
					if (!CngSHA256(
							nullptr,
							std::span<const std::byte>{
								result.bytes.data(), result.bytes.size() },
							result.contentDigest, digestError)) {
						SecureScrubBytes(result.bytes);
						return FileFailure(LoaderIssue::kFileDigestFault, digestError);
					}
					result.status = SourceStatus::kSuccess;
					result.issue = LoaderIssue::kNone;
					return result;
				} catch (...) {
					return FileFailure(
						LoaderIssue::kFileReadFault, ERROR_NOT_ENOUGH_MEMORY);
				}
			}

		private:
			std::wstring root_{};
			UniqueHandle directory_{};
			std::vector<ExpectedFileIdentity> expectedIdentities_{};
			DWORD volumeSerial_{ 0 };
			bool enumerated_{ false };
		};

		void CopyCohortDiagnostics(LoadResult& output) noexcept
		{
			output.discoveryIssue = output.cohort.discoveryIssue;
			output.parseIssue = output.cohort.parseIssue;
			output.parseErrorOffset = output.cohort.parseErrorOffset;
			output.builderIssue = output.cohort.builderIssue;
			output.mergeIssue = output.cohort.mergeIssue;
		}

		void DiscardBuilder(CohortBuilder& builder) noexcept
		{
			SecureScrubObject(builder);
		}

		void ScrubCohortValues(
			MirrorAuthorCatalogDiscoveryPolicy::CohortResult& cohort) noexcept
		{
			SecureScrubObject(cohort);
			cohort.disposition =
				MirrorAuthorCatalogDiscoveryPolicy::CohortDisposition::kAborted;
			cohort.builderIssue =
				MirrorAuthorCatalogDiscoveryPolicy::BuilderIssue::kWrongPhase;
		}

		void ResetAsUnexpectedFailure(
			LoadResult& output,
			const std::size_t sourceFileRequests) noexcept
		{
			SecureScrubObject(output);
			output.issue = LoaderIssue::kUnexpectedException;
			output.nativeError = ERROR_NOT_ENOUGH_MEMORY;
			output.sourceFileRequests = sourceFileRequests;
			output.cohort.disposition =
				MirrorAuthorCatalogDiscoveryPolicy::CohortDisposition::kAborted;
			output.cohort.builderIssue =
				MirrorAuthorCatalogDiscoveryPolicy::BuilderIssue::kWrongPhase;
		}

		void ResetLoadResult(LoadResult& output) noexcept
		{
			SecureScrubObject(output);
			output.issue = LoaderIssue::kUnexpectedException;
			output.cohort.disposition =
				MirrorAuthorCatalogDiscoveryPolicy::CohortDisposition::kAborted;
			output.cohort.builderIssue =
				MirrorAuthorCatalogDiscoveryPolicy::BuilderIssue::kWrongPhase;
		}

		void CoordinatorFailure(
			CohortBuilder& builder,
			LoadResult& output,
			const LoaderIssue issue,
			const std::uint32_t nativeError,
			const std::size_t sourceFileRequests,
			const bool captureBuilderDiagnostics = false,
			const MirrorAuthorCatalogDiscoveryPolicy::DiscoveryIssue
				discoveryOverride =
					MirrorAuthorCatalogDiscoveryPolicy::DiscoveryIssue::kNone) noexcept
		{
			output.issue = issue;
			output.nativeError = nativeError;
			output.sourceFileRequests = sourceFileRequests;
			if (captureBuilderDiagnostics) {
				output.cohort = builder.Result();
				CopyCohortDiagnostics(output);
			}
			if (discoveryOverride !=
				MirrorAuthorCatalogDiscoveryPolicy::DiscoveryIssue::kNone) {
				output.discoveryIssue = discoveryOverride;
			}
			DiscardBuilder(builder);
		}
	}

	void LoadFromSourceInto(
		CatalogSource& source,
		const DigestFunction digestFunction,
		void* const digestContext,
		LoadResult& output) noexcept
	{
		std::unique_ptr<CohortBuilder> builder{};
		std::size_t sourceFileRequests = 0;
		ResetLoadResult(output);
		try {
			builder = std::make_unique<CohortBuilder>();
			SourceDirectoryResult directory{};
			try {
				directory = source.EnumerateImmediate();
			} catch (...) {
				return CoordinatorFailure(
					*builder, output, LoaderIssue::kUnexpectedException, 0,
					sourceFileRequests);
			}

			if (directory.status == SourceStatus::kFault) {
				return CoordinatorFailure(
					*builder, output,
					directory.issue != LoaderIssue::kNone ? directory.issue :
						LoaderIssue::kDirectoryEnumerationFault,
					directory.nativeError, sourceFileRequests);
			}

			const auto observationCount = (std::min)(
				directory.entries.size(),
				MirrorAuthorCatalogDiscoveryPolicy::kMaximumDirectoryEntries + 1);
			std::vector<DirectoryEntryObservation> observations{};
			observations.reserve(observationCount);
			for (std::size_t index = 0; index < observationCount; ++index) {
				const auto& entry = directory.entries[index];
				observations.push_back({
					.leaf = entry.leaf,
					.kind = entry.kind,
					.reparsePoint = entry.reparsePoint
				});
			}

			const DirectoryObservation observation{
				.state = directory.status == SourceStatus::kMissing ?
					DirectoryState::kMissing : DirectoryState::kDirectory,
				.scope = directory.status == SourceStatus::kMissing ?
					EnumerationScope::kUnknown :
					EnumerationScope::kExactImmediateDirectory,
				.reparsePoint = false,
				.entries = observations
			};
			const DirectoryPlan plan = BuildDirectoryPlan(observation);
			if (!plan.Accepted()) {
				return CoordinatorFailure(
					*builder, output, LoaderIssue::kDirectoryPlanRejected, 0,
					sourceFileRequests, false, plan.issue);
			}
			if (!builder->Begin(plan)) {
				return CoordinatorFailure(
					*builder, output, LoaderIssue::kCohortRejected, 0,
					sourceFileRequests, true);
			}

			std::uint64_t aggregateBytes = 0;
			for (const auto& ownedLeaf : plan.ExpectedLeaves()) {
				const auto leaf = ownedLeaf.View();
				SourceFileResult file{};
				++sourceFileRequests;
				try {
					file = source.ReadOne(leaf);
				} catch (...) {
					return CoordinatorFailure(
						*builder, output, LoaderIssue::kUnexpectedException, 0,
						sourceFileRequests);
				}

				if (file.status != SourceStatus::kSuccess) {
					SecureScrubBytes(file.bytes);
					SecureScrubObject(file.contentDigest);
					return CoordinatorFailure(
						*builder, output,
						file.issue != LoaderIssue::kNone ? file.issue :
							LoaderIssue::kFileReadFault,
						file.nativeError, sourceFileRequests);
				}
				VectorScrubGuard fileBytesGuard(file.bytes);
				ObjectScrubGuard fileDigestGuard(file.contentDigest);
				if (file.bytes.empty() ||
					file.bytes.size() >
						MirrorAuthorCatalogDiscoveryPolicy::kMaximumCatalogFileBytes ||
					file.bytes.size() >
						MirrorAuthorCatalogDiscoveryPolicy::kMaximumAggregateBytes -
							aggregateBytes) {
					SecureScrubBytes(file.bytes);
					SecureScrubObject(file.contentDigest);
					return CoordinatorFailure(
						*builder, output, LoaderIssue::kFileSizeInvalid, 0,
						sourceFileRequests);
				}

				SecureUniquePtr<MirrorAuthorCatalogParser::ParseResult> parse(
					new MirrorAuthorCatalogParser::ParseResult(
						MirrorAuthorCatalogParser::ParseCanonicalCatalog(
							std::span<const std::byte>{
								file.bytes.data(), file.bytes.size() })));
				ObjectScrubGuard parseGuard(*parse);
				if (!parse->Succeeded()) {
					(void)builder->RejectParsedFile(
						leaf, parse->issue, parse->errorOffset);
					SecureScrubObject(*parse);
					SecureScrubBytes(file.bytes);
					SecureScrubObject(file.contentDigest);
					return CoordinatorFailure(
						*builder, output, LoaderIssue::kCatalogParseRejected, 0,
						sourceFileRequests, true);
				}

				const auto acceptedByteCount =
					static_cast<std::uint64_t>(file.bytes.size());
				const auto accepted = builder->AddFile(
					leaf, acceptedByteCount,
					file.contentDigest, parse->catalog);
				SecureScrubObject(*parse);
				SecureScrubBytes(file.bytes);
				SecureScrubObject(file.contentDigest);
				if (!accepted) {
					return CoordinatorFailure(
						*builder, output, LoaderIssue::kCohortRejected, 0,
						sourceFileRequests, true);
				}
				aggregateBytes += acceptedByteCount;
			}

			if (!builder->Close()) {
				return CoordinatorFailure(
					*builder, output, LoaderIssue::kCohortRejected, 0,
					sourceFileRequests, true);
			}

			output.cohort = builder->Result();
			output.sourceFileRequests = sourceFileRequests;
			CopyCohortDiagnostics(output);
			DiscardBuilder(*builder);
			if (!output.cohort.Accepted()) {
				output.issue = LoaderIssue::kCohortRejected;
				ScrubCohortValues(output.cohort);
				return;
			}

			if (digestFunction == nullptr) {
				output.issue = LoaderIssue::kOutputDigestFault;
				ScrubCohortValues(output.cohort);
				return;
			}

			auto inputPreimage = BuildInputCohortDigestInput(
				output.cohort.FileSummaries());
			VectorScrubGuard inputGuard(inputPreimage.material);
			auto semanticPreimage = BuildMergedSemanticDigestInput(
				output.cohort.catalog);
			VectorScrubGuard semanticGuard(semanticPreimage.material);
			if (inputPreimage.material.empty() || semanticPreimage.material.empty()) {
				output.issue = LoaderIssue::kOutputDigestFault;
				ScrubCohortValues(output.cohort);
				return;
			}

			std::uint32_t digestError = 0;
			if (!digestFunction(
					digestContext, inputPreimage.material,
					output.inputCohortDigest, digestError) ||
				!digestFunction(
					digestContext, semanticPreimage.material,
					output.mergedSemanticDigest, digestError)) {
				output.issue = LoaderIssue::kOutputDigestFault;
				output.nativeError = digestError;
				output.inputCohortDigest = {};
				output.mergedSemanticDigest = {};
				ScrubCohortValues(output.cohort);
				return;
			}

			output.issue = LoaderIssue::kNone;
			output.nativeError = 0;
			output.outputDigestsValid = true;
			return;
		} catch (...) {
			if (builder) {
				DiscardBuilder(*builder);
			}
			ResetAsUnexpectedFailure(output, sourceFileRequests);
		}
	}

	LoadResult LoadFromSource(
		CatalogSource& source,
		const DigestFunction digestFunction,
		void* const digestContext) noexcept
	{
		LoadResult output{};
		LoadFromSourceInto(source, digestFunction, digestContext, output);
		return output;
	}

	void LoadFromDataDirectoryInto(
		const std::wstring_view executableDerivedDataDirectory,
		LoadResult& output) noexcept
	{
		ResetLoadResult(output);
		if (executableDerivedDataDirectory.empty() ||
			executableDerivedDataDirectory.find(L'\0') != std::wstring_view::npos) {
			output.issue = LoaderIssue::kInvalidDataDirectory;
			return;
		}
		try {
			std::wstring root(executableDerivedDataDirectory);
			if (root.back() != L'\\' && root.back() != L'/')
				root.push_back(L'\\');
			if (root.size() > std::numeric_limits<std::wstring::size_type>::max() -
				kCatalogDirectoryRelativePath.size() ||
				root.size() + kCatalogDirectoryRelativePath.size() >= 32767U) {
				output.issue = LoaderIssue::kInvalidDataDirectory;
				return;
			}
			root.append(kCatalogDirectoryRelativePath);
			Win32CatalogSource source(std::move(root));
			LoadFromSourceInto(source, CngSHA256, nullptr, output);
		} catch (...) {
			ResetAsUnexpectedFailure(output, 0);
		}
	}

	LoadResult LoadFromDataDirectory(
		const std::wstring_view executableDerivedDataDirectory) noexcept
	{
		LoadResult output{};
		LoadFromDataDirectoryInto(executableDerivedDataDirectory, output);
		return output;
	}
}
