#include "Headphones.h"
#include "CommandSerializer.h"
#include "Log.h"

#include <stdexcept>

Headphones::Headphones(BluetoothWrapper& conn) : _conn(conn)
{
	this->_conn.setMessageHandler([this](const CommandSerializer::Message& msg) { this->_handleMessage(msg); });
}

void Headphones::_handleMessage(const CommandSerializer::Message& msg)
{
	NcAsmStateV2 state;
	if (CommandSerializer::parseNcAsmReportV2(msg.messageBytes, state.mode, state.focusOnVoice, state.asmLevel))
	{
		static const char* modeNames[] = { "Noise Cancelling", "Ambient Sound", "Off" };
		Log::line("STATE", std::string("Headphones report: ") + modeNames[static_cast<int>(state.mode)]
			+ ", ambient level " + std::to_string(state.asmLevel) + (state.focusOnVoice ? ", focus on voice" : ""));

		std::lock_guard guard(this->_propertyMtx);
		this->_reportedNcAsmV2 = state;
		this->_hasReportedNcAsmV2 = true;
		this->_reportCount++;
	}
	else if (msg.messageBytes.size() >= 2 && msg.messageBytes[0] == static_cast<char>(COMMAND_TYPE::CONNECT_RET_PROTOCOL_INFO))
	{
		Log::line("STATE", "Protocol info: " + CommandSerializer::toHex(msg.messageBytes));
		return;
	}

	unsigned char infoType;
	std::string infoValue;
	int percent;
	bool charging;
	AUDIO_CODEC codec;
	EqStateV2 eq;
	bool stcEnabled;

	if (CommandSerializer::parseSpeakToChatV2(msg.messageBytes, stcEnabled))
	{
		Log::line("STATE", std::string("Speak-to-Chat: ") + (stcEnabled ? "on" : "off") + " (raw " + CommandSerializer::toHex(msg.messageBytes) + ")");
		std::lock_guard guard(this->_propertyMtx);
		this->_reportedStc = stcEnabled;
		this->_hasReportedStc = true;
		this->_stcReportCount++;
	}
	else if (CommandSerializer::parseDeviceInfo(msg.messageBytes, infoType, infoValue))
	{
		Log::line("STATE", std::string(infoType == 0x01 ? "Model: " : "Firmware: ") + infoValue);
		std::lock_guard guard(this->_propertyMtx);
		(infoType == 0x01 ? this->_deviceInfo.modelName : this->_deviceInfo.firmwareVersion) = infoValue;
	}
	else if (CommandSerializer::parseBatteryV2(msg.messageBytes, percent, charging))
	{
		Log::line("STATE", "Battery: " + std::to_string(percent) + "%" + (charging ? ", charging" : ""));
		std::lock_guard guard(this->_propertyMtx);
		this->_deviceInfo.batteryPercent = percent;
		this->_deviceInfo.charging = charging;
	}
	else if (CommandSerializer::parseCodecV2(msg.messageBytes, codec))
	{
		Log::line("STATE", std::string("Codec: ") + CommandSerializer::codecName(codec));
		std::lock_guard guard(this->_propertyMtx);
		this->_deviceInfo.codec = codec;
	}
	else if (CommandSerializer::parseEqV2(msg.messageBytes, eq.preset, eq.bands))
	{
		std::string bandText;
		for (int band : eq.bands)
		{
			bandText += (bandText.empty() ? "" : " ") + std::to_string(band);
		}
		Log::line("STATE", "Equalizer: preset " + CommandSerializer::toHex({ static_cast<char>(eq.preset) }) + ", bands " + bandText);

		//Keep the slider count fixed even if a reply carries fewer bands
		eq.bands.resize(EQ_BAND_COUNT, 0);
		std::lock_guard guard(this->_propertyMtx);
		this->_reportedEq = eq;
		this->_hasReportedEq = true;
		this->_eqReportCount++;
	}
}

DeviceInfoV2 Headphones::getDeviceInfoV2()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_deviceInfo;
}

void Headphones::setEqV2(const EqStateV2& val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_eqV2.desired = val;
}

bool Headphones::hasReportedEqV2()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_hasReportedEq;
}

