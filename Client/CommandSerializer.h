#pragma once
#include "Constants.h"
#include "ByteMagic.h"
#include <cstddef>
#include <vector>
#include <stdexcept>
#include "Exceptions.h"

constexpr int MINIMUM_VOICE_FOCUS_STEP = 2;
constexpr unsigned int ASM_LEVEL_DISABLED = -1;

namespace CommandSerializer
{
	struct Message
	{
		DATA_TYPE dataType;
		unsigned char seqNumber;
		//The command payload, without the header and checksum
		Buffer messageBytes;
	};

	//Format bytes as space-separated hex, for logging
	std::string toHex(const char* data, size_t size);
	std::string toHex(const Buffer& src);

	//escape special chars

	Buffer _escapeSpecials(const Buffer& src);
	Buffer _unescapeSpecials(const Buffer& src);
	unsigned char _sumChecksum(const char* src, size_t size);
	unsigned char _sumChecksum(const Buffer& src);
	//Package a serialized command according to the protocol
	/*
	References:
	* DataType
	* CommandBluetoothSender.sendCommandWithRetries
	* BluetoothSenderWrapper.sendCommandViaBluetooth
	* 
	* Serialized data format: <START_MARKER>ESCAPE_SPECIALS(<DATA_TYPE><SEQ_NUMBER><BIG ENDIAN 4 BYTE SIZE OF UNESCAPED DATA><DATA><1 BYTE CHECKSUM>)<END_MARKER>
	*/
	Buffer packageDataForBt(const Buffer& src, DATA_TYPE dataType, unsigned int seqNumber);

	Message unpackBtMessage(const Buffer& src);

	NC_DUAL_SINGLE_VALUE getDualSingleForAsmLevel(char asmLevel);
	Buffer serializeNcAndAsmSetting(NC_ASM_EFFECT ncAsmEffect, NC_ASM_SETTING_TYPE ncAsmSettingType, ASM_SETTING_TYPE asmSettingType, ASM_ID asmId, char asmLevel);
	Buffer serializeVPTSetting(VPT_INQUIRED_TYPE type, unsigned char preset);

	//Experimental XM4/XM5 ("v2") commands, based on community reverse engineering, not yet confirmed
	Buffer serializeProtocolInfoRequest();
	Buffer serializeNcAsmQueryV2();

	//Read-only queries sent on connect. None of them change settings.
	std::vector<Buffer> connectQueriesV2();
	Buffer serializeBatteryQueryV2();

	//Parsers for XM4/XM5 replies (query + 1) and notifications (query + 3). Return false if the
	//payload isn't that kind of message.
	bool parseDeviceInfo(const Buffer& payload, unsigned char& infoType, std::string& value);
	bool parseBatteryV2(const Buffer& payload, int& percent, bool& charging);
	bool parseCodecV2(const Buffer& payload, AUDIO_CODEC& codec);
	bool parseEqV2(const Buffer& payload, EQ_PRESET& preset, std::vector<int>& bands);

	const char* codecName(AUDIO_CODEC codec);

	//Experimental: Speak-to-Chat
	//Simple device settings (see DeviceSettingSpec)
	const DeviceSettingSpec& deviceSettingSpec(DeviceSetting id);
	Buffer serializeDeviceSettingQuery(DeviceSetting id);
	//Builds the set command from the last reply, so bytes we don't understand are sent back unchanged
	Buffer serializeDeviceSetting(DeviceSetting id, const Buffer& lastReport, unsigned char value);
	//Matches a reply or notification for any of the settings
	bool parseDeviceSetting(const Buffer& payload, DeviceSetting& id, unsigned char& value);

	//Experimental: equalizer set commands are not yet confirmed on the XM5
	Buffer serializeEqPresetV2(EQ_PRESET preset);
	//bands are -10..+10, Clear Bass first
	Buffer serializeEqCustomV2(const std::vector<int>& bands);
	Buffer serializeNcAsmSettingV2(NC_ASM_MODE_V2 mode, bool focusOnVoice, unsigned char asmLevel);
	//Parses a 67 17 (reply to a query) or 69 17 (change notification) payload. False if it's neither.
	bool parseNcAsmReportV2(const Buffer& payload, NC_ASM_MODE_V2& mode, bool& focusOnVoice, int& asmLevel);
}

