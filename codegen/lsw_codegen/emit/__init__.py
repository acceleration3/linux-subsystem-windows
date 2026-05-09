"""Code-generation backends.

Each backend is a small module exporting an ``emit(reg, out_dir)``
function that writes one or more files into ``out_dir``. The orchestrator
in ``__main__`` runs them all.

To add a new backend (say, a tracing emitter or a wire-format validator):
write a new module with an ``emit`` function and append it to
``ALL_BACKENDS`` below.
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable, NamedTuple

from ..ir import Registry
from . import (
    client_dispatch,
    command_names,
    icd_entries,
    ids,
    serialize,
    server_backends,
    server_dispatch,
)


class Backend(NamedTuple):
    """A code-generation backend.

    ``name``    short label (shown in ``--verbose`` output)
    ``emit``    callable taking ``(Registry, Path)`` and writing files
    ``outputs`` filenames (relative to ``out_dir``) the backend produces
    """

    name: str
    emit: Callable[[Registry, Path], None]
    outputs: tuple[str, ...]


ALL_BACKENDS: tuple[Backend, ...] = (
    Backend("ids",              ids.emit,              ("vk_protocol_ids.h",)),
    Backend("command_names",    command_names.emit,    ("vk_command_names.h", "vk_command_names.cpp")),
    Backend("serialize",        serialize.emit,        ("vk_serialize.h", "vk_handle_default.cpp")),
    Backend("client_dispatch",  client_dispatch.emit,  ("vk_dispatch_client.h", "vk_dispatch_client.cpp")),
    Backend("server_dispatch",  server_dispatch.emit,  ("vk_dispatch_server.h", "vk_dispatch_server.cpp")),
    Backend("server_backends",  server_backends.emit,  ("vk_default_dispatch.h", "vk_real_dispatch.h")),
    Backend("icd_entries",      icd_entries.emit,      ("vk_icd_entries.h", "vk_icd_entries.cpp")),
)


__all__ = [
    "ALL_BACKENDS",
    "Backend",
    "client_dispatch",
    "command_names",
    "icd_entries",
    "ids",
    "serialize",
    "server_backends",
    "server_dispatch",
]
