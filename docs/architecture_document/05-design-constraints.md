# **5. Design Constraints**

The following constraints apply to the whole project. All members shall be aware of and comply with them.

|           |                                                                                                                                                                           |                        |
|-----------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------|
| **ID**    | **Constraint**                                                                                                                                                            | **Source**             |
| CON-HW-01 | Runs on the CanaKit Raspberry Pi 5 Starter Kit (Pi 5, 8 GB RAM, 128 GB microSD)                                                                                           | p.26 / Raspberry Pi    |
| CON-HW-02 | 1280×800 5-inch capacitive touchscreen (HDMI + USB) environment                                                                                                           | p.27 / Touchscreen     |
| CON-HW-03 | Uses a Weishi-style timegrapher microphone input                                                                                                                          | p.28 / Time Grapher    |
| CON-SW-03 | Runs on the supplied Raspberry Pi 5 system image                                                                                                                          | p.29 / Raspberry Pi OS |
| CON-OP-01 | For consistent signal analysis and measurement, the AGC (Automatic Gain Control) of both the platform (OS) and the hardware must be turned OFF.                           | p.30 / AGC warning     |
| CON-OP-02 | Lift Angle is a user-configurable parameter                                                                                                                               | p.10 / Lift Angle note |
| CON-RF-01 | The system interface and user workflow shall be designed using the functional specification of the Witschi Chronoscope X1 G3 instruction manual as the primary reference. | p.33 / Witschi Manual  |
| CON-RF-02 | The core audio signal-processing and measurement modules shall be implemented in strict compliance with the measurement formulas defined in TimeGrapher Equations_v0.pdf. | p.33 / Equations       |
