"""Structured logging. specs/02-development-standards.md DEV-060/062:

    <ISO8601-UTC> <LEVEL> <module> event=<name> k1=v1 k2=v2

One line per event, machine-parseable, and -- this is the hard requirement,
not a style preference -- NEVER an image, a landmark coordinate, or anything
a face could be reconstructed from (DEV-062, flowing from SYS-AR-004). This
module only ever receives already-summarized scalars (levels, states,
metric values), so there is nothing here that could leak one even by
accident of a careless call site.
"""

from __future__ import annotations

import sys
from datetime import UTC, datetime
from enum import Enum


class LogLevel(Enum):
    DEBUG = "DEBUG"
    INFO = "INFO"
    WARN = "WARN"
    ERROR = "ERROR"


def log(level: LogLevel, module: str, event: str, **fields: object) -> None:
    timestamp = datetime.now(UTC).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
    kv = " ".join(f"{k}={v}" for k, v in fields.items())
    line = f"{timestamp} {level.value} {module} event={event}"
    if kv:
        line += f" {kv}"
    print(line, file=sys.stderr, flush=True)


def debug(module: str, event: str, **fields: object) -> None:
    log(LogLevel.DEBUG, module, event, **fields)


def info(module: str, event: str, **fields: object) -> None:
    log(LogLevel.INFO, module, event, **fields)


def warn(module: str, event: str, **fields: object) -> None:
    log(LogLevel.WARN, module, event, **fields)


def error(module: str, event: str, **fields: object) -> None:
    log(LogLevel.ERROR, module, event, **fields)
