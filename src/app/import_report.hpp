#pragma once
// The shared import report (the screens refactor of R097.e1, R098.a3 and R097.e2): one state
// for the last import of a format, one chip function and one "show the last report" path,
// parameterised by the format's face. The window used to hold the IFC and STEP copies of this
// state (the file, the report, the elapsed time, the restarts, the action and the chip each
// twice); a new format (R102.c, the .skp import) needs only this state and its strings.
#include <QJsonObject>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <utility>
#include <vector>
namespace sketchy {
// The last import of a format (the screens refactor): done is a committed import (the chip's
// lifetime is that model's, 16.1); a refusal or a failure leaves the previous state, and a
// clear (a new model) empties it.
struct ImportReportState {
    bool done = false; // a committed import (the report exists); the action and chip follow it
    std::string code; // "" on success
    QString fileName;
    QJsonObject report;
    qint64 elapsedMs = 0;
    int restarts = 0;
};
// A report sheet's face (the format-specific parts; the rest — the banner, the identity with
// its bar, the grouped list, the notices and the footer — is the shared behaviour in
// ifc_sheets.cpp). `summary`, `how` and `groups` read the helper's report.
struct ImportReportFace {
    QString sheetName; // "ifcImportReport"
    QString title; // "IFC import report"
    QString prefix; // the object-name prefix ("ifcReport" ...)
    QString identitySubject; // "entities" / "nodes": what was read
    QString chipWord; // "IFC" / "STEP": the word in the chip's text
    // What needs attention after the import (the chip's warning line): the IFC import's is what
    // the helper did not convert (the accounting's omitted); the STEP import's is the helper's
    // own (the solids it read but did not convert — the conversion omits nothing).
    std::function<int(const ImportReportState &)> attention;
    // The format-specific parts, read from the state's report (the file name rides the state).
    std::function<QString(const ImportReportState &)> summary;
    std::function<QString(const ImportReportState &)> how; // the "how it was read" block
    // The grouped list: a group name and its rows (a label and a count, pre-formatted).
    std::function<std::vector<std::pair<QString, QStringList>>(const ImportReportState &)> groups;
};
// The IFC import's face (the design's 16.2) and the STEP import's (ADR 0172 section 8),
// defined in ifc_sheets.cpp with the rest of the import screens.
ImportReportFace ifcImportReportFace();
ImportReportFace stepImportReportFace();
// The report chip (the design's mockup `after`): while the imported model is open it says
// what the import produced and reopens the report; the accessible name follows the design's
// 16.9 wording. Hidden until a committed import produces it.
void setImportReportChip(QPushButton *chip, const ImportReportFace &face,
                          const ImportReportState &state);
// One "show the last report" path: the report sheet with the format's face. The IFC and STEP
// report actions and the two chips all lead here (window_ifc.cpp, window_step.cpp).
void showImportReport(QWidget *parent, const ImportReportFace &face,
                       const ImportReportState &state);
} // namespace sketchy
