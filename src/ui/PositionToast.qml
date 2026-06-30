import QtQuick

// =============================================================================
//  PositionToast — 감지 포지션 변경 시 탭(그래프) 위에 잠깐 뜨는 카드.
//  · 카드가 QQuickWidget 전체를 꽉 채운다(불투명) → 플랫폼 투명 합성에 의존하지 않음.
//    위젯 자체의 지오메트리를 C++ 에서 확대/축소(grow/shrink)해 줌인 효과를 낸다.
//  · 내용(아이콘/글자)은 카드 크기에 비례 → 위젯이 커지면 함께 커진다.
//  · cppBackend.detectedPosition 으로 현재 포지션 아이콘/이름 표시.
// =============================================================================
Rectangle {
    id: card
    anchors.fill: parent
    color: "#1c1c24"
    radius: Math.min(width, height) * 0.09
    border.width: Math.max(2, height * 0.013)
    border.color: card.info && card.info.present ? "#4caf50" : "#ff7043"

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

    Column {
        anchors.centerIn: parent
        spacing: card.height * 0.05

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: card.info ? card.info.icon : ""
            width: card.height * 0.55
            height: card.height * 0.55
            fillMode: Image.PreserveAspectFit
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: card.info ? card.info.name : ""
            color: "#ffffff"
            font.bold: true
            font.pixelSize: Math.max(13, card.height * 0.11)
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
