// DiagWorker.h — Stop 시 t1/t3 시계열을 받아 진단(고장유형 분류)을 전용 스레드에서 수행.
//   기존 vision::VisionWorker 와 동일한 QObject+QThread 패턴. UI/측정 핫패스와 독립(비차단).
//   슬라이딩 윈도우(WINDOW=64, HOP=32, 반 겹침)로 여러 번 추론 후 확률 평균 보팅.
#ifndef DIAG_WORKER_H
#define DIAG_WORKER_H

#include <QByteArray>
#include <QObject>
#include <QVector>

#include <memory>
#include <vector>

namespace vision { class TfliteApi; }   // TFLite C API 동적 로더(vision 과 공유)

namespace diag {

class DiagWorker : public QObject
{
    Q_OBJECT
public:
    explicit DiagWorker(QObject *parent = nullptr);
    ~DiagWorker() override;

public slots:
    // 스레드 시작 후 1회 호출(라이브러리/모델 로드는 워커 스레드에서 이뤄져야 한다).
    void init();
    // 진단 실행: t1=rateTicY, t3=rateTocY. 공통 최소 길이로 맞춰 윈도우 보팅.
    //   데이터가 64 미만이면 error() 시그널로 사유 통지(진단 미수행).
    void runDiagnosis(const QVector<double> &t1, const QVector<double> &t3);

signals:
    // 진단 결과(라벨 + 평균 신뢰도 + 사용 윈도우 수).
    void resultReady(const QString &label, float confidence, int windows);
    // 진단 불가/실패 사유(데이터 부족, 모델 미로드 등).
    void error(const QString &message);

private:
    std::unique_ptr<vision::TfliteApi> mTflite;
    QByteArray                          mModelData;  // qrc 모델 바이트(인터프리터보다 오래 유지)
    bool                                mReady = false;
};

} // namespace diag

#endif // DIAG_WORKER_H
