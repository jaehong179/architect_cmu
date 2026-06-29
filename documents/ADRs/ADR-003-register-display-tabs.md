# ADR-003: Register display tabs through a Tab Manager

We use a Tab Manager that registers each visualization tab through a uniform interface, so adding a new tab needs no changes to the core DSP modules or existing tab files.

***Decision***

- MainWindow creates one TabManager and, in RegisterDisplayTabs(), registers each tab with registerTab(tab).
- Every tab follows a common tab interface, so the Tab Manager handles all tabs uniformly without knowing their concrete type.
- The Tab Manager is the single integration point: tabs don't reference each other, and DSP modules don't reference any tab.
- Adding a new tab = implement it against the interface + add one registerTab(newTab) call. No existing tab or DSP module is changed.

***Rationale***

Without a registration point, each new tab needs edits scattered across the layout, sibling tabs, and sometimes DSP — the "module change explosion" of [RISK-06](../README.md#risk-06). Routing every tab through registerTab() inverts the dependencies: the manager and the tabs depend on the interface, not on each other. This confines change to the new tab plus one line, exactly what [QAS-08](../Requirements/quality-attribute-requirements.md#qas-08--new-tab-extensibility) requires.

***Status***

Accepted

***Consequences***

Positive

- Satisfies [QAS-08](../Requirements/quality-attribute-requirements.md#qas-08--new-tab-extensibility): a new tab needs one registerTab() call and no DSP/existing-tab changes, so regressions stay at zero.
- Mitigates [RISK-06](../README.md#risk-06) by confining change to one integration point.
- Tabs are decoupled from each other and from DSP, improving testability and parallel work.
- Extensibility doesn't hurt latency: every tab reuses the same per-block Tic/Toc results and wave processing, so a new tab only renders and adds no signal-processing cost. Adding tabs therefore does not increase per-block processing latency [QAS-02](../Requirements/quality-attribute-requirements.md#qas-02--end-to-end-latency).

Negative / costs

- Migrating the existing code to this structure takes some effort.
