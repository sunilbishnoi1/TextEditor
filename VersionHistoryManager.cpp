#include "VersionHistoryManager.h"
#include <Windows.h>
#include <stack>


// --- Helper Function: Apply Change ---

std::wstring VersionHistoryManager::applyChangeToString(const std::wstring& text, const TextChange& change) {
    std::wstring result = text;
    size_t pos = change.position;
    size_t delLength = change.deletedText.length();
    size_t insLength = change.insertedText.length();

    // Clamp position to be within the bounds of the string
    if (pos > result.length()) {
        pos = result.length();
    }

    // Perform deletion first, clamping length if necessary
    if (delLength > 0) {
        if (pos + delLength > result.length()) {
            delLength = result.length() - pos; // Adjust length if it goes past the end
        }
        if (delLength > 0) { // Check again after adjustment
            result.erase(pos, delLength);
        }
    }

    // Perform insertion
    if (insLength > 0) {
        result.insert(pos, change.insertedText);
    }

    return result;
}

// --- Constructor ---

VersionHistoryManager::VersionHistoryManager(const std::wstring& initialContent)
    : initialRootState(initialContent) {
    // Root node represents the initial state; it has no parent and no change leading to it.
    root = std::make_shared<HistoryNode>();
    currentNode = root;
}

// --- Core Recording Method ---

void VersionHistoryManager::recordChange(const TextChange& change, const std::wstring&message) {
    // Avoid recording changes that result in no actual text difference.
    if (change.insertedText.empty() && change.deletedText.empty()) {
        return;
    }

    // Create a new node representing the state *after* the change.
    auto newNode = std::make_shared<HistoryNode>(currentNode, change, message);

    // Add the new node as a child of the current node.
    currentNode->children.push_back(newNode);

    // Move the current node pointer forward to the newly created node.
    // This automatically handles branching if the user performed a standard undo
    // before making this new change.
    currentNode = newNode;

    // Optional: Implement history pruning (e.g., limit depth or node count) here if needed.
}


void VersionHistoryManager::setCurrentNode(std::shared_ptr<HistoryNode> node) {
    // Basic validation: Ensure the node actually exists (is part of the tree reachable from root)?
    // This could involve walking up the parent chain from 'node' to see if we reach 'root'.
    // For performance, we might skip this check, assuming the caller provides a valid node
    // obtained from findNodeMatchingState or getHistoryTreeRoot/getChildren traversal.
    if (node) { // At least check if it's not null
        currentNode = node;
        // NOTE: This function ONLY changes the internal pointer.
        // It does NOT update the editor content or the 'textAtLastHistoryPoint' baseline.
        // The caller (e.g., SyncHistoryManagerToEditor, History UI logic) is responsible
        // for coordinating the editor state and baseline text updates.
    }
    else {
        // Handle error: Tried to set current node to null? Log or throw?
        // For robustness, perhaps revert to root? Or just ignore?
        // Let's ignore for now, but logging would be good.
        OutputDebugStringW(L"VersionHistoryManager::setCurrentNode called with null node.\n");
    }
}

// --- State Information & Pointer Navigation ---

bool VersionHistoryManager::canUndo() const {
    // Can undo if the current node is not the root (meaning it has a parent).
    return currentNode && !currentNode->parent.expired();
}

bool VersionHistoryManager::canRedo() const {
    // Can redo if the current node has children.
    return currentNode && !currentNode->children.empty();
}

// Moves internal pointer back for synchronization after standard undo.
bool VersionHistoryManager::moveCurrentNodeToParent() {
    if (!canUndo()) {
        return false;
    }
    std::shared_ptr<HistoryNode> parentNode = currentNode->parent.lock();
    if (parentNode) {
        currentNode = parentNode;
        return true;
    }
    return false; // Should not happen if canUndo was true, but check anyway
}

// Moves internal pointer forward for synchronization after standard redo.
bool VersionHistoryManager::moveCurrentNodeToChild(size_t childIndex) {
    if (!canRedo()) {
        return false;
    }

    size_t targetIndex = childIndex;
    // Default to the most recently added child if index is invalid or max value.
    if (targetIndex == std::numeric_limits<size_t>::max() || targetIndex >= currentNode->children.size()) {
        if (currentNode->children.empty()) return false; // Should be caught by canRedo, but double check
        targetIndex = currentNode->children.size() - 1;
    }

    // Check bounds again after potential adjustment
    if (targetIndex < currentNode->children.size()) {
        currentNode = currentNode->children[targetIndex];
        return true;
    }

    OutputDebugStringW(L"VersionHistoryManager::moveCurrentNodeToChild: Provided childIndex out of bounds.\n");
    return false;
}


