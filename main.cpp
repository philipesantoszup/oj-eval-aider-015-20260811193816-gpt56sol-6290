#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

constexpr const char *kDatabaseFilename = ".problem015.db";
constexpr std::size_t kMaximumKeys = 48;
constexpr std::size_t kLeafMinimumKeys = kMaximumKeys / 2;
constexpr std::size_t kInternalMinimumKeys = (kMaximumKeys - 1) / 2;
constexpr std::uint64_t kNullNode = std::numeric_limits<std::uint64_t>::max();
constexpr std::size_t kMaximumTreeDepth = 32;

struct Key {
    char index[65];
    std::int32_t value;
};

struct Node {
    // 0: internal node, 1: leaf node, 2: free node
    std::uint8_t type;
    std::uint16_t count;
    std::uint64_t next;
    Key keys[kMaximumKeys];
    std::uint64_t children[kMaximumKeys + 1];
};

struct Header {
    char magic[8];
    std::uint32_t version;
    std::uint32_t node_size;
    std::uint64_t root;
    std::uint64_t node_count;
    std::uint64_t free_head;
};

struct PathEntry {
    std::uint64_t node_id;
    std::uint16_t child_index;
};

constexpr char kDatabaseMagic[8] = {
    'P', '0', '1', '5', 'D', 'B', '0', '2'
};
constexpr std::uint32_t kDatabaseVersion = 2;

int compareKeys(const Key &left, const Key &right) {
    const int index_comparison = std::strcmp(left.index, right.index);
    if (index_comparison < 0) {
        return -1;
    }
    if (index_comparison > 0) {
        return 1;
    }
    if (left.value < right.value) {
        return -1;
    }
    if (left.value > right.value) {
        return 1;
    }
    return 0;
}

int compareIndex(const Key &key, const std::string &index) {
    return std::strcmp(key.index, index.c_str());
}

Key makeKey(const std::string &index, std::int32_t value) {
    Key key{};
    std::memcpy(key.index, index.data(), index.size());
    key.index[index.size()] = '\0';
    key.value = value;
    return key;
}

class BPlusTree {
public:
    BPlusTree() {
        file_ = std::fopen(kDatabaseFilename, "r+b");
        if (file_ == nullptr) {
            file_ = std::fopen(kDatabaseFilename, "w+b");
            if (file_ == nullptr) {
                throw std::runtime_error("cannot create database file");
            }
            initialize();
        } else {
            loadHeader();
        }
    }

    ~BPlusTree() {
        if (file_ != nullptr) {
            std::fflush(file_);
            std::fclose(file_);
        }
    }

    BPlusTree(const BPlusTree &) = delete;
    BPlusTree &operator=(const BPlusTree &) = delete;

    void insert(const std::string &index, std::int32_t value) {
        const Key key = makeKey(index, value);

        Node root{};
        readNode(header_.root, root);

        if (root.count == kMaximumKeys) {
            splitRoot(root);
        }

        std::uint64_t current_id = header_.root;
        Node current{};
        readNode(current_id, current);

        while (current.type == 0) {
            std::size_t child_index = internalChildIndex(current, key);
            std::uint64_t child_id = current.children[child_index];

            Node child{};
            readNode(child_id, child);

            if (child.count == kMaximumKeys) {
                splitChild(current, child_index, child_id, child);
                writeNode(current_id, current);

                if (compareKeys(key, current.keys[child_index]) >= 0) {
                    child_id = current.children[child_index + 1];
                    readNode(child_id, child);
                }
            }

            current_id = child_id;
            current = child;
        }

        const std::size_t position = leafLowerBound(current, key);
        if (position < current.count &&
            compareKeys(current.keys[position], key) == 0) {
            return;
        }

        for (std::size_t i = current.count; i > position; --i) {
            current.keys[i] = current.keys[i - 1];
        }
        current.keys[position] = key;
        ++current.count;
        writeNode(current_id, current);
    }

