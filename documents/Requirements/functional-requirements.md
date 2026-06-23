# **3. Requirements Analysis**
This section identifies and prioritizes functional requirements extracted from the source PDF.

## **3.1 Priority Decision Tree**

The Priority column of each FR is determined by the following 4-step decision tree.

<img src="../images/image2.png" style="width:5.375in;height:6.5in" />

## 

## **3.2 Functional Requirements**

#### **▸ Measurement Summary Bar \[MSB\]**

| **ID**       | **Requirements**                                                                                                                                                        | **Source**                                       | **Pri** |
|--------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------|---------|
| <a id="fr-msb-1"></a>**FR-MSB-1** | The system shall compute and display, the rate (s/d), amplitude (deg), beat error (ms), and beat rate (BPH) through a measurement summary bar at the top of the screen. | p.6 *Current Features / Measurement Summary Bar* | Low     |

#### **▸ Rate/Scope Tab \[RST\]**

| **ID**         | **Requirements**                                                                                                                                                                                                      | **Source**                                            | **Pri** |
|----------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------|---------|
| **FR-RST-1**   | The system shall provide a Tabbed Graph Panel as the primary visual-analysis area of the GUI. New graphs shall be implemented as new tabs.                                                                            | p.6 *Current Features / Tabbed Graph Panel*           | Low     |
| **FR-RST-2 ▼** | The system shall display, at the top of the Rate/Scope tab, the Rate Error graph (tic·tac lines) together with the Amplitude graph.                                                                                   | p.6 *Current Features / Rate/Scope Tab*               | Low     |
| **FR-RST-2.1** | The system shall visualize, in the Rate Error graph, the watch running fast/slow from the slope of the two tic·tac lines, and the beat-error trend from the gap between the two lines.                                | p.6 *Current Features / Rate/Scope Tab*               | Low     |
| **FR-RST-2.2** | The system shall mark the A-event onset with a green dotted line and the C-event peak with a red dotted line in the Amplitude graph and display the A-onset-to-C-peak interval in milliseconds (ms) above the C peak. | p.6 *Current Features / Rate/Scope Tab*               | Low     |
| **FR-RST-2.3** | The system shall display the interval between consecutive A events in the Amplitude graph as a double-ended arrow between green dotted lines.                                                                         | p.6 *Current Features / Rate/Scope Tab*               | Low     |
| **FR-RST-3**   | The system shall provide zoom in/out of the waveform, backward/forward movement in time over the captured signal and pause/resume in the Amplitude graph.                                                             | p.7 *Current Features / Rate/Scope Tab*               | Low     |
| **FR-RST-4**   | The system shall be able to display the raw watch sound waveform in the Rate graph and the Sound Print tab.                                                                                                           | p.12 *Expected Enhancements / Rate/Scope Enhancement* | Medium  |

#### **▸ Sound Print Tab \[SPT\]**

| **ID**       | **Requirements**                                                                                                                                                                                  | **Source**                                             | **Pri** |
|--------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------|---------|
| **FR-SPT-1** | The system shall display the signal in a sample-based vertically aligned view in the Sound Print tab, marking detected A events with green dots and C events with blue dots.                      | p.7 *Current Features / Sound Print Tab*               | Low     |
| **FR-SPT-2** | The system shall determine the full vertical range of the Sound Print display according to the selected sample rate. (e.g., 48,000 sps → about 8,000 samples, 192,000 sps → about 32,000 samples) | p.7 *Current Features / Sound Print Tab*               | Low     |
| **FR-SPT-3** | The system shall provide visualization of timing variation in the Sound Print display.                                                                                                            | p.11 *Expected Enhancements / Sound Print Enhancement* | Medium  |
| **FR-SPT-4** | The system shall provide optional averaging over a user-selectable period in the Sound Print display.                                                                                             | p.11 *Expected Enhancements / Sound Print Enhancement* | Medium  |
| <a id="fr-spt-5"></a>**FR-SPT-5** | The system shall provide filtering in the Sound Print display that reduces ambient noise while preserving the watch sounds needed for analysis.                                                   | p.11 *Expected Enhancements / Sound Print Enhancement* | Medium  |
| **FR-SPT-6** | The system shall provide an oscilloscope-like (scope-like) view of the acoustic waveform to support detailed inspection of watch faults.                                                          | p.11 *Expected Enhancements / Sound Print Enhancement* | Medium  |
| **FR-SPT-7** | The system shall provide, in the Sound Print display, user-selectable threshold/reference-line display and pause-and-review behavior to examine prior traces by position and condition.           | p.11 *Expected Enhancements / Sound Print Enhancement* | Medium  |