bool Headphones::takeExternalEqV2(EqStateV2& out)
{
	std::lock_guard guard(this->_propertyMtx);
	if (this->_eqReportCount == this->_appliedEqReportCount)
	{
		return false;
	}
	//Adopt the first report after connecting right away. Later, same rules as the NC/ambient
	//state: let our own commands settle, and don't override the user's pending changes.
	if (!this->_awaitingInitialEq)
	{
		if (Clock::now() - this->_lastEqSendTime < REPORT_SETTLE_TIME || !this->_eqV2.isFulfilled())
		{
			return false;
		}
	}
	this->_awaitingInitialEq = false;
	this->_appliedEqReportCount = this->_eqReportCount;
	out = this->_reportedEq;
	this->_eqV2.current = this->_eqV2.desired = out;
	return true;
}

void Headphones::setSpeakToChatV2(bool val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_speakToChatV2.desired = val;
}

bool Headphones::hasReportedSpeakToChatV2()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_hasReportedStc;
}

bool Headphones::takeExternalSpeakToChatV2(bool& out)
{
	std::lock_guard guard(this->_propertyMtx);
	if (this->_stcReportCount == this->_appliedStcReportCount)
	{
		return false;
	}
	if (!this->_awaitingInitialStc)
	{
		if (Clock::now() - this->_lastStcSendTime < REPORT_SETTLE_TIME || !this->_speakToChatV2.isFulfilled())
		{
			return false;
		}
	}
	this->_awaitingInitialStc = false;
	this->_appliedStcReportCount = this->_stcReportCount;
	out = this->_reportedStc;
	this->_speakToChatV2.current = this->_speakToChatV2.desired = out;
	return true;
}

bool Headphones::_isSpeakToChatV2Changed()
{
	return !this->_awaitingInitialStc && !this->_speakToChatV2.isFulfilled();
}

bool Headphones::_isEqV2Changed()
{
	//Wait for the headphones' own EQ before sending anything, so we don't overwrite it with defaults
	return !this->_awaitingInitialEq && !this->_eqV2.isFulfilled();
}

bool Headphones::_isBatteryRefreshDue()
{
	return Clock::now() - this->_lastBatteryQuery > BATTERY_REFRESH_INTERVAL;
}

void Headphones::onConnecting()
{
	std::lock_guard guard(this->_propertyMtx);
	this->_hasReportedNcAsmV2 = false;
	this->_appliedReportCount = this->_reportCount;
	this->_awaitingInitialState = true;
	this->_connectTime = Clock::now();
	//The connect queries include battery and EQ
	this->_lastBatteryQuery = this->_connectTime;
	this->_deviceInfo = {};
	this->_hasReportedEq = false;
	this->_awaitingInitialEq = true;
	this->_appliedEqReportCount = this->_eqReportCount;
	this->_hasReportedStc = false;
	this->_awaitingInitialStc = true;
	this->_appliedStcReportCount = this->_stcReportCount;
	//Force the current settings to be resent on the new connection
	this->_useV2Protocol.current = !this->_useV2Protocol.desired;
	this->_surroundPosition.current = SOUND_POSITION_PRESET::OUT_OF_RANGE;
}

bool Headphones::getReportedNcAsmV2(NcAsmStateV2& out)
{
	std::lock_guard guard(this->_propertyMtx);
	out = this->_reportedNcAsmV2;
	return this->_hasReportedNcAsmV2;
}

bool Headphones::takeExternalNcAsmV2(NcAsmStateV2& out)
{
	std::lock_guard guard(this->_propertyMtx);
	if (!this->_useV2Protocol.desired || this->_reportCount == this->_appliedReportCount)
	{
		return false;
	}
	//On connect, adopt the first report right away. Later, wait until our own commands have settled,
	//and never override a change the user made that hasn't been sent yet.
	if (!this->_awaitingInitialState)
	{
		if (Clock::now() - this->_lastSendTime < REPORT_SETTLE_TIME)
		{
			return false;
		}
		if (!(this->_ncAsmModeV2.isFulfilled() && this->_asmLevel.isFulfilled() && this->_focusOnVoice.isFulfilled()))
		{
			return false;
		}
	}

	this->_appliedReportCount = this->_reportCount;
	this->_awaitingInitialState = false;
	out = this->_reportedNcAsmV2;

	//The headphones already have this state, so there's nothing to send
	this->_ncAsmModeV2.current = this->_ncAsmModeV2.desired = out.mode;
	this->_focusOnVoice.current = this->_focusOnVoice.desired = out.focusOnVoice;
	this->_asmLevel.current = this->_asmLevel.desired = out.asmLevel;
	this->_useV2Protocol.fulfill();
	return true;
}

