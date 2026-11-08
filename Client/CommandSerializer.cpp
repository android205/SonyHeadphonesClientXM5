#include "CommandSerializer.h"

constexpr unsigned char ESCAPED_BYTE_SENTRY = 61;
constexpr unsigned char ESCAPED_60 = 44;
constexpr unsigned char ESCAPED_61 = 45;
constexpr unsigned char ESCAPED_62 = 46;
constexpr int MAX_STEPS_WH_1000_XM3 = 19;

namespace CommandSerializer
{
	std::string toHex(const char* data, size_t size)
	{
		static const char digits[] = "0123456789ABCDEF";
		std::string ret;
		ret.reserve(size * 3);
		for (size_t i = 0; i < size; i++)
		{
			auto b = static_cast<unsigned char>(data[i]);
			if (i) ret.push_back(' ');
			ret.push_back(digits[b >> 4]);
			ret.push_back(digits[b & 0xF]);
		}
		return ret;
	}

	std::string toHex(const Buffer& src)
	{
		return toHex(src.data(), src.size());
	}

	Buffer _escapeSpecials(const Buffer& src)
	{
		Buffer ret;
		ret.reserve(src.size());

		for (auto&& b : src)
		{
			switch (b)
			{
			case 60:
				ret.push_back(ESCAPED_BYTE_SENTRY);
				ret.push_back(ESCAPED_60);
				break;

			case 61:
				ret.push_back(ESCAPED_BYTE_SENTRY);
				ret.push_back(ESCAPED_61);
				break;

			case 62:
				ret.push_back(ESCAPED_BYTE_SENTRY);
				ret.push_back(ESCAPED_62);
				break;

			default:
				ret.push_back(b);
				break;
			}
		}

		return ret;
	}

	Buffer _unescapeSpecials(const Buffer& src)
	{
		Buffer ret;
		ret.reserve(src.size());

		for (size_t i = 0; i < src.size(); i++)
		{
			auto currByte = src[i];
			if (currByte == ESCAPED_BYTE_SENTRY)
			{
				if (i == src.size() - 1)
				{
					throw std::runtime_error("No data left for escaped byte data");
				}
				i = i + 1;
				switch (src[i])
				{
				case ESCAPED_60:
					ret.push_back(60);
					break;

				case ESCAPED_61:
					ret.push_back(61);
					break;

				case ESCAPED_62:
					ret.push_back(62);
					break;

				default:
					throw std::runtime_error("Unexpected escaped byte");
					break;
				}
			}
			else
			{
				ret.push_back(currByte);
			}
		}

		return ret;
	}

	unsigned char _sumChecksum(const char* src, size_t size)
	{
		unsigned char accumulator = 0;
		for (size_t i = 0; i < size; i++)
		{
			accumulator += src[i];
		}
		return accumulator;
	}

	unsigned char _sumChecksum(const Buffer& src)
	{
		return _sumChecksum(src.data(), src.size());
	}

	Buffer packageDataForBt(const Buffer& src, DATA_TYPE dataType, unsigned int seqNumber)
	{
		//Reserve at least the size for the size, start&end markers, and the source
		Buffer toEscape;
		toEscape.reserve(src.size() + 2 + sizeof(int));
		Buffer ret;
		ret.reserve(toEscape.capacity());
		toEscape.push_back(static_cast<unsigned char>(dataType));
		toEscape.push_back(seqNumber);
		auto retSize = intToBytesBE(static_cast<unsigned int>(src.size()));
		//Insert data size
		toEscape.insert(toEscape.end(), retSize.begin(), retSize.end());
		//Insert command data
		toEscape.insert(toEscape.end(), src.begin(), src.end());

		auto checksum = _sumChecksum(toEscape);
		toEscape.push_back(checksum);
		toEscape = _escapeSpecials(toEscape);

		
		ret.push_back(START_MARKER);
		ret.insert(ret.end(), toEscape.begin(), toEscape.end());
		ret.push_back(END_MARKER);


		// Message will be chunked if it's larger than MAX_BLUETOOTH_MESSAGE_SIZE, just crash to deal with it for now
		if (ret.size() > MAX_BLUETOOTH_MESSAGE_SIZE)
		{
			throw std::runtime_error("Exceeded the max bluetooth message size, and I can't handle chunked messages");
		}

		return ret;
	}