#### **▸ Control Panel \[CP\]**

| **ID**       | **Requirements**                                                                                                                                                                                     | **Source**                                                                             | **Pri** |
|--------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------|---------|
| **FR-CP-1**  | The system shall provide a Gain control so the user can adjust the input signal level used for analysis.                                                                                             | p.7 *Current Features / Control Panel - Run Parameters / Gain*                         | Medium  |
| **FR-CP-2**  | The system shall provide a Live Mode to capture and analyze signal data from the microphone in real time.                                                                                            | p.7 *Current Features / Control Panel - Run Parameters / Live Mode*                    | High    |
| **FR-CP-3**  | The system shall provide a Playback Mode to use a previously recorded signal instead of live microphone input.                                                                                       | p.7 *Current Features / Control Panel - Run Parameters / Playback Mode*                | Medium  |
| <a id="fr-cp-4"></a>**FR-CP-4**  | The system shall provide a Sim Mode to generate a synthetic watch signal for testing·development.                                                                                                    | p.8 *Current Features / Control Panel - Run Parameters / Sim Mode*                     | Medium  |
| **FR-CP-5**  | The system shall provide a Refresh control to restore the Run Parameters to their initial values.                                                                                                    | p.8 *Current Features / Control Panel - Run Parameters / Refresh*                      | Low     |
| <a id="fr-cp-6"></a>**FR-CP-6**  | The system shall provide a Sample Rate control to select the sampling frequency (Hz) used for signal acquisition·processing.                                                                         | p.8 *Current Features / Control Panel - Run Parameters / Sample Rate*                  | High    |
| **FR-CP-7**  | The system shall provide an Averaging Period control to define the time window over which measurements are averaged before being displayed.                                                          | p.8 *Current Features / Control Panel - Run Parameters / Averaging Period*             | Medium  |
| **FR-CP-8**  | The system shall provide a Start control to begin signal acquisition·analysis with the currently selected settings.                                                                                  | p.8 *Current Features / Control Panel - Run Parameters / Start*                        | High    |
| **FR-CP-9**  | The system shall provide a Stop control to stop acquisition·analysis while preserving the current display state for review.                                                                          | p.8 *Current Features / Control Panel - Run Parameters / Stop*                         | High    |
| **FR-CP-10** | The system shall provide a Save control to store the current recording·measurements·display data for later review·debugging·comparison.                                                              | p.8 *Current Features / Control Panel - Run Parameters / Save*                         | Medium  |
| **FR-CP-11** | The system shall provide a Watch Parameters - BPH drop-down to select the nominal beat rate of the watch movement, and shall provide an automatic-detection option where possible.                   | p.8 *Current Features / Control Panel - Watch Parameters / BPH*                        | High    |
| **FR-CP-12** | The system shall provide a Watch Parameters - Lift Angle input/drop-down so the user can select or modify the lift angle used in the amplitude calculation.                                          | p.8 *Current Features / Control Panel - Watch Parameters / Lift Angle*                 | High    |
| **FR-CP-13** | The system shall provide a Simulation Parameters - BPH control to set the nominal beat rate of the simulated watch signal.                                                                           | p.9 *Current Features / Control Panel - Simulation Parameters / BPH*                   | Medium  |
| **FR-CP-14** | The system shall provide a Simulation Parameters - Error Rate control to set the simulated rate deviation from ideal timekeeping.                                                                    | p.9 *Current Features / Control Panel - Simulation Parameters / Error Rate*            | Medium  |
| **FR-CP-15** | The system shall provide a Simulation Parameters - Amplitude control to set the simulated balance amplitude of the generated signal.                                                                 | p.9 *Current Features / Control Panel - Simulation Parameters / Amplitude*             | Medium  |
| **FR-CP-16** | The system shall provide a Simulation Parameters - Beat Error control to set the simulated asymmetry between the tick·tock intervals.                                                                | p.9 *Current Features / Control Panel - Simulation Parameters / Beat Error*            | Medium  |
| **FR-CP-17** | The system shall provide a Simulation Parameters - Realistic option to enable a realistic simulation that introduces variability·noise·non-ideal signal characteristics.                             | p.9 *Current Features / Control Panel - Simulation Parameters / Realistic*             | Medium  |
| **FR-CP-18** | The system shall provide a Misc. Parameters - Low Pass control to set the low-pass filter that removes high-frequency noise from the signal.                                                         | p.9 *Current Features / Control Panel - Misc Parameters / Low Pass*                    | High    |
| **FR-CP-19** | The system shall provide a Misc. Parameters - High Pass control to set the high-pass filter that removes low-frequency components such as background hum, handling vibration, and slow signal drift. | p.9 *Current Features / Control Panel - Misc Parameters / High Pass*                   | High    |
| **FR-CP-20** | The system shall provide a Misc. Parameters - C Event Use Onset Amplitude control to set the amplitude threshold and sensitivity used to identify the onset of the C event.                          | p.9 *Current Features / Control Panel - Misc Parameters / C Event Use Onset Amplitude* | High    |