void Headphones::setAmbientSoundControl(bool val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_ambientSoundControl.desired = val;
}

bool Headphones::getAmbientSoundControl()
{
	return this->_ambientSoundControl.current;
}

bool Headphones::isFocusOnVoiceAvailable()
{
	return this->_ambientSoundControl.current && this->_asmLevel.current > MINIMUM_VOICE_FOCUS_STEP;
}

void Headphones::setFocusOnVoice(bool val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_focusOnVoice.desired = val;
}

bool Headphones::getFocusOnVoice()
{
	return this->_focusOnVoice.current;
}

bool Headphones::isSetAsmLevelAvailable()
{
	return this->_ambientSoundControl.current;
}

void Headphones::setAsmLevel(int val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_asmLevel.desired = val;
}

int Headphones::getAsmLevel()
{
	return this->_asmLevel.current;
}

void Headphones::setSurroundPosition(SOUND_POSITION_PRESET val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_surroundPosition.desired = val;
}

SOUND_POSITION_PRESET Headphones::getSurroundPosition()
{
	return this->_surroundPosition.current;
}

void Headphones::setVptType(int val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_vptType.desired = val;
}

int Headphones::getVptType()
{
	return this->_vptType.current;
}

void Headphones::setUseV2Protocol(bool val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_useV2Protocol.desired = val;
}

bool Headphones::getUseV2Protocol()
{
	return this->_useV2Protocol.desired;
}

void Headphones::setNcAsmModeV2(NC_ASM_MODE_V2 val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_ncAsmModeV2.desired = val;
}

bool Headphones::_isNcAsmV2Changed()
{
	if (!(this->_useV2Protocol.isFulfilled() && this->_ncAsmModeV2.isFulfilled() && this->_focusOnVoice.isFulfilled() && this->_asmLevel.isFulfilled()))
	{
		//Don't overwrite the headphones' real state with the GUI defaults while we wait for their report
		if (this->_awaitingInitialState && Clock::now() - this->_connectTime < INITIAL_STATE_TIMEOUT)
		{
			return false;
		}
		return true;
	}
	return false;
}

bool Headphones::_isVptChanged()
{
	//The XM5 has no VPT / sound position, so these aren't sent in XM4/XM5 mode
	return !this->_useV2Protocol.desired && !(this->_surroundPosition.isFulfilled() && this->_vptType.isFulfilled());
}

bool Headphones::isChanged()
{
	std::lock_guard guard(this->_propertyMtx);
	if (this->_useV2Protocol.desired)
	{
		return this->_isNcAsmV2Changed() || this->_isEqV2Changed() || this->_isSpeakToChatV2Changed() || this->_isBatteryRefreshDue();
	}
	return !(this->_useV2Protocol.isFulfilled() && this->_ambientSoundControl.isFulfilled() && this->_asmLevel.isFulfilled() && this->_focusOnVoice.isFulfilled())
		|| this->_isVptChanged();
}

