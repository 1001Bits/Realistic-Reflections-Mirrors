#pragma once
#include <atomic>

namespace MirrorAcquisition
{
	inline std::atomic_bool merchantsEnabled{ true };
	inline std::atomic_bool craftingEnabled{ true };
	inline std::atomic_bool homesEnabled{ true };
	inline std::atomic_bool innsEnabled{ true };
	void OnDataLoaded();
	void OnGameLoaded();
	void OnPreLoadGame();
	void QueueApply();
	void QueueFurnishings();
}
