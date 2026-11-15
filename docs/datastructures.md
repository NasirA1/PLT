# The data structures reference

`import "datastructures"` brings the classic data structures into a program: a **stack**, a
**queue**, a **deque**, a **linked list** (singly and doubly linked), a **binary search
tree**, a **heap** (priority queue), two kinds of **set** (unique elements, insertion-ordered
or sorted), a **graph** with its searches, and a **trie**. Nothing
in the module is native - it is ordinary Thunder in
[`lib/thunder/en/datastructures.th`](../lib/thunder/en/datastructures.th), written to be read:
open it to see how each structure really works. The Pashto module
([`lib/thunder/ps/datastructures.th`](../lib/thunder/ps/datastructures.th)) is the same
structures with Pashto names; both are found automatically, next to the toolchain.

Every structure is **generic**: `Stack<T>` holds any element type `T`, deduced from how the
structure is used - `Stack(items: ["a"])` is a `Stack<string>`, and
`var s: Stack<int> = new_stack()` says what an empty one will hold (see the
[language reference](language.md), section 8). The tree and the heap compare their elements
with `<`, so their elements are numbers or strings. Taking from an empty structure stops the
program with a runtime error, as a wrong built-in call would; ask `stack_is_empty` /
`queue_is_empty` / `heap_is_empty` first.

**Try it:** [`samples/advanced/en/01_datastructures.th`](../samples/advanced/en/01_datastructures.th)
walks through every structure (Pashto:
[`samples/advanced/ps/01_datastructures.th`](../samples/advanced/ps/01_datastructures.th)).
These samples are deliberately not in the IDE's picker - open them from here.

## Stack - last in, first out

A pile of plates: what was put on top last comes back first. Every operation is O(1).

| Function | What it does |
| --- | --- |
| `new_stack<T>(): Stack<T>` | A new, empty stack. |
| `stack_push<T>(stack, value)` | Puts a value on top. |
| `stack_pop<T>(stack): T` | Takes the top value off and returns it. |
| `stack_peek<T>(stack): T` | The top value, without taking it off. |
| `stack_size<T>(stack): int` | How many values the stack holds. |
| `stack_is_empty<T>(stack): bool` | True when nothing is on it. |

```thunder
import "datastructures"

var plates: Stack<int> = new_stack()
stack_push(plates, 1)
stack_push(plates, 2)
println(stack_pop(plates))   # 2 - the last one in is the first one out
println(stack_peek(plates))  # 1 - still there
```

## Queue - first in, first out

A line at a counter: `queue_add` joins at the back, `queue_take` serves the front. The front
index moves instead of shifting every element, so both are O(1) on average.

| Function | What it does |
| --- | --- |
| `new_queue<T>(): Queue<T>` | A new, empty queue. |
| `queue_add<T>(queue, value)` | Joins the back of the queue. |
| `queue_take<T>(queue): T` | Serves (removes and returns) the front. |
| `queue_peek<T>(queue): T` | The front, without serving it. |
| `queue_size<T>(queue): int` | How many values are waiting. |
| `queue_is_empty<T>(queue): bool` | True when nobody is waiting. |

## Linked list - a chain of nodes

Each node (`ListNode<T>`) holds its value and the node that follows; `nothing` ends the chain.
This is the textbook picture, made of a recursive struct. Adding at the front is O(1); adding
at the end and searching walk the chain, O(n).

| Function | What it does |
| --- | --- |
| `new_linked_list<T>(): LinkedList<T>` | A new, empty list. |
| `list_add_first<T>(list, value)` | Adds a value at the front, O(1). |
| `list_add_last<T>(list, value)` | Adds a value at the end. |
| `list_remove_first<T>(list): T` | Removes the front value and returns it. |
| `list_remove_value<T>(list, value): bool` | Removes the first node with this value; true if found. |
| `list_has<T>(list, value): bool` | True if some node holds this value. |
| `list_size<T>(list): int` | How many values the list holds. |
| `list_to_array<T>(list): T[]` | The values in list order, as an ordinary array. |

## Binary search tree - ordered lookups

