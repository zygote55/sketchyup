#include "app/catalog_report.hpp"

#include "app/ifc_sheets.hpp"
#include "app/report_sheet.hpp"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace sketchy {
namespace {

// The accounting identity (ADR 0171 section 4): every requested entry is inserted, refused with a
// reason, or not inserted because the request was refused as a whole. Nothing is inserted here,
// so the first count is always zero.
struct Accounting {
    qsizetype inserted{}, refused{}, notInserted{};
};

Accounting accounting(const CatalogInsertReport &report) {
    Accounting result;
    for (const auto &entry : report.entries) {
        if (entry.refused)
            ++result.refused;
        else
            ++result.notInserted;
    }
    return result;
}

QString entryCount(qsizetype count) {
    return count == 1 ? QStringLiteral("1 entry") : QStringLiteral("%1 entries").arg(count);
}

QString capitalised(const QString &text) {
    return text.isEmpty() ? text : text.left(1).toUpper() + text.mid(1);
}

// The banner and its sentence: what did not happen, then the reason and the next step (UX 16.6).
QString summaryText(const CatalogInsertReport &report, const Accounting &counts) {
    QStringList parts;
    if (!report.refusal.isEmpty())
        parts << QStringLiteral("The engine refused the request: %1.").arg(report.refusal);
    if (counts.refused > 0)
        parts << QStringLiteral("%1 %2 a missing or mismatched dependency. Fix or deselect %3, then "
                                "insert again.")
                     .arg(capitalised(entryCount(counts.refused)))
                     .arg(counts.refused == 1 ? QStringLiteral("has") : QStringLiteral("have"))
                     .arg(counts.refused == 1 ? QStringLiteral("it") : QStringLiteral("them"));
    parts << QStringLiteral("Your model and the catalog file are unchanged.");
    return parts.join(QLatin1Char(' '));
}

QString entryRow(const CatalogReportEntry &entry) {
    const QString tail = entry.refused ? entry.reason : QStringLiteral("refused with the request");
    return QStringLiteral("%1 · %2 · entry %3 — %4")
        .arg(entry.name, entry.kind)
        .arg(entry.number)
        .arg(tail);
}

} // namespace

