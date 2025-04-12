//#ifndef HISTORY_NODE_H
//#define HISTORY_NODE_H
#pragma once

#include <vector>
#include <memory>   // For std::shared_ptr, std::weak_ptr
#include <chrono>   // For std::chrono::system_clock::time_point
#include <string>
#include "TextChange.h" // Include our change definition

// Forward declaration to avoid circular dependency if VersionHistoryManager needs it
class VersionHistoryManager;

class HistoryNode {
public:
    // --- Data ---
    TextChange changeFromParent; // The change that led *to* this node *from* its parent
    std::weak_ptr<HistoryNode> parent; // Use weak_ptr to avoid ownership cycles
    std::vector<std::shared_ptr<HistoryNode>> children; // Owns the child nodes
    std::chrono::system_clock::time_point timestamp;
    // Note: Full text state is not stored here to save memory.

    // --- Constructors ---
    // Constructor for the root node (no parent, represents initial state)
    HistoryNode();

    // Constructor for subsequent nodes based on a change from a parent
    HistoryNode(std::weak_ptr<HistoryNode> parentNode, const TextChange& change);

    // --- Methods ---
    bool isRoot() const; // Checks if this node is the root
    TextChange getReverseChange() const; // Gets the reverse of the change leading to this node

private:
    // Friend declaration allows VersionHistoryManager access if needed for future optimizations
    friend class VersionHistoryManager;
};

//#endif // HISTORY_NODE_H