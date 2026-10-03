#pragma once
#include "app/viewport.hpp"
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <functional>
namespace sketchy {
class Window : public QMainWindow {
    Q_OBJECT
  public:
    explicit Window(QWidget *parent = nullptr);
    Document &document() { return doc_; }
    Viewport *viewport() { return viewport_; }
    void demo();
    void openPath(const QString &path);

  protected:
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    Document doc_;
    Viewport *viewport_{};
    QListWidget *outliner_{};
    QLabel *info_{};
    QLabel *status_{};
    QLabel *title_{};
    QLineEdit *measurements_{};
    QWidget *tray_{};
    QString path_;
    QAction *undo_{};
    QAction *redo_{};
    std::vector<QAction *> publicActions_;
    void sync();
    void run(const std::function<void()> &action);
    bool save(bool saveAs = false);
    bool canReplace();
    void palette();
    void tool(Viewport::Tool tool, const QString &instruction);
    QAction *action(const QString &title, const QKeySequence &shortcut,
                    const std::function<void()> &fn);
};
} // namespace sketchy
