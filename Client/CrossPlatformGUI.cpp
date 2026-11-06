#include "CrossPlatformGUI.h"
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <iterator>

namespace
{
	//Theme colours
	const ImVec4 COL_BG = WINDOW_COLOR;
	const ImVec4 COL_CARD = ImVec4(0.118f, 0.129f, 0.153f, 1.00f);
	const ImVec4 COL_CARD_ERROR = ImVec4(0.30f, 0.12f, 0.12f, 1.00f);
	const ImVec4 COL_CONTROL = ImVec4(0.173f, 0.188f, 0.220f, 1.00f);
	const ImVec4 COL_CONTROL_HOVER = ImVec4(0.224f, 0.243f, 0.282f, 1.00f);
	const ImVec4 COL_CONTROL_ACTIVE = ImVec4(0.275f, 0.298f, 0.345f, 1.00f);
	const ImVec4 COL_ACCENT = ImVec4(0.949f, 0.639f, 0.208f, 1.00f);
	const ImVec4 COL_ACCENT_HOVER = ImVec4(0.980f, 0.710f, 0.330f, 1.00f);
	const ImVec4 COL_ON_ACCENT = ImVec4(0.10f, 0.08f, 0.04f, 1.00f);
	const ImVec4 COL_TEXT = ImVec4(0.925f, 0.933f, 0.949f, 1.00f);
	const ImVec4 COL_TEXT_DIM = ImVec4(0.560f, 0.588f, 0.639f, 1.00f);
	const ImVec4 COL_GOOD = ImVec4(0.35f, 0.80f, 0.45f, 1.00f);
	const ImVec4 COL_WARN = ImVec4(0.95f, 0.70f, 0.25f, 1.00f);
	const ImVec4 COL_BAD = ImVec4(0.92f, 0.35f, 0.32f, 1.00f);

	constexpr float CARD_PADDING = 16.0f;
	constexpr float CARD_ROUNDING = 12.0f;
	constexpr float CARD_SPACING = 12.0f;

	const char* MODE_NAMES[] = { "Noise Cancelling", "Ambient Sound", "Off" };
	const char* EQ_PRESET_LABELS[] = { "Off", "Bright", "Excited", "Mellow", "Relaxed", "Vocal", "Treble Boost",
		"Bass Boost", "Speech", "Custom", "User 1", "User 2" };
	const char* EQ_BAND_LABELS[EQ_BAND_COUNT] = { "Clear Bass", "400", "1k", "2.5k", "6.3k", "16k" };
	constexpr int EQ_CUSTOM_INDEX = 9;
	//" · " in UTF-8, which is what ImGui expects
	constexpr const char* DOT_SEPARATOR = "  \xC2\xB7  ";

	//A rounded "card" behind a group of widgets. Cards can't be nested.
	//The background is drawn after the contents (on a lower draw channel), so it can fit them exactly.
	struct Card
	{
		float width = 0;
		ImVec4 color;

		explicit Card(const ImVec4& color = COL_CARD) : color(color)
		{
			this->width = ImGui::GetContentRegionAvail().x;
			auto* drawList = ImGui::GetWindowDrawList();
			drawList->ChannelsSplit(2);
			drawList->ChannelsSetCurrent(1);
			ImGui::BeginGroup();
			ImGui::Dummy(ImVec2(this->width, CARD_PADDING - ImGui::GetStyle().ItemSpacing.y));
			ImGui::Indent(CARD_PADDING);
		}

		//Width available for widgets inside the card
		float inner() const { return this->width - 2 * CARD_PADDING; }

