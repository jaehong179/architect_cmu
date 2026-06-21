# EXP-12 / Experiment for [FR-POS-1]: USB Protocol Analysis for Automated Watch Position Detection

## Objective

Investigate the USB communication protocol of the connected Timegrapher hardware to verify if the physical position state data of the watch is transmitted alongside the audio stream, thereby determining the implementation complexity of the automated position detection feature.

## Status

- [Planned | In progress | Suspended | Canceled | **Concluded**]

## Expected outcomes

- Clear confirmation (Yes/No) of whether watch position information is embedded in the USB data stream.

- If supported, define the specific registers or byte sequences representing each position (e.g., Dial Up/Down, Crown Up/Down/Left/Right).

- If not supported, formulate an architectural alternative (e.g., manual GUI input, audio-based heuristic inference, or secondary sensor integration).

## Resources required

- Target Timegrapher hardware (microphone stand and main unit).

- Raspberry Pi 5 with USB interface.

- Development environment for custom USB/HID communication scripts (e.g., Python with PyUSB or C-based HIDAPI).

- Standard HID peripherals (e.g., mouse, keyboard) for script validation.

## Experiment description

- Inspect the USB descriptors of the connected Timegrapher hardware to identify available endpoints (confirming the presence of both Audio and HID interfaces)

- Develop a custom script to directly read data payloads from the identified HID endpoint.

- Validate the reliability of the custom script by connecting standard HID input devices (mouse/keyboard) and verifying that input data is correctly received and parsed.

- Connect the Timegrapher microphone hardware, execute the validated script, and manually rotate the physical position of the watch stand through various states.

- Monitor the script's output in real-time to determine if any position-related data packets are generated and transmitted by the hardware.

## Duration

06/03–06/04

## Links and references

[RISK-15](../06-risk-management.md#risk-15)

## Results and recommendations

- USB packet analysis revealed the presence of a standard HID (Human Interface Device) endpoint in addition to the primary audio streaming interface.

- A custom script was developed to read data from this HID endpoint. The script's functionality was successfully validated by connecting standard HID peripherals (mouse/keyboard) and confirming stable data reception.

- However, when the target watch microphone device was connected and physically rotated through various positions, absolutely no data was transmitted via the HID endpoint.

- Conclusion: The watch microphone hardware does not transmit watch position data over the USB connection.

- Since automated position detection via hardware is confirmed to be unsupported, we must activate the fallback plan for [RISK-18](../06-risk-management.md#risk-18).

- Update the software architecture and UI/UX design to implement a manual position selection feature via the GUI, allowing users to specify the position (e.g., Dial Up, Crown Left) before initiating the measurement.
