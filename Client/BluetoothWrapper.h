#pragma once

#include "IBluetoothConnector.h"
#include "CommandSerializer.h"
#include "Constants.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <thread>

//How long sendCommand waits for the headphones to ACK before giving up
inline constexpr auto ACK_TIMEOUT = std::chrono::seconds(3);

//Thread-safety: This class is thread-safe.
//While connected, a background thread reads everything the headphones send: it ACKs their
//messages straight away (so they don't resend them) and passes them to the message handler.
class BluetoothWrapper
{
public:
	using MessageHandler = std::function<void(const CommandSerializer::Message&)>;

	BluetoothWrapper(std::unique_ptr<IBluetoothConnector> connector);
	~BluetoothWrapper();

	BluetoothWrapper(const BluetoothWrapper&) = delete;
	BluetoothWrapper& operator=(const BluetoothWrapper&) = delete;

	//Only valid while disconnected
	BluetoothWrapper(BluetoothWrapper&& other) noexcept;
	BluetoothWrapper& operator=(BluetoothWrapper&& other) noexcept;

	//Sends a command and blocks until the headphones ACK it
	int sendCommand(const std::vector<char>& bytes);

	bool isConnected() noexcept;
	//Try to connect to the headphones
	void connect(const std::string& addr);
	void disconnect() noexcept;

	//Called from the reader thread for every non-ACK message from the headphones.
	//Set it before connecting.
	void setMessageHandler(MessageHandler handler);

	//If the reader thread lost the connection, returns true once and gives the reason
	bool takeConnectionError(std::string& reason);

	std::vector<BluetoothDevice> getConnectedDevices();

private:
	void _readerLoop();
	//Blocks until one complete message has been received. Only called from the reader thread.
	CommandSerializer::Message _readMessage();
	void _sendAck(unsigned char seqNumber);
	void _stopReader() noexcept;

	std::unique_ptr<IBluetoothConnector> _connector;
	MessageHandler _handler;
	Buffer _recvBuffer;

	//Serializes whole commands (send + wait for ACK), so only one is in flight
	std::mutex _commandMtx;
	//Serializes writes to the socket (commands from the GUI, ACKs from the reader)
	std::mutex _sendMtx;

	//ACK bookkeeping, shared between sendCommand and the reader thread
	std::mutex _ackMtx;
	std::condition_variable _ackCv;
	unsigned int _acksReceived = 0;
	unsigned int _seqNumber = 0;
	bool _readerRunning = false;

	std::thread _reader;
	std::mutex _errorMtx;
	std::string _connectionError;
	bool _hasConnectionError = false;
};
