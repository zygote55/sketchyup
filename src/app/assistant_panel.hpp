#pragma once
#include "app/viewport.hpp"
#include "automation/native_assistant_session.hpp"
#include <QNetworkAccessManager>
#include <QWidget>
namespace sketchy {
class AssistantPanel : public QWidget {
    Q_OBJECT
  public:
    struct HostServices {
        QString outcomeRoot, credentialExecutable;
        QNetworkAccessManager *network{};
        TransactionCoordinator::Options transactions;
    };
    AssistantPanel(Document &document, Viewport &viewport, HostServices services,
                   QWidget *parent = nullptr);
    ~AssistantPanel() override;
    void showSetup();
    void focusComposer();
    void refresh();
    bool uncertain() const;
    void closeSession();
    void submit(const QString &prompt);
    void apply();
    void stop();
    void reconcile();
    QJsonObject result() const;
  signals:
    void modelChanged();
    void fenceChanged(bool uncertain);
    void focusViewport();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sketchy
