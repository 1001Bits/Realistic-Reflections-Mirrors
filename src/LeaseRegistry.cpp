#if defined(RR_LEASE_REGISTRY_STANDALONE)
#include "NoDestructor.h"
#else
#include "PCH.h"
#endif

#include "LeaseRegistry.h"

#include <mutex>
#include <vector>

namespace LeaseRegistry
{
	namespace
	{
		constexpr std::size_t kFamilyCount =
			static_cast<std::size_t>(Family::kCount);

		struct State
		{
			std::mutex lock{};
			std::vector<Registration> armed{};
			std::array<std::string_view, kFamilyCount> unprovenHolder{};
			std::uint32_t arms{ 0 };
			std::uint32_t refusals{ 0 };
			std::uint32_t familyConflicts{ 0 };
		};

		// Process-lifetime storage: this is consulted from marker inspection at
		// InputLoaded/DataLoaded and must never run a destructor that could
		// resolve through an unmapped Address Library at exit (see
		// docs/re/Shutdown_Static_Destruction_Crash.md).
		[[nodiscard]] State& GetState() noexcept
		{
			static stl::no_destructor<State> state{};
			return state.get();
		}
	}

	bool TryArm(const Registration& a_registration) noexcept
	{
		if (a_registration.name.empty() ||
			a_registration.family >= Family::kCount ||
			a_registration.maturity > Maturity::kUnproven) {
			return false;
		}

		try {
			auto& state = GetState();
			std::scoped_lock guard{ state.lock };
			for (const auto& armed : state.armed) {
				if (armed.name == a_registration.name)
					return armed.family == a_registration.family &&
						armed.maturity == a_registration.maturity;
			}

			const auto index = static_cast<std::size_t>(a_registration.family);
			if (a_registration.maturity == Maturity::kUnproven &&
				!state.unprovenHolder[index].empty()) {
				++state.refusals;
				++state.familyConflicts;
				try {
#if !defined(RR_LEASE_REGISTRY_STANDALONE)
					logger::critical(
						"[RR][Lease] REFUSED '{}': family {} already holds unproven "
						"lease '{}' this session. Exactly one unproven engine-state "
						"lease may be armed per run, so a failure stays attributable. "
						"Disarm the other marker and relaunch to test this one.",
						a_registration.name,
						FamilyName(a_registration.family),
						state.unprovenHolder[index]);
#endif
				} catch (...) {
				}
				return false;
			}

			state.armed.push_back(a_registration);
			if (a_registration.maturity == Maturity::kUnproven)
				state.unprovenHolder[index] = a_registration.name;
			++state.arms;
			try {
#if !defined(RR_LEASE_REGISTRY_STANDALONE)
				logger::info(
					"[RR][Lease] armed '{}' family={} maturity={}",
					a_registration.name, FamilyName(a_registration.family),
					a_registration.maturity == Maturity::kRuntimeProven ?
						"runtime-proven" : "unproven");
#endif
			} catch (...) {
			}
			return true;
		} catch (...) {
			// No recorded owner means no authority to mutate engine state. The
			// vector insertion precedes every commit, preserving existing leases.
			return false;
		}
	}

	bool IsArmed(std::string_view a_name) noexcept
	{
		try {
			auto& state = GetState();
			std::scoped_lock guard{ state.lock };
			for (const auto& armed : state.armed) {
				if (armed.name == a_name)
					return true;
			}
		} catch (...) {
		}
		return false;
	}

	Diagnostics GetDiagnostics() noexcept
	{
		Diagnostics out{};
		try {
			auto& state = GetState();
			std::scoped_lock guard{ state.lock };
			out.arms = state.arms;
			out.refusals = state.refusals;
			out.familyConflicts = state.familyConflicts;
			out.unprovenHolder = state.unprovenHolder;
		} catch (...) {
		}
		return out;
	}

	void Reset() noexcept
	{
		try {
			auto& state = GetState();
			std::scoped_lock guard{ state.lock };
			state.armed.clear();
			state.unprovenHolder = {};
			state.arms = 0;
			state.refusals = 0;
			state.familyConflicts = 0;
		} catch (...) {
		}
	}
}
