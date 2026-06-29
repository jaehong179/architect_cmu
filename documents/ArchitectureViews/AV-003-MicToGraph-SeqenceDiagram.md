# Live Microphone to Graph Behavior Diagram

This diagram shows the process of displaying the audio signal captured from the microphone as a graph.
Through this diagram, you can identify the parts needed to verify whether the process from audio signal to graph display satisfies the condition ([QAS-01](../Requirements/quality-attribute-requirements.md#qas-01--real-time-streaming-throughput)) of completing within 125ms per T1~T3 cycle (based on 28800 BPH).

![Diagram](../images/audio_thread_cc.png)

## Element Catalog

#### Audio source thread «Producer»
- runs the active audio worker; captures PCM and writes it to the shared ring buffer.

#### MAIN / GUI thread «Consumer»
- pulls audio, runs measurement (DSP), and updates the graph widgets.

#### WATCHDOG thread (500 ms tick) 
- periodically snapshots liveness state and raises fault events.
- 

## Behavior

- A UML Sequence Diagram representing the call process from the audio signal to its display on the TimeGrapher's graph.
- This diagram allows you to identify the components required for the [EXP-02](../Experiments/EXP-02-end-to-end-latency-measurement.md) experiment.

![Sequence Diagram](../images/MicToGraph_SeqenceDiagram.png)

## Related ADRs
- N/A

## Related Views
- N/A
