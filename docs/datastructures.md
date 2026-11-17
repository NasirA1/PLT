# The data structures reference

`import "datastructures"` brings the classic data structures into a program: a **stack**, a
**queue**, a **deque**, a **linked list** (singly and doubly linked), a **binary search
tree**, a **heap** (priority queue), two kinds of **set** (unique elements, insertion-ordered
or sorted), a **graph** with its searches, and a **trie**. Nothing
in the file is native - it is ordinary Thunder in
[`lib/thunder/en/datastructures.th`](../lib/thunder/en/datastructures.th), written to be read:
open it to see how each structure really works. The Pashto file
([`lib/thunder/ps/datastructures.th`](../lib/thunder/ps/datastructures.th)) is the same
structures with Pashto names; both are found automatically, next to the toolchain.

Each structure is a **module** of its own, holding its struct and its functions: the stack is
`stack.Stack<T>`, made by `stack.new()` and used with `stack.push`, `stack.pop` ...; the
queue is `queue.Queue<T>` with `queue.add`, `queue.take` ...; and so on below.

| Module | Struct(s) | | Module | Struct(s) |
| --- | --- | --- | --- | --- |
| `stack` | `stack.Stack<T>` | | `set` | `set.Set<T>` |
| `queue` | `queue.Queue<T>` | | `ordered_set` | `ordered_set.OrderedSet<T>` |
| `linked_list` | `linked_list.LinkedList<T>`, `linked_list.Node<T>` | | `deque` | `deque.Deque<T>` |
| `tree` | `tree.BinaryTree<T>`, `tree.Node<T>` | | `doubly_linked_list` | `doubly_linked_list.DoublyLinkedList<T>`, `doubly_linked_list.Node<T>` |
| `heap` | `heap.Heap<T>` | | `graph` | `graph.Graph<T>` |
| `trie` | `trie.Trie`, `trie.Node` | | | |

**The module names are reserved.** In a program that imports `"datastructures"`, `stack`,
`queue`, `linked_list`, `tree`, `heap`, `set`, `ordered_set`, `deque`, `doubly_linked_list`,
`graph` and `trie` name the modules, so no variable, parameter or function may be called that
(error `S065`, "'tree' is the name of a module; give this variable another name"). Name the
variable after what it holds instead: `plates`, `words`, `seen`, `roads`.

Every structure is **generic**: `stack.Stack<T>` holds any element type `T`, deduced from how
the structure is used - `stack.Stack(items: ["a"])` is a `stack.Stack<string>`, and
`var s: stack.Stack<int> = stack.new()` says what an empty one will hold (see the
[language reference](language.md), section 8). The tree and the heap compare their elements
with `<`, so their elements are numbers or strings. Taking from an empty structure stops the
program with a runtime error, as a wrong built-in call would; ask `stack.is_empty` /
`queue.is_empty` / `heap.is_empty` first.

**Try it:** [`samples/advanced/en/01_datastructures.th`](../samples/advanced/en/01_datastructures.th)
walks through every structure (Pashto:
[`samples/advanced/ps/01_datastructures.th`](../samples/advanced/ps/01_datastructures.th)).
These samples are deliberately not in the IDE's picker - open them from here.

## Stack - last in, first out

A pile of plates: what was put on top last comes back first. Every operation is O(1).

| Function | What it does |
| --- | --- |
| `stack.new<T>(): stack.Stack<T>` | A new, empty stack. |
| `stack.push<T>(s, value)` | Puts a value on top. |
| `stack.pop<T>(s): T` | Takes the top value off and returns it. |
| `stack.peek<T>(s): T` | The top value, without taking it off. |
| `stack.size<T>(s): int` | How many values the stack holds. |
| `stack.is_empty<T>(s): bool` | True when nothing is on it. |

```thunder
import "datastructures"

var plates: stack.Stack<int> = stack.new()
stack.push(plates, 1)
stack.push(plates, 2)
println(stack.pop(plates))   # 2 - the last one in is the first one out
println(stack.peek(plates))  # 1 - still there
```

