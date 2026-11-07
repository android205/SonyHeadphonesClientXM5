#include "SingleInstanceFuture.h"
#include "BluetoothWrapper.h"
#include "Constants.h"

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

//Noise cancelling / ambient sound state as the XM4/XM5 report it
struct NcAsmStateV2
{
	NC_ASM_MODE_V2 mode = NC_ASM_MODE_V2::NOISE_CANCELLING;
	bool focusOnVoice = false;
	int asmLevel = 1;
};

//Read-only information the XM4/XM5 report
struct DeviceInfoV2
{
	std::string modelName;
	std::string firmwareVersion;
	int batteryPercent = -1;   //-1 = not reported yet
	bool charging = false;
	AUDIO_CODEC codec = AUDIO_CODEC::UNKNOWN;
};

struct EqStateV2
{
	EQ_PRESET preset = EQ_PRESET::OFF;
	std::vector<int> bands = std::vector<int>(EQ_BAND_COUNT, 0);  //-10..+10, Clear Bass first

	bool operator==(const EqStateV2& other) const { return preset == other.preset && bands == other.bands; }
};

inline constexpr auto BATTERY_REFRESH_INTERVAL = std::chrono::seconds(60);

//How long to wait for the headphones to report their state after connecting before
//sending the GUI's settings anyway
inline constexpr auto INITIAL_STATE_TIMEOUT = std::chrono::milliseconds(2000);
//Reports that arrive this soon after we sent a command may be for an older command,
//so the GUI waits this long before adopting them
inline constexpr auto REPORT_SETTLE_TIME = std::chrono::milliseconds(700);

template <class T>
struct Property {
	T current;
	T desired;

	void fulfill();
	bool isFulfilled() const;
};

class Headphones {
public:
	Headphones(BluetoothWrapper& conn);

	void setAmbientSoundControl(bool val);
	bool getAmbientSoundControl();

	bool isFocusOnVoiceAvailable();
	void setFocusOnVoice(bool val);
	bool getFocusOnVoice();

	bool isSetAsmLevelAvailable();
	void setAsmLevel(int val);
	int getAsmLevel();

	void setSurroundPosition(SOUND_POSITION_PRESET val);
	SOUND_POSITION_PRESET getSurroundPosition();

	void setVptType(int val);
	int getVptType();

	//Experimental XM4/XM5 command format
	void setUseV2Protocol(bool val);
	bool getUseV2Protocol();
	void setNcAsmModeV2(NC_ASM_MODE_V2 val);

	//Call right before connecting, so stale state from a previous connection is dropped
	void onConnecting();
	//The latest state the headphones reported, for display. False if none yet.
	bool getReportedNcAsmV2(NcAsmStateV2& out);
	//If the headphones reported a state the GUI hasn't adopted yet (e.g. you pressed the button
	//on the earcup), returns true once with that state and marks it as already applied.
	bool takeExternalNcAsmV2(NcAsmStateV2& out);

	DeviceInfoV2 getDeviceInfoV2();

	//Equalizer. Like the NC/ambient state, the GUI adopts what the headphones report.
	void setEqV2(const EqStateV2& val);
	bool hasReportedEqV2();
	bool takeExternalEqV2(EqStateV2& out);

	//Simple device settings (Speak-to-Chat, auto power off, ...), same adopt-what-the-headphones-report rules.
	//Values are the raw bytes the headphones use (see DeviceSetting in Constants.h).
	void setDeviceSetting(DeviceSetting id, unsigned char value);
	bool hasReportedDeviceSetting(DeviceSetting id);
	bool takeExternalDeviceSetting(DeviceSetting id, unsigned char& out);
	
	bool isChanged();
	void setChanges();
private:
	using Clock = std::chrono::steady_clock;

	void _handleMessage(const CommandSerializer::Message& msg);
	bool _isNcAsmV2Changed();
	bool _isVptChanged();
	bool _isEqV2Changed();
	void _setChangesV2();
	bool _isBatteryRefreshDue();

	DeviceInfoV2 _deviceInfo;
	Clock::time_point _lastBatteryQuery;
	EqStateV2 _reportedEq;
	bool _hasReportedEq = false;
	unsigned int _eqReportCount = 0;
	unsigned int _appliedEqReportCount = 0;
	bool _awaitingInitialEq = true;
	Clock::time_point _lastEqSendTime;
	Property<EqStateV2> _eqV2;

	struct DeviceSettingState
	{
		Buffer lastReport;             //Raw reply, used as the template when changing the setting
		unsigned char reported = 0;
		bool hasReport = false;
		unsigned int reportCount = 0;
		unsigned int appliedReportCount = 0;
		bool awaitingInitial = true;
		Clock::time_point lastSendTime;
		Property<unsigned char> value = { 0, 0 };
	};
	bool _isDeviceSettingChanged(const DeviceSettingState& state);
	DeviceSettingState _settings[static_cast<int>(DeviceSetting::COUNT)];

	NcAsmStateV2 _reportedNcAsmV2;
	bool _hasReportedNcAsmV2 = false;
	unsigned int _reportCount = 0;
	unsigned int _appliedReportCount = 0;
	bool _awaitingInitialState = false;
	Clock::time_point _connectTime;
	Clock::time_point _lastSendTime;

	Property<bool> _ambientSoundControl = { 0 };
	Property<bool> _focusOnVoice = { 0 };
	Property<int> _asmLevel = { 0 };
	Property<SOUND_POSITION_PRESET> _surroundPosition = { SOUND_POSITION_PRESET::OUT_OF_RANGE, SOUND_POSITION_PRESET::OFF };
	Property<int> _vptType = { 0 };
	Property<bool> _useV2Protocol = { false };
	Property<NC_ASM_MODE_V2> _ncAsmModeV2 = { NC_ASM_MODE_V2::NOISE_CANCELLING, NC_ASM_MODE_V2::NOISE_CANCELLING };
	std::mutex _propertyMtx;

	BluetoothWrapper& _conn;
};

template<class T>
inline void Property<T>::fulfill()
{
	this->current = this->desired;
}

template<class T>
inline bool Property<T>::isFulfilled() const
{
	return this->desired == this->current;
}
