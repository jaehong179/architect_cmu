#ifndef RADARCHARTWIDGET_H
#define RADARCHARTWIDGET_H

#include <QWidget>
#include <QMap>
#include <QString>

class RadarChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RadarChartWidget(QWidget *parent = nullptr);

    // 포지션별 Rate 값을 설정하고 업데이트
    void setPositionRate(const QString &position, double rate, bool valid);
    void clearData();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QMap<QString, double> mRates;
    QMap<QString, bool> mValids;

    // 포지션 이름에 따른 각도(라디안) 리턴
    double getPositionAngleRad(const QString &pos) const;
};

#endif // RADARCHARTWIDGET_H
