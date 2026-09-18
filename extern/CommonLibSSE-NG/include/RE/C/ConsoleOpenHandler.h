#pragma once

#include "RE/M/MenuEventHandler.h"

namespace RE
{
	struct ConsoleOpenHandler : public MenuEventHandler
	{
	public:
		inline static constexpr auto RTTI = RTTI_ConsoleOpenHandler;
		inline static constexpr auto VTABLE = VTABLE_ConsoleOpenHandler;

		~ConsoleOpenHandler() override;  // 00

		// override (MenuEventHandler)
		bool CanProcess(InputEvent* a_event) override;      // 01
		bool ProcessButton(ButtonEvent* a_event) override;  // 05
	};
	static_assert(sizeof(ConsoleOpenHandler) == 0x10);
}
