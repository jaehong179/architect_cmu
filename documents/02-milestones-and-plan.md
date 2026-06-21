# **2. Milestones and Plan**

## 2.1 Plan

Based on the Agile/Scrum framework. The 5 weeks are divided into 5 Sprints (Sprint 0 + Sprints 1~4).

<table>
<colgroup>
<col style="width: 9%" />
<col style="width: 10%" />
<col style="width: 9%" />
<col style="width: 29%" />
<col style="width: 33%" />
<col style="width: 7%" />
</colgroup>
<thead>
<tr class="header">
<th><strong>Sprint</strong></th>
<th colspan="2"><strong>Duration</strong></th>
<th><strong>Purpose</strong></th>
<th><strong>Key Deliverables</strong></th>
<th><strong>Linked Milestone</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>Sprint 0</td>
<td>Week 1</td>
<td>05/25-06/05</td>
<td>Inception — Requirements·Risk·Experiment·Architecture draft</td>
<td><p>Project Plan<br />
Requirements Analysis<br />
Risk Management<br />
Architecture overview</p>
<p>Experiment Plan</p></td>
<td>MS-1 submission</td>
</tr>
<tr class="even">
<td>Sprint 1</td>
<td>Week 2</td>
<td>06/08-06/12</td>
<td>Architecture finalization &amp; experiment execution — design decisions and baseline/feasibility experiments</td>
<td>Software Architecture Document / Experiment results (baseline·feasibility: EXP-08/09/10/12/14) / Validated design decisions</td>
<td>MS-2 preparation</td>
</tr>
<tr class="odd">
<td>Sprint 2</td>
<td>Week 3</td>
<td>06/15-06/19</td>
<td>Core build &amp; initial visualization — signal capture·detection·measurement·basic display</td>
<td>Design-decision ADRs (EXP-15/16/17) / Revised Software Architecture Document / Live Mode operation · rate/BE/amp computation / Summary Bar / Vario·Sequence / Single-Beat Waveform / Scope/2·Beat Error / Long-Term Trace Display</td>
<td>MS-2 submission</td>
</tr>
<tr class="even">
<td>Sprint 3</td>
<td>Week 4</td>
<td>06/22-06/26</td>
<td>Visualization expansion and AI feature implementation</td>
<td>Waveform Compare<br />
Sync Sweep<br />
F0~F3 Filter<br />
TinyML signal quality improvement and anomaly detection</td>
<td>MS-3 preparation</td>
</tr>
<tr class="odd">
<td>Sprint 4</td>
<td>Week 5</td>
<td>06/29-07/01</td>
<td>Integration·demo preparation·document finalization</td>
<td><p>End-to-end demo</p>
<p>Presentation materials</p>
<p>Lessons Learned</p></td>
<td>MS-3 submission</td>
</tr>
</tbody>
</table>

The task-level schedule below breaks each sprint into concrete tasks with an assigned owner (by role) and start/end dates, and includes the technical experiments (spikes) as scheduled tasks. Day-to-day progress is tracked on the team Kanban board, where each backlog card carries an assignee: <u>https://miro.com/app/board/uXjVHFnTVy0=/?share_link_id=685489384283</u>

*Figure 2-1. Project plan — task schedule by owner, with technical experiments included as spikes.*

<img src="images/image1.png" style="width:6.21875in;height:6.5in" />

## **2.2 Roles**

|                      |                              |                                            |                                                                                  |
|----------------------|------------------------------|--------------------------------------------|----------------------------------------------------------------------------------|
| **Role**             | **Owner**                    | **Key Items**                              | **R&R**                                                                          |
| Product Owner        | Nam Sangjae                  | Project management                         | Backlog prioritization·stakeholder handling·approval                             |
| Scrum Master         | Lee Tae-hoon                 | Process + system-wide                      | Daily Standup·Planning·Review·Retro·blocker removal·metrics management           |
| DSP Engineer         | Yoon Joongcheol, Nam Sangjae | SignalProcessing · Detection · Measurement | Signal-processing pipeline·filters·envelope·T1/T3 detection·measurement formulas |
| Application Engineer | Choi Jinsuk, Lee Tae-hoon    | Timegrapher · Workers · Domain model       | DSP↔UI domain flow·Worker Thread·event dispatch·Mode abstraction                 |
| UI Engineer          | Kim Jae-hong, Cho Jin-young  | Visualization Layer · MainWindow           | Rendering·interaction·UX consistency of 12 display tabs                          |
| AI Developer         | Park Jongjin                 | AI Feature                                 | TinyML model evaluation·POC integration·Raspberry Pi inference                   |
