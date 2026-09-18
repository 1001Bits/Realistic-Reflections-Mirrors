#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace MirrorAuthorCatalogParser
{
	// This engine-independent byte parser is now consumed by the default-off
	// manifest diagnostic.  It performs no discovery, filesystem I/O, TES lookup,
	// authority grant, recognition, or delivery itself.
	inline constexpr bool kRuntimeIntegrationWired = false;
	inline constexpr bool kRuntimeTESAuthorityWired = false;
	inline constexpr bool kManifestRuntimeLoaderWired = true;
	inline constexpr bool kRecognitionWired = false;
	inline constexpr bool kDeliveryWired = false;
	inline constexpr bool kSupportedRuntimeFeature = false;

	inline constexpr std::size_t kMaximumCatalogBytes = 128U * 1024U;
	inline constexpr std::size_t kMaximumCatalogEntries = 256;
	inline constexpr std::size_t kPluginStorageBytes = 256;
	inline constexpr std::uint32_t kMaximumPluginFilenameBytes = 255;
	inline constexpr std::uint32_t kMaximumFullPluginLocalFormID = 0x00FFFFFFU;
	inline constexpr std::uint32_t kMinimumLightPluginOwnedLocalFormID = 0x800U;
	inline constexpr std::uint32_t kMaximumLightPluginLocalFormID = 0xFFFU;
	inline constexpr std::uint32_t kRectangularSchema = 2;
	inline constexpr std::uint32_t kCatalogVersion = 1;
	inline constexpr std::string_view kCatalogFormat =
		"RealisticReflections.MirrorAuthorCatalog";

	struct CanonicalDeclaration
	{
		std::array<char, kPluginStorageBytes> sourcePlugin{};
		std::uint32_t sourcePluginLength{ 0 };
		std::uint32_t localFormID{ 0 };
		std::uint32_t mirrorSchema{ 0 };
		std::uint32_t flags{ 0 };

		[[nodiscard]] constexpr std::string_view PluginName() const noexcept
		{
			return sourcePluginLength <= kMaximumPluginFilenameBytes ?
				std::string_view{ sourcePlugin.data(), sourcePluginLength } :
				std::string_view{};
		}
	};

	struct CanonicalCatalog
	{
		std::uint32_t entryCount{ 0 };
		std::array<CanonicalDeclaration, kMaximumCatalogEntries> registrations{};

		[[nodiscard]] constexpr std::span<const CanonicalDeclaration>
		Entries() const noexcept
		{
			return entryCount <= registrations.size() ?
				std::span<const CanonicalDeclaration>{
					registrations.data(), entryCount } :
				std::span<const CanonicalDeclaration>{};
		}
	};

	enum class ParseIssue : std::uint8_t
	{
		kNone,
		kEmptyInput,
		kCatalogTooLarge,
		kEncodingNotCanonicalASCII,
		kTrailingLineFeedInvalid,
		kStructureMismatch,
		kIntegerExpected,
		kIntegerOutOfRange,
		kEntryCapacityExceeded,
		kEntryCountMismatch,
		kPluginBasenameInvalid,
		kFirstPartyNamespaceReserved,
		kUnsupportedSchema,
		kUnsupportedFlags,
		kRegistrationsNotStrictlySortedUnique,
		kRuntimeIntegrationFlagNotFalse,
		kUnsupportedVersion,
		kCanonicalRoundTripMismatch
	};

	struct ParseResult
	{
		ParseIssue issue{ ParseIssue::kEmptyInput };
		std::size_t errorOffset{ 0 };
		CanonicalCatalog catalog{};

		[[nodiscard]] constexpr bool Succeeded() const noexcept
		{
			return issue == ParseIssue::kNone;
		}
	};

	static_assert(std::is_trivially_copyable_v<CanonicalDeclaration>);
	static_assert(std::is_trivially_copyable_v<CanonicalCatalog>);
	static_assert(std::is_trivially_copyable_v<ParseResult>);
	static_assert(sizeof(CanonicalDeclaration) == 272);

	[[nodiscard]] constexpr int CompareIdentity(
		const CanonicalDeclaration& left,
		const CanonicalDeclaration& right) noexcept
	{
		const auto leftName = left.PluginName();
		const auto rightName = right.PluginName();
		const auto commonLength =
			leftName.size() < rightName.size() ? leftName.size() : rightName.size();
		for (std::size_t index = 0; index < commonLength; ++index) {
			const auto leftByte = static_cast<unsigned char>(leftName[index]);
			const auto rightByte = static_cast<unsigned char>(rightName[index]);
			if (leftByte < rightByte)
				return -1;
			if (leftByte > rightByte)
				return 1;
		}
		if (leftName.size() < rightName.size())
			return -1;
		if (leftName.size() > rightName.size())
			return 1;
		if (left.localFormID < right.localFormID)
			return -1;
		if (left.localFormID > right.localFormID)
			return 1;
		return 0;
	}

	[[nodiscard]] constexpr bool SameIdentity(
		const CanonicalDeclaration& left,
		const CanonicalDeclaration& right) noexcept
	{
		return CompareIdentity(left, right) == 0;
	}

	[[nodiscard]] constexpr bool SameDeclaration(
		const CanonicalDeclaration& left,
		const CanonicalDeclaration& right) noexcept
	{
		return SameIdentity(left, right) &&
		       left.mirrorSchema == right.mirrorSchema &&
		       left.flags == right.flags;
	}

	namespace detail
	{
		[[nodiscard]] constexpr unsigned char ByteAt(
			const std::span<const std::byte> bytes,
			const std::size_t index) noexcept
		{
			return std::to_integer<unsigned char>(bytes[index]);
		}

		class Cursor
		{
		public:
			explicit constexpr Cursor(
				const std::span<const std::byte> bytes) noexcept :
				bytes_(bytes)
			{}

			[[nodiscard]] constexpr bool Consume(
				const char expected,
				const ParseIssue issue = ParseIssue::kStructureMismatch) noexcept
			{
				if (index_ >= bytes_.size() ||
					ByteAt(bytes_, index_) != static_cast<unsigned char>(expected)) {
					return Fail(issue);
				}
				++index_;
				return true;
			}

			[[nodiscard]] constexpr bool Consume(
				const std::string_view expected,
				const ParseIssue issue = ParseIssue::kStructureMismatch) noexcept
			{
				for (const char value : expected) {
					if (!Consume(value, issue))
						return false;
				}
				return true;
			}

			[[nodiscard]] constexpr bool ParseUnsigned(
				std::uint32_t& output) noexcept
			{
				if (index_ >= bytes_.size() ||
					ByteAt(bytes_, index_) < static_cast<unsigned char>('0') ||
					ByteAt(bytes_, index_) > static_cast<unsigned char>('9')) {
					return Fail(ParseIssue::kIntegerExpected);
				}

				std::uint32_t value = 0;
				do {
					const auto digit = static_cast<std::uint32_t>(
						ByteAt(bytes_, index_) - static_cast<unsigned char>('0'));
					if (value >
						(std::numeric_limits<std::uint32_t>::max() - digit) / 10U) {
						return Fail(ParseIssue::kIntegerOutOfRange);
					}
					value = value * 10U + digit;
					++index_;
				} while (index_ < bytes_.size() &&
					ByteAt(bytes_, index_) >= static_cast<unsigned char>('0') &&
					ByteAt(bytes_, index_) <= static_cast<unsigned char>('9'));
				output = value;
				return true;
			}

			[[nodiscard]] constexpr bool AtEnd() const noexcept
			{
				return index_ == bytes_.size();
			}

			[[nodiscard]] constexpr bool NextIs(const char value) const noexcept
			{
				return index_ < bytes_.size() &&
				       ByteAt(bytes_, index_) == static_cast<unsigned char>(value);
			}

			[[nodiscard]] constexpr std::size_t Offset() const noexcept
			{
				return index_;
			}

			[[nodiscard]] constexpr ParseIssue Issue() const noexcept
			{
				return issue_;
			}

			[[nodiscard]] constexpr std::size_t ErrorOffset() const noexcept
			{
				return errorOffset_;
			}

			[[nodiscard]] constexpr unsigned char CurrentByte() const noexcept
			{
				return index_ < bytes_.size() ? ByteAt(bytes_, index_) : 0;
			}

			constexpr void Advance() noexcept
			{
				if (index_ < bytes_.size())
					++index_;
			}

			[[nodiscard]] constexpr bool Fail(const ParseIssue issue) noexcept
			{
				if (issue_ == ParseIssue::kNone) {
					issue_ = issue;
					errorOffset_ = index_;
				}
				return false;
			}

		private:
			std::span<const std::byte> bytes_{};
			std::size_t index_{ 0 };
			ParseIssue issue_{ ParseIssue::kNone };
			std::size_t errorOffset_{ 0 };
		};

		[[nodiscard]] constexpr bool EndsWith(
			const std::string_view value,
			const std::string_view suffix) noexcept
		{
			if (value.size() < suffix.size())
				return false;
			return value.substr(value.size() - suffix.size()) == suffix;
		}

		[[nodiscard]] constexpr bool ParsePluginBasename(
			Cursor& cursor,
			CanonicalDeclaration& declaration) noexcept
		{
			std::size_t length = 0;
			while (!cursor.NextIs('"')) {
				if (cursor.CurrentByte() == 0)
					return cursor.Fail(ParseIssue::kPluginBasenameInvalid);
				if (length >= kMaximumPluginFilenameBytes)
					return cursor.Fail(ParseIssue::kPluginBasenameInvalid);
				const auto value = cursor.CurrentByte();
				if (value < 0x20U || value > 0x7EU ||
					(value >= static_cast<unsigned char>('A') &&
						value <= static_cast<unsigned char>('Z')) ||
					value == static_cast<unsigned char>('<') ||
					value == static_cast<unsigned char>('>') ||
					value == static_cast<unsigned char>(':') ||
					value == static_cast<unsigned char>('"') ||
					value == static_cast<unsigned char>('/') ||
					value == static_cast<unsigned char>('\\') ||
					value == static_cast<unsigned char>('|') ||
					value == static_cast<unsigned char>('?') ||
					value == static_cast<unsigned char>('*')) {
					return cursor.Fail(ParseIssue::kPluginBasenameInvalid);
				}
				declaration.sourcePlugin[length++] = static_cast<char>(value);
				cursor.Advance();
			}

			if (length == 0 || declaration.sourcePlugin[0] == ' ' ||
				declaration.sourcePlugin[0] == '.' ||
				declaration.sourcePlugin[length - 1] == ' ' ||
				declaration.sourcePlugin[length - 1] == '.') {
				return cursor.Fail(ParseIssue::kPluginBasenameInvalid);
			}
			declaration.sourcePluginLength = static_cast<std::uint32_t>(length);
			const auto name = declaration.PluginName();
			if (!EndsWith(name, ".esp") && !EndsWith(name, ".esm") &&
				!EndsWith(name, ".esl")) {
				return cursor.Fail(ParseIssue::kPluginBasenameInvalid);
			}
			if (name == "realisticreflections.esp" ||
				name == "mirrorsofskyrim.esp") {
				return cursor.Fail(ParseIssue::kFirstPartyNamespaceReserved);
			}
			return true;
		}

		[[nodiscard]] constexpr bool ParseDeclaration(
			Cursor& cursor,
			CanonicalDeclaration& declaration) noexcept
		{
			if (!cursor.Consume("{\"flags\":"))
				return false;
			if (!cursor.ParseUnsigned(declaration.flags))
				return false;
			if (declaration.flags != 0)
				return cursor.Fail(ParseIssue::kUnsupportedFlags);
			if (!cursor.Consume(",\"local_form_id\":"))
				return false;
			if (!cursor.ParseUnsigned(declaration.localFormID))
				return false;
			if (declaration.localFormID == 0 ||
				declaration.localFormID > kMaximumFullPluginLocalFormID) {
				return cursor.Fail(ParseIssue::kIntegerOutOfRange);
			}
			if (!cursor.Consume(",\"mirror_schema\":"))
				return false;
			if (!cursor.ParseUnsigned(declaration.mirrorSchema))
				return false;
			if (declaration.mirrorSchema != kRectangularSchema)
				return cursor.Fail(ParseIssue::kUnsupportedSchema);
			if (!cursor.Consume(",\"source_plugin\":\""))
				return false;
			if (!ParsePluginBasename(cursor, declaration))
				return false;
			if (!cursor.Consume('"') || !cursor.Consume('}'))
				return false;

			if (EndsWith(declaration.PluginName(), ".esl") &&
				(declaration.localFormID < kMinimumLightPluginOwnedLocalFormID ||
					declaration.localFormID > kMaximumLightPluginLocalFormID)) {
				return cursor.Fail(ParseIssue::kIntegerOutOfRange);
			}
			return true;
		}

		class CanonicalVerifier
		{
		public:
			explicit constexpr CanonicalVerifier(
				const std::span<const std::byte> expected) noexcept :
				expected_(expected)
			{}

			constexpr void Emit(const char value) noexcept
			{
				if (offset_ >= expected_.size() ||
					ByteAt(expected_, offset_) != static_cast<unsigned char>(value)) {
					if (matches_)
						mismatchOffset_ = offset_;
					matches_ = false;
				}
				++offset_;
			}

			constexpr void Emit(const std::string_view value) noexcept
			{
				for (const char character : value)
					Emit(character);
			}

			constexpr void EmitUnsigned(std::uint32_t value) noexcept
			{
				std::array<char, 10> reverse{};
				std::size_t count = 0;
				do {
					reverse[count++] =
						static_cast<char>('0' + static_cast<char>(value % 10U));
					value /= 10U;
				} while (value != 0);
				while (count != 0)
					Emit(reverse[--count]);
			}

			[[nodiscard]] constexpr bool Complete() noexcept
			{
				if (offset_ != expected_.size()) {
					if (matches_)
						mismatchOffset_ = offset_ < expected_.size() ?
							offset_ : expected_.size();
					matches_ = false;
				}
				return matches_;
			}

			[[nodiscard]] constexpr std::size_t MismatchOffset() const noexcept
			{
				return mismatchOffset_;
			}

		private:
			std::span<const std::byte> expected_{};
			std::size_t offset_{ 0 };
			std::size_t mismatchOffset_{ 0 };
			bool matches_{ true };
		};

		class CanonicalSizeCounter
		{
		public:
			constexpr void Emit(const char) noexcept
			{
				++size_;
			}

			constexpr void Emit(const std::string_view value) noexcept
			{
				size_ += value.size();
			}

			constexpr void EmitUnsigned(std::uint32_t value) noexcept
			{
				do {
					++size_;
					value /= 10U;
				} while (value != 0);
			}

			[[nodiscard]] constexpr std::size_t Size() const noexcept
			{
				return size_;
			}

		private:
			std::size_t size_{ 0 };
		};

		class CanonicalBufferWriter
		{
		public:
			explicit constexpr CanonicalBufferWriter(
				const std::span<std::byte> output) noexcept :
				output_(output)
			{}

			constexpr void Emit(const char value) noexcept
			{
				if (offset_ < output_.size()) {
					output_[offset_] = static_cast<std::byte>(
						static_cast<unsigned char>(value));
				}
				++offset_;
			}

			constexpr void Emit(const std::string_view value) noexcept
			{
				for (const char character : value)
					Emit(character);
			}

			constexpr void EmitUnsigned(std::uint32_t value) noexcept
			{
				std::array<char, 10> reverse{};
				std::size_t count = 0;
				do {
					reverse[count++] =
						static_cast<char>('0' + static_cast<char>(value % 10U));
					value /= 10U;
				} while (value != 0);
				while (count != 0)
					Emit(reverse[--count]);
			}

			[[nodiscard]] constexpr std::size_t Size() const noexcept
			{
				return offset_;
			}

		private:
			std::span<std::byte> output_{};
			std::size_t offset_{ 0 };
		};

		template <class Emitter>
		constexpr void EmitCanonicalCatalogBytes(
			Emitter& emitter,
			const CanonicalCatalog& catalog) noexcept
		{
			emitter.Emit("{\"entry_count\":");
			emitter.EmitUnsigned(catalog.entryCount);
			emitter.Emit(",\"format\":\"");
			emitter.Emit(kCatalogFormat);
			emitter.Emit("\",\"registrations\":[");
			for (std::size_t index = 0; index < catalog.entryCount; ++index) {
				if (index != 0)
					emitter.Emit(',');
				const auto& declaration = catalog.registrations[index];
				emitter.Emit("{\"flags\":");
				emitter.EmitUnsigned(declaration.flags);
				emitter.Emit(",\"local_form_id\":");
				emitter.EmitUnsigned(declaration.localFormID);
				emitter.Emit(",\"mirror_schema\":");
				emitter.EmitUnsigned(declaration.mirrorSchema);
				emitter.Emit(",\"source_plugin\":\"");
				emitter.Emit(declaration.PluginName());
				emitter.Emit("\"}");
			}
			emitter.Emit("],\"runtime_integration_wired\":false,\"version\":");
			emitter.EmitUnsigned(kCatalogVersion);
			emitter.Emit("}\n");
		}

		[[nodiscard]] constexpr bool VerifyCanonicalRoundTrip(
			const CanonicalCatalog& catalog,
			const std::span<const std::byte> bytes,
			std::size_t& mismatchOffset) noexcept
		{
			CanonicalVerifier verifier(bytes);
			EmitCanonicalCatalogBytes(verifier, catalog);
			const bool complete = verifier.Complete();
			mismatchOffset = verifier.MismatchOffset();
			return complete;
		}
	}

	[[nodiscard]] constexpr ParseIssue ValidateCanonicalCatalog(
		const CanonicalCatalog& catalog) noexcept
	{
		if (catalog.entryCount > kMaximumCatalogEntries)
			return ParseIssue::kEntryCapacityExceeded;
		for (std::size_t index = 0; index < catalog.entryCount; ++index) {
			const auto& declaration = catalog.registrations[index];
			if (declaration.sourcePluginLength == 0 ||
				declaration.sourcePluginLength > kMaximumPluginFilenameBytes) {
				return ParseIssue::kPluginBasenameInvalid;
			}
			const auto name = declaration.PluginName();
			if (name.front() == ' ' || name.front() == '.' ||
				name.back() == ' ' || name.back() == '.' ||
				declaration.sourcePlugin[declaration.sourcePluginLength] != '\0') {
				return ParseIssue::kPluginBasenameInvalid;
			}
			for (const char raw : name) {
				const auto value = static_cast<unsigned char>(raw);
				if (value < 0x20U || value > 0x7EU ||
					(raw >= 'A' && raw <= 'Z') || raw == '<' || raw == '>' ||
					raw == ':' || raw == '"' || raw == '/' || raw == '\\' ||
					raw == '|' || raw == '?' || raw == '*') {
					return ParseIssue::kPluginBasenameInvalid;
				}
			}
			if (!detail::EndsWith(name, ".esp") &&
				!detail::EndsWith(name, ".esm") &&
				!detail::EndsWith(name, ".esl")) {
				return ParseIssue::kPluginBasenameInvalid;
			}
			if (name == "realisticreflections.esp" ||
				name == "mirrorsofskyrim.esp") {
				return ParseIssue::kFirstPartyNamespaceReserved;
			}
			if (declaration.localFormID == 0 ||
				declaration.localFormID > kMaximumFullPluginLocalFormID) {
				return ParseIssue::kIntegerOutOfRange;
			}
			if (detail::EndsWith(name, ".esl") &&
				(declaration.localFormID < kMinimumLightPluginOwnedLocalFormID ||
					declaration.localFormID > kMaximumLightPluginLocalFormID)) {
				return ParseIssue::kIntegerOutOfRange;
			}
			if (declaration.mirrorSchema != kRectangularSchema)
				return ParseIssue::kUnsupportedSchema;
			if (declaration.flags != 0)
				return ParseIssue::kUnsupportedFlags;
			if (index != 0 &&
				CompareIdentity(catalog.registrations[index - 1], declaration) >= 0) {
				return ParseIssue::kRegistrationsNotStrictlySortedUnique;
			}
		}
		return ParseIssue::kNone;
	}

	[[nodiscard]] constexpr std::size_t CanonicalByteSize(
		const CanonicalCatalog& catalog) noexcept
	{
		if (ValidateCanonicalCatalog(catalog) != ParseIssue::kNone)
			return 0;
		detail::CanonicalSizeCounter counter;
		detail::EmitCanonicalCatalogBytes(counter, catalog);
		return counter.Size() <= kMaximumCatalogBytes ? counter.Size() : 0;
	}

	[[nodiscard]] constexpr bool TryEmitCanonicalCatalog(
		const CanonicalCatalog& catalog,
		const std::span<std::byte> output,
		std::size_t& bytesWritten) noexcept
	{
		bytesWritten = 0;
		const auto required = CanonicalByteSize(catalog);
		if (required == 0 || output.size() < required)
			return false;
		detail::CanonicalBufferWriter writer(output.first(required));
		detail::EmitCanonicalCatalogBytes(writer, catalog);
		bytesWritten = writer.Size();
		return bytesWritten == required;
	}

	// The caller owns one immutable complete-file snapshot for this synchronous
	// call.  No pointer or view into that snapshot survives the return; all
	// accepted declaration bytes are copied into ParseResult.
	[[nodiscard]] inline ParseResult ParseCanonicalCatalog(
		const std::span<const std::byte> ownedSnapshot) noexcept
	{
		ParseResult result{};
		auto reject = [&](const ParseIssue issue, const std::size_t offset) noexcept {
			result.catalog = {};
			result.issue = issue;
			result.errorOffset = offset;
			return result;
		};

		if (ownedSnapshot.empty())
			return reject(ParseIssue::kEmptyInput, 0);
		if (ownedSnapshot.size() > kMaximumCatalogBytes)
			return reject(ParseIssue::kCatalogTooLarge, kMaximumCatalogBytes);
		for (std::size_t index = 0; index < ownedSnapshot.size(); ++index) {
			const auto value = detail::ByteAt(ownedSnapshot, index);
			if (value > 0x7FU)
				return reject(ParseIssue::kEncodingNotCanonicalASCII, index);
			if ((value == static_cast<unsigned char>('\n') &&
					index + 1 != ownedSnapshot.size()) ||
				value == static_cast<unsigned char>('\r')) {
				return reject(ParseIssue::kTrailingLineFeedInvalid, index);
			}
		}
		if (detail::ByteAt(ownedSnapshot, ownedSnapshot.size() - 1) !=
			static_cast<unsigned char>('\n')) {
			return reject(ParseIssue::kTrailingLineFeedInvalid,
				ownedSnapshot.size());
		}

		const auto documentBytes = ownedSnapshot.first(ownedSnapshot.size() - 1);
		detail::Cursor cursor(documentBytes);
		std::uint32_t declaredEntryCount = 0;
		if (!cursor.Consume("{\"entry_count\":") ||
			!cursor.ParseUnsigned(declaredEntryCount)) {
			return reject(cursor.Issue(), cursor.ErrorOffset());
		}
		if (declaredEntryCount > kMaximumCatalogEntries) {
			return reject(ParseIssue::kEntryCapacityExceeded, cursor.Offset());
		}
		if (!cursor.Consume(",\"format\":\"") ||
			!cursor.Consume(kCatalogFormat) || !cursor.Consume('"') ||
			!cursor.Consume(",\"registrations\":[")) {
			return reject(cursor.Issue(), cursor.ErrorOffset());
		}

		while (!cursor.NextIs(']')) {
			if (result.catalog.entryCount >= kMaximumCatalogEntries) {
				return reject(ParseIssue::kEntryCapacityExceeded, cursor.Offset());
			}
			auto& declaration =
				result.catalog.registrations[result.catalog.entryCount];
			if (!detail::ParseDeclaration(cursor, declaration))
				return reject(cursor.Issue(), cursor.ErrorOffset());
			if (result.catalog.entryCount != 0 &&
				CompareIdentity(
					result.catalog.registrations[result.catalog.entryCount - 1],
					declaration) >= 0) {
				return reject(ParseIssue::kRegistrationsNotStrictlySortedUnique,
					cursor.Offset());
			}
			++result.catalog.entryCount;
			if (!cursor.NextIs(','))
				break;
			cursor.Advance();
		}

		if (!cursor.Consume(']') ||
			!cursor.Consume(",\"runtime_integration_wired\":")) {
			return reject(cursor.Issue(), cursor.ErrorOffset());
		}
		if (!cursor.Consume("false",
			ParseIssue::kRuntimeIntegrationFlagNotFalse)) {
			return reject(cursor.Issue(), cursor.ErrorOffset());
		}
		if (!cursor.Consume(",\"version\":"))
			return reject(cursor.Issue(), cursor.ErrorOffset());
		std::uint32_t version = 0;
		if (!cursor.ParseUnsigned(version))
			return reject(cursor.Issue(), cursor.ErrorOffset());
		if (version != kCatalogVersion)
			return reject(ParseIssue::kUnsupportedVersion, cursor.Offset());
		if (!cursor.Consume('}') || !cursor.AtEnd())
			return reject(ParseIssue::kStructureMismatch, cursor.Offset());
		if (declaredEntryCount != result.catalog.entryCount)
			return reject(ParseIssue::kEntryCountMismatch, cursor.Offset());
		if (const auto validationIssue = ValidateCanonicalCatalog(result.catalog);
			validationIssue != ParseIssue::kNone) {
			return reject(validationIssue, cursor.Offset());
		}

		std::size_t mismatchOffset = 0;
		if (!detail::VerifyCanonicalRoundTrip(
			result.catalog, ownedSnapshot, mismatchOffset)) {
			return reject(ParseIssue::kCanonicalRoundTripMismatch, mismatchOffset);
		}

		result.issue = ParseIssue::kNone;
		result.errorOffset = ownedSnapshot.size();
		return result;
	}
}
