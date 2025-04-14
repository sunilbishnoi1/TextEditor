#include "HistoryNode.h"

// Constructor for the root node
HistoryNode::HistoryNode()
    : timestamp(std::chrono::system_clock::now()),
	commitMessage(L"Initial State") // Default commit message for the root node
    // Root node's 'changeFromParent' is default initialized (empty).
    // 'parent' weak_ptr is default initialized (expired).
    // 'children' vector is default initialized (empty).
{
}

// Constructor for subsequent nodes
HistoryNode::HistoryNode(std::weak_ptr<HistoryNode> parentNode, const TextChange& change, const std::wstring& message)
    : parent(parentNode),
    changeFromParent(change),
    timestamp(std::chrono::system_clock::now()),
	commitMessage(message) 
{
}

// Checks if this node is the root (has no parent)
bool HistoryNode::isRoot() const {
    return parent.expired(); // If the weak_ptr to the parent has expired, it's the root.
}

// Helper to get the inverse change (for conceptual undo)
TextChange HistoryNode::getReverseChange() const {
    // Delegate to the TextChange object's method
    return changeFromParent.getReverseChange();
}