### 

#### **▸ Watch-Position Testing \[WPT\]**

| **ID**         | **Requirements**                                                                                                                                                                                                 | **Source**                                    | **Pri** |
|----------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------|---------|
| **FR-WPT-1 ▼** | The system shall automatically detect the watch’s current measurement position and display it in the GUI during measurement, and shall support testing a mechanical watch in all standard measurement positions. | p.13 *Expected Enhancements / Test Positions* | Medium  |
| **FR-WPT-1.1** | The system shall follow the position convention of the Witschi Chronoscope X1 (G3) manual (horizontal CH·CB, vertical 6H·9H·3H·12H, intermediate positions) and shall allow custom positions.                    | p.13 *Expected Enhancements / Test Positions* | Medium  |

#### **▸ Trace Display \[TD\]**

| **ID**      | **Requirements**                                                                                                                                                                                                                                                                                                                      | **Source**                                     | **Pri** |
|-------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------|---------|
| **FR-TD-1** | The system shall provide a Trace Display that continuously records·displays rate deviation and amplitude over time in real time. The two measurements may be presented as stacked or separate graphs.                                                                                                                                 | p.14 *Expected Enhancements / Trace Display*   | Medium  |
| **FR-TD-2** | The system shall include a smoothing function on the s/d measurement of the Trace Display so that short-term fluctuations do not make it hard to interpret.                                                                                                                                                                           | p.14 *Expected Enhancements / Trace Display*   | Medium  |
| **FR-TD-3** | The system shall alert the user when the rate indicates the watch is running late.                                                                                                                                                                                                                                                    | p.14 *Expected Enhancements / Trace Display*   | Medium  |
| **FR-TD-4** | The system shall alert the user when the amplitude falls outside the normal operating range of 270°~300°.                                                                                                                                                                                                                             | p.14 *Expected Enhancements / Trace Display*   | Medium  |
| **FR-TD-5** | The system shall include short explanatory text·labels in the GUI so the user can interpret the graph outputs.                                                                                                                                                                                                                        | p.14 *Expected Enhancements / Trace Display*   | Low     |
| **FR-TD-6** | The system shall support a long-period summary such as a daily average for the two measurements (rate·amplitude), and a rolling average that updates over time.                                                                                                                                                                       | p.14 *Expected Enhancements / Trace Display*   | Medium  |
| **FR-TD-7** | The system shall be able to compute·display derived timing measures — DiffTicTac (difference in duration between tick·tock), DiffPeriod (e.g., average difference between measured and expected beat duration over a fixed 4-second interval), and Avg Period (average difference accumulated since measurement start or last reset). | p.10 *Expected Enhancements (Chour reference)* | Medium  |

#### **▸ Rate and Amplitude Stability Over Time \[RAS\]**

| **ID**         | **Requirements**                                                                                                                                                                                                                       | **Source**                                                            | **Pri** |
|----------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------|---------|
| **FR-RAS-1 ▼** | The system shall provide a Vario Display that shows the long-term stability of rate·amplitude, and shall continuously update the minimum·maximum·average·standard deviation·elapsed measurement time·current value during measurement. | p.15 *Expected Enhancements / Rate and Amplitude Stability Over Time* | Medium  |
| **FR-RAS-1.1** | The system shall display, in the Vario Display, the acceptable min/max range as a green region, the measured min/max values as blue arrows, and the average value as a red arrow.                                                      | p.15 *Expected Enhancements / Rate and Amplitude Stability Over Time* | Medium  |

#### **▸ Multi-Position Sequence Display \[MPS\]**

