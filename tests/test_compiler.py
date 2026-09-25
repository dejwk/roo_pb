"""Verifies independent parsing, diagnostics and deterministic generation."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from roo_pbc.schema import Schema, SchemaError
from roo_pbc.generate import generate


class CompilerTest(unittest.TestCase):
    # Verifies the renamed CLI discovers sidecars and emits the public API.
    def test_roo_pbc_cli(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.proto').write_text('message M { optional string text=1; }')
            sidecar = root / 'test.roo_pb.toml'
            sidecar.write_text('[fields."M.text"]\nmax_bytes=17\n')
            depfile = root / 'test.d'
            subprocess.run([sys.executable, '-m', 'roo_pbc', '-I', str(root),
                            '--out', str(root), '--depfile', str(depfile),
                            'test.proto'], check=True, capture_output=True, text=True)
            output = (root / 'test.pb.h').read_text()
            self.assertIn('"roo_pb/message.h"', output)
            self.assertIn('::roo_pb::BoundedString<17>', output)
            self.assertIn(str(sidecar), depfile.read_text())

    def compile(self, text, options='', files=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.proto').write_text(text)
            if options:
                (root / 'test.roo_pb.toml').write_text(options)
            for name, content in (files or {}).items():
                (root / name).write_text(content)
            schema = Schema([root])
            source = schema.load('test.proto')
            schema.validate()
            output = generate(schema, source)
            self.assertEqual(output, generate(schema, source))
            return output

    def test_import_nested_and_forward_resolution(self):
        output = self.compile('''syntax="proto3"; package p;
          import "other.proto";
          message Outer { Inner child=1; other.Value other=2;
            message Inner { uint64 value=1; } }
        ''', files={'other.proto': 'syntax="proto3"; package other; message Value { uint32 x=1; }'})
        self.assertIn('class Outer_Inner', output)
        self.assertIn('using Inner = ::p::Outer_Inner', output)
        self.assertIn('#include "other.pb.h"', output)

    def test_diagnostics(self):
        cases = [
            ('syntax="proto3"; message M { int32 a=0; }', 'field number'),
            ('syntax="proto3"; message M { int32 a=19000; }', 'field number'),
            ('syntax="proto3"; message M { int32 a=1; bool b=1; }', 'duplicate'),
            ('syntax="proto3"; message M { reserved 2; int32 a=2; }', 'reserved'),
            ('syntax="proto3"; message M { required int32 a=1; }', 'required'),
            ('syntax="proto3"; message M { string a=1; }', 'max_bytes'),
            ('syntax="proto3"; message M { repeated int32 a=1; }', 'max_count'),
            ('syntax="proto3"; message M { Missing a=1; }', 'unknown type'),
            ('syntax="proto3"; message M { M a=1; }', 'recursive'),
            ('syntax="proto3"; enum E { ONE=1; }', 'zero'),
            ('syntax="proto3"; message M { bool a=1 [packed=true]; }', 'packed'),
            ('syntax="proto3"; message M { bool a=1 [default=true]; }', 'default'),
            ('syntax="proto3"; message M { int32 a=1;', 'end of file'),
        ]
        for text, error in cases:
            with self.subTest(text=text), self.assertRaisesRegex(SchemaError, error):
                self.compile(text)

    def test_storage_validation(self):
        with self.assertRaisesRegex(SchemaError, 'unknown fields'):
            self.compile('message M {}', '[fields."M.missing"]\nmax_bytes=2')
        with self.assertRaisesRegex(SchemaError, 'capacity'):
            self.compile('message M { optional bytes a=1; }', '[fields."M.a"]\nmax_bytes=-1')

    def test_recursive_callback(self):
        output = self.compile('syntax="proto3"; message Node { Node child=1; }',
                              '[fields."Node.child"]\nstorage="callback"')
        self.assertIn('EncodeCallback child_encoder_', output)
        self.assertIn('DecodeCallback child_decoder_', output)

    def test_callback_accessors(self):
        output = self.compile('message M { optional bytes payload=1; }',
                              '[fields."M.payload"]\nstorage="callback"')
        self.assertIn('void set_payload_encoder(::roo_pb::EncodeCallback binding)', output)
        self.assertIn('void set_payload_decoder(::roo_pb::DecodeCallback binding)', output)
        self.assertNotIn('void set_payload(', output)
        for suffix in ('encoder', 'decoder'):
            for fields in (f'bytes payload=1; int32 payload_{suffix}=2;',
                           f'int32 payload_{suffix}=2; bytes payload=1;'):
                with self.subTest(fields=fields), self.assertRaisesRegex(SchemaError, 'collision'):
                    self.compile('syntax="proto3"; message M {' + fields + '}',
                                 '[fields."M.payload"]\nstorage="callback"')
        # Directional names remain available when the field is resident.
        self.compile('syntax="proto3"; message M { int32 a=1; int32 a_encoder=2; }')

    def test_proto2_defaults_extensions(self):
        output = self.compile('''syntax="proto2"; package p;
          message M { optional int64 x=1 [default=-9223372036854775808]; extensions 100 to 200; }
          extend M { optional uint32 extra=100; }
        ''')
        self.assertIn('INT64_MIN', output)
        self.assertIn('kExtraFieldNumber = 100', output)

    def test_cpp_name_collisions(self):
        for field_name in ['Clear', 'unknown_fields_encoder', 'unknown_fields_decoder', 'IsInitialized']:
            with self.subTest(name=field_name), self.assertRaisesRegex(SchemaError, 'collision'):
                self.compile(f'syntax="proto3"; message M {{ int32 {field_name}=1; }}')
        with self.assertRaisesRegex(SchemaError, 'collision'):
            self.compile('syntax="proto3"; message M { int32 a=1; int32 clear_a=2; }')

    def test_private_transitive_import(self):
        files = {'a.proto': 'syntax="proto3"; import "b.proto"; message A {}',
                 'b.proto': 'syntax="proto3"; message B {}'}
        with self.assertRaisesRegex(SchemaError, 'not imported'):
            self.compile('syntax="proto3"; import "a.proto"; message M { B b=1; }', files=files)
        files['a.proto'] = 'syntax="proto3"; import public "b.proto"; message A {}'
        self.compile('syntax="proto3"; import "a.proto"; message M { B b=1; }', files=files)

    def test_unknown_retention_opt_in(self):
        plain = self.compile('syntax="proto3"; message M { int32 x=1; }')
        enabled = self.compile('syntax="proto3"; message M { int32 x=1; }',
                               '[messages."M"]\nunknown_fields=true')
        self.assertNotIn('EncodeCallback unknown_fields_encoder_', plain)
        self.assertNotIn('DecodeCallback unknown_fields_decoder_', plain)
        self.assertIn('EncodeCallback unknown_fields_encoder_', enabled)
        self.assertIn('DecodeCallback unknown_fields_decoder_', enabled)

    def test_buffer_entry_points(self):
        output = self.compile('syntax="proto3"; message M { int32 x=1; }')
        self.assertIn('serialize(::roo_pb::Output& output)', output)
        self.assertIn('mergeFrom(::roo_pb::Input& input)', output)
        self.assertNotIn('template <', output)
        self.assertNotIn('Iterator', output)
        self.assertNotIn('CountingOutputSink', output)

    def test_named_wire_types(self):
        output = self.compile('package p; message M { repeated int32 values=1 [packed=true]; extensions 100 to 200; } extend M { repeated int32 extra=100 [packed=true]; }',
                              '[fields."p.M.values"]\nmax_count=2\n[fields."p.extra"]\nmax_count=2')
        self.assertIn('::roo_pb::GetWireType(tag)', output)
        self.assertIn('::roo_pb::WireType::kLengthDelimited', output)
        self.assertIn('::roo_pb::GetWireType(::roo_pb::Kind::kInt32)', output)
        self.assertNotIn('tag & 7', output)

    def test_grouped_presence(self):
        output = self.compile('message M { optional bool a=1; optional int64 b=2; }')
        self.assertIn('uint8_t has_a_ : 1;\nuint8_t has_b_ : 1;', output)
        self.assertIn('has_a_(0), has_b_(0)', output)
        self.assertNotIn('bool has_a_', output)

    def test_nested_default_initialization(self):
        output = self.compile('''enum E { FIRST=7; SECOND=8; }
          message Leaf { optional int32 n=1 [default=-7]; optional E state=2;
                         optional string text=3 [default="seed"]; }
          message Parent { optional Leaf child=1; }
        ''', '[fields."Leaf.text"]\nstorage="dynamic"')
        self.assertIn('int32_t n_{-7};', output)
        self.assertIn('::E state_{::E::FIRST};', output)
        self.assertIn('child_.Clear();', output)
        self.assertNotIn('child_ = {};', output)
        for line in output.splitlines():
            self.assertNotEqual(line, 'Clear();')

    def test_empty_dynamic_defaults_do_not_allocate(self):
        output = self.compile('message M { optional string empty=1; }',
                              '[fields."M.empty"]\nstorage="dynamic"')
        self.assertIn('empty_.clear();', output)
        self.assertNotIn('empty_.assign("", 0)', output)

    def test_resident_string_reads(self):
        output = self.compile('''message M {
          optional string text=1; optional bytes fixed=2;
          oneof choice { string other=3; }
        }''', '''[fields."M.text"]
max_bytes=16
[fields."M.fixed"]
max_bytes=4
fixed_length=true
[fields."M.other"]
max_bytes=8
''')
        self.assertIn('ReadValue<::roo_pb::Kind::kString>(input, text_);', output)
        self.assertIn('has_text_ = true;', output)
        self.assertNotIn('*mutable_text() = std::move(item);', output)
        self.assertIn('*mutable_fixed() = std::move(item);', output)
        self.assertIn('*mutable_other() = std::move(item);', output)

    def test_oneof_union_storage(self):
        output = self.compile('''enum E { FIRST=7; SECOND=8; }
          message M { oneof choice { E state=1; string text=2; } }
        ''', '[fields."M.text"]\nmax_bytes=32')
        self.assertIn('union {\n::E state_;\n::roo_pb::BoundedString<32> text_;\n};', output)
        self.assertIn('static const ::E default_value{::E::FIRST};', output)
        self.assertIn('text_.~Value();', output)
        self.assertIn('M(const M& other)', output)
        self.assertIn('M(M&& other) noexcept', output)
        self.assertNotIn('std::variant', output)

    def test_callback_only_oneof(self):
        output = self.compile('message M { oneof choice { bytes a=1; bytes b=2; } }',
                              '[fields."M.a"]\nstorage="callback"\n[fields."M.b"]\nstorage="callback"')
        self.assertNotIn('union {', output)
        self.assertIn('DecodeCallback a_decoder_', output)
        self.assertIn('DecodeCallback b_decoder_', output)

    def test_binary_and_unicode_defaults(self):
        output = self.compile(r'message M { optional bytes b=1 [default="\377é"]; }',
                              '[fields."M.b"]\nmax_bytes=3')
        self.assertIn(r'\377\303\251', output)
        with self.assertRaisesRegex(SchemaError, 'UTF-8'):
            self.compile(r'message M { optional string s=1 [default="\377"]; }',
                         '[fields."M.s"]\nmax_bytes=4')

    def test_sandbox_symlink_import_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'physical').mkdir()
            (root / 'sandbox').mkdir()
            source = root / 'physical/test.proto'
            source.write_text('syntax="proto3"; message M {}')
            (root / 'sandbox/test.proto').symlink_to(source)
            schema = Schema([root / 'sandbox'])
            loaded = schema.load('test.proto')
            self.assertEqual(loaded.logical, 'test.proto')
            schema.validate()


if __name__ == '__main__':
    unittest.main()
