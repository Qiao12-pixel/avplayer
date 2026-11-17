"""Compatibility entry: shared MySQL integration supersedes local SQLite tests."""
from mysql_shared_integration import main
if __name__ == '__main__':
    main()
