// =============================================================================
//  DiagCatalog 구현 — 카탈로그 테이블 + 유형별 예시 트레이스 렌더링
// =============================================================================
#include "DiagCatalog.h"

#include <QPainter>
#include <QColor>
#include <array>
#include <cmath>
#include <functional>

namespace diagui {

// ── 카탈로그 (라벨 키는 DiagConfig.h.in kLabels 와 동일) ───────────────────────
static const std::array<DiagEntry, 12> kCatalog = {{
    { QStringLiteral("normal"),
      QStringLiteral("Normal"),
      QStringLiteral("The two dotted traces run parallel and stable. Movement is in good condition."),
      QStringLiteral("No action needed. Keep the current condition."),
      PatternNormal, true },

    { QStringLiteral("beat_error_high"),
      QStringLiteral("Beat Error High"),
      QStringLiteral("The movement runs fine, but the gap between the two lines (beat error) is large (about 3 ms)."),
      QStringLiteral("Adjust the beat error first, then re-regulate the rate (accuracy)."),
      PatternBeatErrorHigh, false },

    { QStringLiteral("rate_fast"),
      QStringLiteral("Rate Fast"),
      QStringLiteral("The trace slopes steeply upward. The movement runs too fast (+)."),
      QStringLiteral("Re-regulate the rate (slower)."),
      PatternRateFast, false },

    { QStringLiteral("rate_slow"),
      QStringLiteral("Rate Slow"),
      QStringLiteral("The trace slopes steeply downward. The movement runs too slow (-)."),
      QStringLiteral("Re-regulate the rate (faster)."),
      PatternRateSlow, false },

    { QStringLiteral("gear_train_defect"),
      QStringLiteral("Gear Train Defect"),
      QStringLiteral("Large but regular rate variations appear. This indicates a gear train defect."),
      QStringLiteral("Inspect the gear train and replace parts if necessary."),
      PatternGearTrain, false },

    { QStringLiteral("irregular_insufficient_amplitude"),
      QStringLiteral("Irregular, Insufficient Amplitude"),
      QStringLiteral("The rate is irregular and shows defects, usually due to insufficient amplitude."),
      QStringLiteral("Perform an overhaul (disassemble, clean, and reassemble)."),
      PatternIrregularAmp, false },

    { QStringLiteral("balance_knock_occasional"),
      QStringLiteral("Balance Knock, Occasional"),
      QStringLiteral("The balance wheel occasionally 'knocks'. Caused by excessive amplitude (<330 deg). Heard as a double 'tick-tock' on the speaker."),
      QStringLiteral("Replace the mainspring, pallet stones and/or escape wheel."),
      PatternKnockOccasional, false },

    { QStringLiteral("balance_knock_continuous"),
      QStringLiteral("Balance Knock, Continuous"),
      QStringLiteral("The balance wheel continuously 'knocks'. Caused by excessive amplitude (<330 deg). Heard as a double 'tick-tock' on the speaker."),
      QStringLiteral("Replace the mainspring, pallet stones and/or escape wheel."),
      PatternKnockContinuous, false },

    { QStringLiteral("escape_wheel_untrue"),
      QStringLiteral("Escape-wheel Untrue"),
      QStringLiteral("The escape wheel runs untrue (wobbling). A periodic sine shape appears (15-21 teeth = one anchor-wheel turn)."),
      QStringLiteral("Replace the escape wheel."),
      PatternEscapeWheelUntrue, false },

    { QStringLiteral("entry_pallet_poor"),
      QStringLiteral("Entry Pallet Poor"),
      QStringLiteral("The entry pallet does not lock properly or is smeared. The trace becomes blurred and thick."),
      QStringLiteral("Clean the escapement or replace the escape wheel."),
      PatternEntryPallet, false },

    { QStringLiteral("hairspring_touches"),
      QStringLiteral("Hairspring Touches"),
      QStringLiteral("The hairspring touches the regulator pin or the stud. A scratching noise is heard on the speaker."),
      QStringLiteral("Center the hairspring and adjust the rate."),
      PatternHairspring, false },

    { QStringLiteral("slow_oscillating_after_position_change"),
      QStringLiteral("Slow Oscillating After Position Change"),
      QStringLiteral("The balance wheel oscillates slowly after a position change. The balance/gear-train bearings are poorly lubricated or dry."),
      QStringLiteral("Clean and lubricate; perform an overhaul if necessary."),
      PatternSlowOscillating, false },
}};

const DiagEntry *lookup(const QString &labelKey)
{
    for (const auto &e : kCatalog)
        if (e.key == labelKey) return &e;
    return nullptr;
}

// ── 예시 트레이스 그리기 ──────────────────────────────────────────────────────
//  norm(t): t∈[0,1] → 정규화 높이 [0,1] (0=바닥, 1=천장). 두 점선을 center±gap/2 로.
namespace {

const QColor kDot(40, 40, 40);

QRectF dotAt(double x, double y, double s = 3.0)
{
    return QRectF(x - s * 0.5, y - s * 0.5, s, s);
}

// 두 평행 점선(center ± gap/2). pad: 위/아래 여백 비율.
void twinTrace(QPainter &p, const QRectF &r,
               const std::function<double(double)> &norm,
               const std::function<double(double)> &gapPx,
               int dots = 76)
{
    const double pad = r.height() * 0.16;
    const double top = r.top() + pad;
    const double h   = r.height() - 2 * pad;
    for (int i = 0; i < dots; ++i) {
        const double t  = static_cast<double>(i) / (dots - 1);
        const double yc = top + (1.0 - norm(t)) * h;
        const double g  = gapPx(t) * 0.5;
        const double x  = r.left() + r.width() * 0.06 + t * r.width() * 0.88;
        p.fillRect(dotAt(x, yc - g), kDot);
        p.fillRect(dotAt(x, yc + g), kDot);
    }
}

// 단일 점선.
void singleTrace(QPainter &p, const QRectF &r,
                 const std::function<double(double)> &norm, int dots = 90)
{
    const double pad = r.height() * 0.16;
    const double top = r.top() + pad;
    const double h   = r.height() - 2 * pad;
    for (int i = 0; i < dots; ++i) {
        const double t = static_cast<double>(i) / (dots - 1);
        const double y = top + (1.0 - norm(t)) * h;
        const double x = r.left() + r.width() * 0.06 + t * r.width() * 0.88;
        p.fillRect(dotAt(x, y), kDot);
    }
}

} // namespace

void paintExample(QPainter &p, const QRect &rect, int pattern)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect, Qt::white);
    p.setPen(QColor(200, 200, 200));
    p.drawRect(rect.adjusted(0, 0, -1, -1));

    const QRectF r = rect;
    const double flatGap = 8.0;

    auto constGap = [flatGap](double) { return flatGap; };

    switch (pattern) {
    case PatternNormal:
        twinTrace(p, r, [](double) { return 0.5; }, constGap);
        break;

    case PatternBeatErrorHigh:
        // 일정한 큰 간격(비트 에러)을 유지한 채 평행하게 우상향(레퍼런스 2번).
        twinTrace(p, r,
                  [](double t) { return 0.42 + 0.16 * t; },
                  [](double) { return 20.0; });
        break;

    case PatternRateFast:
        twinTrace(p, r, [](double t) { return 0.15 + 0.6 * t; }, constGap);
        break;

    case PatternRateSlow:
        twinTrace(p, r, [](double t) { return 0.85 - 0.6 * t; }, constGap);
        break;

    case PatternGearTrain:
        // 크고 규칙적인 단일 점선 파형(약 2주기) — 레퍼런스 1번.
        singleTrace(p, r,
                    [](double t) { return 0.5 + 0.33 * std::sin(2.0 * M_PI * 1.9 * t); });
        break;

    case PatternIrregularAmp:
        twinTrace(p, r,
                  [](double t) {
                      return 0.5 + 0.18 * std::sin(8.0 * M_PI * t)
                                 + 0.09 * std::sin(17.0 * M_PI * t + 1.0);
                  },
                  [](double) { return 10.0; });
        break;

    case PatternKnockOccasional: {
        // 두 점선이 평행하게 이어지며 완만한 계단(2단)으로 상승(레퍼런스 2번).
        auto ramp = [](double x, double a, double b) {
            if (x < a) return 0.0;
            if (x > b) return 1.0;
            return (x - a) / (b - a);
        };
        auto step = [ramp](double t) {
            double lvl = 0.30;
            lvl += 0.20 * ramp(t, 0.28, 0.40);   // 1단
            lvl += 0.20 * ramp(t, 0.60, 0.72);   // 2단
            return lvl;
        };
        twinTrace(p, r, step, [](double) { return 11.0; });
        break;
    }

    case PatternKnockContinuous: {
        // 아래 두 줄은 연속, 맨 위 한 줄만 군데군데 끊긴(점멸) 평행 트레이스(레퍼런스 2번).
        const double pad = r.height() * 0.16;
        const double top = r.top() + pad;
        const double h   = r.height() - 2 * pad;
        auto wave = [](double t) {
            return 0.42 + 0.06 * std::sin(2.0 * M_PI * 1.6 * t)
                        + 0.03 * std::sin(2.0 * M_PI * 3.3 * t + 0.7);
        };
        const int dots = 120;
        for (int i = 0; i < dots; ++i) {
            const double t  = static_cast<double>(i) / (dots - 1);
            const double x  = r.left() + r.width() * 0.05 + t * r.width() * 0.90;
            const double yc = top + (1.0 - wave(t)) * h;
            // 연속 하단 두 줄(바짝 붙게)
            p.fillRect(dotAt(x, yc), kDot);
            p.fillRect(dotAt(x, yc + 5), kDot);
            // 상단 끊긴 줄 — 큰 덩어리(segment) 단위로 띄엄띄엄(켜짐 62% / 꺼짐 38%)
            const double phase = t * 4.5;
            const double frac  = phase - std::floor(phase);
            if (frac < 0.62)
                p.fillRect(dotAt(x, yc - 16), kDot);
        }
        break;
    }

    case PatternEscapeWheelUntrue:
        singleTrace(p, r,
                    [](double t) { return 0.5 + 0.32 * std::sin(2.0 * M_PI * 2.0 * t); });
        break;

    case PatternEntryPallet: {
        // 두 선이 가깝게 붙어 지글거리며 번지듯 두꺼운 우상향 트레이스(레퍼런스 2번).
        const double pad = r.height() * 0.16;
        const double top = r.top() + pad;
        const double h   = r.height() - 2 * pad;
        const int dots = 130;
        for (int i = 0; i < dots; ++i) {
            const double t  = static_cast<double>(i) / (dots - 1);
            const double x  = r.left() + r.width() * 0.06 + t * r.width() * 0.88;
            const double yc = top + (1.0 - (0.30 + 0.42 * t)) * h;
            const double gap = 6.0 + 2.0 * std::sin(t * 23.0);     // 살짝 들쭉날쭉한 간격
            const double j1  = 1.8 * std::sin(t * 47.0 + 0.3);     // 위 선 지터
            const double j2  = 1.8 * std::sin(t * 51.0 + 1.7);     // 아래 선 지터
            // 번짐(thick): 각 선 주변에 점을 겹쳐 두껍게 표현
            for (double dy = -1.5; dy <= 1.5; dy += 1.5) {
                p.fillRect(dotAt(x, yc - gap * 0.5 + j1 + dy, 2.6), kDot);
                p.fillRect(dotAt(x, yc + gap * 0.5 + j2 + dy, 2.6), kDot);
            }
        }
        break;
    }

    case PatternHairspring: {
        // 윗선(촘촘) + 아래로 흩뿌려진 점
        const double pad = r.height() * 0.16;
        const double top = r.top() + pad;
        const double h   = r.height() - 2 * pad;
        const double yTop = top + (1.0 - 0.72) * h;
        for (int i = 0; i < 90; ++i) {
            const double x = r.left() + r.width() * 0.06 + (i / 89.0) * r.width() * 0.88;
            p.fillRect(dotAt(x, yTop), kDot);
        }
        for (int i = 0; i < 60; ++i) {
            const double t = (i % 30) / 30.0;
            const double x = r.left() + r.width() * 0.06 + t * r.width() * 0.88;
            const double frac = 0.1 + 0.6 * std::fabs(std::sin(i * 2.3));
            const double y = top + (1.0 - (0.62 - frac * 0.5)) * h;
            p.fillRect(dotAt(x, y), kDot);
        }
        break;
    }

    case PatternSlowOscillating:
        // U자 곡선(딥 후 회복)
        singleTrace(p, r,
                    [](double t) { return 0.70 - 0.46 * std::sin(M_PI * t); });
        break;

    default:
        twinTrace(p, r, [](double) { return 0.5; }, constGap);
        break;
    }

    p.restore();
}

} // namespace diagui
