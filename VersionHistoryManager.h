#pragma once

#define NOMINMAX // Prevent Windows headers from defining min/max macros

#include <memory>       // For std::shared_ptr, std::weak_ptr
#include <string>       
#include <vector>       
#include <optional>     
#include <stack>        
#include <queue>        
#include <map>          
#include <chrono>       
#include <limits>       
#include <stdexcept>    

// First include TextChange.h as it doesn't depend on HistoryNode
#include "TextChange.h"

// Forward declaration of HistoryNode before defining the class
class HistoryNode;

// Now include HistoryNode.h after forward declaration
#include "HistoryNode.h"

class VersionHistoryManager {
public:
    // Constructor and Destructor
    explicit VersionHistoryManager(const std::wstring& initialContent);
    ~VersionHistoryManager() = default;

    // Deleted copy/move constructors and assignment operators
    VersionHistoryManager(const VersionHistoryManager&) = delete;
    VersionHistoryManager& operator=(const VersionHistoryManager&) = delete;
    VersionHistoryManager(VersionHistoryManager&&) = delete;
    VersionHistoryManager& operator=(VersionHistoryManager&&) = delete;

    // Core Recording Operation
    void recordChange(const TextChange& change, const std::wstring& message = L"");

    // Sets the internal current node pointer directly, Used after finding a matching state or navigating via history UI.
    void setCurrentNode(std::shared_ptr<HistoryNode> node);

    // State Information & Navigation
    bool canUndo() const;
    bool canRedo() const;
    bool moveCurrentNodeToParent();
    bool moveCurrentNodeToChild(size_t childIndex = std::numeric_limits<size_t>::max());
    std::vector<std::wstring> getRedoBranchDescriptions() const;
    std::wstring switchToNode(std::shared_ptr<HistoryNode> targetNode);
    std::wstring reconstructStateToNode(std::shared_ptr<const HistoryNode> targetNode) const;
    std::wstring getCurrentState() const;
    // History Modification
    bool deleteNode(std::shared_ptr<HistoryNode> nodeToDelete); // Use non-const shared_ptr as we modify the tree
    std::shared_ptr<HistoryNode> findNodeMatchingState(const std::wstring& state) const;
    std::shared_ptr<const HistoryNode> getHistoryTreeRoot() const;
    std::shared_ptr<const HistoryNode> getCurrentNode() const;

    // Helper to get the *non-const* current node if modification is needed
    // Be careful using this, primarily for internal operations like deletion checks.
    std::shared_ptr<HistoryNode> getMutableCurrentNode();

private:
    // Internal State
    std::wstring initialRootState;
    std::shared_ptr<HistoryNode> root;
    std::shared_ptr<HistoryNode> currentNode;

    // Helper Functions
    static std::wstring applyChangeToString(const std::wstring& text, const TextChange& change);
};

