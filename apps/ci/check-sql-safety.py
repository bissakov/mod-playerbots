#!/usr/bin/env python3

from pathlib import Path
import sys

from sqlfluff.core import Linter
from sqlfluff.core.config import FluffConfig

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SQL_PARSER = Linter(config=FluffConfig.from_path(str(REPOSITORY_ROOT)))


def engine_changes(parsed_file) -> list[tuple[int, str]]:
    if parsed_file.tree is None:
        return []

    changes: list[tuple[int, str]] = []
    for statement_type in ("create_table_statement", "alter_table_statement"):
        for statement in parsed_file.tree.recursive_crawl(statement_type):
            code_segments = [segment for segment in statement.raw_segments if segment.is_code]
            for index, segment in enumerate(code_segments):
                if segment.get_type() != "parameter" or segment.raw_upper != "ENGINE":
                    continue

                value_index = index + 1
                if value_index < len(code_segments) and code_segments[value_index].raw == "=":
                    value_index += 1
                if value_index >= len(code_segments):
                    continue

                value = code_segments[value_index]
                engine = value.raw.strip("`")
                if engine.casefold() != "innodb":
                    line = value.pos_marker.line_no if value.pos_marker else 1
                    changes.append((line, engine))

    return changes


def main() -> int:
    errors: list[str] = []

    for raw_path in sys.argv[1:]:
        path = Path(raw_path)
        if not path.is_file():
            continue

        try:
            contents = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            errors.append(f"{raw_path}: file is not valid UTF-8")
            continue

        parsed_file = SQL_PARSER.parse_string(contents, fname=raw_path)
        for line, engine in engine_changes(parsed_file):
            errors.append(f"{raw_path}:{line}: new or changed tables must use InnoDB, not {engine}")

    if errors:
        print("SQL safety checks failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
