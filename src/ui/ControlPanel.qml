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

    // 스크롤뷰를 사용해 모든 화면 해상도에서 흘러내리지 않도록 함
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: contentColumn.implicitHeight
        clip: true

        ColumnLayout {
            id: contentColumn
            width: parent.width - 16
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 12

            // 상단 여백
            Item { Layout.preferredHeight: 4 }

            // ==========================================
            // 1. MODE / SOURCE PANEL
            // ==========================================
            Rectangle {
                Layout.fillWidth: true
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

                    ComboBox {
                        id: modeComboBox
                        Layout.fillWidth: true
                        model: ["Live", "Playback", "Sim"]
                        currentIndex: cppBackend.currentMode
                        onActivated: (index) => { cppBackend.currentMode = index }
                        enabled: !cppBackend.isRunning

                        background: Rectangle {
                            color: root.colorBgInput
                            border.color: parent.enabled ? root.colorBorder : "#2d2d38"
                            radius: 4
                        }
                        contentItem: Text {
                            text: modeComboBox.displayText
                            color: parent.enabled ? root.colorTextMain : root.colorTextSub
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 8
                            font.bold: true
                        }
                    }
                }
            }

            // ==========================================
            // 2. DYNAMIC PARAMETERS STACK (Live / Playback / Sim)
            // ==========================================
            Rectangle {
                Layout.fillWidth: true
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

                    // LIVE MODE PANEL
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: cppBackend.currentMode === 0 // LIVE
                        spacing: 8

                        Text {
                            text: "Run Parameters (Live)"
                            color: root.colorTextMain
                            font.bold: true
                            font.pixelSize: 13
                        }

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
                                        leftPadding: 8
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }
                                Button {
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 28
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
                                    leftPadding: 8
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }

                    // PLAYBACK MODE PANEL
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: cppBackend.currentMode === 1 // PLAYBACK
                        spacing: 10

                        Text {
                            text: "Run Parameters (Playback)"
                            color: root.colorTextMain
                            font.bold: true
                            font.pixelSize: 13
                        }

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

                    // SIMULATION MODE PANEL
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: cppBackend.currentMode === 2 // SIMULATION
                        spacing: 6

                        Text {
                            text: "Simulation Parameters"
                            color: root.colorTextMain
                            font.bold: true
                            font.pixelSize: 13
                        }

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

            // ==========================================
            // 3. MEASUREMENT / DETECTOR PARAMETERS
            // ==========================================
            Rectangle {
                id: measurementSettingsCard
                Layout.fillWidth: true
                Layout.preferredHeight: measurementSettingsExpanded
                    ? (measureHeader.implicitHeight + measureDetails.implicitHeight + 16)
                    : (measureHeader.implicitHeight + 16)
                color: root.colorBgCard
                radius: 8
                border.color: root.colorBorder
                clip: true

                property bool measurementSettingsExpanded: false

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

                    }
                }
            }

            // ==========================================
            // 4. ADVANCED / DETECTOR TUNING
            // ==========================================
            Rectangle {
                id: advancedTuningCard
                Layout.fillWidth: true
                Layout.preferredHeight: advancedTuningExpanded
                    ? (advHeader.implicitHeight + advDetails.implicitHeight + 16)
                    : (advHeader.implicitHeight + 16)
                color: root.colorBgCard
                radius: 8
                border.color: root.colorBorder
                clip: true

                property bool advancedTuningExpanded: false

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

                        // High Pass Cutoff (LineEdit 대신 스핀박스로 입력 제한 강화)
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "High Pass Cutoff (Hz)"; color: root.colorTextSub; font.pixelSize: 11; Layout.fillWidth: true }
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
                            id: startBtn
                            Layout.fillWidth: true
                            text: "▶ START"
                            enabled: !cppBackend.isRunning
                            onClicked: { cppBackend.startSession() }
                            
                            background: Rectangle {
                                color: parent.enabled ? "#4caf50" : "#2d382e"
                                radius: 4
                            }
                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled ? "#ffffff" : "#608060"
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Button {
                            id: stopBtn
                            Layout.fillWidth: true
                            text: "⏹ STOP"
                            enabled: cppBackend.isRunning
                            onClicked: { cppBackend.stopSession() }

                            background: Rectangle {
                                color: parent.enabled ? "#f44336" : "#382d2d"
                                radius: 4
                            }
                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled ? "#ffffff" : "#806060"
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
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
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                color: root.colorBgCard
                radius: 8
                border.color: root.colorBorder

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
                            name = "DU (Dial Up)";
                            icon = "qrc:/images/src/ui/images/pos_du.svg";
                            iconEmpty = "qrc:/images/src/ui/images/pos_du_empty.svg";
                            break;
                        case "DD":
                            name = "DD (Dial Down)";
                            icon = "qrc:/images/src/ui/images/pos_dd.svg";
                            iconEmpty = "qrc:/images/src/ui/images/pos_dd_empty.svg";
                            break;
                        case "CR":
                            name = "12H (Crown Right)";
                            icon = "qrc:/images/src/ui/images/pos_cr.svg";
                            iconEmpty = "qrc:/images/src/ui/images/pos_cr_empty.svg";
                            break;
                        case "CL":
                            name = "6H (Crown Left)";
                            icon = "qrc:/images/src/ui/images/pos_cl.svg";
                            iconEmpty = "qrc:/images/src/ui/images/pos_cl_empty.svg";
                            break;
                        case "CU":
                            name = "3H (Crown Up)";
                            icon = "qrc:/images/src/ui/images/pos_cu.svg";
                            iconEmpty = "qrc:/images/src/ui/images/pos_cu_empty.svg";
                            break;
                        case "CD":
                            name = "9H (Crown Down)";
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

                readonly property var currentInfo: getPositionInfo(cppBackend.detectedPosition)

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

            // 하단 여백
            Item { Layout.preferredHeight: 12 }
        }
    }
}
