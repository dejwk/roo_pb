#!/usr/bin/env python3
"""Optional external oracle; Google protobuf is a test-only dependency.

Run with a Python environment containing protobuf after tools/test.py. These
independent descriptors intentionally do not use the roo_pbc schema model.
"""
from pathlib import Path
import random
import subprocess
from google.protobuf import descriptor_pb2 as d, descriptor_pool, message_factory


def field(message, name, number, type_id, *, repeated=False, type_name='', default=None, oneof=None):
    result = message.field.add(name=name, number=number, type=type_id,
                               label=3 if repeated else 1)
    if type_name:
        result.type_name = type_name
    if default is not None:
        result.default_value = str(default)
    if oneof is not None:
        result.oneof_index = oneof
    return result


def classes():
    pool = descriptor_pool.DescriptorPool()
    file = d.FileDescriptorProto(name='oracle.proto', package='test', syntax='proto2')
    enum = file.enum_type.add(name='State')
    enum.value.add(name='UNKNOWN', number=0)
    enum.value.add(name='READY', number=1)
    required = file.message_type.add(name='Required')
    field(required, 'x', 1, 5).label = 2
    field(required, 'y', 2, 5).label = 2
    legacy = file.message_type.add(name='Legacy')
    field(legacy, 'count', 1, 5, default=-7)
    field(legacy, 'label', 2, 9, default='hello')
    field(legacy, 'child', 3, 11, type_name='.test.Required')
    field(legacy, 'state', 4, 14, type_name='.test.State', default='READY')
    field(legacy, 'states', 5, 14, type_name='.test.State', repeated=True).options.packed = True
    scalars = file.message_type.add(name='Scalars')
    for number, (name, type_id) in enumerate([
        ('i32', 5), ('i64', 3), ('u32', 13), ('u64', 4), ('s32', 17), ('s64', 18),
        ('f32', 7), ('f64', 6), ('sf32', 15), ('sf64', 16), ('real32', 2), ('real64', 1),
        ('flag', 8), ('payload', 12)], 1):
        field(scalars, name, number, type_id)
    pool.Add(file)
    modern_file = d.FileDescriptorProto(name='modern.proto', package='test', syntax='proto3')
    modern_file.dependency.append('oracle.proto')
    modern = modern_file.message_type.add(name='Modern')
    modern.oneof_decl.add(name='selection')
    modern.oneof_decl.add(name='_narrow')
    field(modern, 'values', 1, 5, repeated=True)
    entry = modern.nested_type.add(name='CountsEntry')
    entry.options.map_entry = True
    field(entry, 'key', 1, 9)
    field(entry, 'value', 2, 5)
    field(modern, 'counts', 2, 11, repeated=True, type_name='.test.Modern.CountsEntry')
    field(modern, 'child', 3, 11, type_name='.test.Required', oneof=0)
    field(modern, 'note', 4, 9, oneof=0)
    field(modern, 'stream', 5, 12)
    field(modern, 'big', 6, 4, repeated=True)
    field(modern, 'dynamic', 7, 9)
    field(modern, 'narrow', 8, 5, oneof=1).proto3_optional = True
    field(modern, 'token', 9, 12)
    field(modern, 'pair', 10, 5, repeated=True)
    pool.Add(modern_file)
    device_file = d.FileDescriptorProto(name='device.proto', package='example', syntax='proto3')
    device = device_file.message_type.add(name='Device')
    device.oneof_decl.add(name='state')
    device.oneof_decl.add(name='_name')
    field(device, 'id', 1, 13)
    field(device, 'name', 2, 9, oneof=1).proto3_optional = True
    field(device, 'readings', 3, 17, repeated=True)
    field(device, 'online', 4, 8, oneof=0)
    field(device, 'reason', 5, 9, oneof=0)
    pool.Add(device_file)
    return {name: message_factory.GetMessageClass(pool.FindMessageTypeByName(full))
            for name, full in [('scalars', 'test.Scalars'), ('legacy', 'test.Legacy'),
                               ('modern', 'test.Modern'), ('device', 'example.Device')]}


def main():
    types = classes()
    driver = Path(__file__).resolve().parents[1] / 'build/oracle_driver'
    rng = random.Random(7321)
    count = 0
    def check(kind, original, wire=None):
        nonlocal count
        result = subprocess.run([str(driver), kind], input=original.SerializeToString() if wire is None else wire,
                                stdout=subprocess.PIPE, check=True)
        decoded = types[kind].FromString(result.stdout)
        assert decoded == original, (kind, original, decoded)
        count += 1
    for _ in range(150):
        scalar = types['scalars']()
        for name in ['i32', 's32', 'sf32']:
            setattr(scalar, name, rng.randint(-2**31, 2**31-1))
        for name in ['i64', 's64', 'sf64']:
            setattr(scalar, name, rng.randint(-2**63, 2**63-1))
        for name in ['u32', 'f32']:
            setattr(scalar, name, rng.getrandbits(32))
        for name in ['u64', 'f64']:
            setattr(scalar, name, rng.getrandbits(64))
        scalar.real32 = rng.uniform(-1000, 1000)
        scalar.real64 = rng.uniform(-1e100, 1e100)
        scalar.flag = bool(rng.getrandbits(1))
        scalar.payload = rng.randbytes(rng.randrange(17))
        check('scalars', scalar)
        device = types['device'](id=rng.getrandbits(32), name='kitchen\0é')
        device.readings.extend(rng.randint(-2**31, 2**31-1) for _ in range(rng.randrange(17)))
        if rng.getrandbits(1):
            device.online = bool(rng.getrandbits(1))
        else:
            device.reason = 'offline'
        check('device', device)
    legacy = types['legacy']()
    legacy.child.x = 1
    legacy.child.y = 2
    check('legacy', legacy, b'\x1a\x02\x08\x01\x1a\x02\x10\x02')
    modern = types['modern']()
    wire = bytes([8, 1, 10, 2, 2, 3, 8, 4, 18, 5, 10, 1, 97, 16, 1,
                  18, 5, 10, 1, 97, 16, 2, 26, 2, 8, 1, 26, 2, 16, 2, 82, 2, 5, 6])
    modern.ParseFromString(wire)
    check('modern', modern, wire)
    print(f'PASS {count} independent Google → roo_pb → Google semantic comparisons')


if __name__ == '__main__':
    main()
