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

Each structure is a **struct with a constructor and methods**: the stack is `Stack<T>`, made
by `Stack()` and used with `s.push(1)`, `s.pop()` ...; the queue is `Queue<T>` with `q.add`,
`q.take` ...; and so on below.

| Struct | Its nodes | | Struct | Its nodes |
| --- | --- | --- | --- | --- |
| `Stack<T>` | | | `Set<T>` | |
| `Queue<T>` | | | `OrderedSet<T>` | |
| `LinkedList<T>` | `ListNode<T>` | | `Deque<T>` | |
| `BinaryTree<T>` | `TreeNode<T>` | | `DoublyLinkedList<T>` | `DllNode<T>` |
| `Heap<T>` | | | `Graph<T>` | |
| `Trie` | `TrieNode` | | | |

**The insides are private.** A member whose name ends in `_` can only be touched by the
struct's own methods (`items_`, `head_`, `count_`, ... - error `S076` from outside): programs
use the methods, never the insides. And since the structures are structs, not modules, a
variable may be called `stack` or `tree` freely; still, name it after what it holds:
`plates`, `words`, `seen`, `roads`.

Every structure is **generic**: `Stack<T>` holds any element type `T`, and the element type is
named at the declaration - `var s: Stack<int> = Stack()` says what the empty stack will hold
(see the [language reference](language.md), section 8). The tree and the heap compare their
elements with `<`, so their elements are numbers or strings. Taking from an empty structure
stops the program with a runtime error, as a wrong built-in call would; ask `is_empty()`
first.

**Try it:** [`samples/advanced/en/01_datastructures.th`](../samples/advanced/en/01_datastructures.th)
walks through every structure (Pashto:
[`samples/advanced/ps/01_datastructures.th`](../samples/advanced/ps/01_datastructures.th)).
These samples are deliberately not in the IDE's picker - open them from here.

## Stack - last in, first out

A pile of plates: what was put on top last comes back first. Every operation is O(1).

| Method | What it does |
| --- | --- |
| `Stack()` | A new, empty stack: `var s: Stack<int> = Stack()`. |
| `s.push(value)` | Puts a value on top. |
| `s.pop(): T` | Takes the top value off and returns it. |
| `s.peek(): T` | The top value, without taking it off. |
| `s.size(): int` | How many values the stack holds. |
| `s.is_empty(): bool` | True when nothing is on it. |

```thunder
import "datastructures"

var plates: Stack<int> = Stack()
plates.push(1)
plates.push(2)
println(plates.pop())   # 2 - the last one in is the first one out
println(plates.peek())  # 1 - still there
```

`plates.push` is the stack's own method; the plain `push` of the language (for arrays) keeps
its meaning everywhere - inside the methods too, where `push(items_, value)` is the built-in.

## Queue - first in, first out

A line at a counter: `add` joins at the back, `take` serves the front. The front
index moves instead of shifting every element, so both are O(1) on average.

| Method | What it does |
| --- | --- |
| `Queue()` | A new, empty queue: `var q: Queue<string> = Queue()`. |
| `q.add(value)` | Joins the back of the queue. |
| `q.take(): T` | Serves (removes and returns) the front. |
| `q.peek(): T` | The front, without serving it. |
| `q.size(): int` | How many values are waiting. |
| `q.is_empty(): bool` | True when nobody is waiting. |

## Linked list - a chain of nodes

Each node (`ListNode<T>`) holds its value and the node that follows; `nothing` ends
the chain. This is the textbook picture, made of a recursive struct; the nodes are plain
data, and `LinkedList<T>` is the object that owns and works the chain. Adding at the front is
O(1); adding at the end and searching walk the chain, O(n).

| Method | What it does |
| --- | --- |
| `LinkedList()` | A new, empty list: `var l: LinkedList<int> = LinkedList()`. |
| `l.add_first(value)` | Adds a value at the front, O(1). |
| `l.add_last(value)` | Adds a value at the end. |
| `l.remove_first(): T` | Removes the front value and returns it. |
| `l.remove_value(value): bool` | Removes the first node with this value; true if found. |
| `l.has(value): bool` | True if some node holds this value. |
| `l.size(): int` | How many values the list holds. |
| `l.to_array(): T[]` | The values in list order, as an ordinary array. |

## Binary search tree - ordered lookups

Every node's left subtree holds smaller values, its right subtree larger ones, so one value
is found in O(height) steps - O(log n) when the tree is reasonably balanced. Duplicates are
ignored on insert.