std::vector<std::wstring> VersionHistoryManager::getRedoBranchDescriptions() const {
    std::vector<std::wstring> descriptions;
    if (!currentNode || currentNode->children.empty()) {
        return descriptions; // Return empty vector if no children
    }

    descriptions.reserve(currentNode->children.size()); // Optimize allocation

    for (const auto& child : currentNode->children) {
        if (!child) continue; // Ensure child pointer is valid

        std::wstring description;
        std::wstring commitMsg = child->commitMessage; // Get the commit message

        // Format timestamp
        time_t tt = std::chrono::system_clock::to_time_t(child->timestamp);
        tm local_tm;
        localtime_s(&local_tm, &tt);
        char timeBuffer[80];
        strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &local_tm); // e.g., 14:35:10
        // Or use: "%Y-%m-%d %H:%M:%S" for full date and time
        std::string timestampStr(timeBuffer);
        std::wstring wTimestampStr(timestampStr.begin(), timestampStr.end());

        // Format: [Time] Commit Message (+Ins/-Del)
        description = L"[" + wTimestampStr + L"]";

        // Add commit message if present, otherwise indicate Auto
        if (!commitMsg.empty()) {
            description += L" " + commitMsg;
        }
        else {
            // Check if the default message from recordChange was used
            // For now, let's assume an empty message implies Auto.
            description += L" (Auto)"; // Indicate automatic commit if no message
        }

        description += L" (+" + std::to_wstring(child->changeFromParent.insertedText.length())
            + L" / -" + std::to_wstring(child->changeFromParent.deletedText.length())
            + L")";

        descriptions.push_back(description);
    }

    return descriptions;
}

// --- State Reconstruction and Switching ---

// Reconstructs state by applying changes down from the root to the target node.
std::wstring VersionHistoryManager::reconstructStateToNode(std::shared_ptr<const HistoryNode> targetNode) const {
    if (!targetNode) {
        // Consider logging this error or handling it based on application needs
        return L""; // Return empty for null target
    }

    // Use a stack to store changes while walking up to the root.
    std::stack<TextChange> changesToApply;
    std::shared_ptr<const HistoryNode> walker = targetNode;

    while (walker && !walker->isRoot()) {
        changesToApply.push(walker->changeFromParent);
        // Check parent validity before locking
        if (walker->parent.expired()) break;
        walker = walker->parent.lock();
    }

    // Start with the initial state stored at the manager level.
    std::wstring currentState = initialRootState;

    // Apply changes sequentially down from the root.
    while (!changesToApply.empty()) {
        currentState = applyChangeToString(currentState, changesToApply.top());
        changesToApply.pop();
    }

    return currentState;
}

// Gets the state corresponding to the internal current node pointer.
std::wstring VersionHistoryManager::getCurrentState() const {
    // Reconstruct state based on where the internal 'currentNode' pointer is.
    return reconstructStateToNode(currentNode);
}

// Switches the internal pointer and returns the full state for the History UI.
std::wstring VersionHistoryManager::switchToNode(std::shared_ptr<HistoryNode> targetNode) {
    if (!targetNode) {
        throw std::invalid_argument("Target node cannot be null for switchToNode.");
    }

    // Optional: Add validation here to ensure targetNode belongs to this tree,
    //           though it adds overhead. Assume valid node is passed from UI.

    // Reconstruct the state at the target node BEFORE changing the internal pointer.
    std::wstring targetState = reconstructStateToNode(targetNode);

    // Update the internal current node pointer *after* successful reconstruction.
    currentNode = targetNode;

    // Return the full text state for the editor UI to display.
    return targetState;
}

// --- Node Finding (for Syncing Editor State to History) ---

