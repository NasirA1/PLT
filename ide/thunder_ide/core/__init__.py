"""The toolkit-independent part of the Thunder IDE.

Nothing in this package imports a GUI toolkit. It contains the application logic
(``controller``), the abstract interfaces a GUI must implement (``ui``), and the
helpers that turn the toolchain's JSON reports into data that any toolkit can show
(``viewmodels``, ``explain``). A new front end (for example Qt) implements the
interfaces in ``ui`` and reuses everything here unchanged.
"""
