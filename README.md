<h1 align="center">Sony Headphones Client (XM5 edition)</h1>

<p align="center">
  A desktop app for controlling Sony headphones from your PC, now with WH-1000XM5 support.<br/>
  Switch noise cancelling, change the EQ and check the battery without picking up your phone.
</p>

<p align="center">
  <img width="400" src="static/screenshot.png" alt="Screenshot of the app connected to a WH-1000XM5"/>
</p>

> [!WARNING]
> This program is not affiliated with Sony. It talks to your headphones using a reverse-engineered protocol, and you're responsible for any damage that may occur while using it.

## Features

Everything here has been tested on a WH-1000XM5 (firmware 2.5.1) on Windows.

- Switch between noise cancelling, ambient sound and off, and set the ambient level (1 to 20) and Focus on Voice
- Pick any equalizer preset (Bright, Excited, Mellow, Relaxed, Vocal, Treble Boost, Bass Boost, Speech and so on), or set Clear Bass and the 5 bands yourself
- See the battery level, which refreshes every minute, and whether it's charging
- See the model, firmware version and the Bluetooth codec in use (SBC, AAC or LDAC)
- Turn Speak-to-Chat, pause when taken off and DSEE Extreme on or off, choose between sound quality and a stable connection, and set auto power off
- When it connects, the app shows the settings your headphones already have, and it keeps up when you press the button on the earcup
- On Windows there's a tray icon: hover it for battery and mode, or right-click to switch modes. Closing the window leaves the app running in the tray
- If you only have one pair of Sony headphones paired, it connects to them by itself

Adaptive Sound Control, multipoint and touch control settings aren't supported yet.

## Supported headphones

| Headphones | Status |
|---|---|
| WH-1000XM5 | Tested, everything above works |
| WH-1000XM4 | Untested. It's reported to use the same newer protocol, so it will probably work |
| WH-1000XM3 and older | Open Advanced and untick "XM4/XM5 command format" to get the original controls, including Virtual Sound |

If you try it with other Sony headphones, please open an issue and say what works.

## Download

Get the Windows zip from the [Releases page](https://github.com/android205/SonyHeadphonesClientXM5/releases/latest), unzip it anywhere and run `SonyHeadphonesClient.exe`. Nothing needs installing. The exe isn't code-signed, so Windows SmartScreen may warn you the first time; click "More info", then "Run anyway".

## Using it

1. Pair your headphones with Windows and make sure they're connected.
2. Start `SonyHeadphonesClient.exe`. With one pair of Sony headphones it connects by itself; otherwise pick yours from the list.
3. Closing the window sends it to the tray. To quit, choose Exit from the tray menu.

The app logs every message to and from the headphones in `SonyHeadphonesClient.log`, next to the exe. Attach it if you're reporting a problem; it's also how the XM5 features here were worked out.

## Building

Clone with the [Dear ImGui](https://github.com/ocornut/imgui) submodule:

```sh
git clone --recurse-submodules https://github.com/android205/SonyHeadphonesClientXM5.git
```

If you already cloned without it, run `git submodule update --init`.

### Windows

You need Visual Studio 2022 with the "Desktop development with C++" workload, which includes CMake. From a Developer Command Prompt:

```sh
cd Client
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The app ends up in `build\Release\SonyHeadphonesClient.exe`.

> If the build fails with `FileTracker` or `FTK1011` errors, your folder path is too long for MSBuild. Clone somewhere short like `C:\src`, or build with `-G Ninja` and put the build folder somewhere short.

### Linux

Debian / Ubuntu:

```sh
sudo apt install libbluetooth-dev libglew-dev libglfw3-dev libdbus-1-dev
```

Fedora:

```sh
sudo dnf install bluez-libs-devel glew-devel glfw-devel dbus-devel
```

Then build with CMake as above, leaving out the `-G` and `-A` options. I haven't tested the Linux build since the XM5 changes.

### macOS

The Xcode project in `Client/macos` hasn't been updated for this fork. It's missing the new `Log.cpp`, so it won't build as it is.

## Protocol notes (XM4/XM5)

If you want to add features, this is what's been worked out so far. Payloads go out as `DATA_MDR` (`0x0C`) messages. A reply uses the query byte plus 1, and a notification the headphones send on their own uses the query byte plus 3.

| Feature | Query | Reply / notify | Set |
|---|---|---|---|
| Protocol info | `00 00` | `01 00 …` | |
| Model / firmware | `04 01` / `04 02` | `05 01 <len> <text>` | |
| Supported functions | `06 00` | `07 00 <count> <id, value>…` | |
| Battery | `22 00` | `23 00 <percent> <charging>` / `25 00 …` | |
| Codec | `12 02` | `13 02 <codec>` (01 SBC, 02 AAC, 10 LDAC) | |
| NC / ambient | `66 17` | `67 17 …` / `69 17 01 <on> <ambient> <voice> <level>` | `68 17 01 <on> <ambient> <voice> <level>` |
| Equalizer | `56 00` | `57 00 <preset> 06 <6 bands, 0-20>` / `59 00 …` | `58 00 <preset> 00`, or `58 00 A0 06 <bands>` for custom |
| Speak-to-Chat | `F6 0C` | `F7 0C <on/off> <x>` / `F9 0C …` | `F8 0C <on/off> <x>` |
| Pause when taken off | `F6 01` | `F7 01 <on/off>` / `F9 01 …` | `F8 01 <on/off>` |
| DSEE Extreme | `D6 D1` | `D7 D1 00 <on/off>` / `D9 D1 …` | `D8 D1 00 <on/off>` |
| Connection mode | `E6 01` | `E7 01 <01 sound quality / 00 stable connection>` / `E9 01 …` | `E8 01 <mode>` |
| Auto power off | `26 05` | `27 05 <10 when taken off / 11 never> 00` / `29 05 …` | `28 05 <value> 00` |

On/off settings use `00` for on and `01` for off. To change one of these settings, send the headphones' own reply back with the first byte raised by one (`F7` becomes `F8`) and the value changed; any byte you don't understand, like `<x>` above, goes back unchanged. While Speak-to-Chat is on, the headphones also send `F5 0C …` status messages, which aren't decoded yet.

The headphones expect you to ACK every message they send straight away, and they keep resending it until you do. That's why the client reads from them on a background thread.

The settings above were worked out by recording Sony's phone app with Android's "Bluetooth HCI snoop log" developer option and decoding the result. That's the easiest way to find more.

## Credits

This is a fork of [Plutoberth/SonyHeadphonesClient](https://github.com/Plutoberth/SonyHeadphonesClient), by way of [Alpakash/SonyHeadphonesClientXM5](https://github.com/Alpakash/SonyHeadphonesClientXM5), which found the XM5's Bluetooth service ID.

Original contributors:

* [Plutoberth](https://github.com/Plutoberth): initial work and Windows version
* [Mr-M33533K5](https://github.com/Mr-M33533K5): Bluetooth interface and other help
* [semvis123](https://github.com/semvis123): macOS version
* [jimzrt](https://github.com/jimzrt): Linux version
* [guilhermealbm](https://github.com/guilhermealbm): noise cancelling switch

## License

MIT. See [LICENSE](LICENSE).