		~Card()
		{
			ImGui::Unindent(CARD_PADDING);
			ImGui::Dummy(ImVec2(this->width, CARD_PADDING - ImGui::GetStyle().ItemSpacing.y));
			ImGui::EndGroup();

			auto* drawList = ImGui::GetWindowDrawList();
			auto min = ImGui::GetItemRectMin();
			auto max = ImVec2(min.x + this->width, ImGui::GetItemRectMax().y);
			drawList->ChannelsSetCurrent(0);
			drawList->AddRectFilled(min, max, ImGui::GetColorU32(this->color), CARD_ROUNDING);
			drawList->ChannelsMerge();
			ImGui::Dummy(ImVec2(0, CARD_SPACING - ImGui::GetStyle().ItemSpacing.y));
		}
	};

	void textDim(const char* fmt, ...)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
		va_list args;
		va_start(args, fmt);
		ImGui::TextV(fmt, args);
		va_end(args);
		ImGui::PopStyleColor();
	}

	void sectionTitle(const char* title)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
		ImGui::TextUnformatted(title);
		ImGui::PopStyleColor();
		ImGui::Dummy(ImVec2(0, 2));
	}

	//A button that's filled with the accent colour when selected
	bool toggleButton(const char* label, bool selected, const ImVec2& size)
	{
		if (selected)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, COL_ACCENT);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACCENT_HOVER);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, COL_ACCENT);
			ImGui::PushStyleColor(ImGuiCol_Text, COL_ON_ACCENT);
		}
		bool pressed = ImGui::Button(label, size);
		if (selected)
		{
			ImGui::PopStyleColor(4);
		}
		return pressed;
	}

	bool accentButton(const char* label, const ImVec2& size)
	{
		return toggleButton(label, true, size);
	}

	//Battery outline with a fill proportional to the level
	void drawBattery(int percent, bool charging)
	{
		const ImVec2 bodySize(38, 18);
		const float nubWidth = 3;
		auto pos = ImGui::GetCursorScreenPos();
		pos.y += (ImGui::GetTextLineHeight() - bodySize.y) / 2;
		auto* drawList = ImGui::GetWindowDrawList();

		ImVec4 fillColor = charging ? COL_GOOD : (percent > 50 ? COL_GOOD : (percent > 20 ? COL_WARN : COL_BAD));
		drawList->AddRect(pos, ImVec2(pos.x + bodySize.x, pos.y + bodySize.y), ImGui::GetColorU32(COL_TEXT_DIM), 4, ImDrawCornerFlags_All, 1.5f);
		drawList->AddRectFilled(ImVec2(pos.x + bodySize.x + 1, pos.y + 5), ImVec2(pos.x + bodySize.x + 1 + nubWidth, pos.y + bodySize.y - 5),
			ImGui::GetColorU32(COL_TEXT_DIM), 1);
		float fill = (bodySize.x - 6) * (std::clamp(percent, 0, 100) / 100.0f);
		if (fill > 0)
		{
			drawList->AddRectFilled(ImVec2(pos.x + 3, pos.y + 3), ImVec2(pos.x + 3 + fill, pos.y + bodySize.y - 3), ImGui::GetColorU32(fillColor), 2);
		}
		ImGui::Dummy(ImVec2(bodySize.x + nubWidth + 2, ImGui::GetTextLineHeight()));
	}

	bool looksLikeSonyHeadphones(const std::string& name)
	{
		for (const char* prefix : { "WH-", "WF-", "WI-", "MDR-", "LE_WH-", "LE_WF-", "ULT WEAR" })
		{
			if (name.rfind(prefix, 0) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool fileExists(const char* path)
	{
		std::ifstream file(path, std::ios::binary);
		return file.good();
	}

	void spinnerText(const char* text)
	{
		textDim("%s %c", text, "|/-\\"[(int)(ImGui::GetTime() / 0.08f) & 3]);
	}
}

bool CrossPlatformGUI::performGUIPass()
{
	ImGui::NewFrame();

	ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos({ 0, 0 });
	ImGui::SetNextWindowSize(io.DisplaySize);
	ImGui::Begin("Sony Headphones", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

	std::string connectionError;
	if (this->_bt.takeConnectionError(connectionError))
	{
		this->_bt.disconnect();
		this->_mq.addMessage("Disconnected: " + connectionError);
	}
	this->_pollConnection();
	this->_pollDiscovery();

	this->_drawErrors();

	if (this->_bt.isConnected())
	{
		this->_drawHeader();
		if (this->_useV2Protocol)
		{
			this->_drawNoiseControlV2();
			this->_drawEqualizer();
		}
		else
		{
			this->_drawLegacyControls();
		}
		this->_drawAdvanced();
		this->_setHeadphoneSettings();
	}
	else
	{
		this->_drawConnectScreen();
	}

	this->_drawFooter();

	ImGui::End();
	ImGui::Render();

	return true;
}

void CrossPlatformGUI::_pollConnection()
{
	if (this->_connectFuture.valid() && this->_connectFuture.ready())
	{
		try
		{
			this->_connectFuture.get();
		}
		catch (const RecoverableException& exc)
		{
			if (exc.shouldDisconnect)
			{
				this->_bt.disconnect();
			}
			this->_mq.addMessage(exc.what());
		}
	}
}

void CrossPlatformGUI::_pollDiscovery()
{
	if (!(this->_connectedDevicesFuture.valid() && this->_connectedDevicesFuture.ready()))
	{
		return;
	}

	try
	{
		this->_devices = this->_connectedDevicesFuture.get();
		this->_selectedDevice = this->_devices.size() == 1 ? 0 : -1;
	}
	catch (const RecoverableException& exc)
	{
		if (exc.shouldDisconnect)
		{
			this->_bt.disconnect();
		}
		this->_mq.addMessage(exc.what());
		return;
	}

	//If exactly one paired device looks like Sony headphones, connect to it straight away
	if (!this->_autoConnectTried)
	{
		this->_autoConnectTried = true;
		int sonyIndex = -1;
		int sonyCount = 0;
		for (int i = 0; i < static_cast<int>(this->_devices.size()); i++)
		{
			if (looksLikeSonyHeadphones(this->_devices[i].name))
			{
				sonyIndex = i;
				sonyCount++;
			}
		}
		if (sonyCount == 1)
		{
			this->_selectedDevice = sonyIndex;
			this->_startConnect(this->_devices[sonyIndex]);
		}
	}
}

void CrossPlatformGUI::_startConnect(const BluetoothDevice& device)
{
	if (this->_connectFuture.valid())
	{
		return;
	}
	this->_connectedDevice = device;
	this->_headphones.onConnecting();
	this->_connectFuture.setFromAsync([this]() { this->_bt.connect(this->_connectedDevice.mac); });
}

void CrossPlatformGUI::_drawErrors()
{
	//There's a slight race condition here but I don't care, it'd only be for one frame.
	if (this->_mq.begin() == this->_mq.end())
	{
		return;
	}

	Card card(COL_CARD_ERROR);
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + card.inner());
	for (auto&& message : this->_mq)
	{
		ImGui::TextUnformatted(message.message.c_str());
	}
	ImGui::PopTextWrapPos();
}

void CrossPlatformGUI::_drawConnectScreen()
{
	ImGui::Dummy(ImVec2(0, 24));
	ImGui::PushFont(this->_fontTitle);
	ImGui::TextUnformatted("Sony Headphones");
	ImGui::PopFont();
	textDim("Pick your headphones to connect. They need to be");
	textDim("paired and connected in Windows Bluetooth settings.");
	ImGui::Dummy(ImVec2(0, 12));

	Card card;
	sectionTitle("PAIRED DEVICES");

	bool discovering = this->_connectedDevicesFuture.valid();
	bool connecting = this->_connectFuture.valid();

	if (this->_devices.empty())
	{
		if (discovering)
		{
			spinnerText("Looking for devices");
		}
		else
		{
			textDim("No connected Bluetooth devices found.");
		}
	}

	ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
	for (int i = 0; i < static_cast<int>(this->_devices.size()); i++)
	{
		const auto& device = this->_devices[i];
		bool selected = this->_selectedDevice == i;
		ImGui::PushID(i);
		if (selected)
		{
			ImGui::PushStyleColor(ImGuiCol_Header, COL_CONTROL_ACTIVE);
		}
		if (ImGui::Selectable(device.name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(card.inner(), 36)))
		{
			this->_selectedDevice = i;
			if (ImGui::IsMouseDoubleClicked(0) && !connecting)
			{
				this->_startConnect(device);
			}
		}
		if (selected)
		{
			ImGui::PopStyleColor();
		}
		ImGui::PopID();
	}
	ImGui::PopStyleVar();

	ImGui::Dummy(ImVec2(0, 6));
	float buttonWidth = (card.inner() - ImGui::GetStyle().ItemSpacing.x) / 2;
	if (connecting)
	{
		spinnerText(("Connecting to " + this->_connectedDevice.name).c_str());
	}
	else
	{
		if (accentButton("Connect", ImVec2(buttonWidth, 40)) && this->_selectedDevice >= 0
			&& this->_selectedDevice < static_cast<int>(this->_devices.size()))
		{
			this->_startConnect(this->_devices[this->_selectedDevice]);
		}
		ImGui::SameLine();
		if (ImGui::Button(discovering ? "Refreshing..." : "Refresh", ImVec2(buttonWidth, 40)) && !discovering)
		{
			this->_connectedDevicesFuture.setFromAsync([this]() { return this->_bt.getConnectedDevices(); });
		}
	}
}

void CrossPlatformGUI::_drawHeader()
{
	auto info = this->_headphones.getDeviceInfoV2();
	const std::string& name = info.modelName.empty() ? this->_connectedDevice.name : info.modelName;

	Card card;
	float startX = ImGui::GetCursorPosX();

	ImGui::BeginGroup();
	ImGui::PushFont(this->_fontTitle);
	ImGui::TextUnformatted(name.c_str());
	ImGui::PopFont();

	std::string details;
	if (!info.firmwareVersion.empty())
	{
		details = "Firmware " + info.firmwareVersion;
	}
	if (info.codec != AUDIO_CODEC::UNKNOWN)
	{
		details += (details.empty() ? "" : DOT_SEPARATOR) + std::string(CommandSerializer::codecName(info.codec));
	}
	if (this->_sending)
	{
		details += (details.empty() ? "" : DOT_SEPARATOR) + std::string("Syncing...");
	}
	textDim("%s", details.empty() ? "Connected" : details.c_str());
	ImGui::EndGroup();

	//Battery and Disconnect on the right
	const float rightWidth = 110;
	//SameLine(x) measures from the enclosing group, so position explicitly instead
	ImGui::SameLine();
	ImGui::SetCursorPosX(startX + card.inner() - rightWidth);
	ImGui::BeginGroup();
	if (info.batteryPercent >= 0)
	{
		drawBattery(info.batteryPercent, info.charging);
		ImGui::SameLine();
		ImGui::Text("%d%%", info.batteryPercent);
		if (info.charging)
		{
			textDim("Charging");
		}
	}
	else
	{
		textDim("Battery ...");
	}
	ImGui::PushFont(this->_fontSmall);
	if (ImGui::Button("Disconnect", ImVec2(rightWidth, 0)))
	{
		this->_bt.disconnect();
	}
	ImGui::PopFont();
	ImGui::EndGroup();
}

QuickStatus CrossPlatformGUI::getQuickStatus()
{
	QuickStatus status;
	status.connected = this->_bt.isConnected();
	if (!status.connected)
	{
		return status;
	}

	auto info = this->_headphones.getDeviceInfoV2();
	status.name = info.modelName.empty() ? this->_connectedDevice.name : info.modelName;
	status.batteryPercent = info.batteryPercent;
	status.charging = info.charging;

	NcAsmStateV2 reported;
	status.modeKnown = this->_useV2Protocol && this->_headphones.getReportedNcAsmV2(reported);
	status.mode = static_cast<NC_ASM_MODE_V2>(this->_ncMode);
	return status;
}

void CrossPlatformGUI::setNcAsmMode(NC_ASM_MODE_V2 mode)
{
	//Picked up by _drawNoiseControlV2 on the next frame, and sent from there
	this->_ncMode = static_cast<int>(mode);
}

void CrossPlatformGUI::_drawNoiseControlV2()
{
	int& mode = this->_ncMode;
	int& asmLevel = this->_asmLevel;
	bool& focusOnVoice = this->_focusOnVoice;

	this->_headphones.setUseV2Protocol(true);

	//Follow changes made on the headphones themselves (and their state when we connect)
	NcAsmStateV2 external;
	if (this->_headphones.takeExternalNcAsmV2(external))
	{
		mode = static_cast<int>(external.mode);
		asmLevel = external.asmLevel;
		focusOnVoice = external.focusOnVoice;
	}

	Card card;
	sectionTitle("AMBIENT SOUND CONTROL");

	NcAsmStateV2 reported;
	if (!this->_headphones.getReportedNcAsmV2(reported))
	{
		spinnerText("Reading the headphones' settings");
		return;
	}

	//Three-way segmented control
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float buttonWidth = (card.inner() - 2 * spacing) / 3;
	for (int i = 0; i < 3; i++)
	{
		if (i > 0)
		{
			ImGui::SameLine();
		}
		if (toggleButton(MODE_NAMES[i], mode == i, ImVec2(buttonWidth, 44)))
		{
			mode = i;
		}
	}

	if (mode == static_cast<int>(NC_ASM_MODE_V2::AMBIENT_SOUND))
	{
		ImGui::Dummy(ImVec2(0, 6));
		float lineStart = ImGui::GetCursorPosX();
		ImGui::Text("Ambient level");
		char levelText[8];
		snprintf(levelText, sizeof(levelText), "%d", asmLevel);
		//Right-align the value
		ImGui::SameLine();
		ImGui::SetCursorPosX(lineStart + card.inner() - ImGui::CalcTextSize(levelText).x);
		ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
		ImGui::TextUnformatted(levelText);
		ImGui::PopStyleColor();
		ImGui::PushItemWidth(card.inner());
		ImGui::SliderInt("##asmLevel", &asmLevel, 1, MAX_ASM_LEVEL_V2, "");
		ImGui::PopItemWidth();
		ImGui::Checkbox("Focus on Voice", &focusOnVoice);
	}

	//No Speak-to-Chat control: F6/F8 0C toggles a real on/off setting on the XM5 (it's
	//confirmed with F9 0C), but turning it on didn't enable Speak-to-Chat. The state is
	//still queried on connect and logged, for working out what it is.

	asmLevel = (std::max)(asmLevel, 1);
	this->_headphones.setNcAsmModeV2(static_cast<NC_ASM_MODE_V2>(mode));
	this->_headphones.setAsmLevel(asmLevel);
	this->_headphones.setFocusOnVoice(focusOnVoice);
}

void CrossPlatformGUI::_drawEqualizer()
{
	static int presetIndex = 0;
	static int bands[EQ_BAND_COUNT] = { 0 };

	//Follow the headphones' EQ (on connect, and after a preset fills in its band values)
	EqStateV2 external;
	if (this->_headphones.takeExternalEqV2(external))
	{
		presetIndex = 0;
		for (int i = 0; i < static_cast<int>(std::size(EQ_PRESET_ARRAY)); i++)
		{
			if (EQ_PRESET_ARRAY[i] == external.preset)
			{
				presetIndex = i;
			}
		}
		for (int i = 0; i < EQ_BAND_COUNT; i++)
		{
			bands[i] = external.bands[i];
		}
	}

	Card card;
	sectionTitle("EQUALIZER");

	if (!this->_headphones.hasReportedEqV2())
	{
		spinnerText("Reading the equalizer");
		return;
	}

	//Preset chips, wrapping onto new lines
	const float lineStart = ImGui::GetCursorPosX();
	const float lineEnd = lineStart + card.inner();
	const auto& style = ImGui::GetStyle();
	ImGui::PushFont(this->_fontSmall);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
	for (int i = 0; i < static_cast<int>(std::size(EQ_PRESET_LABELS)); i++)
	{
		float chipWidth = ImGui::CalcTextSize(EQ_PRESET_LABELS[i]).x + 2 * 12.0f;
		if (i > 0)
		{
			ImGui::SameLine();
			if (ImGui::GetCursorPosX() + chipWidth > lineEnd)
			{
				ImGui::NewLine();
			}
		}
		ImGui::PushID(i);
		if (toggleButton(EQ_PRESET_LABELS[i], presetIndex == i, ImVec2(chipWidth, 28)))
		{
			presetIndex = i;
		}
		ImGui::PopID();
	}
	ImGui::PopStyleVar();
	ImGui::PopFont();

	ImGui::Dummy(ImVec2(0, 10));

	//Vertical sliders, Clear Bass set slightly apart from the five bands
	const float gap = 18.0f;
	const float columnWidth = (card.inner() - gap) / EQ_BAND_COUNT;
	const float sliderWidth = (std::min)(columnWidth - 14.0f, 40.0f);
	const float sliderHeight = 170.0f;
	auto columnX = [&](int i) { return lineStart + i * columnWidth + (i > 0 ? gap : 0.0f); };

	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 6.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 16.0f);
	for (int i = 0; i < EQ_BAND_COUNT; i++)
	{
		if (i > 0)
		{
			ImGui::SameLine();
		}
		ImGui::SetCursorPosX(columnX(i) + (columnWidth - sliderWidth) / 2);
		ImGui::PushID(i);
		//Moving a slider switches to the Custom preset, like Sony's app
		if (ImGui::VSliderInt("##band", ImVec2(sliderWidth, sliderHeight), &bands[i], -EQ_BAND_CENTER, EQ_BAND_CENTER, bands[i] > 0 ? "+%d" : "%d"))
		{
			presetIndex = EQ_CUSTOM_INDEX;
		}
		//Double-click a band to reset it to 0
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
		{
			bands[i] = 0;
			presetIndex = EQ_CUSTOM_INDEX;
		}
		ImGui::PopID();
	}
	ImGui::PopStyleVar(3);

	//Labels under each slider
	ImGui::PushFont(this->_fontSmall);
	for (int i = 0; i < EQ_BAND_COUNT; i++)
	{
		if (i > 0)
		{
			ImGui::SameLine();
		}
		float labelWidth = ImGui::CalcTextSize(EQ_BAND_LABELS[i]).x;
		ImGui::SetCursorPosX(columnX(i) + (columnWidth - labelWidth) / 2);
		textDim("%s", EQ_BAND_LABELS[i]);
	}
	ImGui::PopFont();

	EqStateV2 state;
	state.preset = EQ_PRESET_ARRAY[presetIndex];
	state.bands.assign(bands, bands + EQ_BAND_COUNT);
	this->_headphones.setEqV2(state);
}

void CrossPlatformGUI::_drawLegacyControls()
{
	static bool ambientSoundControl = true;
	static bool focusOnVoice = false;
	static int asmLevel = 0;
	static int soundPosition = 0;
	static int vptType = 0;

	this->_headphones.setUseV2Protocol(false);

	{
		Card card;
		sectionTitle("AMBIENT SOUND (XM3 FORMAT)");
		ImGui::PushItemWidth(card.inner() * 0.6f);

		asmLevel = (std::min)(asmLevel, 19);
		ImGui::Checkbox("Ambient Sound Control", &ambientSoundControl);

		if (this->_headphones.isSetAsmLevelAvailable())
		{
			ImGui::SliderInt("Ambient Sound Level", &asmLevel, 0, 19);

			if (this->_headphones.isFocusOnVoiceAvailable())
			{
				ImGui::Checkbox("Focus on Voice", &focusOnVoice);
			}
			else
			{
				textDim("Focus on Voice isn't enabled on this level.");
			}
		}
		ImGui::PopItemWidth();

		this->_headphones.setAmbientSoundControl(ambientSoundControl);
		this->_headphones.setAsmLevel(asmLevel);
		this->_headphones.setFocusOnVoice(focusOnVoice);
	}

	{
		Card card;
		sectionTitle("VIRTUAL SOUND (XM3 FORMAT)");
		textDim("Only one of the options may be used at a time");
		ImGui::PushItemWidth(card.inner() * 0.6f);

		if (ImGui::Combo("Sound Position", &soundPosition, "Off\0Front Left\0Front Right\0"
			"Front\0Rear Left\0Rear Right\0\0"))
		{
			vptType = 0;
		}

		if (ImGui::Combo("Surround (VPT)", &vptType, "Off\0Outdoor Festival\0Arena\0"
			"Concert Hall\0Club\0\0"))
		{
			soundPosition = 0;
		}
		ImGui::PopItemWidth();

		this->_headphones.setSurroundPosition(SOUND_POSITION_PRESET_ARRAY[soundPosition]);
		this->_headphones.setVptType(vptType);
	}
}

void CrossPlatformGUI::_drawAdvanced()
{
	ImGui::PushStyleColor(ImGuiCol_Header, COL_BG);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, COL_CARD);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, COL_CARD);
	ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
	bool open = ImGui::CollapsingHeader("Advanced");
	ImGui::PopStyleColor(4);
	if (!open)
	{
		return;
	}

	Card card;
	ImGui::Checkbox("XM4/XM5 command format", &this->_useV2Protocol);
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + card.inner());
	textDim("Turn off for older headsets like the WH-1000XM3, which use the original commands "
		"(including Virtual Sound).");
	ImGui::PopTextWrapPos();
	ImGui::Dummy(ImVec2(0, 4));
	textDim("Device: %s", this->_connectedDevice.mac.c_str());
	textDim("Protocol log: SonyHeadphonesClient.log");
}