	Message unpackBtMessage(const Buffer& src)
	{
		//Message data format: ESCAPE_SPECIALS(<DATA_TYPE><SEQ_NUMBER><BIG ENDIAN 4 BYTE SIZE OF UNESCAPED DATA><DATA><1 BYTE CHECKSUM>)
		// The length, header and checksum all refer to the unescaped bytes.
		auto unescaped = _unescapeSpecials(src);

		if (unescaped.size() < 7)
		{
			throw std::runtime_error("Invalid message: Smaller than the minimum message size");
		}

		Message ret;
		ret.dataType = static_cast<DATA_TYPE>(unescaped[0]);
		ret.seqNumber = unescaped[1];
		if ((unsigned char)unescaped[unescaped.size() - 1] != _sumChecksum(unescaped.data(), unescaped.size() - 1))
		{
			throw RecoverableException("Invalid checksum!", true);
		}

		size_t payloadSize =
			(static_cast<size_t>(static_cast<unsigned char>(unescaped[2])) << 24) |
			(static_cast<size_t>(static_cast<unsigned char>(unescaped[3])) << 16) |
			(static_cast<size_t>(static_cast<unsigned char>(unescaped[4])) << 8) |
			static_cast<size_t>(static_cast<unsigned char>(unescaped[5]));
		if (payloadSize != unescaped.size() - 7)
		{
			throw RecoverableException("Invalid message: Size field doesn't match the data", true);
		}
		ret.messageBytes.assign(unescaped.begin() + 6, unescaped.end() - 1);
		return ret;
	}

	NC_DUAL_SINGLE_VALUE getDualSingleForAsmLevel(char asmLevel)
	{
		NC_DUAL_SINGLE_VALUE val = NC_DUAL_SINGLE_VALUE::OFF;
		if (asmLevel > MAX_STEPS_WH_1000_XM3)
		{
			throw std::runtime_error("Exceeded max steps");
		}
		else if (asmLevel == 1)
		{
			val = NC_DUAL_SINGLE_VALUE::SINGLE;
		}
		else if (asmLevel == 0)
		{
			val = NC_DUAL_SINGLE_VALUE::DUAL;
		}
		return val;
	}

	Buffer serializeNcAndAsmSetting(NC_ASM_EFFECT ncAsmEffect, NC_ASM_SETTING_TYPE ncAsmSettingType, ASM_SETTING_TYPE asmSettingType, ASM_ID asmId, char asmLevel)
	{
		Buffer ret;
		ret.push_back(static_cast<unsigned char>(COMMAND_TYPE::NCASM_SET_PARAM));
		ret.push_back(static_cast<unsigned char>(NC_ASM_INQUIRED_TYPE::NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE));
		ret.push_back(static_cast<unsigned char>(ncAsmEffect));
		ret.push_back(static_cast<unsigned char>(ncAsmSettingType));
		ret.push_back(static_cast<unsigned char>(getDualSingleForAsmLevel(asmLevel)));
		ret.push_back(static_cast<unsigned char>(asmSettingType));
		ret.push_back(static_cast<unsigned char>(asmId));
		ret.push_back(asmLevel);
		return ret;
	}

	Buffer serializeVPTSetting(VPT_INQUIRED_TYPE type, unsigned char preset)
	{
		Buffer ret;
		ret.push_back(static_cast<unsigned char>(COMMAND_TYPE::VPT_SET_PARAM));
		ret.push_back(static_cast<unsigned char>(type));
		ret.push_back(preset);

		return ret;
	}

	Buffer serializeProtocolInfoRequest()
	{
		return { static_cast<char>(COMMAND_TYPE::CONNECT_GET_PROTOCOL_INFO), 0x00 };
	}

	Buffer serializeNcAsmQueryV2()
	{
		return { static_cast<char>(COMMAND_TYPE::NCASM_GET_PARAM), NC_ASM_INQUIRED_TYPE_V2 };
	}

