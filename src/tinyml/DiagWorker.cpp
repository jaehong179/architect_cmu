// DiagWorker.cpp — t1/t3 슬라이딩 윈도우 추론 + 보팅 구현.
#include "DiagWorker.h"

#include "DiagConfig.h"
#include "DiagPreprocess.h"
#include "TfliteApi.h"

#include <QDebug>
#include <QFile>

#include <algorithm>
#include <cmath>
#include <vector>

namespace diag {

DiagWorker::DiagWorker(QObject *parent)
    : QObject(parent)
    , mTflite(std::make_unique<vision::TfliteApi>())
{
}

DiagWorker::~DiagWorker() = default;

void DiagWorker::init()
{
    if (!mTflite->loadLibrary()) {
        qWarning() << "[diag] TFLite load failed:" << QString::fromStdString(mTflite->lastError());
        return;
    }
    QFile mf(QString::fromUtf8(kModelResource));
    if (!mf.open(QIODevice::ReadOnly)) {
        qWarning() << "[diag] cannot open embedded model:" << kModelResource;
        return;
    }
    mModelData = mf.readAll();   // 인터프리터 수명 동안 보관
    mf.close();
    if (!mTflite->initModel(mModelData.constData(),
                            static_cast<std::size_t>(mModelData.size()),
                            kNumThreads)) {
        qWarning() << "[diag] model init failed:" << QString::fromStdString(mTflite->lastError());
        return;
    }
    mReady = true;
    qInfo() << "[diag] model ready (fp32). classes:" << kNumClasses;
}

void DiagWorker::runDiagnosis(const QVector<double> &t1, const QVector<double> &t3)
{
    if (!mReady || !mTflite->isReady()) {
        emit error(QStringLiteral("Diagnosis unavailable: model not loaded"));
        return;
    }

    // t1/t3 공통 최소 길이로 정렬(짝을 이루는 구간만 사용).
    const int n = static_cast<int>(std::min(t1.size(), t3.size()));
    if (n < kWindow) {
        emit error(QStringLiteral("Diagnosis skipped: insufficient data (%1 < %2 samples)")
                       .arg(n).arg(kWindow));
        return;
    }

    std::vector<double> votes(kNumClasses, 0.0);
    int nWin = 0;

    std::vector<float> signal, features, probs;
    for (int start = 0; start + kWindow <= n; start += kHop) {
        const double *w1 = t1.constData() + start;
        const double *w3 = t3.constData() + start;

        // 평탄(무신호) 윈도우는 건너뜀(inference_diag.py 와 동일).
        double maxAbs = 0.0;
        for (int i = 0; i < kWindow; ++i) maxAbs = std::max(maxAbs, std::abs(w1[i]));
        if (maxAbs < 1e-6) continue;

        makeSignal(w1, w3, kWindow, signal);
        computeFeaturesStandardized(w1, w3, kWindow, features);

        if (!mTflite->invoke({signal, features}, probs)) {
            qWarning() << "[diag] invoke failed:" << QString::fromStdString(mTflite->lastError());
            continue;
        }
        if (static_cast<int>(probs.size()) < kNumClasses) continue;
        for (int c = 0; c < kNumClasses; ++c) votes[c] += probs[c];
        ++nWin;
    }

    if (nWin == 0) {
        emit error(QStringLiteral("Diagnosis skipped: no usable (non-flat) windows"));
        return;
    }

    int best = 0;
    for (int c = 1; c < kNumClasses; ++c)
        if (votes[c] > votes[best]) best = c;
    const float conf = static_cast<float>(votes[best] / nWin);

    const QString label = QString::fromUtf8(kLabels[best].data(),
                                            static_cast<int>(kLabels[best].size()));
    qInfo().noquote() << QStringLiteral("[diag] %1%2 (%3%, %4 windows)")
                             .arg(conf < kConfThresh ? QStringLiteral("? ") : QString())
                             .arg(label)
                             .arg(QString::number(conf * 100.0f, 'f', 1))
                             .arg(nWin);

    emit resultReady(label, conf, nWin);
}

} // namespace diag
