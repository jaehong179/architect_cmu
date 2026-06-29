#pragma once
// =============================================================================
//  DiagDetailDialog — 진단 상세 가이드 창(모달)
// -----------------------------------------------------------------------------
//  배너 클릭 시 화면 가운데에 뜨는 별도 창. 진단된 고장 유형의 예시 그래프와
//  원인·조치 사항을 보여준다. 닫기 버튼으로 닫는다.
//  의존성: Qt Widgets + DiagCatalog 뿐.
// =============================================================================
#include <QDialog>

class DiagDetailDialog : public QDialog
{
    Q_OBJECT
public:
    // labelKey: 모델 출력 라벨 키(kLabels), confidence: 0~1.
    explicit DiagDetailDialog(const QString &labelKey, float confidence,
                              QWidget *parent = nullptr);
};
