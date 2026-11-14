# The algorithms reference

`import "algorithms"` brings the classic sorting and searching algorithms into a program.
Nothing in the module is native - it is ordinary Thunder in
[`lib/thunder/en/algorithms.th`](../lib/thunder/en/algorithms.th), written to be read: open it
to see how the algorithms every language ships really work. (For everyday sorting Thunder
already has the built-in `sort()`; these spell out what such a built-in does.) The Pashto
module ([`lib/thunder/ps/algorithms.th`](../lib/thunder/ps/algorithms.th)) is the same
functions with Pashto names.

Every sort works on an `int[]` and sorts it **in place**, in ascending order. The searches
return the index of the value, or `-1` when it is not there.

## Sorting

| Function | Idea | Cost |
| --- | --- | --- |
| `bubble_sort(values)` | Sweep again and again, swapping neighbours in the wrong order; a sweep without swaps means sorted. | O(n²) |
| `selection_sort(values)` | Find the smallest of the rest, swap it to the front, repeat. Moves every value at most once. | O(n²) |
| `insertion_sort(values)` | Slide each next value left past every larger one - how most people sort a hand of cards. | O(n²), O(n) when nearly sorted |
| `quick_sort(values)` | Pick a pivot, partition into smaller and larger, sort the two sides - divide and conquer. | O(n log n) on average |
| `merge_sort(values)` | Sort each half, then merge the two sorted halves. Steady, at the price of helper space. | O(n log n) always |

```thunder
import "algorithms"

var numbers = [5, 2, 9, 1, 5, 6]
quick_sort(numbers)
println(numbers)             # [1, 2, 5, 5, 6, 9]
println(is_sorted(numbers))  # true
```

## Searching

| Function | Idea | Cost |
| --- | --- | --- |
| `linear_search(values, wanted): int` | Look at every value until it is the one. Works on any array. | O(n) |
| `binary_search(values, wanted): int` | The array must be **sorted**: compare with the middle, keep only the half that can still hold the value. | O(log n) |

## Helpers

| Function | What it does |
| --- | --- |
| `is_sorted(values): bool` | True if the array is in ascending order (equal neighbours allowed). |
| `index_of_smallest(values): int` | The index of the smallest value (`-1` for an empty array). |
| `shuffle(values)` | Puts the values in a random order (Fisher-Yates). `seed_random()` makes it repeatable. |

See also the [data structures reference](datastructures.md) - stacks, queues, linked lists,
trees and heaps, from the same shelf.
