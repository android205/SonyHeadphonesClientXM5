#include "BluetoothWrapper.h"
#include "Log.h"
#include <algorithm>

BluetoothWrapper::BluetoothWrapper(std::unique_ptr<IBluetoothConnector> connector)
{
	this->_connector.swap(connector);
}

BluetoothWrapper::~BluetoothWrapper()
{
	if (this->_connector)
	{
		this->disconnect();
	}
}

BluetoothWrapper::BluetoothWrapper(BluetoothWrapper&& other) noexcept
{
	this->_connector.swap(other._connector);
	this->_seqNumber = other._seqNumber;
	this->_handler = std::move(other._handler);
}

BluetoothWrapper& BluetoothWrapper::operator=(BluetoothWrapper&& other) noexcept
{
	//self assignment
	if (this == &other) return *this;

	this->_connector.swap(other._connector);
	this->_seqNumber = other._seqNumber;
	this->_handler = std::move(other._handler);

	return *this;
}

int BluetoothWrapper::sendCommand(const std::vector<char>& bytes)
{
	std::lock_guard commandGuard(this->_commandMtx);

	std::unique_lock ackLock(this->_ackMtx);
	if (!this->_readerRunning)
	{
		throw RecoverableException("Not connected to the headphones", true);
	}
	auto seq = this->_seqNumber;
	auto acksBefore = this->_acksReceived;
	ackLock.unlock();

	Log::line("SEND", "seq=" + std::to_string(seq) + " payload: " + CommandSerializer::toHex(bytes));
	auto data = CommandSerializer::packageDataForBt(bytes, DATA_TYPE::DATA_MDR, seq);
	int bytesSent;
	{
		std::lock_guard sendGuard(this->_sendMtx);
		bytesSent = this->_connector->send(data.data(), data.size());
	}

	ackLock.lock();
	bool acked = this->_ackCv.wait_for(ackLock, ACK_TIMEOUT, [&] {
		return this->_acksReceived != acksBefore || !this->_readerRunning;
	});
	if (this->_acksReceived == acksBefore)
	{
		throw RecoverableException(acked ? "Lost the connection to the headphones" : "The headphones didn't respond", true);
	}

	return bytesSent;
}

bool BluetoothWrapper::isConnected() noexcept
{
	return this->_connector->isConnected();
}

void BluetoothWrapper::connect(const std::string& addr)
{
	{
		std::lock_guard commandGuard(this->_commandMtx);
		this->_stopReader();

		Log::line("INFO", "Connecting to " + addr);
		this->_recvBuffer.clear();
		{
			std::lock_guard errorGuard(this->_errorMtx);
			this->_hasConnectionError = false;
		}
		this->_connector->connect(addr);
		Log::line("INFO", "Connected");

		std::lock_guard ackGuard(this->_ackMtx);
		this->_seqNumber = 0;
		this->_readerRunning = true;
		this->_reader = std::thread(&BluetoothWrapper::_readerLoop, this);
	}

	//Sony's app opens with a protocol info request; newer headsets may only start
	//reporting their state after it.
	this->sendCommand(CommandSerializer::serializeProtocolInfoRequest());
	//Ask for the current noise cancelling / ambient state in the XM4/XM5 format
	this->sendCommand(CommandSerializer::serializeNcAsmQueryV2());

	//Model, firmware, battery, codec, equalizer
	for (const auto& query : CommandSerializer::connectQueriesV2())
	{
		this->sendCommand(query);
	}
}

void BluetoothWrapper::disconnect() noexcept
{
	Log::line("INFO", "Disconnecting");
	//Closing the socket wakes the reader thread up, and anyone waiting on an ACK
	this->_connector->disconnect();
	this->_stopReader();
	std::lock_guard ackGuard(this->_ackMtx);
	this->_seqNumber = 0;
}

void BluetoothWrapper::setMessageHandler(MessageHandler handler)
{
	this->_handler = std::move(handler);
}

bool BluetoothWrapper::takeConnectionError(std::string& reason)
{
	std::lock_guard errorGuard(this->_errorMtx);
	if (!this->_hasConnectionError)
	{
		return false;
	}
	this->_hasConnectionError = false;
	reason = this->_connectionError;
	return true;
}

std::vector<BluetoothDevice> BluetoothWrapper::getConnectedDevices()
{
	return this->_connector->getConnectedDevices();
}

void BluetoothWrapper::_stopReader() noexcept
{
	if (this->_reader.joinable() && this->_reader.get_id() != std::this_thread::get_id())
	{
		this->_reader.join();
	}
	else if (this->_reader.joinable())
	{
		this->_reader.detach();
	}
}

void BluetoothWrapper::_readerLoop()
{
	try
	{
		while (true)
		{
			auto msg = this->_readMessage();
			Log::message("RECV", msg);

			if (msg.dataType == DATA_TYPE::ACK)
			{
				std::lock_guard ackGuard(this->_ackMtx);
				this->_seqNumber = msg.seqNumber;
				this->_acksReceived++;
				this->_ackCv.notify_all();
				continue;
			}

			//The headphones are telling us something: ACK it right away so they don't resend it
			this->_sendAck(msg.seqNumber);
			if (this->_handler)
			{
				this->_handler(msg);
			}
		}
	}
	catch (const std::exception& e)
	{
		//Expected when we disconnect on purpose; otherwise the GUI is told via takeConnectionError
		if (this->_connector->isConnected())
		{
			Log::line("INFO", std::string("Connection lost: ") + e.what());
			std::lock_guard errorGuard(this->_errorMtx);
			this->_connectionError = e.what();
			this->_hasConnectionError = true;
		}
	}

	std::lock_guard ackGuard(this->_ackMtx);
	this->_readerRunning = false;
	this->_ackCv.notify_all();
}

CommandSerializer::Message BluetoothWrapper::_readMessage()
{
	while (true)
	{
		//Look for a complete <START_MARKER>...<END_MARKER> frame in what we've received so far
		auto start = std::find(this->_recvBuffer.begin(), this->_recvBuffer.end(), START_MARKER);
		if (start != this->_recvBuffer.end())
		{
			auto end = std::find(start + 1, this->_recvBuffer.end(), END_MARKER);
			if (end != this->_recvBuffer.end())
			{
				Buffer frame(start + 1, end);
				this->_recvBuffer.erase(this->_recvBuffer.begin(), end + 1);
				return CommandSerializer::unpackBtMessage(frame);
			}
			//Drop any garbage before the start marker
			this->_recvBuffer.erase(this->_recvBuffer.begin(), start);
		}
		else
		{
			this->_recvBuffer.clear();
		}

		char buf[MAX_BLUETOOTH_MESSAGE_SIZE];
		auto numRecvd = this->_connector->recv(buf, sizeof(buf));
		if (numRecvd <= 0)
		{
			throw RecoverableException("The headphones closed the connection", true);
		}
		this->_recvBuffer.insert(this->_recvBuffer.end(), buf, buf + numRecvd);
	}
}

void BluetoothWrapper::_sendAck(unsigned char seqNumber)
{
	//The headphones expect an empty ACK carrying the next sequence number
	auto ack = CommandSerializer::packageDataForBt({}, DATA_TYPE::ACK, 1 - seqNumber);
	std::lock_guard sendGuard(this->_sendMtx);
	this->_connector->send(ack.data(), ack.size());
}
