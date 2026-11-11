"""The model behind the Bits tab: bitwise operations shown bit by bit.

Pure functions, so any toolkit can show them and the tests need no GUI. The panel passes the
two operand texts and the operator; this module parses the numbers (in Latin or Pashto digits,
decimal, hex 0x... or binary 0b...), applies the operation with the language's 64-bit
semantics, and lays the operands and the result out as aligned rows of bits.
"""

from __future__ import annotations

from dataclasses import dataclass, field

#: The operators of the language, in menu order. "~" takes one operand.
OPERATORS = ("&", "|", "^", "<<", ">>", "~")

_DIGITS = str.maketrans("۰۱۲۳۴۵۶۷۸۹٠١٢٣٤٥٦٧٨٩", "01234567890123456789")
_INT_MIN, _INT_MAX = -(2**63), 2**63 - 1


def parse_int(text: str) -> int | None:
    """The integer a user typed: decimal, 0x... or 0b..., Latin or Pashto/Arabic digits.

    None if the text is not a whole number or does not fit the language's 64-bit int.
    """
    cleaned = text.strip().translate(_DIGITS).replace("_", "").replace(" ", "")
    if not cleaned:
        return None
    try:
        value = int(cleaned, 0)  # base 0: understands 0x2a and 0b1010 as well as 42
    except ValueError:
        return None
    return value if _INT_MIN <= value <= _INT_MAX else None


@dataclass(frozen=True)
class BitRow:
    """One row of the picture: a label, the bits (most significant first) and the value."""

    label: str
    bits: str  # e.g. "00001100", exactly `width` characters of the view it belongs to
    value: int


@dataclass(frozen=True)
class BitView:
    """What the Bits tab shows: the rows, or the key of the problem with the input.

    ``error`` is a message key ("bits.error.<what>"); ``error_detail`` names the offending
    text. ``highlight`` marks the bit columns (0 = leftmost) where the result differs from
    the first operand - the bits the operation changed.
    """

    width: int = 0
    rows: tuple[BitRow, ...] = ()
    expression: str = ""  # "12 & 10 = 8", ASCII, for the result line
    error: str = ""
    error_detail: str = ""
    highlight: tuple[int, ...] = field(default=())


def _fits(value: int, width: int) -> bool:
    return -(2 ** (width - 1)) <= value <= 2 ** (width - 1) - 1


def _bits(value: int, width: int) -> str:
    return format(value & ((1 << width) - 1), f"0{width}b")  # two's complement


def _to_signed(value: int) -> int:
    value &= (1 << 64) - 1
    return value - (1 << 64) if value >= 1 << 63 else value


def visualise(a_text: str, operator: str, b_text: str) -> BitView:
    """Lays out ``a operator b`` (or ``~a``) bit by bit; see BitView for the error cases."""
    unary = operator == "~"
    a = parse_int(a_text)
    if a is None:
        return BitView(error="bits.error.number", error_detail=a_text.strip())
    b = None
    if not unary:
        b = parse_int(b_text)
        if b is None:
            return BitView(error="bits.error.number", error_detail=b_text.strip())

    if unary:
        result = ~a
    elif operator == "<<" or operator == ">>":
        assert b is not None
        if not 0 <= b <= 63:
            return BitView(error="bits.error.shift", error_detail=str(b))
        result = _to_signed(a << b) if operator == "<<" else a >> b
    elif operator == "&":
        result = a & b  # type: ignore[operator]
    elif operator == "|":
        result = a | b  # type: ignore[operator]
    elif operator == "^":
        result = a ^ b  # type: ignore[operator]
    else:
        return BitView(error="bits.error.operator", error_detail=operator)

    # The narrowest of 8, 16, 32 or 64 bits that shows every value in two's complement.
    involved = [value for value in (a, b, result) if value is not None]
    width = next(w for w in (8, 16, 32, 64) if all(_fits(value, w) for value in involved))

    rows = [BitRow("a", _bits(a, width), a)]
    if not unary and operator not in ("<<", ">>"):
        rows.append(BitRow(f"{operator} b", _bits(b, width), b))  # type: ignore[arg-type]
    label = f"~a" if unary else f"a {operator} {b}" if operator in ("<<", ">>") else f"a {operator} b"
    rows.append(BitRow(label, _bits(result, width), result))

    first, last = rows[0].bits, rows[-1].bits
    highlight = tuple(i for i in range(width) if first[i] != last[i])
    expression = f"~{a} = {result}" if unary else f"{a} {operator} {b} = {result}"
    return BitView(width=width, rows=tuple(rows), expression=expression, highlight=highlight)
