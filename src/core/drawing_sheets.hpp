#pragma once
#include "core/model.hpp"
namespace sketchy {
// Drawing-sheet edit builders (ADR 0163). Each publishes one labeled entry in the
// document's single history, tagged with the drawing-sheet scope, and changes no
// model record. Equal operations are no-ops that do not advance the revision.
Id createDrawingSheet(Document &doc, std::string name, DrawingPaper paper);
void renameDrawingSheet(Document &doc, Id sheet, std::string name);
// Every view on the sheet must still fit the new paper.
void resizeDrawingSheet(Document &doc, Id sheet, DrawingPaper paper);
void reorderDrawingSheets(Document &doc, const std::vector<Id> &order);
// Deletes the sheet and its views in one edit; Undo restores both.
void eraseDrawingSheet(Document &doc, Id sheet);
std::vector<Id> orderedDrawingSheets(const Document &doc);
// Views on one sheet, in ID order.
std::vector<Id> drawingViewsOnSheet(const Document &doc, Id sheet);
// The record's id is ignored; the next view ID is allocated. The caller supplies the
// accepted stamp (the command layer computes it).
Id createDrawingView(Document &doc, DrawingViewRecord view);
// Replaces every field except the ID. Changing the source requires an existing scene.
void updateDrawingView(Document &doc, Id view, DrawingViewRecord record);
// Moves a view to another sheet, keeping its global ID; the frame must fit there.
void moveDrawingView(Document &doc, Id view, Id sheet);
void eraseDrawingView(Document &doc, Id view);
// Stamps one or more views in one edit; unchanged stamps are skipped.
void acceptDrawingViews(Document &doc, const std::map<Id, DrawingViewAcceptance> &stamps);
} // namespace sketchy
