#pragma once

#include "WindowsGUI.h"

//Notification-area (system tray) icon with quick controls.
//Left-click shows the window; right-click opens a menu with battery and NC / Ambient / Off.
namespace Tray
{
	constexpr UINT WM_TRAYICON = WM_APP + 1;

	//An amber headphones icon, drawn at runtime. Also used for the window.
	HICON createAppIcon(int size);

	void create(HWND hwnd, HICON icon);
	void destroy();

	//Refresh the tooltip from the current status (cheap to call often; only changes are sent)
	void update(const QuickStatus& status);

	//Hide the window, leaving the app running in the tray
	void hideToTray(HWND hwnd);
	void showWindow(HWND hwnd);

	//Handles tray and Explorer-restart messages. Returns true if msg was handled.
	bool handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, CrossPlatformGUI* gui);
}