void CrossPlatformGUI::_drawFooter()
{
	ImGui::Dummy(ImVec2(0, 8));
	ImGui::PushFont(this->_fontSmall);
	textDim("Not affiliated with Sony. Use at your own risk.");
	textDim("Based on github.com/Plutoberth/SonyHeadphonesClient");
	ImGui::PopFont();
}

void CrossPlatformGUI::_setHeadphoneSettings() {
	//Only show "Syncing" if a command takes more than a few frames
	static int commandLinger = 0;
	this->_sending = false;

	if (this->_sendCommandFuture.ready())
	{
		commandLinger = 0;
		try
		{
			this->_sendCommandFuture.get();
		}
		catch (const RecoverableException& exc)
		{
			std::string excString;
			//We kinda have to do it here and not in the wrapper, due to async causing timing issues. To fix it, the messagequeue can be made
			//static, but I'm not sure if I wanna do that.
			if (exc.shouldDisconnect)
			{
				this->_bt.disconnect();
				excString = "Disconnected: ";
			}
			this->_mq.addMessage(excString + exc.what());
		}
	}
	//This means that we're waiting
	else if (this->_sendCommandFuture.valid())
	{
		this->_sending = commandLinger++ > (FPS / 10);
	}
	//We're not waiting, and there's no command in the air, so we can evaluate sending a new command
	else if (this->_headphones.isChanged())
	{
		this->_sendCommandFuture.setFromAsync([=, this]() {
			return this->_headphones.setChanges();
		});
	}
}