| **ID**         | **Requirements**                                                                                                                                                                                 | **Source**                                                     | **Pri** |
|----------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------|---------|
| **FR-MPS-1 ▼** | The system shall provide a Sequence Display Mode that can capture·review the results of up to 10 test positions in a single sequence.                                                            | p.16 *Expected Enhancements / Multi-Position Sequence Display* | Medium  |
| **FR-MPS-1.1** | The system shall compute·display rate·amplitude·beat-error results for each position, and shall display the mean X across the sequence and the difference D between the largest·smallest values. | p.16 *Expected Enhancements / Multi-Position Sequence Display* | Medium  |
| **FR-MPS-1.2** | The system shall support comparison between vertical and horizontal positions and, where applicable, include an indicator of possible balance-wheel imbalance.                                   | p.16 *Expected Enhancements / Multi-Position Sequence Display* | Medium  |

#### **▸ Beat-Noise Scope Display \[BNS\]**

| **ID**         | **Requirements**                                                                                                                                                                                                                        | **Source**                                              | **Pri** |
|----------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------------------------------------------------------|---------|
| **FR-BNS-1 ▼** | The system shall graphically display the watch's alternating tick·tock beat noises in the Single-Beat Waveform Scope of the Scope Display Mode, supporting selectable time ranges of 20 ms·200 ms·400 ms.                               | p.17 *Expected Enhancements / Beat-Noise Scope Display* | Medium  |
| **FR-BNS-1.1** | The system shall display recent beat noises as small strips beneath the current waveform after sufficient measurement time in the Single-Beat Waveform Scope, and allow the user to select one of the prior beats for enlarged viewing. | p.17 *Expected Enhancements / Beat-Noise Scope Display* | Medium  |
| **FR-BNS-1.2** | The system shall be able to display the signal as its absolute value in the Single-Beat Waveform Scope when improved readability is needed.                                                                                             | p.17 *Expected Enhancements / Beat-Noise Scope Display* | Medium  |
| **FR-BNS-1.3** | The system shall identify the A·C beats in the Single-Beat Waveform Scope, include a visual marker for the C beat, and present the lift angle associated with the displayed beat pattern.                                               | p.17 *Expected Enhancements / Beat-Noise Scope Display* | Medium  |
| **FR-BNS-2 ▼** | The system shall display tick·tock beat noises on two horizontal axes with a fixed 20 ms time range in the Averaged Dual-Trace Scope, and provide averaging ON/OFF via a Σ control.                                                     | p.17 *Expected Enhancements / Beat-Noise Scope Display* | Medium  |
| **FR-BNS-2.1** | The system shall complete the Averaged Dual-Trace Scope measurement cycle after 50 ticks·50 tocks, and display the average amplitude on each horizontal axis at the end of the cycle.                                                   | p.17 *Expected Enhancements / Beat-Noise Scope Display* | Medium  |
| **FR-BNS-2.2** | The system shall be able to display intermediate averaging results such as after 10·20 cycles in the Averaged Dual-Trace Scope.                                                                                                         | p.17 *Expected Enhancements / Beat-Noise Scope Display* | Low     |

#### **▸ Beat Error Display and Diagnostic Trace \[BED\]**

| **ID**       | **Requirements**                                                                                                       | **Source**                                                             | **Pri** |
|--------------|------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------|---------|
| **FR-BED-1** | The system shall display the rate·amplitude·beat error·BPH values together with trace lines in the Beat Error Display. | p.18 *Expected Enhancements / Beat Error Display and Diagnostic Trace* | Medium  |

#### **▸ Long-Term Performance Graph \[LTP\]**

| **ID**       | **Requirements**                                                                                                                  | **Source**                                                 | **Pri** |
|--------------|-----------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------|---------|
| **FR-LTP-1** | The system shall record·display long-term changes in the watch's rate·amplitude·beat error through a Long-Term Performance Graph. | p.19 *Expected Enhancements / Long-Term Performance Graph* | Medium  |

#### **▸ Escapement Analyzer and Marker-Line Display \[EAM\]**

| **ID**       | **Requirements**                                                                                                                                      | **Source**                                                                 | **Pri** |
|--------------|-------------------------------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------|---------|
| **FR-EAM-1** | The system shall allow inspection of the detailed timing relationships within each watch beat through an Escapement Analyzer and Marker-Line Display. | p.20 *Expected Enhancements / Escapement Analyzer and Marker-Line Display* | Medium  |

#### **▸ Time-Frequency Spectrogram Display \[TFS\]**

| **ID**       | **Requirements**                                                                                                                                                                                           | **Source**                                                        | **Pri** |
|--------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------|---------|
| **FR-TFS-1** | The system shall provide a time-frequency spectrogram display that shows the watch's acoustic energy distribution over time·frequency with horizontal axis=time, vertical axis=frequency, color=intensity. | p.21 *Expected Enhancements / Time-Frequency Spectrogram Display* | Medium  |

