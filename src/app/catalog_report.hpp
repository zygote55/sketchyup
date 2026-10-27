#pragma once
// R100.f3 (UX_DESIGN §18.5, §18.6, §18.7; ADR 0174 section 5): the missing-dependency report of the
// Asset catalogs dialog. A refused insert is laid out as the §16.5 report: a banner, the accounting
// identity "0 + b + c = n", and a tree of Needs your attention, Not inserted and Notices. A refused
// entry's row carries its failing dependencies and the reason in words. The report changes nothing;
// its row actions return to the dialog, which changes the selection.
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <vector>

class QWidget;

namespace sketchy {

// One requested entry of the insert, in the order the person selected it.
struct CatalogReportEntry {
    QString name;
    QString kind;                 // "component", "material", "style" or "template"
    qulonglong number{};          // the entry's number in its catalog, from 1
    bool refused{};               // refused with a reason, rather than not inserted with the request
    QString reason;               // the row's words, for example "missing texture"
    QString explanation;          // the sentence that says why, or empty
    QStringList dependencies;     // the failing dependencies: "Asset 7 “Chair fabric” · missing"
};

struct CatalogInsertReport {
    QString catalog;                          // "Office fittings 1.0.0", or "Two catalogs"
    QString refusal;                          // the engine's refusal of the whole request, or empty
    std::vector<CatalogReportEntry> entries;  // every requested entry
    QStringList notices;
};

enum class CatalogReportChoice { Close, SelectOthers, RemoveEntry };

struct CatalogReportResult {
    CatalogReportChoice choice{CatalogReportChoice::Close};
    size_t entry{}; // the index in `entries` of the entry RemoveEntry names
};

// Shows the report modally. Close is the default, and Esc closes. The row actions return their
// choice; the caller applies it to the selection and stays open.
CatalogReportResult showCatalogReport(QWidget *parent, const CatalogInsertReport &report);

// The report as plain text, for Copy report.
QString catalogReportText(const CatalogInsertReport &report);

} // namespace sketchy
