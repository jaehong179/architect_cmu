#ifndef LEGENDBOX_H
#define LEGENDBOX_H
// 접이식 범례(공통) — 모든 탭의 범례를 동일 스타일로 통일.
//  토글 버튼(기본 '펼침') + 정렬된 HTML 표 본문(색상 코드). WaveformCompare 스타일 기준.
#include <QString>
class QWidget;

// tableHtml: "<table…>…</table>" 형태(행: <tr><td><b>항목 :</b></td><td>색상 설명</td></tr>).
//  startExpanded: true(기본)=펼침, false=접힘. 버튼으로 접기/펼치기.
QWidget *makeLegendBox(const QString &tableHtml, QWidget *parent, bool startExpanded = true);
#endif // LEGENDBOX_H