	std::vector<Buffer> connectQueriesV2()
	{
		std::vector<Buffer> queries = {
			{ CMD_DEVICE_INFO_GET, 0x01 },        //model name
			{ CMD_DEVICE_INFO_GET, 0x02 },        //firmware version
			{ CMD_SUPPORT_FUNCTION_GET, 0x00 },   //logged, for working out more features later
			serializeBatteryQueryV2(),
			{ CMD_CODEC_GET_V2, 0x02 },
			{ CMD_EQ_GET_V2, 0x00 },
		};
		for (const auto& spec : DEVICE_SETTING_SPECS)
		{
			queries.push_back(serializeDeviceSettingQuery(spec.id));
		}
		return queries;
	}

	Buffer serializeBatteryQueryV2()
	{
		return { CMD_BATTERY_GET_V2, 0x00 };
	}

	//True if payload starts with <query + 1> or <query + 3>, followed by subType
	static bool _isReplyOrNotify(const Buffer& payload, char query, char subType, size_t minSize)
	{
		if (payload.size() < minSize || payload[1] != subType)
		{
			return false;
		}
		return payload[0] == static_cast<char>(query + 1) || payload[0] == static_cast<char>(query + 3);
	}

	bool parseDeviceInfo(const Buffer& payload, unsigned char& infoType, std::string& value)
	{
		//05 <type> <length> <ASCII text>
		if (payload.size() < 3 || payload[0] != CMD_DEVICE_INFO_GET + 1)
		{
			return false;
		}
		infoType = static_cast<unsigned char>(payload[1]);
		size_t length = static_cast<unsigned char>(payload[2]);
		if (infoType > 0x02 || payload.size() < 3 + length)
		{
			return false;
		}
		value.assign(payload.begin() + 3, payload.begin() + 3 + length);
		return true;
	}

	bool parseBatteryV2(const Buffer& payload, int& percent, bool& charging)
	{
		//23 00 <percent> <charging>
		if (!_isReplyOrNotify(payload, CMD_BATTERY_GET_V2, 0x00, 4))
		{
			return false;
		}
		percent = static_cast<unsigned char>(payload[2]);
		charging = payload[3] == 1;
		return percent <= 100;
	}

	bool parseCodecV2(const Buffer& payload, AUDIO_CODEC& codec)
	{
		//13 02 <codec>
		if (!_isReplyOrNotify(payload, CMD_CODEC_GET_V2, 0x02, 3))
		{
			return false;
		}
		codec = static_cast<AUDIO_CODEC>(payload[2]);
		return true;
	}

	bool parseEqV2(const Buffer& payload, EQ_PRESET& preset, std::vector<int>& bands)
	{
		//57 00 <preset> <band count> <bands, 0..20 with 10 = flat>
		if (!_isReplyOrNotify(payload, CMD_EQ_GET_V2, 0x00, 4))
		{
			return false;
		}
		preset = static_cast<EQ_PRESET>(payload[2]);
		size_t count = static_cast<unsigned char>(payload[3]);
		if (payload.size() < 4 + count)
		{
			return false;
		}
		bands.clear();
		for (size_t i = 0; i < count; i++)
		{
			bands.push_back(static_cast<unsigned char>(payload[4 + i]) - EQ_BAND_CENTER);
		}
		return true;
	}

	const char* codecName(AUDIO_CODEC codec)
	{
		switch (codec)
		{
		case AUDIO_CODEC::SBC: return "SBC";
		case AUDIO_CODEC::AAC: return "AAC";
		case AUDIO_CODEC::LDAC: return "LDAC";
		case AUDIO_CODEC::APTX: return "aptX";
		case AUDIO_CODEC::APTX_HD: return "aptX HD";
		default: return "Unknown";
		}
	}

	const DeviceSettingSpec& deviceSettingSpec(DeviceSetting id)
	{
		for (const auto& spec : DEVICE_SETTING_SPECS)
		{
			if (spec.id == id)
			{
				return spec;
			}
		}
		throw std::logic_error("No spec for device setting");
	}

	Buffer serializeDeviceSettingQuery(DeviceSetting id)
	{
		const auto& spec = deviceSettingSpec(id);
		return { spec.queryCmd, spec.subtype };
	}