void CrossPlatformGUI::_loadFonts()
{
	ImGuiIO& io = ImGui::GetIO();

	//Prefer the system UI font where there is one; fall back to the bundled Cascadia Code
	const char* regular = "C:\\Windows\\Fonts\\segoeui.ttf";
	const char* semibold = "C:\\Windows\\Fonts\\seguisb.ttf";

	auto addBundled = [&](float size) {
		//AddFontFromMemory will own the pointer, so there's no leak
		char* fileData = new char[sizeof(CascadiaCodeTTF)];
		memcpy(fileData, CascadiaCodeTTF, sizeof(CascadiaCodeTTF));
		return io.Fonts->AddFontFromMemoryTTF(reinterpret_cast<void*>(fileData), sizeof(CascadiaCodeTTF), size);
	};
	auto addFont = [&](const char* path, float size) {
		return fileExists(path) ? io.Fonts->AddFontFromFileTTF(path, size) : addBundled(size);
	};

	//The first font added is the default
	this->_fontBody = addFont(regular, FONT_SIZE);
	this->_fontTitle = addFont(fileExists(semibold) ? semibold : regular, FONT_SIZE_TITLE);
	this->_fontSmall = addFont(regular, FONT_SIZE_SMALL);
	IM_ASSERT(this->_fontBody && this->_fontTitle && this->_fontSmall);
}