Every node's left subtree holds smaller values, its right subtree larger ones, so one value
is found in O(height) steps - O(log n) when the tree is reasonably balanced. Duplicates are
ignored on insert.

| Function | What it does |
| --- | --- |
| `new_tree<T>(): BinaryTree<T>` | A new, empty tree. |
| `tree_insert<T>(tree, value)` | Adds a value in its ordered place. |
| `tree_has<T>(tree, value): bool` | True if the value is in the tree. |
| `tree_remove<T>(tree, value): bool` | Removes a value; true if it was there. |
| `tree_size<T>(tree): int` | How many values the tree holds. |
| `tree_height<T>(tree): int` | Nodes on the longest path from the root (0 when empty). |
| `tree_smallest<T>(tree): T` / `tree_largest<T>(tree): T` | The ends of the order. |
| `tree_sorted<T>(tree): T[]` | Every value in ascending order (an in-order walk). |

```thunder
import "datastructures"

var tree: BinaryTree<string> = new_tree()
for word in ["pear", "apple", "fig"]
    tree_insert(tree, word)
println(tree_sorted(tree))     # ["apple", "fig", "pear"] - the tree keeps the order
println(tree_has(tree, "fig")) # true, found in O(log n) steps
```

## Heap - the smallest first

`heap_take` always returns the smallest value first, however the values arrived - a priority
queue. The heap is a binary tree flattened into one array: the children of index `i` sit at
`2*i + 1` and `2*i + 2`, and every parent is `<=` its children. Adding and taking are
O(log n); peeking is O(1).

| Function | What it does |
| --- | --- |
| `new_heap<T>(): Heap<T>` | A new, empty heap. |
| `heap_add<T>(heap, value)` | Adds a value. |
| `heap_take<T>(heap): T` | Removes and returns the smallest value. |
| `heap_peek<T>(heap): T` | The smallest value, without removing it. |
| `heap_size<T>(heap): int` | How many values the heap holds. |
| `heap_is_empty<T>(heap): bool` | True when it holds none. |

## Set - unique elements

A collection in which every value appears once. It is built on Thunder's own map - the value
is the key - so adding, removing and membership are all O(1), and the elements are map keys:
`int`, `bool` or `string`. `set_values` returns them in the order they were first added.

| Function | What it does |
| --- | --- |
| `new_set<T>(): Set<T>` | A new, empty set. |
| `set_add<T>(set, value)` | Puts a value in; a value already there stays single. |
| `set_remove<T>(set, value): bool` | Takes a value out; true if it was there. |
| `set_has<T>(set, value): bool` | True if the value is in the set. |
| `set_size<T>(set): int` / `set_is_empty<T>(set): bool` | How many values it holds. |
| `set_values<T>(set): T[]` | The values, in first-added order. |
| `set_union<T>(a, b): Set<T>` | Every value in either set. |
| `set_intersection<T>(a, b): Set<T>` | Every value in both sets. |
| `set_difference<T>(a, b): Set<T>` | Every value of `a` that is not in `b`. |

```thunder
import "datastructures"

var seen: Set<string> = new_set()
set_add(seen, "red")
set_add(seen, "blue")
set_add(seen, "red")                 # already there: still one "red"
println(set_values(seen))            # ["red", "blue"]
println(set_has(seen, "green"))      # false
```

## OrderedSet - unique and sorted

A set that keeps its values in ascending order: a binary search tree underneath, so adding,
removing and membership are O(log n) and `ordered_set_values` comes out sorted. The elements
are compared with `<`, so they are numbers or strings.

| Function | What it does |
| --- | --- |
| `new_ordered_set<T>(): OrderedSet<T>` | A new, empty ordered set. |
| `ordered_set_add<T>(set, value)` | Puts a value in its ordered place. |
| `ordered_set_remove<T>(set, value): bool` | Takes a value out; true if it was there. |
| `ordered_set_has<T>(set, value): bool` | True if the value is in the set. |
| `ordered_set_size<T>(set): int` / `ordered_set_is_empty<T>(set): bool` | How many values it holds. |
| `ordered_set_values<T>(set): T[]` | The values, in ascending order. |
| `ordered_set_smallest<T>(set): T` / `ordered_set_largest<T>(set): T` | The ends of the order. |