#### **▸ Waveform Comparison Display with Timing Markers \[WCD\]**

| **ID**       | **Requirements**                                                                                                                                                                                                                                                                                                                                                                                | **Source**                                                                     | **Pri** |
|--------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------|---------|
| **FR-WCD-1** | The system shall support, through a waveform-comparison display that shows multiple beat waveforms in aligned lanes with vertical guide markers·envelope curves·rate/beat-error/BPH values, the user performing successive-beat comparison·landmark identification·inspection of waveform-structure change between beats. (optional) It may include degree (°) or time-based reference markers. | p.22 *Expected Enhancements / Waveform Comparison Display with Timing Markers* | Medium  |

#### **▸ Scope Mode with Synchronized Sweep Display \[SMS\]**

| **ID**         | **Requirements**                                                                                                                                                                                                                 | **Source**                                                                | **Pri** |
|----------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------|---------|
| **FR-SMS-1 ▼** | The system shall provide a Scope Mode with synchronized sweep that displays the watch's acoustic signal in real time within a fixed sweep window, using a processed signal that combines the upper·lower halves of the waveform. | p.23 *Expected Enhancements / Scope Mode with Synchronized Sweep Display* | Medium  |
| **FR-SMS-1.1** | The system shall make the Scope sweep time configurable as a multiple of the watch's tick interval, so that the beat pattern is visually stable when the watch is near its nominal rate and drifts when fast·slow.               | p.23 *Expected Enhancements / Scope Mode with Synchronized Sweep Display* | Medium  |
| **FR-SMS-1.2** | The system may also display the daily rate·amplitude·beat error·nominal beat rate from the most recent timing test in the synchronized Scope.                                                                                    | p.23 *Expected Enhancements / Scope Mode with Synchronized Sweep Display* | Low     |

#### **▸ Scope Function with Multiple Filter Views \[SFM\]**

| **ID**         | **Requirements**                                                                                                                                                                                                                    | **Source**                                                                    | **Pri** |
|----------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------|---------|
| **FR-SFM-1 ▼** | The system shall provide a Scope Function with multiple filter views, supporting four filter views F0, F1, F2, F3.                                                                                                                  | p.23 *Expected Enhancements / Scope Function with Multiple Filter Views*      | Medium  |
| **FR-SFM-1.1** | The system shall display the signal as captured in the F0 view, formatted to fit the screen and mirrored around its average value.                                                                                                  | p.23 *Expected Enhancements / Scope Function with Multiple Filter Views / F0* | Medium  |
| **FR-SFM-1.2** | The system shall apply a moving-average filter to the F0 signal in the F1 view to smooth the waveform envelope and reduce background noise.                                                                                         | p.23 *Expected Enhancements / Scope Function with Multiple Filter Views / F1* | Medium  |
| **FR-SFM-1.3** | The system shall apply, in the F2 view, rising-slope emphasis·falling-slope attenuation based on F1 so that T3 and T2 stand out.                                                                                                    | p.23 *Expected Enhancements / Scope Function with Multiple Filter Views / F2* | Medium  |
| **FR-SFM-1.4** | The system shall display, in the F3 view, only the upper portion of the signal relative to its average, bring the lower portion up, and emphasize rising edges·attenuate their falling portions to aid identification of T1 and T3. | p.23 *Expected Enhancements / Scope Function with Multiple Filter Views / F3* | Medium  |

#### **▸ AI Feature \[AI\]**

|             |                                                                                                                                                                |                                           |         |
|-------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------|---------|
| **ID**      | **Requirements**                                                                                                                                               | **Source**                                | **Pri** |
| <a id="fr-ai-1"></a>**FR-AI-1** | The system shall improve measurement quality (rate · beat error · amplitude) through a lightweight AI (recommended: TinyML) model running on the Raspberry Pi. | p.12 *Expected Enhancements / AI Feature* | Medium  |

#### **▸ System-Wide \[SYS\]**

|              |                                                                                                                                                                                                                                                                                                                |                                                 |         |
|--------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------|---------|
| **ID**       | **Requirements**                                                                                                                                                                                                                                                                                               | **Source**                                      | **Pri** |
| **FR-SYS-1** | The system shall provide status·error feedback when measurement continuity is broken — signal loss·missed beats·excessive ambient noise·out-of-range measurement — preserve the last valid measurement, and guide recovery such as watch repositioning·noise reduction·measurement restart·setting adjustment. | p.5 *Project Plan / Usability and User Purpose* | Medium  |

### 
