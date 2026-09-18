#pragma once

#include <cstdint>

namespace PrivateRefractionGroupCleanupPolicy
{
	enum class Channel : std::uint8_t
	{
		kNone,
		kMirror,




	};

	struct Inputs
	{
		Channel channel{ Channel::kNone };
		bool exactStyleExecutor{ false };
		bool dedicatedExactMainCleanupCompleted{ false };
	};

	/** Preserve the mirror replay cleanup; legacy wrappers drain their own group. */
	[[nodiscard]] constexpr bool ShouldRunGenericPostWrapperClear(
		const Inputs& inputs) noexcept
	{
		return !(inputs.channel == Channel::kMirror &&
			inputs.exactStyleExecutor &&
			inputs.dedicatedExactMainCleanupCompleted);
	}
}