    void erase(const std::string &index, std::int32_t value) {
        const Key key = makeKey(index, value);

        std::array<PathEntry, kMaximumTreeDepth> path{};
        std::size_t depth = 0;

        std::uint64_t current_id = header_.root;
        Node current{};
        readNode(current_id, current);

        while (current.type == 0) {
            if (depth == path.size()) {
                throw std::runtime_error("database tree is too deep");
            }

            const std::size_t child_index =
                internalChildIndex(current, key);
            path[depth] = {
                current_id,
                static_cast<std::uint16_t>(child_index)
            };
            ++depth;

            current_id = current.children[child_index];
            readNode(current_id, current);
        }

        const std::size_t position = leafLowerBound(current, key);
        if (position == current.count ||
            compareKeys(current.keys[position], key) != 0) {
            return;
        }

        for (std::size_t i = position; i + 1 < current.count; ++i) {
            current.keys[i] = current.keys[i + 1];
        }
        --current.count;
        writeNode(current_id, current);

        rebalanceAfterDeletion(current_id, current, path, depth);
    }

    void find(const std::string &index) {
        const Key lower_bound_key = makeKey(index, -1);

        Node current{};
        readNode(header_.root, current);

        while (current.type == 0) {
            const std::size_t child_index =
                internalChildIndex(current, lower_bound_key);
            readNode(current.children[child_index], current);
        }

        std::size_t position = leafLowerBound(current, lower_bound_key);
        bool first_value = true;
        bool finished = false;

        while (!finished) {
            for (std::size_t i = position; i < current.count; ++i) {
                const int comparison = compareIndex(current.keys[i], index);

                if (comparison < 0) {
                    continue;
                }
                if (comparison > 0) {
                    finished = true;
                    break;
                }

                if (!first_value) {
                    std::cout << ' ';
                }
                std::cout << current.keys[i].value;
                first_value = false;
            }

            if (finished || current.next == kNullNode) {
                break;
            }

            readNode(current.next, current);
            position = 0;
        }

        if (first_value) {
            std::cout << "null";
        }
        std::cout << '\n';
    }

private:
    FILE *file_ = nullptr;
    Header header_{};

    static std::uint64_t nodeOffset(std::uint64_t node_id) {
        return static_cast<std::uint64_t>(sizeof(Header)) +
               node_id * static_cast<std::uint64_t>(sizeof(Node));
    }

    void seekTo(std::uint64_t offset) {
        if (offset > static_cast<std::uint64_t>(
                         std::numeric_limits<long>::max())) {
            throw std::runtime_error("database file is too large");
        }

        if (std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) {
            throw std::runtime_error("cannot seek in database file");
        }
    }

    void initialize() {
        header_ = {};
        std::memcpy(header_.magic, kDatabaseMagic, sizeof(kDatabaseMagic));
        header_.version = kDatabaseVersion;
        header_.node_size = sizeof(Node);
        header_.root = 0;
        header_.node_count = 1;
        header_.free_head = kNullNode;

        Node root{};
        root.type = 1;
        root.count = 0;
        root.next = kNullNode;

        writeHeader();
        writeNode(0, root);
    }

    void loadHeader() {
        seekTo(0);
        if (std::fread(&header_, sizeof(Header), 1, file_) != 1) {
            throw std::runtime_error("cannot read database header");
        }

        if (std::memcmp(header_.magic, kDatabaseMagic,
                        sizeof(kDatabaseMagic)) != 0 ||
            header_.version != kDatabaseVersion ||
            header_.node_size != sizeof(Node) ||
            header_.node_count == 0 ||
            header_.root >= header_.node_count) {
            throw std::runtime_error("invalid database file");
        }
    }

    void writeHeader() {
        seekTo(0);
        if (std::fwrite(&header_, sizeof(Header), 1, file_) != 1) {
            throw std::runtime_error("cannot write database header");
        }
    }

    void readNode(std::uint64_t node_id, Node &node,
                  bool allow_free_node = false) {
        if (node_id >= header_.node_count) {
            throw std::runtime_error("invalid database node number");
        }

        seekTo(nodeOffset(node_id));
        if (std::fread(&node, sizeof(Node), 1, file_) != 1) {
            throw std::runtime_error("cannot read database node");
        }

        if (node.type > 2 || node.count > kMaximumKeys) {
            throw std::runtime_error("corrupt database node");
        }
        if (node.type == 2 && !allow_free_node) {
            throw std::runtime_error("unexpected free database node");
        }
    }

    void writeNode(std::uint64_t node_id, const Node &node) {
        if (node_id >= header_.node_count) {
            throw std::runtime_error("invalid database node number");
        }

        seekTo(nodeOffset(node_id));
        if (std::fwrite(&node, sizeof(Node), 1, file_) != 1) {
            throw std::runtime_error("cannot write database node");
        }
    }

