#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace stl
{
	/**
	 * Construct a process-lifetime object without registering a CRT destructor.
	 *
	 * SKSE plugins are unloaded during ExitProcess after engine and Address Library
	 * backing state may already be gone. Namespace-static engine/renderer wrappers
	 * must therefore not release through those subsystems from the DLL's atexit
	 * table. Runtime code still performs every safety-critical release explicitly;
	 * the OS reclaims this inert storage at process exit.
	 */
	template <class T>
	class no_destructor
	{
	public:
		template <class... Args>
		explicit no_destructor(Args&&... args) noexcept(
			std::is_nothrow_constructible_v<T, Args...>)
		{
			::new (static_cast<void*>(storage)) T(std::forward<Args>(args)...);
		}

		no_destructor(const no_destructor&) = delete;
		no_destructor& operator=(const no_destructor&) = delete;

		[[nodiscard]] T& get() noexcept
		{
			return *std::launder(reinterpret_cast<T*>(storage));
		}

		[[nodiscard]] const T& get() const noexcept
		{
			return *std::launder(reinterpret_cast<const T*>(storage));
		}

		[[nodiscard]] T* operator->() noexcept { return std::addressof(get()); }
		[[nodiscard]] const T* operator->() const noexcept { return std::addressof(get()); }

	private:
		alignas(T) std::byte storage[sizeof(T)];
	};

	static_assert(std::is_trivially_destructible_v<no_destructor<int>>);
}
