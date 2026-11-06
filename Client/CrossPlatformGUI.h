#pragma once

#include "imgui/imgui.h"
#include "Constants.h"
#include "IBluetoothConnector.h"
#include "BluetoothWrapper.h"
#include "CommandSerializer.h"
#include "Exceptions.h"
#include "TimedMessageQueue.h"
#include "SingleInstanceFuture.h"
#include "CascadiaCodeFont.h"
#include "Headphones.h"

#include <future>

constexpr auto GUI_MAX_MESSAGES = 5;
constexpr auto GUI_HEIGHT = 820;
constexpr auto GUI_WIDTH = 480;
constexpr auto FPS = 60;
constexpr auto MS_PER_FRAME = 1000 / FPS;
constexpr auto FONT_SIZE = 17.0f;
constexpr auto FONT_SIZE_TITLE = 26.0f;
constexpr auto FONT_SIZE_SMALL = 14.0f;
//Background behind the ImGui window; matches the theme's window colour
const auto WINDOW_COLOR = ImVec4(0.075f, 0.082f, 0.098f, 1.00f);

//What a tray icon / menu needs to show
struct QuickStatus
{
	bool connected = false;
	std::string name;
	int batteryPercent = -1;     //-1 = unknown
	bool charging = false;
	//False until the headphones have reported their mode (or when using the XM3 format)
	bool modeKnown = false;
	NC_ASM_MODE_V2 mode = NC_ASM_MODE_V2::NOISE_CANCELLING;
};

//This class should be constructed after AFTER the Dear ImGUI context is initialized.
class CrossPlatformGUI
{
public:
	CrossPlatformGUI(BluetoothWrapper bt);

	//Run the GUI code once. This function should be called from a loop from one of the GUI impls (Windows, OSX, Linux...)
	//O: true if the user wants to close the window
	bool performGUIPass();

	//For quick controls outside the window (e.g. a tray menu). Call from the GUI thread.
	QuickStatus getQuickStatus();
	//Switches noise cancelling / ambient / off, exactly as the window's buttons do
	void setNcAsmMode(NC_ASM_MODE_V2 mode);
private:
	void _applyTheme();
	void _loadFonts();

	void _pollConnection();
	void _pollDiscovery();
	void _startConnect(const BluetoothDevice& device);

	void _drawErrors();
	void _drawConnectScreen();
	void _drawHeader();
	void _drawNoiseControlV2();
	void _drawEqualizer();
	void _drawLegacyControls();
	void _drawAdvanced();
	void _drawFooter();
	void _setHeadphoneSettings();

	BluetoothDevice _connectedDevice;
	BluetoothWrapper _bt;
	SingleInstanceFuture<std::vector<BluetoothDevice>> _connectedDevicesFuture;
	SingleInstanceFuture<void> _sendCommandFuture;
	SingleInstanceFuture<void> _connectFuture;
	TimedMessageQueue _mq;
	Headphones _headphones;

	std::vector<BluetoothDevice> _devices;
	int _selectedDevice = -1;
	//Auto-connect only once per run, so Disconnect stays disconnected
	bool _autoConnectTried = false;
	bool _useV2Protocol = true;
	bool _sending = false;

	//Noise cancelling / ambient controls (shared by the window and quick controls)
	int _ncMode = static_cast<int>(NC_ASM_MODE_V2::NOISE_CANCELLING);
	int _asmLevel = 1;
	bool _focusOnVoice = false;

	ImFont* _fontBody = nullptr;
	ImFont* _fontTitle = nullptr;
	ImFont* _fontSmall = nullptr;
};