## Deque - open at both ends

A queue whose both ends serve: values join and leave at the front and at the back (the
structure behind sliding windows and work stealing). Two stacks back to back make every
operation O(1) on average.

| Function | What it does |
| --- | --- |
| `new_deque<T>(): Deque<T>` | A new, empty deque. |
| `deque_add_first<T>(deque, value)` / `deque_add_last<T>(deque, value)` | Puts a value at either end. |
| `deque_take_first<T>(deque): T` / `deque_take_last<T>(deque): T` | Takes a value off either end. |
| `deque_peek_first<T>(deque): T` / `deque_peek_last<T>(deque): T` | Looks, without taking. |
| `deque_size<T>(deque): int` / `deque_is_empty<T>(deque): bool` | How many values it holds. |
| `deque_to_array<T>(deque): T[]` | The values from front to back. |

## Doubly linked list - walks both ways

Like the linked list, but every node also knows the node before it and the list keeps both
ends: adding and removing at either end is O(1), and it can be walked backwards.

| Function | What it does |
| --- | --- |
| `new_dlist<T>(): DoublyLinkedList<T>` | A new, empty list. |
| `dlist_add_first<T>(list, value)` / `dlist_add_last<T>(list, value)` | Adds at either end, O(1). |
| `dlist_take_first<T>(list): T` / `dlist_take_last<T>(list): T` | Removes from either end, O(1). |
| `dlist_remove_value<T>(list, value): bool` | Removes the first node with this value. |
| `dlist_has<T>(list, value): bool` | True if some node holds this value. |
| `dlist_size<T>(list): int` | How many values the list holds. |
| `dlist_to_array<T>(list): T[]` / `dlist_to_array_reversed<T>(list): T[]` | Both walking directions. |

## Graph - vertices, edges and searches

An adjacency map: each vertex maps to the vertices an edge leads to. Edges are directed;
`graph_connect` adds one each way for an undirected graph. The vertices are map keys (int,
bool or string). The searches use the module's own queue and set.

| Function | What it does |
| --- | --- |
| `new_graph<T>(): Graph<T>` | A new, empty graph. |
| `graph_add_vertex<T>(graph, vertex)` | Adds a vertex with no edges yet. |
| `graph_add_edge<T>(graph, from, to)` | Adds a directed edge (vertices added as needed). |
| `graph_connect<T>(graph, a, b)` | An undirected edge: one each way. |
| `graph_vertices<T>(graph): T[]` / `graph_neighbors<T>(graph, vertex): T[]` | What is there. |
| `graph_bfs<T>(graph, start): T[]` | Breadth-first: every reachable vertex, nearest rings first. |
| `graph_dfs<T>(graph, start): T[]` | Depth-first: each road to its end before the next. |
| `graph_shortest_path<T>(graph, from, to): T[]` | Fewest edges, `[from, ..., to]`; empty when unreachable. |
| `graph_has_path<T>(graph, from, to): bool` | True if some chain of edges leads there. |

```thunder
import "datastructures"

var roads: Graph<string> = new_graph()
graph_connect(roads, "Kabul", "Ghazni")
graph_connect(roads, "Ghazni", "Kandahar")
println(graph_shortest_path(roads, "Kabul", "Kandahar"))
# ["Kabul", "Ghazni", "Kandahar"]
```

## Trie - a tree of prefixes

Every word shares its path with every word that starts the same way, so lookup and prefix
questions cost O(length of the word), however many words are stored - the structure behind
autocomplete. (The trie holds strings, so it is not generic.)

| Function | What it does |
| --- | --- |
| `new_trie(): Trie` | A new, empty trie. |
| `trie_add(trie, word)` | Adds a word (twice keeps it once). |
| `trie_has(trie, word): bool` | True if exactly this word was added. |
| `trie_has_prefix(trie, prefix): bool` | True if some word starts with this prefix. |
| `trie_size(trie): int` | How many distinct words it holds. |
| `trie_words(trie): string[]` | Every word, in alphabetical order. |

See also the [algorithms reference](algorithms.md) - sorting, searching and text algorithms
over plain arrays and strings.
