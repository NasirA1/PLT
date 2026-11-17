# The algorithms reference

`import "algorithms"` brings the classic sorting and searching algorithms into a program, as
three modules: **`sorting`**, **`searching`** and **`text`**. Nothing in the file is native -
it is ordinary Thunder in [`lib/thunder/en/algorithms.th`](../lib/thunder/en/algorithms.th),
written to be read: open it to see how the algorithms every language ships really work. (For
everyday sorting Thunder already has the built-in `sort()`; these spell out what such a
built-in does.) The Pashto file ([`lib/thunder/ps/algorithms.th`](../lib/thunder/ps/algorithms.th))
is the same functions with Pashto names.

| Module | What is in it |
| --- | --- |
| `sorting` | `bubble`, `selection`, `insertion`, `quick` and `merge` sort; `is_sorted`, `shuffle` |
| `searching` | `linear` and `binary` search; `index_of_smallest` |
| `text` | `find`, `find_all`, `is_palindrome`, `longest_common_prefix`, `edit_distance` |

Every function is written with its module in front: `sorting.quick(values)`,
`searching.binary(values, 7)`, `text.find("hello world", "world")`. **The module names are
reserved** in a program that imports `"algorithms"`: a variable cannot be called `sorting`,
`searching` or `text` (error `S065`) - name it `words`, `message`, `line` instead.

The sorting and searching functions are **generic**: they work on any `T[]` whose elements
compare with `<` - numbers or strings (see the [language reference](language.md), section 8).
Sorting is **in place**, in ascending order. The searches return the index of the value, or
`-1` when it is not there.

**Try it:** [`samples/advanced/en/02_algorithms.th`](../samples/advanced/en/02_algorithms.th)
runs every algorithm (Pashto:
[`samples/advanced/ps/02_algorithms.th`](../samples/advanced/ps/02_algorithms.th)). These
samples are deliberately not in the IDE's picker - open them from here.

## Sorting

| Function | Idea | Cost |
| --- | --- | --- |
| `sorting.bubble<T>(values)` | Sweep again and again, swapping neighbours in the wrong order; a sweep without swaps means sorted. | O(n²) |
| `sorting.selection<T>(values)` | Find the smallest of the rest, swap it to the front, repeat. Moves every value at most once. | O(n²) |
| `sorting.insertion<T>(values)` | Slide each next value left past every larger one - how most people sort a hand of cards. | O(n²), O(n) when nearly sorted |
| `sorting.quick<T>(values)` | Pick a pivot, partition into smaller and larger, sort the two sides - divide and conquer. | O(n log n) on average |
| `sorting.merge<T>(values)` | Sort each half, then merge the two sorted halves. Steady, at the price of helper space. | O(n log n) always |

Two helpers live with them:

| Function | What it does |
| --- | --- |
| `sorting.is_sorted<T>(values): bool` | True if the array is in ascending order (equal neighbours allowed). |
| `sorting.shuffle<T>(values)` | Puts the values in a random order (Fisher-Yates). `random.seed(n)` first makes it repeatable. |

```thunder
import "algorithms"

var numbers = [5, 2, 9, 1, 5, 6]
sorting.quick(numbers)
println(numbers)                     # [1, 2, 5, 5, 6, 9]
println(sorting.is_sorted(numbers))  # true
```

## Searching

| Function | Idea | Cost |
| --- | --- | --- |
| `searching.linear<T>(values, wanted): int` | Look at every value until it is the one. Works on any array. | O(n) |
| `searching.binary<T>(values, wanted): int` | The array must be **sorted**: compare with the middle, keep only the half that can still hold the value. | O(log n) |
| `searching.index_of_smallest<T>(values): int` | The index of the smallest value (`-1` for an empty array). | O(n) |

## Text

The string algorithms work per character (a character is one code point, so Pashto text is
measured correctly).

| Function | Idea | Cost |
| --- | --- | --- |
| `text.find(source, pattern): int` | Slide the pattern along the text, compare character by character - what the built-in `index_of` does for strings, spelled out. `-1` when absent. | O(n·m) |
| `text.find_all(source, pattern): int[]` | Every index where the pattern starts, overlapping occurrences included. | O(n·m) |
| `text.is_palindrome(source): bool` | Compare the two ends and walk inwards. | O(n) |
| `text.longest_common_prefix(words): string` | Shrink the first word against each of the others. | O(total length) |
| `text.edit_distance(a, b): int` | The Levenshtein distance: the fewest single-character insertions, deletions and replacements that turn `a` into `b` - dynamic programming, one row at a time. | O(n·m) |

```thunder
import "algorithms"

println(text.find("hello world", "world"))              # 6
println(text.edit_distance("kitten", "sitting"))        # 3
println(text.longest_common_prefix(["flower", "flow"])) # "flow"
```

The module `text` of this file is not the built-in module `string` (`string.trim`,
`string.split` ...): those are the language's own, always there; these are the algorithms,
written out.

## The names before 0.7

| Was | Now |
| --- | --- |
| `bubble_sort`, `selection_sort`, `insertion_sort`, `quick_sort`, `merge_sort` | `sorting.bubble`, `sorting.selection`, `sorting.insertion`, `sorting.quick`, `sorting.merge` |
| `is_sorted`, `shuffle` | `sorting.is_sorted`, `sorting.shuffle` |
| `linear_search`, `binary_search`, `index_of_smallest` | `searching.linear`, `searching.binary`, `searching.index_of_smallest` |
| `find_text`, `find_all_text` | `text.find`, `text.find_all` |
| `is_palindrome`, `longest_common_prefix`, `edit_distance` | `text.is_palindrome`, `text.longest_common_prefix`, `text.edit_distance` |

See also the [data structures reference](datastructures.md) - stacks, queues, deques, linked
lists, trees, heaps, sets, graphs and tries, from the same shelf.
