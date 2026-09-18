#pragma once

#include <functional>
#include <utility>

namespace ProductActivationIntegration
{
	template <class PrepareActivation, class InstallModuleHooks>
	[[nodiscard]] bool RunInputLoaded(
		PrepareActivation&& prepareActivation,
		InstallModuleHooks&& installModuleHooks)
	{
		if (!static_cast<bool>(std::invoke(
				std::forward<PrepareActivation>(prepareActivation)))) {
			return false;
		}
		std::invoke(std::forward<InstallModuleHooks>(installModuleHooks));
		return true;
	}

	template <
		class PrepareModules,
		class CommitActivation,
		class PromoteModules,
		class PublishAPIMask,
		class InitializeRuntimeProbe>
	void RunDataLoaded(
		PrepareModules&& prepareModules,
		CommitActivation&& commitActivation,
		PromoteModules&& promoteModules,
		PublishAPIMask&& publishAPIMask,
		InitializeRuntimeProbe&& initializeRuntimeProbe)
	{
		std::invoke(std::forward<PrepareModules>(prepareModules));
		(void) std::invoke(std::forward<CommitActivation>(commitActivation));
		std::invoke(std::forward<PromoteModules>(promoteModules));
		std::invoke(std::forward<PublishAPIMask>(publishAPIMask));
		std::invoke(std::forward<InitializeRuntimeProbe>(initializeRuntimeProbe));
	}
}
