import QtQuick
import QtQuick.Controls

// =============================================================================
//  PositionToast — 감지 포지션이 바뀌면 탭(그래프) 위에 잠깐 떠서 새 포지션을
//  큰 카드로 보여준 뒤(확대) 다시 줄어들며 사라지는 플로팅 토스트.
//  · 투명 배경(WA_TranslucentBackground) QQuickWidget 위에 그려져 그래프를 가리지 않음.
//  · cppBackend.detectedPosition 변경 시 자동 재생.
// =============================================================================
Item {
    id: toastRoot
    anchors.fill: parent

    // detectedPosition("W_CR"/"H_CR" 등) → { name, icon, present }
    function posInfo(dp) {
        var fallback = { "name": "N/A",
                         "icon": "qrc:/images/src/ui/images/pos_camera_disconnected.svg",
                         "present": false };
        if (!dp || dp === "" || dp === "?")
            return fallback;
        var isWatch = dp.startsWith("W_");
        var suffix = dp.substring(2);
        var map = {
            "DU": ["Dial Up", "pos_du"],   "DD": ["Dial Down", "pos_dd"],
            "CR": ["Crown Right", "pos_cr"], "CL": ["Crown Left", "pos_cl"],
            "CU": ["Crown Up", "pos_cu"],   "CD": ["Crown Down", "pos_cd"]
        };
        if (!(suffix in map))
            return fallback;
        var base = map[suffix][1];
        return {
            "name": map[suffix][0],
            "icon": "qrc:/images/src/ui/images/" + base + (isWatch ? ".svg" : "_empty.svg"),
            "present": isWatch
        };
    }

    property var info: posInfo(cppBackend.detectedPosition)

    // 큰 카드 — 탭 영역 가운데. 평소엔 scale 0.6 / opacity 0 으로 숨김.
    Rectangle {
        id: card
        anchors.centerIn: parent
        width: 300
        height: 300
        radius: 28
        color: "#1c1c24"
        border.width: 4
        border.color: toastRoot.info && toastRoot.info.present ? "#4caf50" : "#ff7043"
        opacity: 0
        scale: 0.6
        visible: opacity > 0
        transformOrigin: Item.Center

        Column {
            anchors.centerIn: parent
            spacing: 20

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: toastRoot.info ? toastRoot.info.icon : ""
                width: 168
                height: 168
                fillMode: Image.PreserveAspectFit
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: toastRoot.info ? toastRoot.info.name : ""
                color: "#ffffff"
                font.bold: true
                font.pixelSize: 34
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // 확대 → 유지 → 축소
    SequentialAnimation {
        id: anim
        ParallelAnimation {
            NumberAnimation { target: card; property: "opacity"; to: 1.0; duration: 240 }
            NumberAnimation { target: card; property: "scale"; to: 1.0; duration: 300; easing.type: Easing.OutBack }
        }
        PauseAnimation { duration: 1500 }
        ParallelAnimation {
            NumberAnimation { target: card; property: "opacity"; to: 0.0; duration: 260 }
            NumberAnimation { target: card; property: "scale"; to: 0.6; duration: 260; easing.type: Easing.InCubic }
        }
    }

    function play() {
        toastRoot.info = posInfo(cppBackend.detectedPosition);
        anim.restart();
    }

    Connections {
        target: cppBackend
        function onDetectedPositionChanged() {
            var dp = cppBackend.detectedPosition;
            if (dp && dp !== "" && dp !== "?")
                toastRoot.play();
        }
    }
}