    std::uint64_t reserveNode() {
        if (header_.free_head != kNullNode) {
            const std::uint64_t node_id = header_.free_head;
            Node free_node{};
            readNode(node_id, free_node, true);

            if (free_node.type != 2) {
                throw std::runtime_error("corrupt database free list");
            }

            header_.free_head = free_node.next;
            writeHeader();
            return node_id;
        }

        const std::uint64_t node_id = header_.node_count;
        ++header_.node_count;
        writeHeader();
        return node_id;
    }

    void releaseNode(std::uint64_t node_id) {
        Node free_node{};
        free_node.type = 2;
        free_node.count = 0;
        free_node.next = header_.free_head;

        writeNode(node_id, free_node);
        header_.free_head = node_id;
        writeHeader();
    }

    static std::size_t leafLowerBound(const Node &leaf, const Key &key) {
        std::size_t left = 0;
        std::size_t right = leaf.count;

        while (left < right) {
            const std::size_t middle = left + (right - left) / 2;
            if (compareKeys(leaf.keys[middle], key) < 0) {
                left = middle + 1;
            } else {
                right = middle;
            }
        }

        return left;
    }

    static std::size_t internalChildIndex(const Node &node,
                                          const Key &key) {
        std::size_t left = 0;
        std::size_t right = node.count;

        while (left < right) {
            const std::size_t middle = left + (right - left) / 2;
            if (compareKeys(key, node.keys[middle]) >= 0) {
                left = middle + 1;
            } else {
                right = middle;
            }
        }

        return left;
    }

    void splitRoot(Node &old_root) {
        const std::uint64_t old_root_id = header_.root;
        const std::size_t middle = kMaximumKeys / 2;

        Node right{};
        right.type = old_root.type;
        right.next = kNullNode;

        Key separator{};
        const std::uint64_t right_id = reserveNode();

        if (old_root.type == 1) {
            right.count =
                static_cast<std::uint16_t>(kMaximumKeys - middle);
            for (std::size_t i = 0; i < right.count; ++i) {
                right.keys[i] = old_root.keys[middle + i];
            }

            right.next = old_root.next;
            old_root.count = static_cast<std::uint16_t>(middle);
            old_root.next = right_id;
            separator = right.keys[0];
        } else {
            separator = old_root.keys[middle];
            right.count = static_cast<std::uint16_t>(
                kMaximumKeys - middle - 1);

            for (std::size_t i = 0; i < right.count; ++i) {
                right.keys[i] = old_root.keys[middle + 1 + i];
            }
            for (std::size_t i = 0; i <= right.count; ++i) {
                right.children[i] =
                    old_root.children[middle + 1 + i];
            }

            old_root.count = static_cast<std::uint16_t>(middle);
        }

        writeNode(old_root_id, old_root);
        writeNode(right_id, right);

        Node new_root{};
        new_root.type = 0;
        new_root.count = 1;
        new_root.next = kNullNode;
        new_root.keys[0] = separator;
        new_root.children[0] = old_root_id;
        new_root.children[1] = right_id;

        const std::uint64_t new_root_id = reserveNode();
        writeNode(new_root_id, new_root);

        header_.root = new_root_id;
        writeHeader();
    }

    void splitChild(Node &parent, std::size_t child_index,
                    std::uint64_t child_id, Node &child) {
        if (parent.count >= kMaximumKeys ||
            child.count != kMaximumKeys) {
            throw std::runtime_error("invalid B+ tree split");
        }

        const std::size_t middle = kMaximumKeys / 2;

        Node right{};
        right.type = child.type;
        right.next = kNullNode;

        Key separator{};
        const std::uint64_t right_id = reserveNode();

        if (child.type == 1) {
            right.count =
                static_cast<std::uint16_t>(kMaximumKeys - middle);
            for (std::size_t i = 0; i < right.count; ++i) {
                right.keys[i] = child.keys[middle + i];
            }

            right.next = child.next;
            child.count = static_cast<std::uint16_t>(middle);
            child.next = right_id;
            separator = right.keys[0];
        } else {
            separator = child.keys[middle];
            right.count = static_cast<std::uint16_t>(
                kMaximumKeys - middle - 1);

            for (std::size_t i = 0; i < right.count; ++i) {
                right.keys[i] = child.keys[middle + 1 + i];
            }
            for (std::size_t i = 0; i <= right.count; ++i) {
                right.children[i] =
                    child.children[middle + 1 + i];
            }

            child.count = static_cast<std::uint16_t>(middle);
        }

        for (std::size_t i = parent.count; i > child_index; --i) {
            parent.keys[i] = parent.keys[i - 1];
        }
        for (std::size_t i = parent.count + 1;
             i > child_index + 1; --i) {
            parent.children[i] = parent.children[i - 1];
        }

        parent.keys[child_index] = separator;
        parent.children[child_index + 1] = right_id;
        ++parent.count;

        writeNode(child_id, child);
        writeNode(right_id, right);
    }

