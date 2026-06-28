# Top-Level Module Uses View (Package Diagram)

The scope is the static code structure of the TimeGrapher application.
The diagram shows seven top-level packages and their `«uses»` dependencies.

Key pattern applied:
- **Layered** — dependencies flow top-down (ui → engine → core, ui → audio → core). No upward or circular dependencies are permitted. watchdog is a cross-cutting exception, referenced by all layers for system health monitoring.
  
![Package Diagram](../images/TimeGrapherModuleView.jpg)


## Element Catalog

#### ui
- User interface layer. Contains MainWindow (thin coordinator for widget wiring and low-frequency display updates) and TabManager (Publish–Subscribe hub that broadcasts data to display tabs without them knowing the data source).

#### ui/tabs
- TabView abstract interface and 13 concrete subclasses (RateScope, SoundPrint, TraceDisplay, Spectrogram, etc.). Adding a new tab requires only 1 subclass + 1 registration line — no changes to existing code (OCP).

#### engine
- Domain orchestration. CaptureController acts as a Facade over audio sources, DSP pipeline, and measurement calculation. MeasurementEngine computes rate, beat error, and amplitude from detected events.

#### vision
- Watch position detection via USB camera. Classifies the watch's current position among 6 standard positions (DU, DD, CU, CD, CR, CL). The detected position is passed to engine for correlation with timing measurements (Modifiability: Increase cohesion — vision logic is isolated from audio and engine).

#### render
- Folding sound image pixel rendering, separated from the Qt widget display layer.

#### audio
- Audio input sources and thread-shared ring buffer. Each Worker runs on a dedicated thread (Producer–Consumer pattern with the ring buffer).

#### audio/capture
- Live microphone capture worker with platform-specific backends (LinuxAudio, WindowsAudio).

#### audio/playback
- WAV file replay worker.

#### audio/recording
- WAV file reader and writer. Extracted from MainWindow for cohesion.

#### audio/sim
- Synthetic watch signal generator for testing and demonstration.

#### core
- Pure domain logic with no Qt/UI dependency. Contains the signal processing pipeline arranged as a Pipe-and-Filter (HPF → Envelope → Detector → BPH Tracker). Independently unit-testable.

#### core/detection
- FSM-based onset/peak detector that classifies A (unlock) and C (drop) events from the envelope signal.

#### core/dsp
- Independent signal processing filters (HPF, Envelope). Each filter is individually replaceable.

#### core/stats
- Rolling statistical utilities (RollingAverage, RollingLeastSquares) for stable trend calculation.

#### core/timing
- Timegrapher C library that drives the signal detection pipeline, and BPH tracker for beat-rate estimation.

#### watchdog
- Cross-cutting system health monitoring. Detects microphone disconnect and camera feed loss. Triggers recovery actions (e.g., notify UI) when anomalies are detected (Availability: Fault detection).

## Behavior
- N/A

## Related ADRs
[ADR-002: Adopt a watchdog (timeout) for microphone-disconnect detection](../ADRs/ADR-002-Adopt%20a%20watchdog%20(timeout)%20for%20microphone-disconnect%20detection.md)

## Related Views
- TBD
