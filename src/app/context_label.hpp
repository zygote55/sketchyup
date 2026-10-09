#pragma once
#include <QLabel>
namespace sketchy {
// Context markup is authored by the application; model names are HTML-escaped
// by its callers. Preserve source markup so theme changes never stack styles.
inline void setContextLabelText(QLabel *label, const QString &markup) {
    label->setProperty("contextMarkup", markup);
    auto rendered = markup;
    const auto color = label->property("contextLinkColor").value<QColor>();
    if (color.isValid())
        rendered.replace("<a ", "<a style=\"color:" + color.name() + ";\" ");
    label->setText(rendered);
}
inline void setContextLabelColor(QLabel *label, const QColor &color) {
    label->setProperty("contextLinkColor", color);
    auto palette = label->palette();
    palette.setColor(QPalette::Link, color);
    palette.setColor(QPalette::LinkVisited, color);
    label->setPalette(palette);
    setContextLabelText(label, label->property("contextMarkup").toString());
}
} // namespace sketchy
