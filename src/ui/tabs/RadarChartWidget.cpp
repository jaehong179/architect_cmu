#include "RadarChartWidget.h"
#include "../PositionNames.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

RadarChartWidget::RadarChartWidget(QWidget *parent) : QWidget(parent)
{
    // 기본 크기 설정
    setMinimumSize(250, 250);
}

void RadarChartWidget::setPositionRate(const QString &position, double rate, bool valid)
{
    const QString key = canonicalCorePositionKey(position);
    if (key.isEmpty()) return;
    mRates[key] = rate;
    mValids[key] = valid;
    update(); // 위젯 다시 그리기
}

void RadarChartWidget::clearData()
{
    mRates.clear();
    mValids.clear();
    update();
}

double RadarChartWidget::getPositionAngleRad(const QString &pos) const
{
    // 6개 포지션을 360도 공간에 60도 간격으로 균등 배치 (시계 방향 순서)
    // CH (12시)  = 90도
    // CB (2시)   = 30도
    // 9H (4시)   = 330도 (-30도)
    // 6H (6시)   = 270도 (-90도)
    // 3H (8시)   = 210도 (-150도)
    // 12H (10시) = 150도 (-210도)
    double deg = 0.0;
    if (pos == QStringLiteral("CH")) deg = 90.0;
    else if (pos == QStringLiteral("CB")) deg = 30.0;
    else if (pos == QStringLiteral("9H")) deg = 330.0;
    else if (pos == QStringLiteral("6H")) deg = 270.0;
    else if (pos == QStringLiteral("3H")) deg = 210.0;
    else if (pos == QStringLiteral("12H")) deg = 150.0;
    else return -1.0; // 정의되지 않은 포지션

    return deg * M_PI / 180.0;
}

void RadarChartWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // 1. 배경 채우기 (다크 테마)
    painter.fillRect(rect(), QColor(20, 20, 20));

    // 2. 좌표 설정
    QPointF center(width() / 2.0, height() / 2.0);
    const QFont labelFont(QStringLiteral("Segoe UI"), 8, QFont::Bold);
    const QFontMetrics labelFm(labelFont);
    const QString labels[6] = {
        QStringLiteral("Dial Down"), QStringLiteral("Dial Up"), QStringLiteral("Crown Down"),
        QStringLiteral("Crown Left"), QStringLiteral("Crown Up"), QStringLiteral("Crown Right")
    };
    int maxLabelWidth = 0;
    for (const QString &label : labels) {
        maxLabelWidth = std::max(maxLabelWidth, labelFm.horizontalAdvance(label));
    }

    const double labelGap = 12.0;
    const double edgePadding = 8.0;
    const double maxRadiusX = width() / 2.0 - (labelGap + maxLabelWidth + edgePadding);
    const double maxRadiusY = height() / 2.0 - (labelGap + labelFm.height() + edgePadding);
    double R = std::min(maxRadiusX, maxRadiusY);
    double R_0 = R * 0.5; // 0 s/d 기준선 반경

    if (R <= 10.0) return;

    // 3. 동심원 격자선 그리기 (-20, -10, 0, +10, +20 s/d)
    // 최대 한도는 ±30 s/d로 설정
    double maxVal = 30.0;

    auto getRadiusForValue = [&](double val) -> double {
        double norm = std::max(-maxVal, std::min(val, maxVal));
        return R_0 * (1.0 + norm / maxVal);
    };

    // 격자 원 반지름 계산
    double r_m20 = getRadiusForValue(-20.0);
    double r_m10 = getRadiusForValue(-10.0);
    double r_0   = R_0;
    double r_p10 = getRadiusForValue(10.0);
    double r_p20 = getRadiusForValue(20.0);

    // (a) -20 s/d 원 (빨간색)
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(160, 40, 40, 150), 1, Qt::SolidLine));
    painter.drawEllipse(center, r_m20, r_m20);

    // (b) -10 s/d 원 (어두운 회색)
    painter.setPen(QPen(QColor(80, 80, 80, 150), 1, Qt::SolidLine));
    painter.drawEllipse(center, r_m10, r_m10);

    // (c) 0 s/d 원 (굵은 연회색/흰색)
    painter.setPen(QPen(QColor(180, 180, 180, 200), 1.5, Qt::SolidLine));
    painter.drawEllipse(center, r_0, r_0);

    // (d) +10 s/d 원 (어두운 회색)
    painter.setPen(QPen(QColor(80, 80, 80, 150), 1, Qt::SolidLine));
    painter.drawEllipse(center, r_p10, r_p10);

    // (e) +20 s/d 원 (빨간색)
    painter.setPen(QPen(QColor(160, 40, 40, 150), 1, Qt::SolidLine));
    painter.drawEllipse(center, r_p20, r_p20);

    // (f) 격자선 수치 텍스트 표시 (12시 방향 약간 오른쪽 또는 중심 세로선)
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 7, QFont::Bold));
    
    auto drawGridText = [&](double val, const QString &text, const QColor &color, double r) {
        painter.setPen(color);
        // 중심선 바로 위에 그리면 축선과 겹치므로 Y 오프셋을 조절하고 X는 중심 근처로
        QRectF textRect(center.x() - 15, center.y() - r - 6, 30, 12);
        painter.drawText(textRect, Qt::AlignCenter, text);
    };

    drawGridText(-20.0, QStringLiteral("-20"), QColor(180, 60, 60), r_m20);
    drawGridText(-10.0, QStringLiteral("-10"), QColor(130, 130, 130), r_m10);
    drawGridText(0.0,   QStringLiteral("0"),   QColor(220, 220, 220), r_0);
    drawGridText(10.0,  QStringLiteral("+10"), QColor(130, 130, 130), r_p10);
    drawGridText(20.0,  QStringLiteral("+20"), QColor(180, 60, 60), r_p20);

    // 4. 6개 방위 축선 및 포지션 라벨 그리기
    const QString corePos[6] = {
        QStringLiteral("CB"), QStringLiteral("CH"), QStringLiteral("12H"),
        QStringLiteral("3H"), QStringLiteral("6H"), QStringLiteral("9H")
    };

    painter.setFont(labelFont);

    for (const QString &pos : corePos) {
        double rad = getPositionAngleRad(pos);
        if (rad < 0) continue;

        QPointF dir(std::cos(rad), -std::sin(rad));
        QPointF endPt = center + dir * R;

        // 방사축 라인 (매우 어둡고 투명한 선)
        painter.setPen(QPen(QColor(60, 60, 60, 120), 1, Qt::DashLine));
        painter.drawLine(center, endPt);

        // 포지션 라벨 텍스트 배치 (라벨 길이에 맞춰 동적 배치 + 경계 클램프)
        QPointF labelAnchor = center + dir * (R + labelGap);
        painter.setPen(QColor(150, 150, 150));

        QString label;
        if (pos == QStringLiteral("CH")) label = QStringLiteral("Dial Up");
        else if (pos == QStringLiteral("CB")) label = QStringLiteral("Dial Down");
        else if (pos == QStringLiteral("9H")) label = QStringLiteral("Crown Right");
        else if (pos == QStringLiteral("6H")) label = QStringLiteral("Crown Left");
        else if (pos == QStringLiteral("3H")) label = QStringLiteral("Crown Up");
        else if (pos == QStringLiteral("12H")) label = QStringLiteral("Crown Down");
        else label = pos;

        const double textWidth = labelFm.horizontalAdvance(label);
        const double textHeight = labelFm.height();

        double x = 0.0;
        double y = 0.0;

        if (std::abs(dir.y()) > 0.95) {
            x = labelAnchor.x() - textWidth / 2.0;
            y = (dir.y() < 0) ? (labelAnchor.y() - textHeight) : labelAnchor.y();
        } else if (dir.x() > 0) {
            x = labelAnchor.x();
            y = labelAnchor.y() - textHeight / 2.0;
        } else {
            x = labelAnchor.x() - textWidth;
            y = labelAnchor.y() - textHeight / 2.0;
        }

        x = std::clamp(x, edgePadding, width() - edgePadding - textWidth);
        y = std::clamp(y, edgePadding, height() - edgePadding - textHeight);

        painter.drawText(QRectF(x, y, textWidth, textHeight), Qt::AlignCenter, label);
    }

    // 5. 측정 데이터 플로팅 및 폐곡선 그리기
    struct PlottedPoint {
        QString pos;
        double angle;
        QPointF pt;
        double val;
        bool isCritical;
    };

    QVector<PlottedPoint> pts;
    for (const QString &pos : corePos) {
        if (mValids.value(pos, false)) {
            double rate = mRates.value(pos, 0.0);
            double rad = getPositionAngleRad(pos);
            double r_val = getRadiusForValue(rate);
            
            QPointF dir(std::cos(rad), -std::sin(rad));
            QPointF pt = center + dir * r_val;
            
            PlottedPoint p;
            p.pos = pos;
            p.angle = rad;
            p.pt = pt;
            p.val = rate;
            p.isCritical = (std::abs(rate) > 20.0); // ±20 s/d 초과 시 임계값 경고
            pts.push_back(p);
        }
    }

    if (pts.size() >= 3) {
        // (a) 각도에 따라 반시계 방향 정렬 (0 ~ 2*PI)
        std::sort(pts.begin(), pts.end(), [](const PlottedPoint &a, const PlottedPoint &b) {
            return a.angle < b.angle;
        });

        // (b) 베이지어 곡선(Catmull-Rom 기반 Cubic Spline) 경로 생성 및 채우기
        QPainterPath areaPath;
        areaPath.moveTo(pts[0].pt);

        int n = pts.size();
        double tension = 0.20; // 곡선의 부드러움 텐션 계수

        for (int i = 0; i < n; ++i) {
            int prevIdx  = (i - 1 + n) % n;
            int currIdx  = i;
            int nextIdx  = (i + 1) % n;
            int next2Idx = (i + 2) % n;

            QPointF pPrev  = pts[prevIdx].pt;
            QPointF pCurr  = pts[currIdx].pt;
            QPointF pNext  = pts[nextIdx].pt;
            QPointF pNext2 = pts[next2Idx].pt;

            // Catmull-Rom 방식을 근사하여 제어점 계산
            QPointF c1 = pCurr + tension * (pNext - pPrev);
            QPointF c2 = pNext - tension * (pNext2 - pCurr);

            areaPath.cubicTo(c1, c2, pNext);
        }

        // 파란색 반투명 영역 채우기
        painter.setBrush(QBrush(QColor(0, 162, 232, 35)));
        painter.setPen(Qt::NoPen);
        painter.drawPath(areaPath);

        // 파란색 외곽선 그리기
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 162, 232, 220), 2.5, Qt::SolidLine));
        painter.drawPath(areaPath);

    } else if (pts.size() == 2) {
        // 데이터가 2개인 경우 선 하나만 그림
        painter.setPen(QPen(QColor(0, 162, 232, 200), 2, Qt::SolidLine));
        painter.drawLine(pts[0].pt, pts[1].pt);
    }

    // (c) 각 점 마커 드로잉 (외곽선 흰색 + 내부 파랑/빨강)
    for (const auto &p : pts) {
        painter.setPen(QPen(Qt::white, 1.5));
        if (p.isCritical) {
            painter.setBrush(QBrush(QColor(230, 40, 40))); // 오차가 큰 경우 빨간색 마커
        } else {
            painter.setBrush(QBrush(QColor(0, 162, 232))); // 정상 범위 파란색 마커
        }
        painter.drawEllipse(p.pt, 4.0, 4.0);
    }
}
