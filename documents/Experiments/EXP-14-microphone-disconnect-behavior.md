# EXP-14 / Experiment for [[RISK-19](../README.md#risk-19)][[QAS-11](../README.md#qas-11-microphone-disconnect-user-notification)]: Characterize current behavior on microphone (USB) disconnect

## Objective

- Determine what the current system actually does when the measurement microphone (USB audio input) is disconnected while running in Live mode — i.e., how the GUI behaves — so the right disconnect-detection and notification tactic can be chosen.

## Status

- [Planned | In progress | Suspended | Canceled | **Concluded**]

## Expected outcomes

- A documented current-state behavior for each disconnect case: (a) how the GUI reacts (freezes, keeps last value, shows nothing, or errors), (b) measured time until any visible change, and (d) whether capture/measurement auto-recovers on reconnect. This becomes the baseline and the detection-signal basis for [QAS-11](../README.md#qas-11-microphone-disconnect-user-notification) / [EXP-06](EXP-06-fault-handling-feedback-verification.md).

## Resources required

- Raspberry Pi 5 + the timegrapher USB microphone; TimeGrapher GUI in Live mode; a stopwatch to time the GUI reaction. No new code (observation only).

## Experiment description

- Run the TimeGrapher in Live mode capturing a watch signal.

- Observe the GUI: does it freeze, keep displaying the last value, show nothing, or display an error? Record the time from unplug to any visible change.

- Reconnect the microphone and observe whether capture/measurement resumes automatically or requires a restart.

## Duration

06/15-06/16

## Links and references

[QAS-01](../README.md#qas-01-real-time-streaming-throughput) · [RISK-19](../README.md#risk-19) · [EXP-06](EXP-06-fault-handling-feedback-verification.md)

## Results and recommendations

- The GUI froze within 0.1ms, keeping the last displayed value.

- No specific error message or indicator was shown.

- The system did not automatically recover when the microphone was reconnected.
