#include "app/report_sheet.hpp"
#include "app/window.hpp"
#include "automation/inspection.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QWindow>
#include <algorithm>
#include <limits>
namespace sketchy {
namespace {
class RepairHandoff final : public QObject {
    Viewport &view_;
    QTimer timer_;
    QElapsedTimer elapsed_, settled_;
    QPointer<QDialog> report_;
    QElapsedTimer activation_;
    bool canceled_{}, closeRequested_{}, synchronized_{};
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == &view_ && event->type() == QEvent::KeyPress &&
            static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
            canceled_ = true;
        return QObject::eventFilter(object, event);
    }

  public:
    RepairHandoff(Viewport &view, QWidget *host, QDialog *report, std::function<void()> validate,
                  std::function<void(QObject *)> stage)
        : QObject(&view), view_(view), report_(report) {
        setObjectName("diagnosticRepairHandoff");
        view_.installEventFilter(this);
        elapsed_.start();
        timer_.setInterval(20);
        connect(&timer_, &QTimer::timeout, this,
                [this, host, validate = std::move(validate), stage = std::move(stage)] {
                    try {
                        if (canceled_)
                            throw std::runtime_error("Repair preview canceled.");
                        validate();
                        if (elapsed_.elapsed() > 5000)
                            throw std::runtime_error("The model window did not receive focus. "
                                                     "Return to it and reopen diagnostics.");
                        // Request activation while the report is still a live,
                        // focused surface. Process pending window-system events
                        // before closing the source of the focus transition.
                        if (report_ && !closeRequested_) {
                            closeRequested_ = true;
                            host->activateWindow();
                            view_.setFocus();
                            activation_.restart();
                            QPointer<RepairHandoff> alive(this);
                            QGuiApplication::sync();
                            if (!alive)
                                return;
                            if (report_)
                                report_->accept();
                            return;
                        }
                        // Closing a DeleteOnClose dialog schedules destruction. A
                        // timer may run first, especially on a busy compositor.
                        // Wait for the actual QObject/native-window lifetime, then
                        // flush the destroy request before requesting activation.
                        if (report_)
                            return;
                        if (!synchronized_) {
                            synchronized_ = true;
                            QPointer<RepairHandoff> alive(this);
                            QGuiApplication::sync();
                            if (!alive)
                                return;
                        }
                        if (QApplication::activeWindow() != host ||
                            QGuiApplication::focusWindow() != host->windowHandle()) {
                            settled_.invalidate();
                            if (!activation_.isValid() || activation_.elapsed() >= 200) {
                                host->activateWindow();
                                view_.setFocus();
                                activation_.restart();
                            }
                            return;
                        }
                        if (!settled_.isValid()) {
                            settled_.start();
                            return;
                        }
                        if (settled_.elapsed() < 80)
                            return;
                        validate();
                        stage(this);
                    } catch (const std::exception &error) {
                        emit view_.message(QString::fromUtf8(error.what()));
                    }
                    timer_.stop();
                    QPointer<RepairHandoff> alive(this);
                    if (report_)
                        report_->close();
                    if (alive)
                        deleteLater();
                });
        timer_.start();
    }
};
class DiagnosticsReport final : public ReportSheet {
    Document &document_;
    Viewport &view_;
    Id body_;
    std::function<bool()> fenced_;
    Document::SaveStamp scope_, stamp_;
    std::uint64_t revision_{};
    Id context_{};
    QJsonArray findings_;
    QTreeWidget *rows_;
    QPlainTextEdit *details_;
    QLabel *notice_;
    QComboBox *reference_;
    QPushButton *refresh_, *select_, *frame_, *repair_;
    bool current() const {
        return document_.owns(scope_) && document_.isCurrentSnapshot(stamp_) &&
               document_.revision() == revision_ && document_.bodies().contains(body_) &&
               view_.selectionState().context() == context_;
    }
    bool available() const {
        return !view_.findChild<QObject *>("diagnosticRepairHandoff") && current() && !fenced_() &&
               !view_.inspectionBusy() && !view_.hasAssistantPreview();
    }
    QJsonObject finding() const {
        const auto *row = rows_->currentItem();
        const auto index =
            row ? rows_->indexOfTopLevelItem(const_cast<QTreeWidgetItem *>(row)) : -1;
        return index >= 0 && index < findings_.size() ? findings_[index].toObject() : QJsonObject{};
    }
    bool orient() const {
        const auto row = finding();
        return row["code"] == "inconsistent_winding" && row["countedKind"] == "edge";
    }
    SelectionSet entities(bool facesOnly = false) const {
        SelectionSet result;
        for (const auto &value : finding()["references"].toArray()) {
            const auto ref = value.toObject();
            const auto kind = ref["kind"].toString();
            if (kind == "face" || (!facesOnly && kind == "edge"))
                result.insert({body_, kind == "face" ? SelectionKind::Face : SelectionKind::Edge,
                               ref["id"].toString().toULongLong()});
        }
        return result;
    }
    SelectionSet repairFaces() const {
        if (!orient())
            return finding()["reverseShellsEligible"].toBool() ? entities(true) : SelectionSet{};
        const auto face = reference_->currentData().toULongLong();
        return face ? SelectionSet{{body_, SelectionKind::Face, face}} : SelectionSet{};
    }
    bool eligible(const SelectionSet &entities) const {
        return !entities.empty() && std::all_of(entities.begin(), entities.end(), [&](auto entity) {
            return view_.selectionState().selectable(document_, entity);
        });
    }
    void controls() {
        const bool sameDocument = document_.owns(scope_) && document_.bodies().contains(body_);
        refresh_->setEnabled(!view_.findChild<QObject *>("diagnosticRepairHandoff") &&
                             sameDocument && !fenced_() && !view_.inspectionBusy() &&
                             !view_.hasAssistantPreview());
        const bool ready = available();
        const auto refs = entities();
        select_->setEnabled(ready && std::any_of(refs.begin(), refs.end(), [&](auto entity) {
                                return view_.selectionState().selectable(document_, entity);
                            }));
        frame_->setEnabled(ready && !finding()["references"].toArray().empty());
        reference_->setEnabled(ready && orient());
        repair_->setEnabled(ready && eligible(repairFaces()));
        if (!sameDocument)
            notice_->setText("This model or body was replaced. Close the report and diagnose the "
                             "current selection.");
        else if (!current())
            notice_->setText(
                "The model or editing context changed. Refresh before using these findings.");
        else if (!ready)
            notice_->setText("Finish the active preview or operation before using this report.");
    }
    template <class F> void perform(F operation) {
        try {
            if (!available())
                throw std::runtime_error(
                    "Refresh stale findings and finish other previews before continuing.");
            operation();
        } catch (const std::exception &error) {
            notice_->setText(QString::fromUtf8(error.what()));
        }
        controls();
    }
    void choose() {
        const auto row = finding();
        reference_->blockSignals(true);
        reference_->clear();
        reference_->addItem("Choose a reference face (its direction stays unchanged)");
        if (orient())
            for (const auto entity : entities(true))
                reference_->addItem(QString("Face %1").arg(entity.entity),
                                    QVariant::fromValue(qulonglong(entity.entity)));
        reference_->blockSignals(false);
        reference_->setVisible(orient());
        repair_->setText(orient() ? "Preview orientation" : "Preview reversal");
        repair_->setVisible(orient() || row["reverseShellsEligible"].toBool());
        QString text = row["message"].toString();
        if (row["truncated"].toBool())
            text += "\nOnly a sample is listed; counts include unlisted geometry. Partial samples "
                    "cannot reverse a whole shell.";
        if (!row.isEmpty() && !row["countExact"].toBool())
            text += "\nCount is a lower bound: deeper analysis stopped at a defect.";
        text += "\n\nListed geometry:";
        for (const auto &value : row["references"].toArray()) {
            const auto ref = value.toObject();
            text += QString("\n%1 %2").arg(ref["kind"].toString(), ref["id"].toString());
        }
        if (!row.isEmpty() && !orient() && !row["reverseShellsEligible"].toBool())
            text += "\n\nInspect the listed geometry and use modeling tools for an explicit "
                    "repair. No geometry is removed automatically.";
        if (orient() || row["reverseShellsEligible"].toBool())
            text += "\n\nPreview shows new front directions. Enter applies; Escape cancels. "
                    "Physical materials are retained; Undo restores the edit. Shared component "
                    "edits affect all instances. Use Make Unique first for an isolated change.";
        details_->setPlainText(row.isEmpty() ? "No findings in this body record." : text);
        notice_->setText(
            "Select uses editable faces/edges in the current context. Frame also locates vertices. "
            "Enter the owning context or reveal/unlock geometry to edit it.");
        controls();
    }
    void refresh() {
        if (!document_.owns(scope_) || !document_.bodies().contains(body_)) {
            controls();
            return;
        }
        try {
            if (fenced_() || view_.inspectionBusy() || view_.hasAssistantPreview())
                throw std::runtime_error(
                    "Finish the active preview before refreshing diagnostics.");
            stamp_ = document_.saveStamp();
            revision_ = document_.revision();
            context_ = view_.selectionState().context();
            const auto result =
                inspectDocument(document_,
                                {{"apiVersion", 1},
                                 {"documentId", QString::fromStdString(document_.identity())},
                                 {"expectedRevision", QString::number(revision_)},
                                 {"query", "geometry.diagnose"},
                                 {"target", inspectionReference(document_, body_)}})["data"]
                    .toObject();
            findings_ = result["findings"].toArray();
            rows_->setMaximumHeight(std::clamp(28 + int(findings_.size()) * 24, 96, 240));
            rows_->blockSignals(true);
            rows_->clear();
            for (const auto &value : findings_) {
                const auto row = value.toObject();
                auto count = QString::number(row["count"].toInt());
                if (!row["countExact"].toBool())
                    count.prepend("At least ");
                auto *item = new QTreeWidgetItem(
                    rows_, {row["severity"].toString(), row["message"].toString(), count});
                item->setData(0, Qt::UserRole, row["code"].toString());
            }
            rows_->blockSignals(false);
            const auto complete = result["analysisComplete"].toBool();
            QString text =
                QString("%1 — %2\nThis body's own geometry, including hidden geometry; child "
                        "objects are excluded.\nOpen sheets and wires may be intentional; these "
                        "checks assess closed-solid geometry.")
                    .arg(QString::fromStdString(document_.bodies().at(body_)->name),
                         complete ? "Analysis complete"
                                  : "Analysis incomplete: findings prevent further checks");
            if (!result["materialVolume"].isNull())
                text += QString("\nLocal material volume: %1 m³")
                            .arg(result["materialVolume"].toDouble(), 0, 'g', 8);
            summary->setText(text);
            rows_->setCurrentItem(rows_->topLevelItem(0));
            choose();
        } catch (const std::exception &error) {
            stamp_ = {};
            notice_->setText(QString::fromUtf8(error.what()));
        }
        controls();
    }
    void frame() {
        const auto &body = *document_.bodies().at(body_);
        const auto world = document_.worldTransform(body_);
        Vec3 low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
        auto include = [&](Id vertex) {
            const auto point = world.point(body.surface.vertices.at(vertex));
            low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
            high = {std::max(high.x, point.x), std::max(high.y, point.y),
                    std::max(high.z, point.z)};
        };
        for (const auto &value : finding()["references"].toArray()) {
            const auto ref = value.toObject();
            const auto id = ref["id"].toString().toULongLong();
            if (ref["kind"] == "vertex")
                include(id);
            else if (ref["kind"] == "edge") {
                const auto &edge = body.topology.edges.at(id);
                include(edge.a);
                include(edge.b);
            } else if (ref["kind"] == "face")
                for (const auto &loop : body.surface.faces.at(id).loops)
                    for (auto vertex : loop)
                        include(vertex);
        }
        view_.frameBounds(low, high);
        notice_->setText("Framed the listed geometry. Model and selection are unchanged.");
    }
    void repair() {
        const auto faces = repairFaces();
        if (!eligible(faces))
            throw std::runtime_error(
                "Enter the affected geometry's context and choose an editable reference.");
        auto &doc = document_;
        auto &view = view_;
        auto *host = parentWidget();
        const auto stamp = stamp_;
        const auto revision = revision_;
        const auto context = context_;
        const auto connected = orient();
        const auto fenced = fenced_;
        auto validate = [&doc, &view, stamp, revision, context, faces, fenced] {
            if (!doc.isCurrentSnapshot(stamp) || doc.revision() != revision ||
                view.selectionState().context() != context || fenced() || view.inspectionBusy() ||
                view.hasAssistantPreview())
                throw std::runtime_error("The model, context or preview changed before repair. "
                                         "Reopen diagnostics and try again.");
            for (auto face : faces)
                if (!view.selectionState().selectable(doc, face))
                    throw std::runtime_error(
                        "The repair faces are no longer editable in this context.");
        };
        auto stage = [&view, host, faces, connected](QObject *owner) {
            const auto oldSelection = view.selectionState().entities();
            view.setTool(Viewport::Tool::Select);
            view.selectEntities(faces);
            auto *mode = host->findChild<QAction *>(connected ? "orientation.connected"
                                                              : "orientation.reverse");
            if (!mode)
                throw std::logic_error("Missing native orientation action");
            mode->trigger();
            QString message;
            const auto connection = QObject::connect(&view, &Viewport::message, owner,
                                                     [&](QString value) { message = value; });
            view.setTool(Viewport::Tool::Orientation);
            QObject::disconnect(connection);
            if (!view.previewValid()) {
                view.setTool(Viewport::Tool::Select);
                view.selectEntities(oldSelection);
                throw std::runtime_error(message.isEmpty() ? "Cannot preview this repair"
                                                           : message.toStdString());
            }
            view.setFocus();
        };
        new RepairHandoff(view, host, this, std::move(validate), std::move(stage));
        // The handoff requests activation before closing this report, then waits
        // for its actual native lifetime before creating an orientation preview.
    }

