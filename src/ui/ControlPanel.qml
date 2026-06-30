import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    width: 242
    height: 750
    color: "#18181f" // Sleek dark mode background

    // UI 디자인 토큰
    readonly property color colorPrimary: "#ab47bc"    // Modern Purple
    readonly property color colorPrimaryLight: "#df78ef"
    readonly property color colorBgCard: "#252530"     // Soft card background
    readonly property color colorBgInput: "#1e1e26"    // Input field background
    readonly property color colorTextMain: "#ffffff"
    readonly property color colorTextSub: "#9e9e9e"
    readonly property color colorBorder: "#3a3a4a"
    readonly property color colorAccent: "#ff7043"     // Orange Accent for alert/status

    // ─── helper: mode label → short icon text ───────────────────────────────
    readonly property var modeIcons: ["●", "▶", "⚙"]  // Live / Playback / Sim

    readonly property var currentPositionInfo: getPositionInfo(cppBackend.detectedPosition)

    function getPositionInfo(dp) {
        if (!dp || dp === "" || dp === "?") {
            return {
                "name": "N/A",
                "status": "Camera Disconnected",
                "icon": "qrc:/images/src/ui/images/pos_camera_disconnected.svg",
                "colorBg": "#1f1d24", 
                "colorBorder": "#3a3a4a", 
                "colorText": "#9e9e9e", 
                "statusColor": "#ff7043" 
            };
        }

        var isWatch = dp.startsWith("W_");
        var suffix = dp.substring(2); 
        var name = "";
        var icon = "";
        var iconEmpty = "";

        switch (suffix) {
            case "DU":
                name = "Dial Up";
                icon = "qrc:/images/src/ui/images/pos_du.svg";
                iconEmpty = "qrc:/images/src/ui/images/pos_du_empty.svg";
                break;
            case "DD":
                name = "Dial Down";
                icon = "qrc:/images/src/ui/images/pos_dd.svg";
                iconEmpty = "qrc:/images/src/ui/images/pos_dd_empty.svg";
                break;
            case "CR":
                name = "Crown Right";
                icon = "qrc:/images/src/ui/images/pos_cr.svg";
                iconEmpty = "qrc:/images/src/ui/images/pos_cr_empty.svg";
                break;
            case "CL":
                name = "Crown Left";
                icon = "qrc:/images/src/ui/images/pos_cl.svg";
                iconEmpty = "qrc:/images/src/ui/images/pos_cl_empty.svg";
                break;
            case "CU":
                name = "Crown Up";
                icon = "qrc:/images/src/ui/images/pos_cu.svg";
                iconEmpty = "qrc:/images/src/ui/images/pos_cu_empty.svg";
                break;
            case "CD":
                name = "Crown Down";
                icon = "qrc:/images/src/ui/images/pos_cd.svg";
                iconEmpty = "qrc:/images/src/ui/images/pos_cd_empty.svg";
                break;
            default:
                return {
                    "name": "N/A",
                    "status": "Unknown Position",
                    "icon": "qrc:/images/src/ui/images/pos_camera_disconnected.svg",
                    "colorBg": "#1e1e26",
                    "colorBorder": root.colorBorder,
                    "colorText": root.colorTextMain,
                    "statusColor": root.colorTextSub
                };
        }

        return {
            "name": name,
            "status": isWatch ? "Watch Present" : "Empty Holder",
            "icon": isWatch ? icon : iconEmpty,
            "colorBg": isWatch ? "#1e3822" : "#382c1e",
            "colorBorder": isWatch ? "#4caf50" : "#ff7043",
            "colorText": isWatch ? "#4caf50" : "#ff7043",
            "statusColor": isWatch ? "#81c784" : "#ffb74d"
        };
    }

    // ─── session transport helpers (shared by collapsed + expanded panels) ──
    readonly property bool sessionIdle: !cppBackend.isRunning
    readonly property bool sessionRunning: cppBackend.isRunning && !cppBackend.isPaused
    readonly property bool sessionPaused: cppBackend.isRunning && cppBackend.isPaused
    readonly property bool playPauseEnabled: !cppBackend.isRunning
        || cppBackend.isPaused
        || !cppBackend.inWarmup

    function primarySessionAction() {
        if (sessionIdle)
            cppBackend.startSession()
        else
            cppBackend.togglePauseSession()
    }

    function primaryButtonIconSource() {
        return (sessionIdle || sessionPaused)
            ? "qrc:/images/src/ui/images/ic_play.svg"
            : "qrc:/images/src/ui/images/ic_pause.svg"
    }

    function primaryButtonShortLabel() {
        return (sessionIdle || sessionPaused) ? "GO" : "PAUSE"
    }

    function primaryButtonFullLabel() {
        if (sessionIdle || sessionPaused)
            return "START"
        return "PAUSE"
    }

    function primaryButtonTooltip() {
        if (sessionIdle)
            return "Start Session"
        if (cppBackend.inWarmup)
            return "Pause unavailable during warm-up"
        if (sessionPaused)
            return "Resume Session"
        return "Pause Session"
    }

    function primaryButtonFillColor() {
        if (sessionIdle)
            return "#1b5e20"
        if (sessionPaused)
            return "#e65100"
        return "#f57c00"
    }

    function primaryButtonBorderColor() {
        if (sessionIdle)
            return "#4caf50"
        if (sessionPaused)
            return "#ff9800"
        return "#ffb74d"
    }

    // ─── Header Bar ──────────────────────────────────────────────────────────
    Rectangle {
        id: headerBar
        visible: false   // [설정 팝업] 사이드바 전환: 확장/축소 헤더(햄버거·체브론) 제거
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 48
        color: "#1c1c26"
        border.color: root.colorBorder
        border.width: 1

        // Title – only visible when expanded
        Text {
            text: "Control Panel"
            color: root.colorTextMain
            font.bold: true
            font.pixelSize: 14
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            visible: !cppBackend.controlPanelCollapsed
        }

        // Toggle button (hamburger collapsed / chevron expanded)
        Button {
            id: toggleBtn
            width: 32
            height: 32
            padding: 0
            anchors.verticalCenter: parent.verticalCenter
            x: cppBackend.controlPanelCollapsed
               ? (parent.width - width) / 2
               : (parent.width - width - 8)

            background: Rectangle {
                color: toggleBtn.down ? root.colorPrimary
                                      : (toggleBtn.hovered ? "#2d2d3d" : root.colorBgCard)
                radius: 6
                border.color: root.colorBorder
                border.width: 1
            }
            Item {
                id: toggleIcon
                anchors.fill: parent

                readonly property bool collapsed: cppBackend.controlPanelCollapsed
                readonly property color iconColor: toggleBtn.hovered ? root.colorPrimaryLight : root.colorTextMain

                // Collapsed: hamburger menu
                Item {
                    anchors.fill: parent
                    opacity: toggleIcon.collapsed ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.InOutQuad } }

                    Rectangle {
                        width: 14; height: 2; radius: 1
                        color: toggleIcon.iconColor
                        x: (parent.width - width) / 2
                        y: 11
                        Behavior on color { ColorAnimation { duration: 100 } }
                    }
                    Rectangle {
                        width: 14; height: 2; radius: 1
                        color: toggleIcon.iconColor
                        x: (parent.width - width) / 2
                        y: 15
                        Behavior on color { ColorAnimation { duration: 100 } }
                    }
                    Rectangle {
                        width: 14; height: 2; radius: 1
                        color: toggleIcon.iconColor
                        x: (parent.width - width) / 2
                        y: 19
                        Behavior on color { ColorAnimation { duration: 100 } }
                    }
                }

                // Expanded: collapse chevron (←) — geometry-based for true centering
                Item {
                    width: 12
                    height: 12
                    anchors.centerIn: parent
                    opacity: toggleIcon.collapsed ? 0 : 1
                    visible: opacity > 0

                    Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.InOutQuad } }

                    Rectangle {
                        width: 8
                        height: 2
                        radius: 1
                        color: toggleIcon.iconColor
                        x: 2
                        y: 6
                        rotation: -45
                        transformOrigin: Item.Left
                        Behavior on color { ColorAnimation { duration: 100 } }
                    }
                    Rectangle {
                        width: 8
                        height: 2
                        radius: 1
                        color: toggleIcon.iconColor
                        x: 2
                        y: 6
                        rotation: 45
                        transformOrigin: Item.Left
                        Behavior on color { ColorAnimation { duration: 100 } }
                    }
                }
            }

            ToolTip.visible: toggleBtn.hovered
            ToolTip.text: cppBackend.controlPanelCollapsed ? "Expand panel" : "Collapse panel"
            ToolTip.delay: 400

            onClicked: { cppBackend.controlPanelCollapsed = !cppBackend.controlPanelCollapsed }
        }
    }

    // ─── Collapsed Mini-Info Panel ────────────────────────────────────────────
    // Shown only when the panel is collapsed; displays mode icon + position icon
    Column {
        id: collapsedInfo
        anchors.top: parent.top
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 18
        visible: !cppBackend.settingsOpen

        // ── Start/Pause + Stop ──
        Rectangle {
            id: miniPlayPauseBtn
            width: 68
            height: 68
            radius: 14
            anchors.horizontalCenter: parent.horizontalCenter
            color: root.primaryButtonFillColor()
            border.color: root.primaryButtonBorderColor()
            border.width: 1
            opacity: root.playPauseEnabled ? 1.0 : 0.35

            Behavior on color { ColorAnimation { duration: 200 } }

            Image {
                anchors.centerIn: parent
                source: root.primaryButtonIconSource()
                width: 42
                height: 42
                fillMode: Image.PreserveAspectFit
            }

            ToolTip.visible: playPauseHover.hovered
            ToolTip.text: root.primaryButtonTooltip()
            ToolTip.delay: 400

            HoverHandler { id: playPauseHover }

            MouseArea {
                anchors.fill: parent
                enabled: root.playPauseEnabled
                onClicked: { root.primarySessionAction() }
            }
        }

        Rectangle {
            id: miniStopBtn
            width: 68
            height: 68
            radius: 14
            anchors.horizontalCenter: parent.horizontalCenter
            visible: cppBackend.isRunning
            color: "#b71c1c"
            border.color: "#f44336"
            border.width: 1

            Image {
                anchors.centerIn: parent
                source: "qrc:/images/src/ui/images/ic_stop.svg"
                width: 40
                height: 40
                fillMode: Image.PreserveAspectFit
            }

            ToolTip.visible: stopHover.hovered
            ToolTip.text: "Stop Session"
            ToolTip.delay: 400

            HoverHandler { id: stopHover }

            MouseArea {
                anchors.fill: parent
                onClicked: { cppBackend.stopSession() }
            }
        }

        // ── History QR (평상시 이력 QR) ──
        Rectangle {
            id: miniQrBtn
            width: 68
            height: 68
            radius: 14
            anchors.horizontalCenter: parent.horizontalCenter
            color: root.colorBgCard
            border.color: root.colorBorder
            border.width: 1

            Image {
                anchors.centerIn: parent
                source: "qrc:/images/src/ui/images/ic_qr.svg"
                width: 40
                height: 40
                fillMode: Image.PreserveAspectFit
            }

            ToolTip.visible: qrHover.hovered
            ToolTip.text: "View History QR"
            ToolTip.delay: 400

            HoverHandler { id: qrHover }

            MouseArea {
                anchors.fill: parent
                onClicked: { cppBackend.showHistoryQr() }
            }
        }

        // ── Settings (가운데 설정 팝업 열기) ──
        Rectangle {
            id: miniSettingsBtn
            width: 68
            height: 68
            radius: 14
            anchors.horizontalCenter: parent.horizontalCenter
            color: root.colorBgCard
            border.color: root.colorBorder
            border.width: 1

            Image {
                anchors.centerIn: parent
                source: "qrc:/images/src/ui/images/ic_settings.svg"
                width: 40
                height: 40
                fillMode: Image.PreserveAspectFit
            }

            ToolTip.visible: settingsHover.hovered
            ToolTip.text: "Settings"
            ToolTip.delay: 400

            HoverHandler { id: settingsHover }

            MouseArea {
                anchors.fill: parent
                onClicked: { cppBackend.settingsOpen = true }
            }
        }
    }

    // ─── Sidebar: monitoring status (좌측 하단) ───────────────────────────────
    //  모드 표시(색 점) + 감지 포지션/카메라 상태 — 컨트롤이 아니라 상태 표시.
    Column {
        id: statusBadges
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 18
        visible: !cppBackend.settingsOpen

        // ── Mode badge ──
        Rectangle {
            width: 68
            height: 68
            radius: 14
            color: root.colorBgCard
            border.color: root.colorBorder
            border.width: 1
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                anchors.centerIn: parent
                text: root.modeIcons[cppBackend.currentMode] || "●"
                font.pixelSize: 32
                color: {
                    switch (cppBackend.currentMode) {
                        case 0: return "#ef5350"  // Live  → red
                        case 1: return "#42a5f5"  // Play  → blue
                        case 2: return "#ab47bc"  // Sim   → purple
                        default: return root.colorTextMain
                    }
                }
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            ToolTip.visible: modeHover.hovered
            ToolTip.text: ["Live", "Playback", "Sim"][cppBackend.currentMode] || ""
            ToolTip.delay: 400
            HoverHandler { id: modeHover }
        }

        // ── Position badge (camera/detected watch) ──
        Rectangle {
            width: 68
            height: 68
            radius: 14
            color: root.colorBgCard
            border.color: root.colorBorder
            border.width: 1
            anchors.horizontalCenter: parent.horizontalCenter

            Image {
                anchors.centerIn: parent
                width: 48
                height: 48
                fillMode: Image.PreserveAspectFit
                source: root.currentPositionInfo.icon
            }

            ToolTip.visible: posHover.hovered
            ToolTip.text: root.currentPositionInfo.name !== "N/A"
                ? root.currentPositionInfo.name + " (" + root.currentPositionInfo.status + ")"
                : "Unknown"
            ToolTip.delay: 400
            HoverHandler { id: posHover }
        }
    }

    // ─── Settings 팝업 배경(딤) — 클릭 시 닫힘 ─────────────────────────────────
    Rectangle {
        id: settingsBackdrop
        anchors.fill: parent
        visible: cppBackend.settingsOpen
        color: "#cc0d0d12"
        MouseArea {
            anchors.fill: parent
            onClicked: { cppBackend.settingsOpen = false }
        }
    }

    // ─── 가운데 설정 카드 (모든 섹션 펼침) ─────────────────────────────────────
    // 스크롤뷰를 사용해 모든 화면 해상도에서 흘러내리지 않도록 함
    ScrollView {
        id: settingsCard
        visible: cppBackend.settingsOpen
        anchors.centerIn: parent
        width: Math.min(parent.width - 60, 760)
        height: Math.min(parent.height - 48, contentColumn.implicitHeight + 24)
        contentWidth: availableWidth
        contentHeight: contentColumn.implicitHeight
        clip: true

        background: Rectangle {
            color: "#181820"
            radius: 12
            border.color: root.colorBorder
            border.width: 1
        }

        GridLayout {
            id: contentColumn
            width: parent.width - 16
            anchors.horizontalCenter: parent.horizontalCenter
            columns: 2
            columnSpacing: 16
            rowSpacing: 16

            // ── 팝업 헤더: 제목 + 닫기(X) — 2열 전체 ──
            RowLayout {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                Layout.topMargin: 8
                Text {
                    text: "Settings"
                    color: root.colorTextMain
                    font.bold: true
                    font.pixelSize: 16
                }
                Item { Layout.fillWidth: true }
                Button {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 28
                    onClicked: { cppBackend.settingsOpen = false }
                    background: Rectangle {
                        color: closeHover.hovered ? "#3a3a44" : "transparent"
                        radius: 6
                        border.color: root.colorBorder
                    }
                    contentItem: Text {
                        text: "✕"
                        color: root.colorTextMain
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 14
                    }
                    HoverHandler { id: closeHover }
                }
            }

            // 헤더 구분선 (2열 전체)
            Rectangle {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                Layout.topMargin: 2
                Layout.bottomMargin: 2
                height: 1
                color: root.colorBorder
            }

            // ==========================================
            // 1. MODE / SOURCE PANEL
            // ==========================================
            Rectangle {
                Layout.fillWidth: true
                Layout.columnSpan: 2
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: modeCol.implicitHeight + 16
                color: root.colorBgCard
                radius: 8
                border.color: root.colorBorder

                ColumnLayout {
                    id: modeCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    Text {
                        text: "Mode / Source"
                        color: root.colorTextMain
                        font.bold: true
                        font.pixelSize: 13
                    }

                    // [UI] 드롭다운 대신 라디오 버튼(Live / Playback / Sim)
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        Layout.bottomMargin: 2
                        spacing: 26

                        Repeater {
                            model: ["Live", "Playback", "Sim"]
                            delegate: Item {
                                implicitWidth: optRow.implicitWidth
                                implicitHeight: 24

                                Row {
                                    id: optRow
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 8

                                    Rectangle {
                                        width: 18; height: 18; radius: 9
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: "transparent"
                                        border.width: 2
                                        border.color: cppBackend.currentMode === index
                                            ? root.colorPrimary : root.colorBorder
                                        Behavior on border.color { ColorAnimation { duration: 120 } }
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 9; height: 9; radius: 4.5
                                            color: root.colorPrimary
                                            visible: cppBackend.currentMode === index
                                        }
                                    }

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData
                                        font.pixelSize: 13
                                        font.bold: cppBackend.currentMode === index
                                        color: cppBackend.currentMode === index
                                            ? root.colorTextMain : root.colorTextSub
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !cppBackend.isRunning
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { cppBackend.currentMode = index }
                                }

                                opacity: cppBackend.isRunning ? 0.45 : 1.0
                            }
                        }
                    }
                }
            }

            // ==========================================
            // 2. DYNAMIC PARAMETERS STACK (Live / Playback / Sim)
            // ==========================================
            Rectangle {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: dynamicStack.implicitHeight + 16
                color: root.colorBgCard
                radius: 8
                border.color: root.colorBorder
                clip: true

                // 모드별 레이아웃 전환
                ColumnLayout {
                    id: dynamicStack
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    property bool runParametersExpanded: true
                    property bool simulationParametersExpanded: true

                    // LIVE MODE PANEL
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: cppBackend.currentMode === 0 // LIVE
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: "Run Parameters (Live)"
                                color: root.colorTextMain
                                font.bold: true
                                font.pixelSize: 13
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 24
                                visible: false   // [설정 팝업] 드롭다운 제거 — 항상 펼침
                                text: dynamicStack.runParametersExpanded ? "▾" : "▸"
                                onClicked: {
                                    dynamicStack.runParametersExpanded = !dynamicStack.runParametersExpanded
                                }
                                background: Rectangle {
                                    color: "transparent"
                                    border.color: root.colorBorder
                                    radius: 4
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: root.colorTextSub
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 11
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: dynamicStack.runParametersExpanded
                            spacing: 8

                            // Gain 조절 (실행 중에도 동작하도록 enabled: true 유지)
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Gain"; color: root.colorTextSub; font.pixelSize: 11 }
                                    Item { Layout.fillWidth: true; Layout.fillHeight: true } // Spacer
                                    Text {
                                        text: (gainSlider.value * 100).toFixed(0) + "%"
                                        color: root.colorPrimaryLight
                                        font.bold: true
                                        font.pixelSize: 11
                                    }
                                }
                                Slider {
                                    id: gainSlider
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 1.0
                                    value: cppBackend.gain
                                    onMoved: { cppBackend.gain = value }
                                }
                            }

                            // Input Device
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "Input Device"; color: root.colorTextSub; font.pixelSize: 11 }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    ComboBox {
                                        id: deviceCombo
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 38
                                        font.pixelSize: 14
                                        model: cppBackend.deviceList
                                        currentIndex: cppBackend.deviceIndex
                                        onActivated: (index) => { cppBackend.deviceIndex = index }
                                        enabled: !cppBackend.isRunning
                                        background: Rectangle {
                                            color: root.colorBgInput
                                            border.color: parent.enabled ? root.colorBorder : "#2d2d38"
                                            radius: 4
                                        }
                                        contentItem: Text {
                                            text: deviceCombo.displayText
                                            color: parent.enabled ? root.colorTextMain : root.colorTextSub
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: 10
                                            font.pixelSize: 14
                                            elide: Text.ElideRight
                                        }
                                    }
                                    Button {
                                        Layout.preferredWidth: 38
                                        Layout.preferredHeight: 38
                                        text: "🔄"
                                        enabled: !cppBackend.isRunning
                                        onClicked: { cppBackend.refreshDevices() }
                                    }
                                }
                            }

                            // Sample Rate
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "Sample Rate"; color: root.colorTextSub; font.pixelSize: 11 }
                                ComboBox {
                                    id: srCombo
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 38
                                    font.pixelSize: 14
                                    model: cppBackend.sampleRateList
                                    currentIndex: cppBackend.sampleRateIndex
                                    onActivated: (index) => { cppBackend.sampleRateIndex = index }
                                    enabled: !cppBackend.isRunning
                                    background: Rectangle {
                                        color: root.colorBgInput
                                        border.color: parent.enabled ? root.colorBorder : "#2d2d38"
                                        radius: 4
                                    }
                                    contentItem: Text {
                                        text: srCombo.displayText
                                        color: parent.enabled ? root.colorTextMain : root.colorTextSub
                                        verticalAlignment: Text.AlignVCenter
                                        leftPadding: 10
                                        font.pixelSize: 14
                                    }
                                }
                            }
                        }
                    }

                    // PLAYBACK MODE PANEL
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: cppBackend.currentMode === 1 // PLAYBACK
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: "Run Parameters (Playback)"
                                color: root.colorTextMain
                                font.bold: true
                                font.pixelSize: 13
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 24
                                visible: false   // [설정 팝업] 드롭다운 제거 — 항상 펼침
                                text: dynamicStack.runParametersExpanded ? "▾" : "▸"
                                onClicked: {
                                    dynamicStack.runParametersExpanded = !dynamicStack.runParametersExpanded
                                }
                                background: Rectangle {
                                    color: "transparent"
                                    border.color: root.colorBorder
                                    radius: 4
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: root.colorTextSub
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 11
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: dynamicStack.runParametersExpanded
                            spacing: 10

                            Button {
                                Layout.fillWidth: true
                                text: "Browse WAV File"
                                enabled: !cppBackend.isRunning
                                onClicked: { cppBackend.choosePlaybackFile() }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                color: root.colorBgInput
                                radius: 4
                                border.color: root.colorBorder

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    spacing: 2
                                    Text { text: "Selected WAV File:"; color: root.colorTextSub; font.pixelSize: 10 }
                                    Text {
                                        text: cppBackend.selectedWavFile !== "" ? cppBackend.selectedWavFile : "No WAV selected"
                                        color: cppBackend.selectedWavFile !== "" ? root.colorPrimaryLight : root.colorTextSub
                                        font.bold: true
                                        font.pixelSize: 11
                                        elide: Text.ElideLeft
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                    }

                    // SIMULATION MODE PANEL
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: cppBackend.currentMode === 2 // SIMULATION
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: "Simulation Parameters"
                                color: root.colorTextMain
                                font.bold: true
                                font.pixelSize: 13
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 24
                                visible: false   // [설정 팝업] 드롭다운 제거 — 항상 펼침
                                text: dynamicStack.simulationParametersExpanded ? "▾" : "▸"
                                onClicked: {
                                    dynamicStack.simulationParametersExpanded = !dynamicStack.simulationParametersExpanded
                                }
                                background: Rectangle {
                                    color: "transparent"
                                    border.color: root.colorBorder
                                    radius: 4
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: root.colorTextSub
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 11
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: dynamicStack.simulationParametersExpanded
                            spacing: 6

                            // Sim BPH
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Sim BPH"; color: root.colorTextSub; font.pixelSize: 11; Layout.fillWidth: true }
                                ComboBox {
                                    id: simBphCombo
                                    Layout.preferredWidth: 110
                                    model: cppBackend.simBphList
                                    currentIndex: cppBackend.simBphIndex
                                    onActivated: (index) => { cppBackend.simBphIndex = index }
                                    enabled: !cppBackend.isRunning
                                }
                            }

                            // Sim Sample Rate
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Sample Rate"; color: root.colorTextSub; font.pixelSize: 11; Layout.fillWidth: true }
                                ComboBox {
                                    id: simSrCombo
                                    Layout.preferredWidth: 110
                                    model: cppBackend.sampleRateList
                                    currentIndex: cppBackend.sampleRateIndex
                                    onActivated: (index) => { cppBackend.sampleRateIndex = index }
                                    enabled: !cppBackend.isRunning
                                }
                            }

                            // Sim Error Rate
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Error Rate (s/d)"; color: root.colorTextSub; font.pixelSize: 11; Layout.fillWidth: true }
                                SpinBox {
                                    Layout.preferredWidth: 110
                                    from: -999
                                    to: 999
                                    value: cppBackend.simErrorRate
                                    enabled: !cppBackend.isRunning
                                    onValueModified: { cppBackend.simErrorRate = value }
                                }
                            }

                            // Sim Amplitude
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Amplitude (°)"; color: root.colorTextSub; font.pixelSize: 11; Layout.fillWidth: true }
                                SpinBox {
                                    Layout.preferredWidth: 110
                                    from: 100
                                    to: 360
                                    value: cppBackend.simAmplitude
                                    enabled: !cppBackend.isRunning
                                    onValueModified: { cppBackend.simAmplitude = value }
                                }
                            }

                            // Sim Beat Error (Decimals SpinBox logic inline)
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Beat Error (ms)"; color: root.colorTextSub; font.pixelSize: 11; Layout.fillWidth: true }
                                SpinBox {
                                    id: simBeSpin
                                    Layout.preferredWidth: 110
                                    from: -100
                                    to: 100
                                    stepSize: 1
                                    value: cppBackend.simBeatError * 10
                                    enabled: !cppBackend.isRunning
                                    onValueModified: { cppBackend.simBeatError = value / 10.0 }

                                    validator: DoubleValidator {
                                        bottom: -10.0
                                        top: 10.0
                                    }
                                    textFromValue: function(value, locale) {
                                        return (value / 10).toFixed(1)
                                    }
                                    valueFromText: function(text, locale) {
                                        return parseFloat(text) * 10
                                    }
                                }
                            }

                            // Realistic Noise
                            CheckBox {
                                text: "Realistic Noise"
                                checked: cppBackend.simRealistic
                                enabled: !cppBackend.isRunning
                                onCheckedChanged: { cppBackend.simRealistic = checked }
                                contentItem: Text {
                                    text: parent.text
                                    color: root.colorTextMain
                                    leftPadding: 24
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
            }

            // ==========================================
            // 3. MEASUREMENT / DETECTOR PARAMETERS
            // ==========================================
            Rectangle {
                id: measurementSettingsCard
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: measurementSettingsExpanded
                    ? (measureHeader.implicitHeight + measureDetails.implicitHeight + 16)
                    : (measureHeader.implicitHeight + 16)
                color: root.colorBgCard
                radius: 8
                border.color: root.colorBorder
                clip: true

                property bool measurementSettingsExpanded: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    RowLayout {
                        id: measureHeader
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: "Measurement Settings"
                            color: root.colorTextMain
                            font.bold: true
                            font.pixelSize: 13
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 24
                            visible: false   // [설정 팝업] 드롭다운 제거 — 항상 펼침
                            text: measurementSettingsCard.measurementSettingsExpanded ? "▾" : "▸"
                            onClicked: {
                                measurementSettingsCard.measurementSettingsExpanded =
                                    !measurementSettingsCard.measurementSettingsExpanded
                            }
                            background: Rectangle {
                                color: "transparent"
                                border.color: root.colorBorder
                                radius: 4
                            }
                            contentItem: Text {
                                text: parent.text
                                color: root.colorTextSub
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 11
                            }
                        }
                    }

                    ColumnLayout {
                        id: measureDetails
                        Layout.fillWidth: true
                        visible: measurementSettingsCard.measurementSettingsExpanded
                        spacing: 8

                        // Detector BPH (Sim 모드가 아닐 때만 콤보박스 노출)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: "Detector BPH"; color: root.colorTextSub; font.pixelSize: 11 }

                            ComboBox {
                                id: detBphCombo
                                Layout.fillWidth: true
                                visible: cppBackend.currentMode !== 2
                                model: cppBackend.bphList
                                currentIndex: cppBackend.detectorBphIndex
                                onActivated: (index) => { cppBackend.detectorBphIndex = index }
                                enabled: !cppBackend.isRunning
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 28
                                color: root.colorBgInput
                                radius: 4
                                visible: cppBackend.currentMode === 2
                                Text {
                                    anchors.centerIn: parent
                                    text: "Follows Sim BPH"
                                    color: root.colorPrimaryLight
                                    font.bold: true
                                    font.pixelSize: 11
                                }
                            }
                        }

                        // Lift Angle
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Lift Angle (°)"; color: root.colorTextSub; font.pixelSize: 11; Layout.fillWidth: true }
                            SpinBox {
                                Layout.preferredWidth: 110
                                from: 30
                                to: 70
                                value: cppBackend.liftAngle
                                enabled: !cppBackend.isRunning
                                onValueModified: { cppBackend.liftAngle = value }
                            }
                        }

                        // Averaging Period
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: "Averaging Period"; color: root.colorTextSub; font.pixelSize: 11 }
                            ComboBox {
                                id: avgCombo
                                Layout.fillWidth: true
                                model: cppBackend.averagingPeriodList
                                currentIndex: cppBackend.averagingPeriodIndex
                                onActivated: (index) => { cppBackend.averagingPeriodIndex = index }
                                enabled: !cppBackend.isRunning
                            }
                        }

                        // Warm-up Delay
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: "Warm-up Delay"; color: root.colorTextSub; font.pixelSize: 11 }
                            ComboBox {
                                id: warmupCombo
                                Layout.fillWidth: true
                                model: cppBackend.warmupDelayList
                                currentIndex: cppBackend.warmupDelayIndex
                                onActivated: (index) => { cppBackend.warmupDelayIndex = index }
                                enabled: !cppBackend.isRunning
                            }
                        }

                    }
                }
            }

            // ==========================================
            // 4. ADVANCED / DETECTOR TUNING
            // ==========================================
            Rectangle {
                id: advancedTuningCard
                Layout.fillWidth: true
                Layout.columnSpan: 2
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: advancedTuningExpanded
                    ? (advHeader.implicitHeight + advDetails.implicitHeight + 16)
                    : (advHeader.implicitHeight + 16)
                color: root.colorBgCard
                radius: 8
                border.color: root.colorBorder
                clip: true

                property bool advancedTuningExpanded: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    RowLayout {
                        id: advHeader
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: "Advanced / Tuning"
                            color: root.colorTextMain
                            font.bold: true
                            font.pixelSize: 13
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 24
                            visible: false   // [설정 팝업] 드롭다운 제거 — 항상 펼침
                            text: advancedTuningCard.advancedTuningExpanded ? "▾" : "▸"
                            onClicked: {
                                advancedTuningCard.advancedTuningExpanded =
                                    !advancedTuningCard.advancedTuningExpanded
                            }
                            background: Rectangle {
                                color: "transparent"
                                border.color: root.colorBorder
                                radius: 4
                            }
                            contentItem: Text {
                                text: parent.text
                                color: root.colorTextSub
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 11
                            }
                        }
                    }

                    ColumnLayout {
                        id: advDetails
                        Layout.fillWidth: true
                        visible: advancedTuningCard.advancedTuningExpanded
                        spacing: 8

                        // Watch ID (cloud save key)
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: "Watch ID"
                                color: root.colorTextSub
                                font.pixelSize: 11
                                Layout.preferredWidth: 90
                            }
                            TextField {
                                Layout.fillWidth: true
                                placeholderText: "e.g. rolex_123456"
                                text: cppBackend.watchId
                                enabled: !cppBackend.isRunning
                                color: root.colorTextMain
                                placeholderTextColor: root.colorTextSub
                                background: Rectangle {
                                    color: root.colorBgInput
                                    border.color: root.colorBorder
                                    radius: 4
                                }
                                onEditingFinished: {
                                    cppBackend.watchId = text.trim()
                                }
                            }
                        }

                        // Engineer (required by cloud API)
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: "Engineer"
                                color: root.colorTextSub
                                font.pixelSize: 11
                                Layout.preferredWidth: 90
                            }
                            TextField {
                                Layout.fillWidth: true
                                placeholderText: "e.g. Mr. Park"
                                text: cppBackend.engineer
                                enabled: !cppBackend.isRunning
                                color: root.colorTextMain
                                placeholderTextColor: root.colorTextSub
                                background: Rectangle {
                                    color: root.colorBgInput
                                    border.color: root.colorBorder
                                    radius: 4
                                }
                                onEditingFinished: {
                                    cppBackend.engineer = text.trim()
                                }
                            }
                        }

                        // High Pass Cutoff (LineEdit 대신 스핀박스로 입력 제한 강화)
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "High Pass Cutoff"; color: root.colorTextSub; font.pixelSize: 11; Layout.fillWidth: true }
                            SpinBox {
                                Layout.preferredWidth: 110
                                from: 10
                                to: 10000
                                stepSize: 50
                                value: cppBackend.highPassCutoff
                                enabled: !cppBackend.isRunning
                                onValueModified: { cppBackend.highPassCutoff = value }
                            }
                        }

                        // Onset Amplitude
                        CheckBox {
                            text: "C Event Onset Amplitude"
                            checked: cppBackend.useConset
                            enabled: !cppBackend.isRunning
                            onCheckedChanged: { cppBackend.useConset = checked }
                            contentItem: Text {
                                text: parent.text
                                color: root.colorTextMain
                                leftPadding: 24
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            // ==========================================
            // 5. CONTROL BUTTONS & RECORDING
            // ==========================================
            Rectangle {
                visible: false   // [설정 팝업] 팝업에서는 제외 — START/STOP/Record 는 사이드바 사용
                Layout.fillWidth: true
                Layout.preferredHeight: ctrlCol.implicitHeight + 16
                color: root.colorBgCard
                radius: 8
                border.color: root.colorBorder

                ColumnLayout {
                    id: ctrlCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    // Record option instead of interrupting popup
                    CheckBox {
                        id: recCheck
                        text: "Record Session (Save WAV)"
                        checked: cppBackend.recordSessionEnabled
                        onCheckedChanged: { cppBackend.recordSessionEnabled = checked }
                        enabled: !cppBackend.isRunning
                        contentItem: Text {
                            text: parent.text
                            color: root.colorAccent
                            font.bold: true
                            leftPadding: 24
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 11
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Button {
                            id: playPauseBtn
                            Layout.fillWidth: true
                            enabled: root.playPauseEnabled
                            onClicked: { root.primarySessionAction() }

                            ToolTip.visible: playPauseBtn.hovered
                            ToolTip.text: root.primaryButtonTooltip()
                            ToolTip.delay: 400

                            background: Rectangle {
                                color: parent.enabled
                                    ? root.primaryButtonFillColor()
                                    : (root.sessionIdle ? "#2d382e" : "#3d3020")
                                radius: 4
                            }
                            contentItem: Row {
                                spacing: 4
                                anchors.centerIn: parent
                                Image {
                                    anchors.verticalCenter: parent.verticalCenter
                                    source: root.primaryButtonIconSource()
                                    width: 16
                                    height: 16
                                    fillMode: Image.PreserveAspectFit
                                    opacity: playPauseBtn.enabled ? 1.0 : 0.4
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.primaryButtonFullLabel()
                                    color: playPauseBtn.enabled
                                        ? "#ffffff"
                                        : (root.sessionIdle ? "#608060" : "#806040")
                                    font.bold: true
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }

                        Button {
                            id: stopBtn
                            Layout.fillWidth: true
                            enabled: cppBackend.isRunning
                            onClicked: { cppBackend.stopSession() }

                            background: Rectangle {
                                color: parent.enabled ? "#f44336" : "#382d2d"
                                radius: 4
                            }
                            contentItem: Row {
                                spacing: 4
                                anchors.centerIn: parent
                                Image {
                                    anchors.verticalCenter: parent.verticalCenter
                                    source: "qrc:/images/src/ui/images/ic_stop.svg"
                                    width: 16
                                    height: 16
                                    fillMode: Image.PreserveAspectFit
                                    opacity: stopBtn.enabled ? 1.0 : 0.4
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "STOP"
                                    color: stopBtn.enabled ? "#ffffff" : "#806060"
                                    font.bold: true
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }
            }
            // ==========================================
            // 6. DETECTED WATCH POSITION
            // ==========================================
            Rectangle {
                id: positionCard
                visible: false   // [설정 팝업] 팝업에서는 제외 — 포지션은 사이드바 badge 사용
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                color: root.colorBgCard
                radius: 8
                border.color: root.colorBorder

                readonly property var currentInfo: root.currentPositionInfo

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Text {
                        text: "Detected Watch Position"
                        color: root.colorTextMain
                        font.bold: true
                        font.pixelSize: 13
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: positionCard.currentInfo.colorBg
                        radius: 6
                        border.color: positionCard.currentInfo.colorBorder
                        border.width: 2

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 12

                            Image {
                                source: positionCard.currentInfo.icon
                                Layout.preferredWidth: 48
                                Layout.preferredHeight: 48
                                fillMode: Image.PreserveAspectFit
                            }

                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                anchors.verticalCenter: parent.verticalCenter

                                Text {
                                    text: positionCard.currentInfo.name
                                    color: positionCard.currentInfo.colorText
                                    font.bold: true
                                    font.pixelSize: 18
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: positionCard.currentInfo.status
                                    color: positionCard.currentInfo.statusColor
                                    font.bold: true
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }

            // 하단 여백 (2열 전체)
            Item { Layout.columnSpan: 2; Layout.preferredHeight: 6 }
        }
    }
}
