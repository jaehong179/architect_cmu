# ADR-004: Share a single WaveLodHistory instance across all display tabs

We decided to maintain one centralized WaveLodHistory buffer in MainWindow and inject it into every visualization tab via pointer, so that the 8-minute waveform history is stored exactly once and all tabs read from the same source.

***Decision***

We keep a single WaveLodHistory instance (`mWaveHistory`) as a member of MainWindow. MainWindow registers it as a WaveSink on the TabManager broadcast bus so it receives every WaveBlock produced by the DSP pipeline. During tab registration, MainWindow calls `setHistory(&mWaveHistory)` on each tab that needs scroll-back or seek-replay capability. Tabs hold a non-owning pointer and query the shared buffer on demand — they never copy or duplicate its contents.

The alternative considered was giving each tab its own 8-minute history buffer. Under that design every tab would independently accumulate the full envelope, raw PCM, LOD pyramid, and event list.

***Rationale***

WaveLodHistory stores a full-resolution ring of the most recent 480 seconds of envelope and raw PCM data. At 48 kHz this amounts to approximately 23 million samples per ring, consuming roughly 92 MB for the envelope ring alone plus another 92 MB for the raw PCM ring — about 184 MB total before the LOD pyramid (~26 MB) is added. Seven tabs currently use the history for scroll-back and seek-replay (TabRateScope, TabBeatNoiseScope, TabEscapementAnalyzer, TabSpectrogram, TabWaveformCompare, TabSyncSweepScope, TabFilterViews). If each tab held its own copy, the system would require 7 × ~210 MB ≈ 1.47 GB of memory solely for history buffers. On the Raspberry Pi 5 with 4–8 GB of total RAM, this would likely exhaust available memory and violate [QAS-03](../04-quality-attribute-requirements.md#qas-03-long-run-resource-stability) (memory increase ≤ 200 MB over 30 minutes).

A single shared instance keeps the history footprint at ~210 MB regardless of how many tabs consume it. All tabs see exactly the same data, so scroll-back and seek-replay produce consistent views across tabs without synchronization logic. The `setHistory()` injection follows the same dependency-inversion pattern established by ADR-003's `registerTab()`: tabs depend on an abstract capability (the history interface), not on each other or on MainWindow internals.

The WaveSink interface (`onWave()`) ensures the buffer receives data through the same broadcast mechanism that feeds every tab's live WaveBuffer, adding no new data paths or coupling to the DSP modules.

***Status***

Accepted

***Consequences***

Positive

- Memory footprint stays bounded at ~210 MB for history regardless of tab count, satisfying [QAS-03](../04-quality-attribute-requirements.md#qas-03-long-run-resource-stability) and mitigating [RISK-01](../06-risk-management.md#risk-01).
- All tabs observe identical history data, eliminating divergence bugs where tabs would show different waveforms for the same seek position.
- Adding a new tab that needs scroll-back requires only one `setHistory()` call in MainWindow — no buffer duplication, no new memory allocation.
- Consistent with the dependency-inversion structure of ADR-003: tabs depend on the shared interface, not on each other.

Negative / costs

- All tabs share a single writer's timeline; if a tab needed a different history length or sample rate, the shared buffer could not accommodate it without extension.
- The non-owning pointer requires that MainWindow outlives every tab — a lifetime constraint enforced by construction order but not by the type system.
- Read contention is theoretically possible if a tab's `queryWindow()` or `replayInto()` overlaps with the push path, though in practice both run on the GUI thread so no data race occurs.