std::shared_ptr<HistoryNode> VersionHistoryManager::findNodeMatchingState(const std::wstring& targetState) const {
    // Uses Breadth-First Search (BFS) with state caching to find the node.
    // This is necessary to sync the editor's state (after standard undo/redo)
    // with our internal history tree before showing the history UI.

    std::queue<std::shared_ptr<HistoryNode>> q;
    // Cache previously reconstructed states to avoid redundant computation.
    // Key: Node pointer, Value: Reconstructed text state for that node.
    std::map<std::shared_ptr<const HistoryNode>, std::wstring> stateCache;

    if (!root) return nullptr; // Should not happen if constructed properly

    // Initialize BFS with the root node and its known state.
    q.push(root);
    stateCache[root] = initialRootState;

    // Keep track of visited nodes during BFS ONLY IF the graph could have cycles
    // (not possible with weak_ptr parent, shared_ptr children). So, not strictly needed here.
    // std::set<std::shared_ptr<const HistoryNode>> visited;
    // visited.insert(root);

    while (!q.empty()) {
        std::shared_ptr<HistoryNode> node = q.front();
        q.pop();

        // Retrieve the state for the current node (should be in cache from parent or initial).
        std::wstring nodeState;
        auto it = stateCache.find(node);
        if (it != stateCache.end()) {
            nodeState = it->second;
        }
        else {
            // This case should ideally not happen if BFS proceeds level by level
            // and states are cached when children are processed. If it does,
            // reconstruct and cache. This indicates potential issue elsewhere.
            // For robustness:
            OutputDebugStringW(L"findNodeMatchingState: Cache miss, reconstructing state...\n");
            nodeState = reconstructStateToNode(node);
            stateCache[node] = nodeState;
            // Log warning? std::cerr << "Warning: Cache miss in findNodeMatchingState BFS for node." << std::endl;
        }

        if (nodeState != targetState) {
            // Only log if they don't match to avoid excessive output
            OutputDebugStringW(L"findNodeMatchingState: Comparing states:\n");
            OutputDebugStringW((L"  Editor Target State : [" + targetState + L"] (Length: " + std::to_wstring(targetState.length()) + L")\n").c_str());
            OutputDebugStringW((L"  Reconstructed State : [" + nodeState + L"] (Length: " + std::to_wstring(nodeState.length()) + L")\n").c_str());
            // Optional: Add code here to compare character by character and log the first difference
            for (size_t i = 0; i < std::min(targetState.length(), nodeState.length()); ++i) {
                if (targetState[i] != nodeState[i]) {
                    OutputDebugStringW((L"  Mismatch found at index " + std::to_wstring(i)
                        + L": Editor char=" + std::to_wstring(targetState[i])
                        + L", Reconstructed char=" + std::to_wstring(nodeState[i]) + L"\n").c_str());
                    break;
                }
            }
            if (targetState.length() != nodeState.length()) {
                OutputDebugStringW(L"  States have different lengths.\n");
            }
        }


        // Check if this node's state matches the target state.
        if (nodeState == targetState) {
            // Found a match. Return this node.
            // Note: If multiple nodes can have identical text states, this finds
            // the first one encountered in BFS. You might need different logic
            // (e.g., find latest timestamp) if specific duplicates must be handled.
            OutputDebugStringW(L"findNodeMatchingState: FOUND matching node.\n"); 
            return node;
        }

        // Process children: reconstruct their state, cache it, and add to queue.
        for (const auto& child : node->children) {
            if (child /* && visited.find(child) == visited.end() */) { // Check if child is valid
                // Calculate child state based on parent state and the change
                if (stateCache.find(child) == stateCache.end()) {
                    std::wstring childState = applyChangeToString(nodeState, child->changeFromParent);
                    stateCache[child] = childState;
                    q.push(child);
                }
                // visited.insert(child); // Only needed if cycles are possible
            }
        }
    }

    // Target state was not found anywhere in the reachable history tree.
    OutputDebugStringW(L"findNodeMatchingState: Target state NOT found after searching tree.\n");
    return nullptr;
}


// --- Accessors for UI ---

std::shared_ptr<const HistoryNode> VersionHistoryManager::getHistoryTreeRoot() const {
    // Return a const pointer to prevent external modification via this getter.
    return root;
}

std::shared_ptr<const HistoryNode> VersionHistoryManager::getCurrentNode() const {
    // Return a const pointer to the node our internal state currently points to.
    return currentNode;
}