| Method | What it does |
| --- | --- |
| `BinaryTree()` | A new, empty tree: `var t: BinaryTree<int> = BinaryTree()`. |
| `t.insert(value)` | Adds a value in its ordered place. |
| `t.has(value): bool` | True if the value is in the tree. |
| `t.remove(value): bool` | Removes a value; true if it was there. |
| `t.size(): int` | How many values the tree holds. |
| `t.height(): int` | Nodes on the longest path from the root (0 when empty). |
| `t.smallest(): T` / `t.largest(): T` | The ends of the order. |
| `t.sorted(): T[]` | Every value in ascending order (an in-order walk). |

```thunder
import "datastructures"

var words: BinaryTree<string> = BinaryTree()
for word in ["pear", "apple", "fig"]
    words.insert(word)
println(words.sorted())     # ["apple", "fig", "pear"] - the tree keeps the order
println(words.has("fig"))   # true, found in O(log n) steps
```

## Heap - the smallest first

`take` always returns the smallest value first, however the values arrived - a priority
queue. The heap is a binary tree flattened into one array: the children of index `i` sit at
`2*i + 1` and `2*i + 2`, and every parent is `<=` its children. Adding and taking are
O(log n); peeking is O(1).

| Method | What it does |
| --- | --- |
| `Heap()` | A new, empty heap: `var h: Heap<int> = Heap()`. |
| `h.add(value)` | Adds a value. |
| `h.take(): T` | Removes and returns the smallest value. |
| `h.peek(): T` | The smallest value, without removing it. |
| `h.size(): int` | How many values the heap holds. |
| `h.is_empty(): bool` | True when it holds none. |

## Set - unique elements

A collection in which every value appears once. It is built on Thunder's own map - the value
is the key - so adding, removing and membership are all O(1), and the elements are map keys:
`int`, `bool` or `string`. `values()` returns them in the order they were first added.

| Method | What it does |
| --- | --- |
| `Set()` | A new, empty set: `var s: Set<string> = Set()`. |
| `s.add(value)` | Puts a value in; a value already there stays single. |
| `s.remove(value): bool` | Takes a value out; true if it was there. |
| `s.has(value): bool` | True if the value is in the set. |
| `s.size(): int` / `s.is_empty(): bool` | How many values it holds. |
| `s.values(): T[]` | The values, in first-added order. |
| `a.union(b): Set<T>` | Every value in either set. |
| `a.intersection(b): Set<T>` | Every value in both sets. |
| `a.difference(b): Set<T>` | Every value of `a` that is not in `b`. |

```thunder
import "datastructures"

var seen: Set<string> = Set()
seen.add("red")
seen.add("blue")
seen.add("red")              # already there: still one "red"
println(seen.values())       # ["red", "blue"]
println(seen.has("green"))   # false
```

## OrderedSet - unique and sorted

A set that keeps its values in ascending order: a binary search tree underneath, so adding,
removing and membership are O(log n) and `values()` comes out sorted. The elements
are compared with `<`, so they are numbers or strings.

| Method | What it does |
| --- | --- |
| `OrderedSet()` | A new, empty ordered set: `var s: OrderedSet<int> = OrderedSet()`. |
| `s.add(value)` | Puts a value in its ordered place. |
| `s.remove(value): bool` | Takes a value out; true if it was there. |
| `s.has(value): bool` | True if the value is in the set. |
| `s.size(): int` / `s.is_empty(): bool` | How many values it holds. |
| `s.values(): T[]` | The values, in ascending order. |
| `s.smallest(): T` / `s.largest(): T` | The ends of the order. |

## Deque - open at both ends

A queue whose both ends serve: values join and leave at the front and at the back (the
structure behind sliding windows and work stealing). Two stacks back to back make every
operation O(1) on average.

| Method | What it does |
| --- | --- |
| `Deque()` | A new, empty deque: `var d: Deque<int> = Deque()`. |
| `d.add_first(value)` / `d.add_last(value)` | Puts a value at either end. |
| `d.take_first(): T` / `d.take_last(): T` | Takes a value off either end. |
| `d.peek_first(): T` / `d.peek_last(): T` | Looks, without taking. |
| `d.size(): int` / `d.is_empty(): bool` | How many values it holds. |
| `d.to_array(): T[]` | The values from front to back. |

## Doubly linked list - walks both ways

Like the linked list, but every node (`DllNode<T>`) also knows the node before
it and the list keeps both ends: adding and removing at either end is O(1), and it can be
walked backwards.

| Method | What it does |
| --- | --- |
| `DoublyLinkedList()` | A new, empty list: `var l: DoublyLinkedList<int> = DoublyLinkedList()`. |
| `l.add_first(value)` / `l.add_last(value)` | Adds at either end, O(1). |
| `l.take_first(): T` / `l.take_last(): T` | Removes from either end, O(1). |
| `l.remove_value(value): bool` | Removes the first node with this value. |
| `l.has(value): bool` | True if some node holds this value. |
| `l.size(): int` | How many values the list holds. |
| `l.to_array(): T[]` / `l.to_array_reversed(): T[]` | Both walking directions. |