    static std::size_t minimumKeys(const Node &node) {
        return node.type == 1
                   ? kLeafMinimumKeys
                   : kInternalMinimumKeys;
    }

    static void removeParentEntry(Node &parent,
                                  std::size_t key_index,
                                  std::size_t child_index) {
        for (std::size_t i = key_index; i + 1 < parent.count; ++i) {
            parent.keys[i] = parent.keys[i + 1];
        }
        for (std::size_t i = child_index; i < parent.count; ++i) {
            parent.children[i] = parent.children[i + 1];
        }
        --parent.count;
    }

    void rebalanceAfterDeletion(
        std::uint64_t current_id,
        Node current,
        const std::array<PathEntry, kMaximumTreeDepth> &path,
        std::size_t depth) {

        while (current_id != header_.root &&
               current.count < minimumKeys(current)) {
            if (depth == 0) {
                throw std::runtime_error("invalid B+ tree path");
            }

            --depth;
            const std::uint64_t parent_id = path[depth].node_id;
            const std::size_t child_index = path[depth].child_index;

            Node parent{};
            readNode(parent_id, parent);

            if (child_index > parent.count ||
                parent.children[child_index] != current_id) {
                throw std::runtime_error("corrupt B+ tree path");
            }

            if (child_index > 0) {
                const std::uint64_t left_id =
                    parent.children[child_index - 1];
                Node left{};
                readNode(left_id, left);

                if (left.count > minimumKeys(left)) {
                    borrowFromLeft(
                        parent, child_index, left, current);
                    writeNode(left_id, left);
                    writeNode(current_id, current);
                    writeNode(parent_id, parent);
                    return;
                }
            }

            if (child_index < parent.count) {
                const std::uint64_t right_id =
                    parent.children[child_index + 1];
                Node right{};
                readNode(right_id, right);

                if (right.count > minimumKeys(right)) {
                    borrowFromRight(
                        parent, child_index, current, right);
                    writeNode(current_id, current);
                    writeNode(right_id, right);
                    writeNode(parent_id, parent);
                    return;
                }
            }

            if (child_index > 0) {
                const std::uint64_t left_id =
                    parent.children[child_index - 1];
                Node left{};
                readNode(left_id, left);

                mergeIntoLeft(
                    parent, child_index, left, current);
                writeNode(left_id, left);
                writeNode(parent_id, parent);
                releaseNode(current_id);
            } else {
                if (parent.count == 0) {
                    throw std::runtime_error("invalid empty parent node");
                }

                const std::uint64_t right_id =
                    parent.children[1];
                Node right{};
                readNode(right_id, right);

                mergeRightIntoCurrent(parent, current, right);
                writeNode(current_id, current);
                writeNode(parent_id, parent);
                releaseNode(right_id);
            }

            current_id = parent_id;
            current = parent;
        }

        if (current_id == header_.root &&
            current.type == 0 &&
            current.count == 0) {
            const std::uint64_t old_root_id = current_id;
            header_.root = current.children[0];
            writeHeader();
            releaseNode(old_root_id);
        }
    }

