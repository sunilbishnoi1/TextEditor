#pragma once

#include <string>
#include <cstddef> // For size_t

// Represents a single atomic change in the document.
// Designed to work with the Rich Edit control.
struct TextChange {
    // Using wide strings as the editor uses WCHAR
    std::wstring insertedText;
    std::wstring deletedText;
    size_t position;            // Start position of the change in the document text
    size_t cursorPositionAfter; // Cursor position after the change was applied

    // Default constructor
    TextChange() : position(0), cursorPositionAfter(0) {}

    // Parameterized constructor
    TextChange(size_t pos, const std::wstring& inserted, const std::wstring& deleted, size_t cursorPosAfter = 0)
        : position(pos), insertedText(inserted), deletedText(deleted), cursorPositionAfter(cursorPosAfter) {
    }

    // Method to get the inverse change (for undo conceptually)
    TextChange getReverseChange() const {
        // Swap inserted and deleted text.
        // The cursor position for the reverse operation typically goes back
        // to the beginning of the change range.
        size_t reverseCursorPos = position;
        return TextChange(position, deletedText, insertedText, reverseCursorPos);
    }
};

//#endif // TEXT_CHANGE_H