#pragma once
#include "core/model.hpp"
#include <optional>
namespace sketchy {
// Drawing-sheet edit builders (ADR 0163). Each publishes one labeled entry in the
// document's single history, tagged with the drawing-sheet scope, and changes no
// model record. Equal operations are no-ops that do not advance the revision.
// The margin defaults to defaultDrawingSheetMarginMm(paper).
Id createDrawingSheet(Document &doc, std::string name, DrawingPaper paper, std::string number = {},
                      std::optional<double> marginMm = std::nullopt);
void renameDrawingSheet(Document &doc, Id sheet, std::string name);
// Every view and item on the sheet must still fit the new paper. A margin larger
// than a quarter of the new shorter side is reduced to that quarter.
void resizeDrawingSheet(Document &doc, Id sheet, DrawingPaper paper);
// Sets the sheet number and page margin (ADR 0164).
void updateDrawingSheetPage(Document &doc, Id sheet, std::string number, double marginMm);
void reorderDrawingSheets(Document &doc, const std::vector<Id> &order);
// Deletes the sheet, its views and its items in one edit; Undo restores them.
void eraseDrawingSheet(Document &doc, Id sheet);
std::vector<Id> orderedDrawingSheets(const Document &doc);
// Views on one sheet, in ID order.
std::vector<Id> drawingViewsOnSheet(const Document &doc, Id sheet);
// The record's id is ignored; the next view ID is allocated. The caller supplies the
// accepted stamp (the command layer computes it).
Id createDrawingView(Document &doc, DrawingViewRecord view);
// Replaces every field except the ID. Changing the source requires an existing scene.
// Changing the sheet moves the view's bound items with it, as moveDrawingView does.
void updateDrawingView(Document &doc, Id view, DrawingViewRecord record);
// Moves a view to another sheet, keeping its global ID; the frame must fit there.
// Items bound to the view (its dimensions and view-target labels) move with it in
// the same edit and go on top of the new sheet's z order, keeping their order.
void moveDrawingView(Document &doc, Id view, Id sheet);
// Deletes the view and the items bound to it in one edit.
void eraseDrawingView(Document &doc, Id view);
// Stamps one or more views in one edit; unchanged stamps are skipped.
void acceptDrawingViews(Document &doc, const std::map<Id, DrawingViewAcceptance> &stamps);
// Page items (ADR 0164). Items on one sheet, bottom first.
std::vector<Id> drawingItemsOnSheet(const Document &doc, Id sheet);
// Allocates the next item ID and puts the item on top of the sheet's z order.
Id createDrawingItem(Document &doc, Id sheet, DrawingItemData data);
// Replaces the item's data; its ID, sheet and z are kept.
void updateDrawingItem(Document &doc, Id item, DrawingItemData data);
// Moves an item to another sheet, on top of its z order; the old sheet is restacked.
void moveDrawingItem(Document &doc, Id item, Id sheet);
// Sets a sheet's z order: every item on the sheet exactly once, bottom first.
void reorderDrawingItems(Document &doc, Id sheet, const std::vector<Id> &order);
// Deletes the item and restacks the rest of its sheet.
void eraseDrawingItem(Document &doc, Id item);
} // namespace sketchy
