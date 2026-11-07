#pragma once

#include <vector>

inline constexpr auto MAX_BLUETOOTH_MESSAGE_SIZE = 2048;
inline constexpr char START_MARKER{ 62 };
inline constexpr char END_MARKER{ 60 };

inline constexpr auto MAC_ADDR_STR_SIZE = 17;

/* inline constexpr auto SERVICE_UUID = "96CC203E-5068-46ad-B32D-E316F5E069BA";
inline unsigned char SERVICE_UUID_IN_BYTES[] = { // this is the SERVICE_UUID but in bytes
	0x96, 0xcc, 0x20, 0x3e, 0x50, 0x68, 0x46, 0xad,
	0xb3, 0x2d, 0xe3, 0x16, 0xf5, 0xe0, 0x69, 0xba
}; */
inline constexpr auto SERVICE_UUID = "956C7B26-D49A-4BA8-B03F-B17D393CB6E2";
inline unsigned char SERVICE_UUID_IN_BYTES[] = { // this is the SERVICE_UUID but in bytes
	0x95, 0x6C, 0x7B, 0x26, 0xD4, 0x9A, 0x4B, 0xA8, 0xB0, 0x3F, 0xB1, 0x7D, 0x39, 0x3C, 0xB6, 0xE2
};

#define APP_NAME "Sony Headphones App v" __HEADPHONES_APP_VERSION__
#define APP_NAME_W (L"" APP_NAME)

using Buffer = std::vector<char>;

enum class DATA_TYPE : signed char
{
	DATA = 0,
	ACK = 1,
    DATA_MC_NO1 = 2,
    DATA_ICD = 9,
    DATA_EV = 10,
	DATA_MDR = 12,
    DATA_COMMON = 13,
    DATA_MDR_NO2 = 14,
    SHOT =  16,
    SHOT_MC_NO1 =  18,
    SHOT_ICD =  25,
    SHOT_EV =  26,
    SHOT_MDR =  28,
    SHOT_COMMON =  29,
    SHOT_MDR_NO2 = 30,
    LARGE_DATA_COMMON =  45,
    UNKNOWN = -1
};


enum class NC_ASM_INQUIRED_TYPE : signed char
{
	NO_USE = 0,
	NOISE_CANCELLING = 1,
	NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE = 2,
	AMBIENT_SOUND_MODE = 3
};

enum class NC_ASM_EFFECT : signed char
{
	OFF = 0,
	ON = 1,
	ADJUSTMENT_IN_PROGRESS = 16,
	ADJUSTMENT_COMPLETION = 17
};

enum class NC_ASM_SETTING_TYPE : signed char
{
	ON_OFF = 0,
	LEVEL_ADJUSTMENT = 1,
	DUAL_SINGLE_OFF = 2
};

enum class ASM_SETTING_TYPE : signed char
{
	ON_OFF = 0,
	LEVEL_ADJUSTMENT = 1
};

enum class ASM_ID : signed char
{
	NORMAL = 0,
	VOICE = 1
};

enum class NC_DUAL_SINGLE_VALUE : signed char
{
	OFF = 0,
	SINGLE = 1,
	DUAL = 2
};

enum class COMMAND_TYPE : signed char
{
	CONNECT_GET_PROTOCOL_INFO = 0,
	CONNECT_RET_PROTOCOL_INFO = 1,
	VPT_SET_PARAM = 72,
	NCASM_GET_PARAM = 102,
	NCASM_RET_PARAM = 103,
	NCASM_SET_PARAM = 104,
	NCASM_NTFY_PARAM = 105
};

//XM4/XM5 ("v2") queries and their replies, confirmed against a WH-1000XM5 (firmware 2.5.1).
//Replies are the query byte + 1; unprompted notifications are + 3.
inline constexpr char CMD_DEVICE_INFO_GET = 0x04;      //04 01 = model name, 04 02 = firmware
inline constexpr char CMD_SUPPORT_FUNCTION_GET = 0x06;
inline constexpr char CMD_CODEC_GET_V2 = 0x12;         //12 02 -> 13 02 <codec>
inline constexpr char CMD_BATTERY_GET_V2 = 0x22;       //22 00 -> 23 00 <percent> <charging>
inline constexpr char CMD_EQ_GET_V2 = 0x56;            //56 00 -> 57 00 <preset> <band count> <bands...>
inline constexpr char CMD_EQ_SET_V2 = 0x58;            //58 00 <preset> 00, or 58 00 A0 06 <bands> for Custom
//Simple device settings, worked out from a capture of Sony's app talking to a WH-1000XM5.
//They all follow one pattern: query <cmd> <subtype>, the reply is <cmd+1> <subtype> ...,
//a notification is <cmd+3> ..., and the setting is changed by sending the reply's own bytes back
//with the first byte replaced by <cmd+2> and the value byte changed.
//On/off settings use 00 = on, 01 = off.
enum class DeviceSetting : int
{
	SPEAK_TO_CHAT = 0,     //F6 0C -> F7 0C <00 on / 01 off> <unknown, kept as is>
	AUTO_PAUSE,            //F6 01 -> F7 01 <00 on / 01 off>  (pause when headphones are taken off)
	AUTO_POWER_OFF,        //26 05 -> 27 05 <value> 00
	SOUND_QUALITY_MODE,    //E6 01 -> E7 01 <01 sound quality / 00 stable connection>
	DSEE_EXTREME,          //D6 D1 -> D7 D1 00 <00 on / 01 off>
	COUNT
};

