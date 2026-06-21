# **1. Overview**

## **1.1 Introduction**

### **Purpose**

This document is the Milestone 1 deliverable of the Time Grapher Project for the LG 2026 SW Architecture Studio. It covers a five-week time-boxed project that extends the baseline Qt/C++ TimeGrapher GUI on the Raspberry Pi 5 to strengthen the acoustic diagnosis and visualization of a mechanical watch. This document includes (i) an Agile/Scrum-based project plan, (ii) 12display tabs, (iii) quality attribute scenarios, (iv) technical Test Experiments logically linked to each quality attribute, and (v) design constraints and risk assessment.

Section 4 specifies the quality-attribute requirements as concrete quality-attribute scenarios using the standard six-part scenario format.

## **1.2 Objectives**

### **Goals**

- **GUI Integration & Refinement (G-1):** Consolidating and finalizing the user interface for the Raspberry Pi 5-based real-time clock diagnostic GUI.

- **Data Reliability Verification (G-2):** Ensuring quantitative accuracy, consistency, and data integrity of clock measurements (BPH, beat error, amplitude).

- **Real-Time Performance Optimization (G-3):** Achieving ultra-low latency and high-throughput real-time processing under multi-sample-rate environments.

- **Architectural Extensibility (G-4):** Designing a modular and clean architecture to ensure easy integration of new features and components.

- **On-Device AI Exploration (G-5):** Researching secure, cloud-independent On-Device AI (TinyML) for signal quality classification.

- **Usability & Deployability (G-6):** Providing a readily reproducible demo and ensuring portability via a dedicated Raspberry Pi OS image.

### **Development Strategy: Attribute-Driven Design (ADD) & Risk-Mitigated Incremental Evolution**

Given the constrained 5-week schedule and a 7-person team, attempting a big-bang implementation of all system components introduces severe architectural risks. Therefore, this project adopts the SEI Attribute-Driven Design (ADD) process to drive an incremental and risk-mitigated development strategy:

\- Addressing Primary Architectural Drivers First

\- Incremental Expansion of Architectural Tactics

\- Early Evaluation via Test Experiments
