# The algorithms reference

`import "algorithms"` brings the classic sorting and searching algorithms into a program.
Nothing in the module is native - it is ordinary Thunder in
[`lib/thunder/en/algorithms.th`](../lib/thunder/en/algorithms.th), written to be read: open it
to see how the algorithms every language ships really work. (For everyday sorting Thunder
already has the built-in `sort()`; these spell out what such a built-in does.) The Pashto
module ([`lib/thunder/ps/algorithms.th`](../lib/thunder/ps/algorithms.th)) is the same
functions with Pashto names.

Every function is **generic**: it works on any `T[]` whose elements compare with `<` -
numbers or strings (see the [language reference](language.md), section 8). Sorting is **in
place**, in ascending order. The searches return the index of the value, or `-1` when it is
not there.

**Try it:** [`samples/advanced/en/02_algorithms.th`](../samples/advanced/en/02_algorithms.th)
runs every algorithm (Pashto:
[`samples/advanced/ps/02_algorithms.th`](../samples/advanced/ps/02_algorithms.th)). These
samples are deliberately not in the IDE's picker - open them from here.

## Sorting

| Function | Idea | Cost |
| --- | --- | --- |
| `bubble_sort<T>(values)` | Sweep again and again, swapping neighbours in the wrong order; a sweep without swaps means sorted. | O(n²) |
| `selection_sort<T>(values)` | Find the smallest of the rest, swap it to the front, repeat. Moves every value at most once. | O(n²) |
| `insertion_sort<T>(values)` | Slide each next value left past every larger one - how most people sort a hand of cards. | O(n²), O(n) when nearly sorted |
| `quick_sort<T>(values)` | Pick a pivot, partition into smaller and larger, sort the two sides - divide and conquer. | O(n log n) on average |
| `merge_sort<T>(values)` | Sort each half, then merge the two sorted halves. Steady, at the price of helper space. | O(n log n) always |

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
| `linear_search<T>(values, wanted): int` | Look at every value until it is the one. Works on any array. | O(n) |
| `binary_search<T>(values, wanted): int` | The array must be **sorted**: compare with the middle, keep only the half that can still hold the value. | O(log n) |

## Helpers

| Function | What it does |
| --- | --- |
| `is_sorted<T>(values): bool` | True if the array is in ascending order (equal neighbours allowed). |
| `index_of_smallest<T>(values): int` | The index of the smallest value (`-1` for an empty array). |
| `shuffle<T>(values)` | Puts the values in a random order (Fisher-Yates). `seed_random()` makes it repeatable. |

## Text

The string algorithms work per character (a character is one code point, so Pashto text is
measured correctly).

| Function | Idea | Cost |
| --- | --- | --- |
| `find_text(text, pattern): int` | Slide the pattern along the text, compare character by character - what the built-in `index_of` does for strings, spelled out. `-1` when absent. | O(n·m) |
| `find_all_text(text, pattern): int[]` | Every index where the pattern starts, overlapping occurrences included. | O(n·m) |
| `is_palindrome(text): bool` | Compare the two ends and walk inwards. | O(n) |
| `longest_common_prefix(words): string` | Shrink the first word against each of the others. | O(total length) |
| `edit_distance(a, b): int` | The Levenshtein distance: the fewest single-character insertions, deletions and replacements that turn `a` into `b` - dynamic programming, one row at a time. | O(n·m) |

```thunder
import "algorithms"

println(find_text("hello world", "world"))              # 6
println(edit_distance("kitten", "sitting"))             # 3
println(longest_common_prefix(["flower", "flow"]))      # "flow"
```

See also the [data structures reference](datastructures.md) - stacks, queues, deques, linked
lists, trees, heaps, sets, graphs and tries, from the same shelf.
