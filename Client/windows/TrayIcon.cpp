#include "TrayIcon.h"

#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Tray
{
	namespace
	{
		enum MenuId : UINT
		{
			ID_STATUS = 1,
			ID_NOISE_CANCELLING,
			ID_AMBIENT,
			ID_OFF,
			ID_SHOW,
			ID_EXIT,
		};

		NOTIFYICONDATAW g_nid = {};
		bool g_created = false;
		bool g_shownHint = false;
		std::wstring g_lastTip;
		UINT g_taskbarCreatedMsg = 0;

		std::wstring toWide(const std::string& utf8)
		{
			if (utf8.empty())
			{
				return {};
			}
			int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
			std::wstring ret(len, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), ret.data(), len);
			return ret;
		}

		const wchar_t* modeName(NC_ASM_MODE_V2 mode)
		{
			switch (mode)
			{
			case NC_ASM_MODE_V2::NOISE_CANCELLING: return L"Noise Cancelling";
			case NC_ASM_MODE_V2::AMBIENT_SOUND: return L"Ambient Sound";
			default: return L"Off";
			}
		}

		std::wstring tooltipFor(const QuickStatus& status)
		{
			if (!status.connected)
			{
				return L"Sony Headphones - not connected";
			}
			std::wstring tip = toWide(status.name);
			if (status.batteryPercent >= 0)
			{
				tip += L"\n" + std::to_wstring(status.batteryPercent) + L"% battery";
				if (status.charging)
				{
					tip += L", charging";
				}
			}
			if (status.modeKnown)
			{
				tip += L"\n";
				tip += modeName(status.mode);
			}
			return tip;
		}

		void addIcon()
		{
			Shell_NotifyIconW(NIM_ADD, &g_nid);
			g_nid.uVersion = NOTIFYICON_VERSION_4;
			Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
		}

		void showMenu(HWND hwnd, CrossPlatformGUI* gui)
		{
			QuickStatus status = gui ? gui->getQuickStatus() : QuickStatus{};
			HMENU menu = CreatePopupMenu();

			std::wstring header;
			if (!status.connected)
			{
				header = L"Not connected";
			}
			else
			{
				header = toWide(status.name);
				if (status.batteryPercent >= 0)
				{
					header += L"  -  " + std::to_wstring(status.batteryPercent) + L"%";
					if (status.charging)
					{
						header += L" (charging)";
					}
				}
			}
			AppendMenuW(menu, MF_STRING | MF_GRAYED, ID_STATUS, header.c_str());
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

			UINT modeFlags = MF_STRING | (status.modeKnown ? 0 : MF_GRAYED);
			AppendMenuW(menu, modeFlags, ID_NOISE_CANCELLING, L"Noise Cancelling");
			AppendMenuW(menu, modeFlags, ID_AMBIENT, L"Ambient Sound");
			AppendMenuW(menu, modeFlags, ID_OFF, L"Off");
			if (status.modeKnown)
			{
				UINT current = status.mode == NC_ASM_MODE_V2::NOISE_CANCELLING ? ID_NOISE_CANCELLING
					: (status.mode == NC_ASM_MODE_V2::AMBIENT_SOUND ? ID_AMBIENT : ID_OFF);
				CheckMenuRadioItem(menu, ID_NOISE_CANCELLING, ID_OFF, current, MF_BYCOMMAND);
			}

			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(menu, MF_STRING, ID_SHOW, L"Open window");
			AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");
			SetMenuDefaultItem(menu, ID_SHOW, FALSE);

			//Required so the menu closes when clicking elsewhere
			SetForegroundWindow(hwnd);
			POINT pt;
			GetCursorPos(&pt);
			UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
			PostMessage(hwnd, WM_NULL, 0, 0);
			DestroyMenu(menu);

			switch (cmd)
			{
			case ID_NOISE_CANCELLING: if (gui) gui->setNcAsmMode(NC_ASM_MODE_V2::NOISE_CANCELLING); break;
			case ID_AMBIENT: if (gui) gui->setNcAsmMode(NC_ASM_MODE_V2::AMBIENT_SOUND); break;
			case ID_OFF: if (gui) gui->setNcAsmMode(NC_ASM_MODE_V2::OFF); break;
			case ID_SHOW: showWindow(hwnd); break;
			case ID_EXIT: DestroyWindow(hwnd); break;
			}
		}
	}

	HICON createAppIcon(int size)
	{
		//Draw an amber headphones glyph into a 32-bit ARGB bitmap, 4x4 supersampled for smooth edges
		BITMAPV5HEADER bi = {};
		bi.bV5Size = sizeof(bi);
		bi.bV5Width = size;
		bi.bV5Height = -size; //top-down
		bi.bV5Planes = 1;
		bi.bV5BitCount = 32;
		bi.bV5Compression = BI_BITFIELDS;
		bi.bV5RedMask = 0x00FF0000;
		bi.bV5GreenMask = 0x0000FF00;
		bi.bV5BlueMask = 0x000000FF;
		bi.bV5AlphaMask = 0xFF000000;

		void* bits = nullptr;
		HDC dc = GetDC(nullptr);
		HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
		ReleaseDC(nullptr, dc);
		if (!color)
		{
			return LoadIcon(nullptr, IDI_APPLICATION);
		}

		//Shapes in a 32x32 design space
		auto inShape = [](float x, float y) {
			//Headband: the top part of a ring
			float dx = x - 16.0f, dy = y - 17.0f;
			float r = std::sqrt(dx * dx + dy * dy);
			if (y <= 18.0f && r >= 10.0f && r <= 13.5f)
			{
				return true;
			}
			//Ear cups: rounded rectangles
			auto cup = [&](float left) {
				const float right = left + 7.5f, top = 15.0f, bottom = 28.5f, radius = 3.0f;
				if (x < left || x > right || y < top || y > bottom) return false;
				float cx = std::clamp(x, left + radius, right - radius);
				float cy = std::clamp(y, top + radius, bottom - radius);
				float ddx = x - cx, ddy = y - cy;
				return ddx * ddx + ddy * ddy <= radius * radius;
			};
			return cup(2.5f) || cup(22.0f);
		};

		const uint8_t red = 242, green = 163, blue = 53;
		auto* pixels = static_cast<uint32_t*>(bits);
		const float scale = 32.0f / size;
		for (int py = 0; py < size; py++)
		{
			for (int px = 0; px < size; px++)
			{
				int hits = 0;
				for (int sy = 0; sy < 4; sy++)
				{
					for (int sx = 0; sx < 4; sx++)
					{
						hits += inShape((px + (sx + 0.5f) / 4) * scale, (py + (sy + 0.5f) / 4) * scale);
					}
				}
				uint32_t alpha = hits * 255 / 16;
				pixels[py * size + px] = (alpha << 24) | (red << 16) | (green << 8) | blue;
			}
		}

		HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
		ICONINFO info = {};
		info.fIcon = TRUE;
		info.hbmColor = color;
		info.hbmMask = mask;
		HICON icon = CreateIconIndirect(&info);
		DeleteObject(color);
		DeleteObject(mask);
		return icon ? icon : LoadIcon(nullptr, IDI_APPLICATION);
	}

	void create(HWND hwnd, HICON icon)
	{
		g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

		g_nid = {};
		g_nid.cbSize = sizeof(g_nid);
		g_nid.hWnd = hwnd;
		g_nid.uID = 1;
		g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
		g_nid.uCallbackMessage = WM_TRAYICON;
		g_nid.hIcon = icon;
		wcscpy_s(g_nid.szTip, L"Sony Headphones");
		addIcon();
		g_created = true;
	}

	void destroy()
	{
		if (g_created)
		{
			Shell_NotifyIconW(NIM_DELETE, &g_nid);
			g_created = false;
		}
	}

	void update(const QuickStatus& status)
	{
		if (!g_created)
		{
			return;
		}
		auto tip = tooltipFor(status);
		if (tip == g_lastTip)
		{
			return;
		}
		g_lastTip = tip;
		wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
		g_nid.uFlags = NIF_TIP | NIF_SHOWTIP;
		Shell_NotifyIconW(NIM_MODIFY, &g_nid);
	}

	void hideToTray(HWND hwnd)
	{
		ShowWindow(hwnd, SW_HIDE);
		//Say where the app went, the first time only
		if (g_created && !g_shownHint)
		{
			g_shownHint = true;
			NOTIFYICONDATAW balloon = g_nid;
			balloon.uFlags = NIF_INFO;
			wcscpy_s(balloon.szInfoTitle, L"Still running");
			wcscpy_s(balloon.szInfo, L"Sony Headphones is in the tray. Right-click the icon for quick controls, or choose Exit to quit.");
			balloon.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
			Shell_NotifyIconW(NIM_MODIFY, &balloon);
		}
	}

	void showWindow(HWND hwnd)
	{
		ShowWindow(hwnd, SW_SHOW);
		ShowWindow(hwnd, SW_RESTORE);
		SetForegroundWindow(hwnd);
	}

	bool handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, CrossPlatformGUI* gui)
	{
		if (msg == WM_TRAYICON)
		{
			//With NOTIFYICON_VERSION_4, the event is in the low word of lParam
			switch (LOWORD(lParam))
			{
			case WM_LBUTTONUP:
			case NIN_SELECT:
			case NIN_KEYSELECT:
				showWindow(hwnd);
				break;
			case WM_CONTEXTMENU:
				//Sent after WM_RBUTTONUP on a right-click (and for Shift+F10); handle only this one
				showMenu(hwnd, gui);
				break;
			}
			return true;
		}
		//Explorer restarted: the icon is gone, add it again
		if (g_taskbarCreatedMsg != 0 && msg == g_taskbarCreatedMsg && g_created)
		{
			addIcon();
			g_lastTip.clear();
			return true;
		}
		return false;
	}
}
