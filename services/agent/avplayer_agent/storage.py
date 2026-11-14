"""Storage configuration descriptor; filesystem ownership belongs to C++."""
from pathlib import Path


class SessionStorage:
    def __init__(self, root: Path) -> None:
        self.root = root