struct DeviceSettingSpec
{
	DeviceSetting id;
	char queryCmd;
	char subtype;
	size_t valueIndex;     //Position of the value byte in the reply / set payload
	const char* name;      //For the log
};

inline const DeviceSettingSpec DEVICE_SETTING_SPECS[] = {
	{ DeviceSetting::SPEAK_TO_CHAT,      (char)0xF6, 0x0C,       2, "Speak-to-Chat" },
	{ DeviceSetting::AUTO_PAUSE,         (char)0xF6, 0x01,       2, "Pause when taken off" },
	{ DeviceSetting::AUTO_POWER_OFF,     0x26,       0x05,       2, "Auto power off" },
	{ DeviceSetting::SOUND_QUALITY_MODE, (char)0xE6, 0x01,       2, "Sound quality mode" },
	{ DeviceSetting::DSEE_EXTREME,       (char)0xD6, (char)0xD1, 3, "DSEE Extreme" },
};

inline constexpr unsigned char SETTING_ON = 0x00;
inline constexpr unsigned char SETTING_OFF = 0x01;
inline constexpr unsigned char AUTO_POWER_OFF_WHEN_REMOVED = 0x10;
inline constexpr unsigned char AUTO_POWER_OFF_NEVER = 0x11;
inline constexpr unsigned char SOUND_QUALITY_PRIORITY = 0x01;
inline constexpr unsigned char STABLE_CONNECTION_PRIORITY = 0x00;

inline constexpr int EQ_BAND_COUNT = 6;                //Clear Bass + 400Hz, 1k, 2.5k, 6.3k, 16k
inline constexpr int EQ_BAND_CENTER = 10;              //Bands are sent as 0..20, meaning -10..+10

enum class AUDIO_CODEC : unsigned char
{
	UNKNOWN = 0,
	SBC = 0x01,
	AAC = 0x02,
	LDAC = 0x10,
	APTX = 0x20,
	APTX_HD = 0x21
};

//Preset IDs from community reverse engineering of Sony's app
enum class EQ_PRESET : unsigned char
{
	OFF = 0x00,
	BRIGHT = 0x10,
	EXCITED = 0x11,
	MELLOW = 0x12,
	RELAXED = 0x13,
	VOCAL = 0x14,
	TREBLE_BOOST = 0x15,
	BASS_BOOST = 0x16,
	SPEECH = 0x17,
	CUSTOM = 0xA0,
	USER_SETTING_1 = 0xA1,
	USER_SETTING_2 = 0xA2
};

//In the order the GUI's combo shows them
inline const EQ_PRESET EQ_PRESET_ARRAY[] = {
	EQ_PRESET::OFF, EQ_PRESET::BRIGHT, EQ_PRESET::EXCITED, EQ_PRESET::MELLOW, EQ_PRESET::RELAXED,
	EQ_PRESET::VOCAL, EQ_PRESET::TREBLE_BOOST, EQ_PRESET::BASS_BOOST, EQ_PRESET::SPEECH,
	EQ_PRESET::CUSTOM, EQ_PRESET::USER_SETTING_1, EQ_PRESET::USER_SETTING_2
};
inline constexpr const char* EQ_PRESET_NAMES = "Off\0Bright\0Excited\0Mellow\0Relaxed\0Vocal\0Treble Boost\0Bass Boost\0Speech\0Custom\0User Setting 1\0User Setting 2\0\0";

//XM4/XM5 ("v2") noise cancelling / ambient sound. Experimental, from community reverse engineering.
inline constexpr char NC_ASM_INQUIRED_TYPE_V2 = 0x17;
inline constexpr int MAX_ASM_LEVEL_V2 = 20;

enum class NC_ASM_MODE_V2 : signed char
{
	NOISE_CANCELLING = 0,
	AMBIENT_SOUND = 1,
	OFF = 2
};

enum class VPT_PRESET_ID : signed char
{
	OFF = 0,
	OUTDOOR_FESTIVAL = 1,
	ARENA = 2,
	CONCERT_HALL = 3,
	CLUB = 4
	//Note: Sony reserved 5~15 "for future"
};

enum class SOUND_POSITION_PRESET : signed char
{
	OFF = 0,
	FRONT_LEFT = 1,
	FRONT_RIGHT = 2,
	FRONT = 3,
	REAR_LEFT = 17,
	REAR_RIGHT = 18,
	OUT_OF_RANGE = -1
};

//Needed for converting the ImGui Combo index into the VPT index.
inline const SOUND_POSITION_PRESET SOUND_POSITION_PRESET_ARRAY[] = {
	SOUND_POSITION_PRESET::OFF,
	SOUND_POSITION_PRESET::FRONT_LEFT,
	SOUND_POSITION_PRESET::FRONT_RIGHT,
	SOUND_POSITION_PRESET::FRONT,
	SOUND_POSITION_PRESET::REAR_LEFT,
	SOUND_POSITION_PRESET::REAR_RIGHT,
	SOUND_POSITION_PRESET::OUT_OF_RANGE
};

enum class VPT_INQUIRED_TYPE : signed char
{
	NO_USE = 0,
	VPT = 1,
	SOUND_POSITION = 2,
	OUT_OF_RANGE = -1
};