`stack.push` is the module's own; the plain `push` of the language (for arrays) keeps its
meaning everywhere - inside the module too, where `stack.push` is written in full.

## Queue - first in, first out

A line at a counter: `queue.add` joins at the back, `queue.take` serves the front. The front
index moves instead of shifting every element, so both are O(1) on average.

| Function | What it does |
| --- | --- |
| `queue.new<T>(): queue.Queue<T>` | A new, empty queue. |
| `queue.add<T>(q, value)` | Joins the back of the queue. |
| `queue.take<T>(q): T` | Serves (removes and returns) the front. |
| `queue.peek<T>(q): T` | The front, without serving it. |
| `queue.size<T>(q): int` | How many values are waiting. |
| `queue.is_empty<T>(q): bool` | True when nobody is waiting. |

## Linked list - a chain of nodes

Each node (`linked_list.Node<T>`) holds its value and the node that follows; `nothing` ends
the chain. This is the textbook picture, made of a recursive struct. Adding at the front is
O(1); adding at the end and searching walk the chain, O(n).

| Function | What it does |
| --- | --- |
| `linked_list.new<T>(): linked_list.LinkedList<T>` | A new, empty list. |
| `linked_list.add_first<T>(list, value)` | Adds a value at the front, O(1). |
| `linked_list.add_last<T>(list, value)` | Adds a value at the end. |
| `linked_list.remove_first<T>(list): T` | Removes the front value and returns it. |
| `linked_list.remove_value<T>(list, value): bool` | Removes the first node with this value; true if found. |
| `linked_list.has<T>(list, value): bool` | True if some node holds this value. |
| `linked_list.size<T>(list): int` | How many values the list holds. |
| `linked_list.to_array<T>(list): T[]` | The values in list order, as an ordinary array. |

## Binary search tree - ordered lookups

Every node's left subtree holds smaller values, its right subtree larger ones, so one value
is found in O(height) steps - O(log n) when the tree is reasonably balanced. Duplicates are
ignored on insert.

| Function | What it does |
| --- | --- |
| `tree.new<T>(): tree.BinaryTree<T>` | A new, empty tree. |
| `tree.insert<T>(t, value)` | Adds a value in its ordered place. |
| `tree.has<T>(t, value): bool` | True if the value is in the tree. |
| `tree.remove<T>(t, value): bool` | Removes a value; true if it was there. |
| `tree.size<T>(t): int` | How many values the tree holds. |
| `tree.height<T>(t): int` | Nodes on the longest path from the root (0 when empty). |
| `tree.smallest<T>(t): T` / `tree.largest<T>(t): T` | The ends of the order. |
| `tree.sorted<T>(t): T[]` | Every value in ascending order (an in-order walk). |

```thunder
import "datastructures"

var words: tree.BinaryTree<string> = tree.new()     # not "var tree": tree is the module
for word in ["pear", "apple", "fig"]
    tree.insert(words, word)
println(tree.sorted(words))     # ["apple", "fig", "pear"] - the tree keeps the order
println(tree.has(words, "fig")) # true, found in O(log n) steps
```

## Heap - the smallest first

`heap.take` always returns the smallest value first, however the values arrived - a priority
queue. The heap is a binary tree flattened into one array: the children of index `i` sit at
`2*i + 1` and `2*i + 2`, and every parent is `<=` its children. Adding and taking are
O(log n); peeking is O(1).

| Function | What it does |
| --- | --- |
| `heap.new<T>(): heap.Heap<T>` | A new, empty heap. |
| `heap.add<T>(h, value)` | Adds a value. |
| `heap.take<T>(h): T` | Removes and returns the smallest value. |
| `heap.peek<T>(h): T` | The smallest value, without removing it. |
| `heap.size<T>(h): int` | How many values the heap holds. |
| `heap.is_empty<T>(h): bool` | True when it holds none. |

## Set - unique elements

A collection in which every value appears once. It is built on Thunder's own map - the value
is the key - so adding, removing and membership are all O(1), and the elements are map keys:
`int`, `bool` or `string`. `set.values` returns them in the order they were first added.

