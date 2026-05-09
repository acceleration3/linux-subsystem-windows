"""LSW Vulkan registry codegen.

Reads ``vk.xml`` and emits serialize/deserialize/dispatch C++ for the LSW
client ICD and render server. See ``docs/serialization-codegen.md``.

Phase 0: package skeleton. Phase 1 fills in actual code generation.
"""

__version__ = "0.0.1"

__all__ = ["__version__"]