  public:
    DiagnosticsReport(Document &document, Viewport &view, Id bodyId, std::function<bool()> fenced,
                      QWidget *parent)
        : ReportSheet("geometryDiagnosticsReport", "Geometry diagnostics", parent),
          document_(document), view_(view), body_(bodyId), fenced_(std::move(fenced)),
          scope_(document.saveStamp()) {
        setAttribute(Qt::WA_DeleteOnClose);
        summary->setObjectName("diagnosticsSummary");
        rows_ = new QTreeWidget;
        rows_->setObjectName("diagnosticsFindings");
        rows_->setHeaderLabels({"Severity", "Finding", "Count"});
        rows_->setRootIsDecorated(false);
        rows_->setSelectionMode(QAbstractItemView::SingleSelection);
        rows_->header()->setStretchLastSection(false);
        rows_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        rows_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        rows_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        body->addWidget(rows_, 2);
        details_ = new QPlainTextEdit;
        details_->setObjectName("diagnosticsDetails");
        details_->setReadOnly(true);
        body->addWidget(details_, 2);
        reference_ = new QComboBox;
        reference_->setObjectName("diagnosticsReference");
        body->addWidget(reference_);
        notice_ = new QLabel;
        notice_->setObjectName("diagnosticsNotice");
        notice_->setTextFormat(Qt::PlainText);
        notice_->setWordWrap(true);
        body->addWidget(notice_);
        auto button = [&](const char *name, const char *label) {
            auto *result = buttons->addButton(label, QDialogButtonBox::ActionRole);
            result->setObjectName(name);
            return result;
        };
        refresh_ = button("diagnosticsRefresh", "Refresh");
        select_ = button("diagnosticsSelect", "Select listed");
        frame_ = button("diagnosticsFrame", "Frame listed");
        repair_ = button("diagnosticsRepair", "Preview reverse shells");
        connect(refresh_, &QPushButton::clicked, this, [this] { refresh(); });
        connect(select_, &QPushButton::clicked, this, [this] {
            perform([this] {
                view_.selectEntities(entities());
                notice_->setText(QString("Selected %1 editable listed faces/edges.")
                                     .arg(view_.selectionState().entities().size()));
            });
        });
        connect(frame_, &QPushButton::clicked, this, [this] { perform([this] { frame(); }); });
        connect(repair_, &QPushButton::clicked, this, [this] { perform([this] { repair(); }); });
        connect(rows_, &QTreeWidget::currentItemChanged, this, [this] { choose(); });
        connect(reference_, &QComboBox::currentIndexChanged, this, [this] { controls(); });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { controls(); });
        timer->start(200);
        refresh();
    }
};
} // namespace
void Window::showGeometryDiagnostics() {
    if (viewport_->inspectionBusy() || viewport_->hasAssistantPreview() ||
        viewport_->findChild<QObject *>("diagnosticRepairHandoff")) {
        status_->setText("Finish the active preview before opening geometry diagnostics.");
        return;
    }
    const auto body = viewport_->selectedBody() ? viewport_->selectedBody()
                                                : viewport_->selectionState().context();
    if (!body || !doc_.bodies().contains(body))
        throw std::runtime_error(
            "Select geometry or enter its editing context before diagnosing it.");
    if (diagnosticsSheet_)
        diagnosticsSheet_->close();
    diagnosticsSheet_ = new DiagnosticsReport(
        doc_, *viewport_, body, [this] { return assistant_ && assistant_->uncertain(); }, this);
    diagnosticsSheet_->show();
}
} // namespace sketchy
