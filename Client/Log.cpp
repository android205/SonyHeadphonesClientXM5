#include "Log.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace Log
{
	namespace
	{
		std::mutex logMtx;

		std::string timestamp()
		{
			auto now = std::chrono::system_clock::now();
			auto t = std::chrono::system_clock::to_time_t(now);
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
			std::tm tm{};
#ifdef _WIN32
			localtime_s(&tm, &t);
#else
			localtime_r(&t, &tm);
#endif
			std::ostringstream ss;
			ss << std::put_time(&tm, "%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms;
			return ss.str();
		}

		const char* dataTypeName(DATA_TYPE type)
		{
			switch (type)
			{
			case DATA_TYPE::DATA: return "DATA";
			case DATA_TYPE::ACK: return "ACK";
			case DATA_TYPE::DATA_MC_NO1: return "DATA_MC_NO1";
			case DATA_TYPE::DATA_ICD: return "DATA_ICD";
			case DATA_TYPE::DATA_EV: return "DATA_EV";
			case DATA_TYPE::DATA_MDR: return "DATA_MDR";
			case DATA_TYPE::DATA_COMMON: return "DATA_COMMON";
			case DATA_TYPE::DATA_MDR_NO2: return "DATA_MDR_NO2";
			case DATA_TYPE::SHOT: return "SHOT";
			case DATA_TYPE::SHOT_MC_NO1: return "SHOT_MC_NO1";
			case DATA_TYPE::SHOT_ICD: return "SHOT_ICD";
			case DATA_TYPE::SHOT_EV: return "SHOT_EV";
			case DATA_TYPE::SHOT_MDR: return "SHOT_MDR";
			case DATA_TYPE::SHOT_COMMON: return "SHOT_COMMON";
			case DATA_TYPE::SHOT_MDR_NO2: return "SHOT_MDR_NO2";
			case DATA_TYPE::LARGE_DATA_COMMON: return "LARGE_DATA_COMMON";
			default: return "UNKNOWN";
			}
		}
	}

	void line(const std::string& tag, const std::string& text)
	{
		std::lock_guard guard(logMtx);
		auto out = timestamp() + " [" + tag + "] " + text;
		std::cout << out << std::endl;
		std::ofstream file("SonyHeadphonesClient.log", std::ios::app);
		if (file)
		{
			file << out << '\n';
		}
	}

	void message(const std::string& tag, const CommandSerializer::Message& msg)
	{
		std::ostringstream ss;
		ss << dataTypeName(msg.dataType) << " (" << static_cast<int>(msg.dataType) << ") seq=" << static_cast<int>(msg.seqNumber);
		if (!msg.messageBytes.empty())
		{
			ss << " payload: " << CommandSerializer::toHex(msg.messageBytes);
		}
		line(tag, ss.str());
	}
}
