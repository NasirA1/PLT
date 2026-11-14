# The data structures reference

`import "datastructures"` brings the classic data structures into a program: a **stack**, a
**queue**, a **linked list**, a **binary search tree** and a **heap** (priority queue). Nothing
in the module is native - it is ordinary Thunder in
[`lib/thunder/en/datastructures.th`](../lib/thunder/en/datastructures.th), written to be read:
open it to see how each structure really works. The Pashto module
([`lib/thunder/ps/datastructures.th`](../lib/thunder/ps/datastructures.th)) is the same
structures with Pashto names; both are found automatically, next to the toolchain.

The elements are `int`. Thunder has no generic types, so for a structure of strings (or of a
struct of your own) copy the structure you need into your program and change the element type -
the algorithms stay exactly the same, which is the point of reading them.

Taking from an empty structure stops the program with a runtime error, as a wrong built-in
call would; ask `stack_is_empty` / `queue_is_empty` / `heap_is_empty` first.

## Stack - last in, first out

A pile of plates: what was put on top last comes back first. Every operation is O(1).

| Function | What it does |
| --- | --- |
| `new_stack(): Stack` | A new, empty stack. |
| `stack_push(stack, value)` | Puts a value on top. |
| `stack_pop(stack): int` | Takes the top value off and returns it. |
| `stack_peek(stack): int` | The top value, without taking it off. |
| `stack_size(stack): int` | How many values the stack holds. |
| `stack_is_empty(stack): bool` | True when nothing is on it. |

```thunder
import "datastructures"

var plates = new_stack()
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
| `new_queue(): Queue` | A new, empty queue. |
| `queue_add(queue, value)` | Joins the back of the queue. |
| `queue_take(queue): int` | Serves (removes and returns) the front. |
| `queue_peek(queue): int` | The front, without serving it. |
| `queue_size(queue): int` | How many values are waiting. |
| `queue_is_empty(queue): bool` | True when nobody is waiting. |

## Linked list - a chain of nodes

Each node knows its value and which node follows. Thunder structs cannot contain themselves,
so the nodes live in parallel arrays and a "pointer" is an index into them (`-1` means
"none") - exactly how linked lists are built inside languages that have no pointers either.
Adding at the front is O(1); adding at the end and searching walk the chain, O(n).

| Function | What it does |
| --- | --- |
| `new_linked_list(): LinkedList` | A new, empty list. |
| `list_add_first(list, value)` | Adds a value at the front, O(1). |
| `list_add_last(list, value)` | Adds a value at the end. |
| `list_remove_first(list): int` | Removes the front value and returns it. |
| `list_remove_value(list, value): bool` | Removes the first node with this value; true if found. |
| `list_has(list, value): bool` | True if some node holds this value. |
| `list_size(list): int` | How many values the list holds. |
| `list_to_array(list): int[]` | The values in list order, as an ordinary array. |

## Binary search tree - ordered lookups

Every node's left subtree holds smaller values, its right subtree larger ones, so one value
is found in O(height) steps - O(log n) when the tree is reasonably balanced. Duplicates are
ignored on insert.

| Function | What it does |
| --- | --- |
| `new_tree(): BinaryTree` | A new, empty tree. |
| `tree_insert(tree, value)` | Adds a value in its ordered place. |
| `tree_has(tree, value): bool` | True if the value is in the tree. |
| `tree_remove(tree, value): bool` | Removes a value; true if it was there. |
| `tree_size(tree): int` | How many values the tree holds. |
| `tree_height(tree): int` | Nodes on the longest path from the root (0 when empty). |
| `tree_smallest(tree): int` / `tree_largest(tree): int` | The ends of the order. |
| `tree_sorted(tree): int[]` | Every value in ascending order (an in-order walk). |

```thunder
import "datastructures"

var tree = new_tree()
for n in [5, 3, 8, 1, 4]
    tree_insert(tree, n)
println(tree_sorted(tree))   # [1, 3, 4, 5, 8] - the tree keeps the order
println(tree_has(tree, 4))   # true, found in O(log n) steps
```

## Heap - the smallest first

`heap_take` always returns the smallest value first, however the values arrived - a priority
queue. The heap is a binary tree flattened into one array: the children of index `i` sit at
`2*i + 1` and `2*i + 2`, and every parent is `<=` its children. Adding and taking are
O(log n); peeking is O(1).

| Function | What it does |
| --- | --- |
| `new_heap(): Heap` | A new, empty heap. |
| `heap_add(heap, value)` | Adds a value. |
| `heap_take(heap): int` | Removes and returns the smallest value. |
| `heap_peek(heap): int` | The smallest value, without removing it. |
| `heap_size(heap): int` | How many values the heap holds. |
| `heap_is_empty(heap): bool` | True when it holds none. |

See also the [algorithms reference](algorithms.md) - sorting and searching over plain arrays.