CatalogReportResult showCatalogReport(QWidget *parent, const CatalogInsertReport &report) {
    CatalogReportResult result;
    const auto counts = accounting(report);
    const qsizetype requested = qsizetype(report.entries.size());

    ReportSheet sheet("catalogInsertReport", "Asset catalog: nothing was inserted", parent);
    sheet.resize(900, 620);
    sheet.summary->hide();

    auto *banner = new QLabel("✕ Nothing was inserted");
    banner->setObjectName("catalogReportBanner");
    banner->setStyleSheet("font-size: 16px; font-weight: 600; color: #c62828;");
    sheet.body->addWidget(banner);
    auto *summary = new QLabel(summaryText(report, counts));
    summary->setObjectName("catalogReportSummary");
    summary->setWordWrap(true);
    sheet.body->addWidget(summary);

    auto *columns = new QHBoxLayout;
    sheet.body->addLayout(columns, 1);

    // The left column: the counts, the accounting identity with its bar and legend, and how the
    // request was checked.
    auto *left = new QVBoxLayout;
    auto *counted = new QLabel(QStringLiteral("Entries requested: %1\nInserted: 0\nRefused, with a "
                                              "reason: %2\nNot inserted (the request was refused as "
                                              "a whole): %3")
                                   .arg(requested)
                                   .arg(counts.refused)
                                   .arg(counts.notInserted));
    counted->setObjectName("catalogReportCounts");
    counted->setTextFormat(Qt::PlainText);
    left->addWidget(counted);
    left->addWidget(new QLabel("Every entry is accounted for:"));
    addSegmentBar(left, counts.inserted, counts.refused, counts.notInserted,
                  QStringLiteral("catalogReportBar"));
    const bool balanced = counts.inserted + counts.refused + counts.notInserted == requested;
    auto *identity = new QLabel(
        balanced ? QStringLiteral("0 + %1 + %2 = %3 ✓ balanced")
                       .arg(counts.refused)
                       .arg(counts.notInserted)
                       .arg(requested)
                 : QStringLiteral("✕ not balanced: the parts do not add up to %1").arg(requested));
    identity->setObjectName("catalogReportIdentity");
    left->addWidget(identity);
    auto *legend = new QLabel(QStringLiteral("Inserted: 0\nRefused, with its reason: %1\nNot inserted, "
                                              "because the request was refused: %2")
                                  .arg(counts.refused)
                                  .arg(counts.notInserted));
    legend->setObjectName("catalogReportLegend");
    legend->setTextFormat(Qt::PlainText);
    left->addWidget(legend);
    auto *checked = new QLabel(QStringLiteral("How it was checked\nCatalog: %1\nEvery entry hash: "
                                              "checked\nEvery dependency: checked\nInsert placement: "
                                              "the model’s origin")
                                   .arg(report.catalog));
    checked->setObjectName("catalogReportChecked");
    checked->setTextFormat(Qt::PlainText);
    checked->setWordWrap(true);
    left->addWidget(checked);
    left->addStretch();
    auto *leftColumn = new QWidget;
    leftColumn->setLayout(left);
    leftColumn->setFixedWidth(300);
    columns->addWidget(leftColumn);

    // The right column: the grouped tree and the row actions for the current row.
    auto *tree = new QTreeWidget;
    tree->setObjectName("catalogReportTree");
    tree->setAccessibleName("Asset catalog report: refused entries, their dependencies and notices");
    tree->setHeaderHidden(true);
    tree->setColumnCount(1);
    tree->setWordWrap(true);
    tree->setRootIsDecorated(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setExpandsOnDoubleClick(false);

    auto *attention = new QTreeWidgetItem(tree, {QStringLiteral("Needs your attention · %1")
                                                     .arg(counts.refused + (report.refusal.isEmpty() ? 0 : 1))});
    attention->setFlags(attention->flags() & ~Qt::ItemIsSelectable);
    attention->setExpanded(true);
    if (!report.refusal.isEmpty()) {
        auto *refusal = new QTreeWidgetItem(attention, {QStringLiteral("The engine refused the request")});
        new QTreeWidgetItem(refusal, {report.refusal});
        refusal->setExpanded(true);
    }
    for (qsizetype index = 0; index < requested; ++index) {
        const auto &entry = report.entries[size_t(index)];
        if (!entry.refused)
            continue;
        auto *row = new QTreeWidgetItem(attention, {entryRow(entry)});
        row->setData(0, Qt::UserRole, qulonglong(index));
        row->setExpanded(true);
        for (const auto &dependency : entry.dependencies)
            new QTreeWidgetItem(row, {dependency});
        if (!entry.explanation.isEmpty())
            new QTreeWidgetItem(row, {entry.explanation});
    }

    auto *notInserted = new QTreeWidgetItem(tree, {QStringLiteral("Not inserted · %1").arg(counts.notInserted)});
    notInserted->setFlags(notInserted->flags() & ~Qt::ItemIsSelectable);
    notInserted->setExpanded(true);
    for (qsizetype index = 0; index < requested; ++index) {
        const auto &entry = report.entries[size_t(index)];
        if (entry.refused)
            continue;
        auto *row = new QTreeWidgetItem(notInserted, {entryRow(entry)});
        row->setData(0, Qt::UserRole, qulonglong(index));
    }

    auto *notices = new QTreeWidgetItem(tree, {QStringLiteral("Notices · %1").arg(report.notices.size())});
    notices->setFlags(notices->flags() & ~Qt::ItemIsSelectable);
    notices->setExpanded(true);
    for (const auto &notice : report.notices)
        new QTreeWidgetItem(notices, {notice});
    if (report.notices.isEmpty())
        new QTreeWidgetItem(notices, {QStringLiteral("None")});

    auto *rightColumn = new QVBoxLayout;
    rightColumn->addWidget(tree, 1);

    // The row actions follow the current row (UX 18.5, 18.7): they are Tab stops after the list.
    auto *actions = new QHBoxLayout;
    auto *selectOthers = new QPushButton("Select the other entries");
    selectOthers->setObjectName("catalogReportSelectOthers");
    selectOthers->setEnabled(counts.notInserted > 0);
    auto *remove = new QPushButton("Remove from selection");
    remove->setObjectName("catalogReportRemove");
    remove->setEnabled(false);
    actions->addWidget(selectOthers);
    actions->addWidget(remove);
    actions->addStretch();
    rightColumn->addLayout(actions);

    auto *rightWidget = new QWidget;
    rightWidget->setLayout(rightColumn);
    columns->addWidget(rightWidget, 1);

    QObject::connect(tree, &QTreeWidget::currentItemChanged, &sheet,
                     [remove, &report](QTreeWidgetItem *current, QTreeWidgetItem *) {
                         const bool isEntry = current && current->data(0, Qt::UserRole).isValid();
                         remove->setEnabled(isEntry);
                         if (isEntry) {
                             const auto &entry =
                                 report.entries[size_t(current->data(0, Qt::UserRole).toULongLong())];
                             remove->setText(QStringLiteral("Remove “%1” from selection").arg(entry.name));
                         } else {
                             remove->setText("Remove from selection");
                         }
                     });
    QObject::connect(selectOthers, &QPushButton::clicked, &sheet, [&result, &sheet] {
        result.choice = CatalogReportChoice::SelectOthers;
        sheet.accept();
    });
    QObject::connect(remove, &QPushButton::clicked, &sheet, [&result, &sheet, tree] {
        const auto *current = tree->currentItem();
        if (!current || !current->data(0, Qt::UserRole).isValid())
            return;
        result.choice = CatalogReportChoice::RemoveEntry;
        result.entry = size_t(current->data(0, Qt::UserRole).toULongLong());
        sheet.accept();
    });

    // Copy report and Close (the default) sit in the footer.
    auto *copy = new QPushButton("Copy report");
    copy->setObjectName("catalogReportCopy");
    QObject::connect(copy, &QPushButton::clicked, &sheet, [&report] {
        QApplication::clipboard()->setText(catalogReportText(report));
    });
    auto *close = new QPushButton("Close");
    close->setObjectName("catalogReportClose");
    close->setDefault(true);
    QObject::connect(close, &QPushButton::clicked, &sheet, &QDialog::reject);
    auto *footerLayout = qobject_cast<QHBoxLayout *>(sheet.footer->layout());
    footerLayout->addWidget(copy);
    footerLayout->addStretch();
    footerLayout->addWidget(close);
    sheet.buttons->hide();

    if (tree->topLevelItemCount() > 0)
        tree->setCurrentItem(attention->childCount() > 0 ? attention->child(0) : notInserted);
    sheet.exec();
    return result;
}

QString catalogReportText(const CatalogInsertReport &report) {
    const auto counts = accounting(report);
    const qsizetype requested = qsizetype(report.entries.size());
    QStringList lines;
    lines << "Asset catalog: nothing was inserted";
    lines << QStringLiteral("%1 · a request of %2").arg(report.catalog).arg(entryCount(requested));
    lines << summaryText(report, counts);
    lines << QStringLiteral("0 + %1 + %2 = %3 ✓ balanced")
                 .arg(counts.refused)
                 .arg(counts.notInserted)
                 .arg(requested);
    if (!report.refusal.isEmpty())
        lines << QStringLiteral("Refused request: %1").arg(report.refusal);
    for (const auto &entry : report.entries) {
        lines << QString();
        lines << entryRow(entry);
        for (const auto &dependency : entry.dependencies)
            lines << QStringLiteral("    %1").arg(dependency);
        if (!entry.explanation.isEmpty())
            lines << QStringLiteral("    %1").arg(entry.explanation);
    }
    for (const auto &notice : report.notices)
        lines << QStringLiteral("Notice: %1").arg(notice);
    return lines.join(QLatin1Char('\n'));
}

} // namespace sketchy
