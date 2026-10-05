#include "app/viewport.hpp"
namespace sketchy {
void Viewport::setBooleanOperation(const QString &operation) {
    if (operation != "union" && operation != "subtract" && operation != "intersection")
        throw std::runtime_error("Choose union, subtract or intersection");
    booleanOperation_ = operation;
    if (tool_ == Tool::Boolean)
        setTool(Tool::Boolean);
}
void Viewport::setBooleanKeepOperands(bool keep) {
    booleanKeepOperands_ = keep;
    if (tool_ == Tool::Boolean)
        setTool(Tool::Boolean);
}
void Viewport::swapBooleanOperands() {
    booleanSwap_ = !booleanSwap_;
    if (tool_ == Tool::Boolean)
        setTool(Tool::Boolean);
}
QString Viewport::booleanSummary() const {
    if (!booleanCommand_)
        return "Solid Boolean: select two solids (a face from each is enough), then Shift+B";
    const auto target = (*booleanCommand_)["body"].toString().toULongLong();
    const auto tool = (*booleanCommand_)["tool"].toString().toULongLong();
    auto label = [&](Id id) {
        return doc_.bodies().contains(id) ? QString::fromStdString(doc_.bodies().at(id)->name) +
                                                " (#" + QString::number(id) + ")"
                                          : QString::number(id);
    };
    return QString("%1 · Target: %2 · Tool: %3 · %4 · Options / Swap in Draw → Solid Boolean")
        .arg(booleanOperation_, label(target), label(tool),
             booleanKeepOperands_ ? "Keep originals" : "Remove both originals");
}
void Viewport::beginBoolean() {
    syncSelection();
    std::set<Id> bodies;
    for (auto entity : selection_.entities()) {
        if ((entity.kind != SelectionKind::Body && entity.kind != SelectionKind::Face) ||
            !selectable(entity) || doc_.bodies().at(entity.body)->kind != BodyKind::Geometry)
            throw std::runtime_error(
                "Select two editable raw solids or their faces; enter group containers first");
        bodies.insert(entity.body);
    }
    if (bodies.size() != 2)
        throw std::runtime_error(
            "Select two solids (a face from each is enough); Ctrl-click adds · Shift+B previews");
    auto target = *bodies.begin(), tool = *bodies.rbegin();
    if (booleanSwap_)
        std::swap(target, tool);
    booleanCommand_ = QJsonObject{
        {"command", "geometry.boolean"},  {"body", QString::number(target)},
        {"tool", QString::number(tool)},  {"context", QString::number(selection_.context())},
        {"operation", booleanOperation_}, {"keepOperands", booleanKeepOperands_}};
    session_.begin();
    previewCommand(*booleanCommand_);
    if (!previewValid_ && previewError_ == "Batch has no committed changes") {
        previewError_ = "Empty Boolean result; originals retained. Choose another operation or "
                        "explicitly turn off Keep originals to remove both";
        emit message(previewError_);
    } else if (previewValid_) {
        emit message(
            booleanSummary() +
            (previewEdges_.empty() ? " · Empty result: both originals will be removed" : "") +
            " · Enter or click applies · Esc cancels");
    }
}
void Viewport::finishBoolean() {
    if (!booleanCommand_ || !session_.active())
        throw std::runtime_error("Select two solids and press Shift+B to preview");
    if (!previewValid_)
        throw std::runtime_error(previewError_.toStdString());
    const auto result = session_.commit(*booleanCommand_);
    SelectionSet generated;
    for (auto value : result["booleans"].toArray())
        for (auto part : value.toObject()["parts"].toArray())
            generated.insert(
                {part.toObject()["body"].toString().toULongLong(), SelectionKind::Body, 0});
    cancel();
    refresh();
    selectEntities(generated);
    emit changed();
    emit message(generated.empty() ? "Empty Boolean applied · Originals removed · Ctrl+Z undoes"
                                   : "Boolean applied · Results selected · Ctrl+Z undoes");
}
} // namespace sketchy