void CrossPlatformGUI::_applyTheme()
{
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();

	style.WindowPadding = ImVec2(18, 18);
	style.FramePadding = ImVec2(12, 8);
	style.ItemSpacing = ImVec2(8, 8);
	style.ItemInnerSpacing = ImVec2(8, 6);
	style.ScrollbarSize = 10;
	style.WindowBorderSize = 0;
	style.FrameBorderSize = 0;
	style.WindowRounding = 0;
	style.FrameRounding = 8;
	style.PopupRounding = 8;
	style.GrabRounding = 6;
	style.ScrollbarRounding = 6;
	style.GrabMinSize = 14;

	ImVec4* c = style.Colors;
	c[ImGuiCol_Text] = COL_TEXT;
	c[ImGuiCol_TextDisabled] = COL_TEXT_DIM;
	c[ImGuiCol_WindowBg] = COL_BG;
	c[ImGuiCol_PopupBg] = COL_CARD;
	c[ImGuiCol_Border] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_FrameBg] = COL_CONTROL;
	c[ImGuiCol_FrameBgHovered] = COL_CONTROL_HOVER;
	c[ImGuiCol_FrameBgActive] = COL_CONTROL_ACTIVE;
	c[ImGuiCol_Button] = COL_CONTROL;
	c[ImGuiCol_ButtonHovered] = COL_CONTROL_HOVER;
	c[ImGuiCol_ButtonActive] = COL_CONTROL_ACTIVE;
	c[ImGuiCol_Header] = COL_CONTROL;
	c[ImGuiCol_HeaderHovered] = COL_CONTROL_HOVER;
	c[ImGuiCol_HeaderActive] = COL_CONTROL_ACTIVE;
	c[ImGuiCol_CheckMark] = COL_ACCENT;
	c[ImGuiCol_SliderGrab] = COL_ACCENT;
	c[ImGuiCol_SliderGrabActive] = COL_ACCENT_HOVER;
	c[ImGuiCol_ScrollbarBg] = COL_BG;
	c[ImGuiCol_ScrollbarGrab] = COL_CONTROL;
	c[ImGuiCol_ScrollbarGrabHovered] = COL_CONTROL_HOVER;
	c[ImGuiCol_ScrollbarGrabActive] = COL_CONTROL_ACTIVE;
	c[ImGuiCol_Separator] = COL_CONTROL;
}

CrossPlatformGUI::CrossPlatformGUI(BluetoothWrapper bt) : _bt(std::move(bt)), _headphones(_bt)
{
	this->_applyTheme();
	ImGuiIO& io = ImGui::GetIO();
	this->_mq = TimedMessageQueue(GUI_MAX_MESSAGES);
	this->_connectedDevicesFuture.setFromAsync([this]() { return this->_bt.getConnectedDevices(); });

	io.IniFilename = nullptr;
	io.WantSaveIniSettings = false;

	this->_loadFonts();
}
