# Fonts shipped with the Thunder IDE

The IDE draws a program with two fonts: a **code font** for Latin letters, digits and symbols,
and a **Pashto font** for Arabic-script text. Choose them in the **Format** menu.

The code font must have *no* Arabic glyphs of its own: Qt uses the first font that can draw a
character, so a code font with Arabic glyphs (DejaVu Sans Mono and Courier New have them) would
draw Pashto too, and the Pashto font would never be used. That is why Pashto looked cramped
before these fonts were added - it was DejaVu Sans Mono's monospaced Arabic.

| Directory | Font | Kind | Licence |
| --- | --- | --- | --- |
| `vazirmatn/` | Vazirmatn | Pashto (sans serif, made for screens) - **the default** | SIL Open Font License 1.1 |
| `notonaskharabic/` | Noto Naskh Arabic | Pashto (naskh, the style of printed books) | SIL Open Font License 1.1 |
| `notosansarabic/` | Noto Sans Arabic | Pashto (sans serif) | SIL Open Font License 1.1 |
| `scheherazadenew/` | Scheherazade New | Pashto (traditional naskh, by SIL) | SIL Open Font License 1.1 |
| `lateef/` | Lateef | Pashto (naskh in the style used further east, by SIL) | SIL Open Font License 1.1 |
| `jetbrainsmono/` | JetBrains Mono | Code (monospaced, no Arabic glyphs) - **the default** | SIL Open Font License 1.1 |

Every Pashto font was checked to have every Pashto letter (ټ ډ ړ ږ ښ ګ ڼ ځ څ ې ۍ ...), the
Pashto digits and the Arabic punctuation (، ؛ ؟ ٫ « »); `tests/python/test_fonts.py` checks it
again. The files come unmodified from [google/fonts](https://github.com/google/fonts) (`ofl/`),
each with its licence, `OFL.txt`, which permits bundling and redistribution with software.