## Graph - vertices, edges and searches

An adjacency map: each vertex maps to the vertices an edge leads to. Edges are directed;
`connect` adds one each way for an undirected graph. The vertices are map keys (int,
bool or string). The searches use the file's own `Queue` and `Set`.

| Method | What it does |
| --- | --- |
| `Graph()` | A new, empty graph: `var g: Graph<string> = Graph()`. |
| `g.add_vertex(vertex)` | Adds a vertex with no edges yet. |
| `g.add_edge(from, to)` | Adds a directed edge (vertices added as needed). |
| `g.connect(a, b)` | An undirected edge: one each way. |
| `g.vertices(): T[]` / `g.neighbors(vertex): T[]` | What is there. |
| `g.bfs(start): T[]` | Breadth-first: every reachable vertex, nearest rings first. |
| `g.dfs(start): T[]` | Depth-first: each road to its end before the next. |
| `g.shortest_path(from, to): T[]` | Fewest edges, `[from, ..., to]`; empty when unreachable. |
| `g.has_path(from, to): bool` | True if some chain of edges leads there. |

```thunder
import "datastructures"

var roads: Graph<string> = Graph()
roads.connect("Kabul", "Ghazni")
roads.connect("Ghazni", "Kandahar")
println(roads.shortest_path("Kabul", "Kandahar"))
# ["Kabul", "Ghazni", "Kandahar"]
```

## Trie - a tree of prefixes

Every word shares its path with every word that starts the same way, so lookup and prefix
questions cost O(length of the word), however many words are stored - the structure behind
autocomplete. (The trie holds strings, so it is not generic.)

| Method | What it does |
| --- | --- |
| `Trie()` | A new, empty trie: `var t: Trie = Trie()`. |
| `t.add(word)` | Adds a word (twice keeps it once). |
| `t.has(word): bool` | True if exactly this word was added. |
| `t.has_prefix(prefix): bool` | True if some word starts with this prefix. |
| `t.size(): int` | How many distinct words it holds. |
| `t.words(): string[]` | Every word, in alphabetical order. |

## The older names

The library has worn two older shapes. Before 0.7 every function carried its structure in its
name (`new_stack()`, `stack_push`, `stack_pop`, ...); 0.7 put each structure in a module of
its own (`stack.new()`, `stack.push(s, 1)`). Now that structs carry their own constructor and
methods, the modules and their `new` are gone - the constructor builds, the methods act:

| Was (the module) | Now (the object) |
| --- | --- |
| `stack.new()`, `stack.push(s, v)`, `stack.pop(s)`, ... | `Stack()`, `s.push(v)`, `s.pop()`, ... |
| `queue.new()`, `queue.add(q, v)`, `queue.take(q)`, ... | `Queue()`, `q.add(v)`, `q.take()`, ... |
| `linked_list.new()`, `linked_list.add_first(l, v)`, ..., `linked_list.Node<T>` | `LinkedList()`, `l.add_first(v)`, ..., `ListNode<T>` |
| `tree.new()`, `tree.insert(t, v)`, ..., `tree.Node<T>` | `BinaryTree()`, `t.insert(v)`, ..., `TreeNode<T>` |
| `heap.new()`, `heap.add(h, v)`, ... | `Heap()`, `h.add(v)`, ... |
| `set.new()`, `set.add(s, v)`, `set.union(a, b)`, ... | `Set()`, `s.add(v)`, `a.union(b)`, ... |
| `ordered_set.new()`, `ordered_set.add(s, v)`, ... | `OrderedSet()`, `s.add(v)`, ... |
| `deque.new()`, `deque.add_first(d, v)`, ... | `Deque()`, `d.add_first(v)`, ... |
| `doubly_linked_list.new()`, `doubly_linked_list.add_first(l, v)`, ..., `doubly_linked_list.Node<T>` | `DoublyLinkedList()`, `l.add_first(v)`, ..., `DllNode<T>` |
| `graph.new()`, `graph.connect(g, a, b)`, `graph.bfs(g, v)`, ... | `Graph()`, `g.connect(a, b)`, `g.bfs(v)`, ... |
| `trie.new()`, `trie.add(t, w)`, ..., `trie.Node` | `Trie()`, `t.add(w)`, ..., `TrieNode` |

The struct types are plain names now, with no module in front: `Stack<int>`,
`BinaryTree<string>`.

See also the [algorithms reference](algorithms.md) - sorting, searching and text algorithms
over plain arrays and strings.
