---
name: "roo_pb Authoring Additions"
description: "Protobuf naming compatibility, Bazel storage limits, and portable validation for roo_pb."
applyTo: "**"
---
# roo_pb Authoring Additions

Apply the [general C++ instructions](general-cpp-code-authoring-instructions.md)
and the [repository-wide design guidance](embedded-design-doc-authoring.instructions.md).

- Generated protobuf compatibility methods intentionally retain protobuf
  spelling. All other public APIs follow the general C++ instructions.
- Run `python3 tools/test.py --roo-root /path/to/roo` for portable validation.
