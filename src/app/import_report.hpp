#pragma once
// The shared import report (the screens refactor of R097.e1, R098.a3 and R097.e2): one state
// for the last import of a format, one chip function and one "show the last report" path,
// parameterised by the format's face. The window used to hold the IFC and STEP copies of this
// state (the file, the report, the elapsed time, the restarts, the action and the chip each
// twice); a new format (R102.c, the .skp import) needs only this state and its strings.
#include <QJsonArray>
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
    // The banner's restart count: the IFC and STEP imports have the helper's restart protocol; a helper
    // with none (the SketchUp import, ADR 0176 section 1) turns it off.
    bool restarts = true;
    // The notices as the report's rows, {message, count}. Unset: the report's own list. A face whose report
    // keeps them another way reads them here.
    std::function<QJsonArray(const ImportReportState &)> notices;
    // The accounting identity in the face's own words (one line per kind). Set: it replaces the default
    // line and its segment bar, which only the IFC and STEP identities use.
    std::function<QString(const ImportReportState &)> identity;
    // A note under the grouped list (the SketchUp import's trademark note, ADR 0176 section 8). Unset or "":
    // no note.
    std::function<QString(const ImportReportState &)> note;
};
// The IFC import's face (the design's 16.2) and the STEP import's (ADR 0172 section 8),
// defined in ifc_sheets.cpp with the rest of the import screens.
ImportReportFace ifcImportReportFace();
ImportReportFace stepImportReportFace();
// The SketchUp import's face (ADR 0176 section 9), the same shared builder with its own words.
ImportReportFace skpImportReportFace();
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