| Function | What it does |
| --- | --- |
| `set.new<T>(): set.Set<T>` | A new, empty set. |
| `set.add<T>(s, value)` | Puts a value in; a value already there stays single. |
| `set.remove<T>(s, value): bool` | Takes a value out; true if it was there. |
| `set.has<T>(s, value): bool` | True if the value is in the set. |
| `set.size<T>(s): int` / `set.is_empty<T>(s): bool` | How many values it holds. |
| `set.values<T>(s): T[]` | The values, in first-added order. |
| `set.union<T>(a, b): set.Set<T>` | Every value in either set. |
| `set.intersection<T>(a, b): set.Set<T>` | Every value in both sets. |
| `set.difference<T>(a, b): set.Set<T>` | Every value of `a` that is not in `b`. |

```thunder
import "datastructures"

var seen: set.Set<string> = set.new()
set.add(seen, "red")
set.add(seen, "blue")
set.add(seen, "red")                 # already there: still one "red"
println(set.values(seen))            # ["red", "blue"]
println(set.has(seen, "green"))      # false
```

## OrderedSet - unique and sorted

A set that keeps its values in ascending order: a binary search tree underneath, so adding,
removing and membership are O(log n) and `ordered_set.values` comes out sorted. The elements
are compared with `<`, so they are numbers or strings.

| Function | What it does |
| --- | --- |
| `ordered_set.new<T>(): ordered_set.OrderedSet<T>` | A new, empty ordered set. |
| `ordered_set.add<T>(s, value)` | Puts a value in its ordered place. |
| `ordered_set.remove<T>(s, value): bool` | Takes a value out; true if it was there. |
| `ordered_set.has<T>(s, value): bool` | True if the value is in the set. |
| `ordered_set.size<T>(s): int` / `ordered_set.is_empty<T>(s): bool` | How many values it holds. |
| `ordered_set.values<T>(s): T[]` | The values, in ascending order. |
| `ordered_set.smallest<T>(s): T` / `ordered_set.largest<T>(s): T` | The ends of the order. |

## Deque - open at both ends

A queue whose both ends serve: values join and leave at the front and at the back (the
structure behind sliding windows and work stealing). Two stacks back to back make every
operation O(1) on average.

| Function | What it does |
| --- | --- |
| `deque.new<T>(): deque.Deque<T>` | A new, empty deque. |
| `deque.add_first<T>(d, value)` / `deque.add_last<T>(d, value)` | Puts a value at either end. |
| `deque.take_first<T>(d): T` / `deque.take_last<T>(d): T` | Takes a value off either end. |
| `deque.peek_first<T>(d): T` / `deque.peek_last<T>(d): T` | Looks, without taking. |
| `deque.size<T>(d): int` / `deque.is_empty<T>(d): bool` | How many values it holds. |
| `deque.to_array<T>(d): T[]` | The values from front to back. |

## Doubly linked list - walks both ways

Like the linked list, but every node (`doubly_linked_list.Node<T>`) also knows the node before
it and the list keeps both ends: adding and removing at either end is O(1), and it can be
walked backwards.

| Function | What it does |
| --- | --- |
| `doubly_linked_list.new<T>(): doubly_linked_list.DoublyLinkedList<T>` | A new, empty list. |
| `doubly_linked_list.add_first<T>(list, value)` / `doubly_linked_list.add_last<T>(list, value)` | Adds at either end, O(1). |
| `doubly_linked_list.take_first<T>(list): T` / `doubly_linked_list.take_last<T>(list): T` | Removes from either end, O(1). |
| `doubly_linked_list.remove_value<T>(list, value): bool` | Removes the first node with this value. |
| `doubly_linked_list.has<T>(list, value): bool` | True if some node holds this value. |
| `doubly_linked_list.size<T>(list): int` | How many values the list holds. |
| `doubly_linked_list.to_array<T>(list): T[]` / `doubly_linked_list.to_array_reversed<T>(list): T[]` | Both walking directions. |

## Graph - vertices, edges and searches

