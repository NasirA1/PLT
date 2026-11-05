"""Thunder IDE: an educational development environment for the Thunder language.

The IDE is a thin graphical front end. All language knowledge lives in the C++
toolchain (``thunderc`` and ``thundervm``); the IDE runs those tools and shows
the JSON reports they produce: tokens, syntax tree, symbols, bytecode, a hex
dump of the compiled file, and a step-by-step trace of the virtual machine.
"""

__version__ = "0.1.0"