void Headphones::_setChangesV2()
{
	std::unique_lock lock(this->_propertyMtx);

	if (this->_isNcAsmV2Changed())
	{
		this->_awaitingInitialState = false;
		this->_lastSendTime = Clock::now();
		NcAsmStateV2 toSend = { this->_ncAsmModeV2.desired, this->_focusOnVoice.desired, this->_asmLevel.desired };
		lock.unlock();

		this->_conn.sendCommand(CommandSerializer::serializeNcAsmSettingV2(
			toSend.mode,
			toSend.focusOnVoice,
			static_cast<unsigned char>(toSend.asmLevel)
		));

		//Record what was actually sent; anything the user changed meanwhile stays pending
		lock.lock();
		this->_lastSendTime = Clock::now();
		this->_useV2Protocol.current = true;
		this->_ncAsmModeV2.current = toSend.mode;
		this->_focusOnVoice.current = toSend.focusOnVoice;
		this->_asmLevel.current = toSend.asmLevel;
	}

	if (this->_isEqV2Changed())
	{
		this->_lastEqSendTime = Clock::now();
		EqStateV2 toSend = this->_eqV2.desired;
		lock.unlock();

		//Presets carry their own band values; only Custom sends ours
		this->_conn.sendCommand(toSend.preset == EQ_PRESET::CUSTOM
			? CommandSerializer::serializeEqCustomV2(toSend.bands)
			: CommandSerializer::serializeEqPresetV2(toSend.preset));
		//Ask for the result, so the sliders show the preset's actual bands
		this->_conn.sendCommand({ CMD_EQ_GET_V2, 0x00 });

		lock.lock();
		this->_lastEqSendTime = Clock::now();
		this->_eqV2.current = toSend;
	}

	if (this->_isSpeakToChatV2Changed())
	{
		this->_lastStcSendTime = Clock::now();
		bool toSend = this->_speakToChatV2.desired;
		lock.unlock();

		this->_conn.sendCommand(CommandSerializer::serializeSpeakToChatV2(toSend));
		//Read it back: if the guessed command didn't take, the checkbox flips back
		this->_conn.sendCommand(CommandSerializer::serializeSpeakToChatQueryV2());

		lock.lock();
		this->_lastStcSendTime = Clock::now();
		this->_speakToChatV2.current = toSend;
	}

	if (this->_isBatteryRefreshDue())
	{
		this->_lastBatteryQuery = Clock::now();
		lock.unlock();
		this->_conn.sendCommand(CommandSerializer::serializeBatteryQueryV2());
	}
}

void Headphones::setChanges()
{
	if (this->_useV2Protocol.desired)
	{
		this->_setChangesV2();
		return;
	}
	else if (!(this->_useV2Protocol.isFulfilled() && this->_ambientSoundControl.isFulfilled() && this->_focusOnVoice.isFulfilled() && this->_asmLevel.isFulfilled()))
	{
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_useV2Protocol.fulfill();
		}
		auto ncAsmEffect = this->_ambientSoundControl.desired ? NC_ASM_EFFECT::ADJUSTMENT_COMPLETION : NC_ASM_EFFECT::OFF;
		auto asmId = this->_focusOnVoice.desired ? ASM_ID::VOICE : ASM_ID::NORMAL;
		auto asmLevel = this->_ambientSoundControl.desired ? this->_asmLevel.desired : ASM_LEVEL_DISABLED;

		this->_conn.sendCommand(CommandSerializer::serializeNcAndAsmSetting(
			ncAsmEffect,
			NC_ASM_SETTING_TYPE::LEVEL_ADJUSTMENT,
			ASM_SETTING_TYPE::LEVEL_ADJUSTMENT,
			asmId,
			asmLevel
		));
		
		std::lock_guard guard(this->_propertyMtx);
		this->_ambientSoundControl.fulfill();
		this->_asmLevel.fulfill();
		this->_focusOnVoice.fulfill();
	}

	if (this->_isVptChanged()) {
		VPT_INQUIRED_TYPE command;
		unsigned char preset;

		if (this->_vptType.desired != 0) {
			command = VPT_INQUIRED_TYPE::VPT;
			preset = static_cast<unsigned char>(this->_vptType.desired);
		}
		else if (this->_surroundPosition.desired != SOUND_POSITION_PRESET::OFF) {
			command = VPT_INQUIRED_TYPE::SOUND_POSITION;
			preset = static_cast<unsigned char>(this->_surroundPosition.desired);
		}
		else {
			// Just used that one because it seems like it disables both
			if (this->_surroundPosition.current != SOUND_POSITION_PRESET::OFF) {
				command = VPT_INQUIRED_TYPE::SOUND_POSITION;
				preset = static_cast<unsigned char>(SOUND_POSITION_PRESET::OFF);
			}
			else if (this->_vptType.current != 0) {
				command = VPT_INQUIRED_TYPE::VPT;
				preset = 0;
			}
			else {
				throw std::logic_error("it's impossible that both values were changed to zero and were also previously zero");
			}
		}

		this->_conn.sendCommand(CommandSerializer::serializeVPTSetting(command, preset));

		std::lock_guard guard(this->_propertyMtx);
		this->_vptType.fulfill();
		this->_surroundPosition.fulfill();
	}
}
