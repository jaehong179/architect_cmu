# Deployment View

The scope is the physical deployment of the TimeGrapher application across three target platforms.
The diagram shows the hardware devices, execution environments, deployed artifacts, and communication paths for each platform.

TimeGrapher is a standalone desktop application with no network dependencies. The same codebase compiles to platform-specific artifacts, with platform-specific audio and camera drivers abstracted behind common interfaces in the `audio/capture` and (planned) `vision` packages.

![Deployment View](../images/deploymentView.jpg)

Notation: UML

## Element Catalog

#### Windows PC x86-64
- Target platform for primary development and end-user deployment. Runs Windows 10/11 with Qt 6 Runtime. Audio input via WASAPI, camera input via DirectShow.

#### Raspberry Pi arm64
- Lightweight embedded deployment target. Runs Raspberry Pi OS with Qt6 and ALSA. Audio input via ALSA (`libasound.so`), camera input via V4L2.

#### macOS Apple Silicon
- Additional desktop deployment target. Runs macOS with Qt 6 Framework. Audio input via CoreAudio, camera input via AVFoundation.

#### USB Mic / USB Audio Device
- External USB microphone that captures the mechanical watch's tick sound as a PCM audio stream. Platform-specific audio driver (WASAPI / ALSA / CoreAudio) provides the communication path to the application.

#### USB Camera
- External USB camera that captures video frames of the watch for position detection.

#### TimeGrapher.exe / TimeGrapher (ELF) / TimeGrapher.app Bundle
- The main application artifact. Same codebase compiled for each platform's architecture (x86-64, arm64) and packaging format (.exe, ELF binary, .app bundle).

#### Qt6*.dll / libQt6*.so / Qt6*.framework
- Qt 6 runtime libraries. Packaging format differs per platform (DLL, shared object, framework).

#### libasound.so
- ALSA audio library, present only on the Raspberry Pi deployment. Windows and macOS use OS-native audio APIs that do not require a separate shared library.

#### perf_log.csv
- Performance instrumentation output file. Only generated when the application is built with `PERF_ENABLE=1`. Not present in production builds.

## Behavior
- N/A.

## Related ADRs
- N/A

## Related Views
- N/A
