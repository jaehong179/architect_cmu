# ADR-004: Register display tabs through a Tab Manager

We decided to introduce a Tab Manager that registers each watch-visualization tab through a uniform registration interface, so that adding a new tab requires no changes to the core DSP modules or to existing tab source files.

***Decision***

We centralize tab registration and lifecycle in a dedicated Tab Manager.

MainWindow creates a single TabManager and, during RegisterDisplayTabs(), registers each visualization tab by calling registerTab(tab) once per tab (tab1, tab2, … tabN).
Every tab conforms to a common tab interface (a shared base type) so the Tab Manager can hold, lay out, and drive all tabs uniformly without knowing their concrete type.
The Tab Manager is the single integration point between the GUI layout and the individual tabs; tabs do not reference one another, and the core DSP modules do not reference any tab.
Adding a new tab therefore means: implement the new tab against the common interface, then add one registerTab(newTab) call. No existing tab file and no DSP module is modified.

***Rationale***

Without a registration point, each new tab tends to require edits scattered across the GUI layout code, sibling tab files, and sometimes the DSP modules — the "module change explosion" captured in RISK-06. By routing every tab through a uniform registerTab() interface on a Tab Manager, the dependency direction is inverted: the Tab Manager depends on the abstract tab interface, not on concrete tabs, and tabs depend on the interface, not on each other. This isolates change to the new tab plus a single registration line, which is exactly what QAS-08 requires (no edits to core DSP or existing tabs, zero regressions). The pattern also keeps MainWindow thin — it only wires up the manager and the list of tabs to register.

***Status***

Accepted

***Consequences***

Positive

- Satisfies QAS-08: a new tab is added with one registerTab() call and no changes to core DSP modules or existing tab source files, so regressions stay at zero.
- Directly mitigates RISK-06 (module change explosion) by confining change to a single integration point.
- Tabs are decoupled from each other and from DSP, which improves testability and parallel development.
- MainWindow stays simple — it only constructs the Tab Manager and registers tabs.

Negative / costs

- Introduces an abstraction layer (the common tab interface + Tab Manager) that every tab must conform to, which is slight up-front overhead.
- All tabs must fit the shared interface; a tab with very different needs may require extending the interface (a controlled change to the manager rather than to other tabs).
- The registration list in RegisterDisplayTabs() still grows by one line per tab (intended and minimal).