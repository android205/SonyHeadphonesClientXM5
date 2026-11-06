#pragma once

#include "CommandSerializer.h"
#include <string>

//Protocol traffic log. Every line goes to the console and to SonyHeadphonesClient.log
//in the working directory, so replies from new headsets (e.g. the XM5) can be studied.
namespace Log
{
	void line(const std::string& tag, const std::string& text);
	void message(const std::string& tag, const CommandSerializer::Message& msg);
}
