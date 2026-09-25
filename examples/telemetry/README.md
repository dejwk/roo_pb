# Bounded telemetry

`main.cpp` is a host example; `telemetry.ino` is the Arduino sketch. Both use
inline bounded storage and the same generated header. The generated header is
checked in so the Arduino sketch can be opened directly; regenerate after
editing the schema or sidecar:

```sh
python3 tools/generate.py -I examples/telemetry --out examples/telemetry telemetry.proto
clang-format -i examples/telemetry/telemetry.pb.h
```

Install roo_pb and roo_io dependencies and select C++17 in the board/project
configuration. The host main is excluded in Arduino builds. The sketch was
compile-checked with the roo_testing Arduino host profile; it has not been run
on physical hardware.