	Buffer serializeDeviceSetting(DeviceSetting id, const Buffer& lastReport, unsigned char value)
	{
		const auto& spec = deviceSettingSpec(id);
		Buffer ret = lastReport;
		if (ret.size() <= spec.valueIndex)
		{
			throw std::logic_error("Can't change a setting the headphones haven't reported");
		}
		ret[0] = static_cast<char>(spec.queryCmd + 2);
		ret[spec.valueIndex] = static_cast<char>(value);
		return ret;
	}

	bool parseDeviceSetting(const Buffer& payload, DeviceSetting& id, unsigned char& value)
	{
		for (const auto& spec : DEVICE_SETTING_SPECS)
		{
			if (_isReplyOrNotify(payload, spec.queryCmd, spec.subtype, spec.valueIndex + 1))
			{
				id = spec.id;
				value = static_cast<unsigned char>(payload[spec.valueIndex]);
				return true;
			}
		}
		return false;
	}

	Buffer serializeEqPresetV2(EQ_PRESET preset)
	{
		//58 00 <preset> 00: switch preset without sending band values
		return { CMD_EQ_SET_V2, 0x00, static_cast<char>(preset), 0x00 };
	}

	Buffer serializeEqCustomV2(const std::vector<int>& bands)
	{
		//58 00 A0 <band count> <bands>, same layout as the 57 reply
		Buffer ret = { CMD_EQ_SET_V2, 0x00, static_cast<char>(EQ_PRESET::CUSTOM), static_cast<char>(bands.size()) };
		for (int band : bands)
		{
			band = band < -EQ_BAND_CENTER ? -EQ_BAND_CENTER : (band > EQ_BAND_CENTER ? EQ_BAND_CENTER : band);
			ret.push_back(static_cast<char>(band + EQ_BAND_CENTER));
		}
		return ret;
	}

	Buffer serializeNcAsmSettingV2(NC_ASM_MODE_V2 mode, bool focusOnVoice, unsigned char asmLevel)
	{
		//<SET_PARAM><inquired type><value change: 1 = applied><on/off><0 = NC, 1 = ambient><focus on voice><ambient level>
		Buffer ret;
		ret.push_back(static_cast<char>(COMMAND_TYPE::NCASM_SET_PARAM));
		ret.push_back(NC_ASM_INQUIRED_TYPE_V2);
		ret.push_back(0x01);
		ret.push_back(mode == NC_ASM_MODE_V2::OFF ? 0x00 : 0x01);
		ret.push_back(mode == NC_ASM_MODE_V2::AMBIENT_SOUND ? 0x01 : 0x00);
		ret.push_back(mode == NC_ASM_MODE_V2::AMBIENT_SOUND && focusOnVoice ? 0x01 : 0x00);
		ret.push_back(mode == NC_ASM_MODE_V2::AMBIENT_SOUND ? asmLevel : 0x00);
		return ret;
	}

	bool parseNcAsmReportV2(const Buffer& payload, NC_ASM_MODE_V2& mode, bool& focusOnVoice, int& asmLevel)
	{
		//Same layout as the set command: <type> 17 <value change> <on/off> <0 = NC, 1 = ambient> <focus on voice> <level>
		if (payload.size() < 7 || payload[1] != NC_ASM_INQUIRED_TYPE_V2)
		{
			return false;
		}
		auto type = static_cast<COMMAND_TYPE>(payload[0]);
		if (type != COMMAND_TYPE::NCASM_RET_PARAM && type != COMMAND_TYPE::NCASM_NTFY_PARAM)
		{
			return false;
		}

		if (payload[3] == 0)
		{
			mode = NC_ASM_MODE_V2::OFF;
		}
		else
		{
			mode = payload[4] == 1 ? NC_ASM_MODE_V2::AMBIENT_SOUND : NC_ASM_MODE_V2::NOISE_CANCELLING;
		}
		focusOnVoice = payload[5] == 1;
		//The headphones remember the ambient level in every mode; keep it in the slider's range
		asmLevel = static_cast<unsigned char>(payload[6]);
		asmLevel = asmLevel < 1 ? 1 : (asmLevel > MAX_ASM_LEVEL_V2 ? MAX_ASM_LEVEL_V2 : asmLevel);
		return true;
	}

}