    static void borrowFromLeft(Node &parent,
                               std::size_t child_index,
                               Node &left,
                               Node &current) {
        if (current.type == 1) {
            for (std::size_t i = current.count; i > 0; --i) {
                current.keys[i] = current.keys[i - 1];
            }

            current.keys[0] = left.keys[left.count - 1];
            --left.count;
            ++current.count;
            parent.keys[child_index - 1] = current.keys[0];
            return;
        }

        for (std::size_t i = current.count; i > 0; --i) {
            current.keys[i] = current.keys[i - 1];
        }
        for (std::size_t i = current.count + 1; i > 0; --i) {
            current.children[i] = current.children[i - 1];
        }

        current.keys[0] = parent.keys[child_index - 1];
        current.children[0] = left.children[left.count];
        parent.keys[child_index - 1] = left.keys[left.count - 1];

        --left.count;
        ++current.count;
    }

    static void borrowFromRight(Node &parent,
                                std::size_t child_index,
                                Node &current,
                                Node &right) {
        if (current.type == 1) {
            current.keys[current.count] = right.keys[0];
            ++current.count;

            for (std::size_t i = 0; i + 1 < right.count; ++i) {
                right.keys[i] = right.keys[i + 1];
            }
            --right.count;

            parent.keys[child_index] = right.keys[0];
            return;
        }

        current.keys[current.count] = parent.keys[child_index];
        current.children[current.count + 1] = right.children[0];
        ++current.count;

        parent.keys[child_index] = right.keys[0];

        for (std::size_t i = 0; i + 1 < right.count; ++i) {
            right.keys[i] = right.keys[i + 1];
        }
        for (std::size_t i = 0; i < right.count; ++i) {
            right.children[i] = right.children[i + 1];
        }
        --right.count;
    }

    static void mergeIntoLeft(Node &parent,
                              std::size_t child_index,
                              Node &left,
                              const Node &current) {
        if (left.type == 1) {
            if (left.count + current.count > kMaximumKeys) {
                throw std::runtime_error("leaf merge overflow");
            }

            for (std::size_t i = 0; i < current.count; ++i) {
                left.keys[left.count + i] = current.keys[i];
            }
            left.count = static_cast<std::uint16_t>(
                left.count + current.count);
            left.next = current.next;
        } else {
            if (left.count + current.count + 1 > kMaximumKeys) {
                throw std::runtime_error("internal merge overflow");
            }

            const std::size_t old_left_count = left.count;
            left.keys[old_left_count] =
                parent.keys[child_index - 1];

            for (std::size_t i = 0; i < current.count; ++i) {
                left.keys[old_left_count + 1 + i] =
                    current.keys[i];
            }
            for (std::size_t i = 0; i <= current.count; ++i) {
                left.children[old_left_count + 1 + i] =
                    current.children[i];
            }

            left.count = static_cast<std::uint16_t>(
                old_left_count + current.count + 1);
        }

        removeParentEntry(
            parent, child_index - 1, child_index);
    }

    static void mergeRightIntoCurrent(Node &parent,
                                      Node &current,
                                      const Node &right) {
        if (current.type == 1) {
            if (current.count + right.count > kMaximumKeys) {
                throw std::runtime_error("leaf merge overflow");
            }

            for (std::size_t i = 0; i < right.count; ++i) {
                current.keys[current.count + i] = right.keys[i];
            }
            current.count = static_cast<std::uint16_t>(
                current.count + right.count);
            current.next = right.next;
        } else {
            if (current.count + right.count + 1 > kMaximumKeys) {
                throw std::runtime_error("internal merge overflow");
            }

            const std::size_t old_current_count = current.count;
            current.keys[old_current_count] = parent.keys[0];

            for (std::size_t i = 0; i < right.count; ++i) {
                current.keys[old_current_count + 1 + i] =
                    right.keys[i];
            }
            for (std::size_t i = 0; i <= right.count; ++i) {
                current.children[old_current_count + 1 + i] =
                    right.children[i];
            }

            current.count = static_cast<std::uint16_t>(
                old_current_count + right.count + 1);
        }

        removeParentEntry(parent, 0, 1);
    }
};

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    try {
        BPlusTree database;

        int command_count = 0;
        if (!(std::cin >> command_count)) {
            return 0;
        }

        for (int i = 0; i < command_count; ++i) {
            std::string command;
            std::string index;
            std::cin >> command >> index;

            if (command == "find") {
                database.find(index);
                continue;
            }

            std::int32_t value = 0;
            std::cin >> value;

            if (command == "insert") {
                database.insert(index, value);
            } else if (command == "delete") {
                database.erase(index, value);
            }
        }
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 0;
}