An adjacency map: each vertex maps to the vertices an edge leads to. Edges are directed;
`graph.connect` adds one each way for an undirected graph. The vertices are map keys (int,
bool or string). The searches use the file's own `queue.Queue` and `set.Set`.

| Function | What it does |
| --- | --- |
| `graph.new<T>(): graph.Graph<T>` | A new, empty graph. |
| `graph.add_vertex<T>(g, vertex)` | Adds a vertex with no edges yet. |
| `graph.add_edge<T>(g, from, to)` | Adds a directed edge (vertices added as needed). |
| `graph.connect<T>(g, a, b)` | An undirected edge: one each way. |
| `graph.vertices<T>(g): T[]` / `graph.neighbors<T>(g, vertex): T[]` | What is there. |
| `graph.bfs<T>(g, start): T[]` | Breadth-first: every reachable vertex, nearest rings first. |
| `graph.dfs<T>(g, start): T[]` | Depth-first: each road to its end before the next. |
| `graph.shortest_path<T>(g, from, to): T[]` | Fewest edges, `[from, ..., to]`; empty when unreachable. |
| `graph.has_path<T>(g, from, to): bool` | True if some chain of edges leads there. |

```thunder
import "datastructures"

var roads: graph.Graph<string> = graph.new()
graph.connect(roads, "Kabul", "Ghazni")
graph.connect(roads, "Ghazni", "Kandahar")
println(graph.shortest_path(roads, "Kabul", "Kandahar"))
# ["Kabul", "Ghazni", "Kandahar"]
```

## Trie - a tree of prefixes

Every word shares its path with every word that starts the same way, so lookup and prefix
questions cost O(length of the word), however many words are stored - the structure behind
autocomplete. (The trie holds strings, so it is not generic.)

| Function | What it does |
| --- | --- |
| `trie.new(): trie.Trie` | A new, empty trie. |
| `trie.add(t, word)` | Adds a word (twice keeps it once). |
| `trie.has(t, word): bool` | True if exactly this word was added. |
| `trie.has_prefix(t, prefix): bool` | True if some word starts with this prefix. |
| `trie.size(t): int` | How many distinct words it holds. |
| `trie.words(t): string[]` | Every word, in alphabetical order. |

## The names before 0.7

Before modules, every function carried its structure in its name. Each is now the module's
member under the short name:

| Was | Now |
| --- | --- |
| `new_stack()`, `stack_push`, `stack_pop`, ... | `stack.new()`, `stack.push`, `stack.pop`, ... |
| `new_queue()`, `queue_add`, `queue_take`, ... | `queue.new()`, `queue.add`, `queue.take`, ... |
| `new_linked_list()`, `list_add_first`, ..., `ListNode<T>` | `linked_list.new()`, `linked_list.add_first`, ..., `linked_list.Node<T>` |
| `new_tree()`, `tree_insert`, ..., `TreeNode<T>` | `tree.new()`, `tree.insert`, ..., `tree.Node<T>` |
| `new_heap()`, `heap_add`, ... | `heap.new()`, `heap.add`, ... |
| `new_set()`, `set_add`, `set_union`, ... | `set.new()`, `set.add`, `set.union`, ... |
| `new_ordered_set()`, `ordered_set_add`, ... | `ordered_set.new()`, `ordered_set.add`, ... |
| `new_deque()`, `deque_add_first`, ... | `deque.new()`, `deque.add_first`, ... |
| `new_dlist()`, `dlist_add_first`, ..., `DllNode<T>` | `doubly_linked_list.new()`, `doubly_linked_list.add_first`, ..., `doubly_linked_list.Node<T>` |
| `new_graph()`, `graph_connect`, `graph_bfs`, ... | `graph.new()`, `graph.connect`, `graph.bfs`, ... |
| `new_trie()`, `trie_add`, ..., `TrieNode` | `trie.new()`, `trie.add`, ..., `trie.Node` |

The struct types are written with their module too: `stack.Stack<int>`, `tree.BinaryTree<string>`.

See also the [algorithms reference](algorithms.md) - sorting, searching and text algorithms
over plain arrays and strings.
