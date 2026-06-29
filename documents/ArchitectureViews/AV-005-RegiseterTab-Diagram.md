# Graph Tab Hierarchy

This view shows the design that makes adding graph tabs to TimeGrapher easy. It satisfies [QAS-08](../Requirements/quality-attribute-requirements.md#qas-08--new-tab-extensibility): adding a graph tab must not require modifying unrelated code. To add a tab, a developer implements one new class that inherits the `TabView` abstract base class and registers it with one `TabManager::registerTab()` call (OCP).

![Diagram](../images/classDiagram.jpg)

## Element Catalog

#### TabView
- An **abstract base class** derived from `QWidget` (not a pure interface — Qt disallows multiple `QObject` inheritance, so a `QWidget`-derived abstract class is used). Declares the contract every tab implements: `tabTitle()` (pure virtual) plus the virtual hooks `onWave()`, `onMeasurement()`, and `onResetSession()`. A concrete tab overrides the hooks it needs.


#### Concrete tabs (×13)
- RateScope, SoundPrint, TraceDisplay, Spectrogram, etc. Each inherits `TabView` and renders the signal in `onWave()` / `onMeasurement()`. No existing tab is touched when a new one is added.
  
#### TabManager
- Holds the list of registered tabs (`registerTab()`) and pushes data to all of them (`broadcastWave()` / `broadcastMeasurement()` / `broadcastReset()`).
  
### Observer pattern
The data fan-out applies the **Observer** pattern:
- **Subject / Observable** = `TabManager` — keeps the list of registered tabs and notifies them on each new data block.
- **Observers** = `TabView` and its concrete subclasses — register via `registerTab()` and react in `onWave()` / `onMeasurement()`.
- **Mechanism**: not Qt signals/slots. `TabManager` iterates its tab list and calls each tab's `onWave()` / `onMeasurement()` directly, so it broadcasts to all tabs without knowing the concrete types or the data source. (A separate `WaveSink` observer list receives `onWave` for non-visual listeners such as the history buffer.)


## Behavior

The sequence diagram below shows tab registration: a new tab is added simply by calling `TabManager::registerTab()`.

![Sequence Diagram](../images/RegisterTab_SequenceDiagram.png)


The runtime broadcast side — `broadcastWave()` calling `onWave()` on every registered tab — is the same `onWave` interaction documented in [AV-003](./AV-003-MicToGraph-SeqenceDiagram.md); see that view for the runtime/threading context.


## Related ADRs
[ADR-003: Register display tabs through a Tab Manager](../ADRs/ADR-003-register-display-tabs.md)

## Related Views
- [AV-003: Live Microphone to Graph Runtime View](./AV-003-MicToGraph-SeqenceDiagram.md) — shares the `onWave` broadcast interaction; AV-003 covers the runtime/thread context, this view covers the static tab hierarchy and registration.
- [AV-002: Top-Level Module Uses View](./AV-002-TimeGrapher-module-view.md) — the `ui/tabs` module that contains these classes.
