"""Entry point: ``python -m thunder_ide [file.th]``."""

import sys

from thunder_ide.app import main

if __name__ == "__main__":  # pragma: no cover - exercised by the run scripts
    sys.exit(main(sys.argv[1:]))
