"""Checks nanopb syntax, storage translation, scoping and standalone imports."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from roo_pbc.schema import Schema, SchemaError
from roo_pbc.generate import generate


class NanopbTest(unittest.TestCase):
    def compile(self, body, sidecar='', files=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.proto').write_text(body)
            if sidecar:
                (root / 'test.roo_pb.toml').write_text(sidecar)
            for name, content in (files or {}).items():
                (root / name).write_text(content)
            schema = Schema([root])
            source = schema.load('test.proto')
            schema.validate()
            output = generate(schema, source)
            self.assertEqual(output, generate(schema, source))
            return schema, output

    # Verifies all supported bounds, widths, allocation and fixed-size options.
    def test_mappings(self):
        schema, output = self.compile('''syntax="proto3"; import "nanopb.proto";
          message M {
            string name=1 [(nanopb).max_size=8];
            bytes blob=2 [(nanopb)={max_size:8 fixed_length:true}];
            repeated int32 items=3 [(nanopb)={max_count:2, fixed_count:true; int_size:IS_8}];
            string longer=4 [(.nanopb).max_length=8, (nanopb).type=FT_STATIC];
            bytes stream=5 [(nanopb).type=FT_CALLBACK];
          }''')
        fields = schema.symbols['M'].fields
        self.assertEqual(fields[0].storage, {'max_bytes': 7})
        self.assertEqual(fields[1].storage, {'max_bytes': 8, 'fixed_length': True})
        self.assertEqual(fields[2].storage, {'max_count': 2, 'fixed_count': True, 'integer_bits': 8})
        self.assertEqual(fields[3].storage, {'storage': 'bounded', 'max_bytes': 8})
        self.assertEqual(fields[4].storage, {'storage': 'callback'})
        self.assertNotIn('nanopb.pb.h', output)
        self.assertNotIn('nanopb.proto', schema.sources)

    # Verifies nanopb file/message/field precedence, including nested messages.
    def test_scopes(self):
        schema, _ = self.compile('''syntax="proto3"; import "nanopb.proto";
          option (nanopb_fileopt) = { max_size:9 int_size:IS_16 max_count:3 };
          message Outer {
            option (nanopb_msgopt).max_size=5;
            string name=1;
            string override=2 [(nanopb).max_size=3];
            repeated int32 numbers=3;
            message Inner { string name=1; }
          }
          message Other { string name=1; }
        ''')
        self.assertEqual(schema.symbols['Outer'].fields[0].storage, {'max_bytes':4})
        self.assertEqual(schema.symbols['Outer'].fields[1].storage, {'max_bytes':2})
        self.assertEqual(schema.symbols['Outer'].fields[2].storage, {'max_count':3, 'integer_bits':16})
        # Nanopb gives nested messages their own file-based defaults.
        self.assertEqual(schema.symbols['Outer.Inner'].fields[0].storage, {'max_bytes':8})
        self.assertEqual(schema.symbols['Other'].fields[0].storage, {'max_bytes':8})

    # Verifies max_length wins over max_size as in nanopb, even across scopes.
    def test_length_precedence_and_zero(self):
        schema, _ = self.compile('''syntax="proto3"; import "nanopb.proto";
          option (nanopb_fileopt).max_length=3;
          message M {
            string text=1 [(nanopb).max_size=100];
            string empty=2 [(nanopb).max_length=0];
            bytes blob=3 [(nanopb).max_size=0];
          }''')
        self.assertEqual([f.storage['max_bytes'] for f in schema.symbols['M'].fields], [3,0,0])
        schema, _ = self.compile('''import "nanopb.proto";
          message M { optional string empty=1 [(nanopb).max_size=1]; }''')
        self.assertEqual(schema.symbols['M'].fields[0].storage['max_bytes'], 0)

    # Verifies TOML can override bounds, widths, constraints and allocation.
    def test_sidecar_precedence(self):
        schema, _ = self.compile('''syntax="proto3"; import "nanopb.proto";
          message M {
            string name=1 [(nanopb).max_size=3];
            bytes stream=2 [(nanopb).max_size=5];
            bytes owned=3 [(nanopb).type=FT_CALLBACK];
            repeated int32 items=4 [(nanopb)={max_count:2 fixed_count:true int_size:IS_8}];
          }''', '''[fields."M.name"]
max_bytes=7
[fields."M.stream"]
storage="callback"
[fields."M.owned"]
storage="bounded"
max_bytes=4
[fields."M.items"]
max_count=4
fixed_count=false
integer_bits=16
''')
        fields=schema.symbols['M'].fields
        self.assertEqual(fields[0].storage, {'max_bytes':7})
        self.assertEqual(fields[1].storage, {'storage':'callback'})
        self.assertEqual(fields[2].storage, {'storage':'bounded', 'max_bytes':4})
        self.assertEqual(fields[3].storage, {'max_count':4, 'fixed_count':False, 'integer_bits':16})

    # Verifies callbacks ignore allocation hints and participate in collision checks.
    def test_callbacks_and_collisions(self):
        schema, output = self.compile('''import "nanopb.proto";
          option (nanopb_fileopt).max_size=16;
          message Node { optional Node child=1 [(nanopb).type=FT_CALLBACK];
            optional bytes stream=2 [(nanopb).type=FT_CALLBACK]; }''')
        self.assertIn('EncodeCallback child_encoder_', output)
        self.assertEqual(schema.symbols['Node'].fields[1].storage, {'storage':'callback'})
        with self.assertRaisesRegex(SchemaError, 'collision'):
            self.compile('''import "nanopb.proto"; message M {
              optional bytes payload=1 [(nanopb).type=FT_CALLBACK];
              optional int32 payload_encoder=2; }''')

    # Verifies file defaults do not leak across ordinary imports.
    def test_imports(self):
        schema, _ = self.compile('''import "base.proto";
          option (nanopb_fileopt).max_size=8;
          message M { optional string text=1; optional Base base=2; }''', files={
              'base.proto': 'import public "nanopb.proto"; option (nanopb_fileopt).max_size=3; message Base { optional string text=1; }'})
        self.assertEqual(schema.symbols['M'].fields[0].storage['max_bytes'], 7)
        self.assertEqual(schema.symbols['Base'].fields[0].storage['max_bytes'], 2)
        with self.assertRaisesRegex(SchemaError, 'require import'):
            self.compile('''import "base.proto"; message M {
              optional string text=1 [(nanopb).max_size=8]; }''', files={
                  'base.proto': 'import "nanopb.proto"; message Base {}'})

    # Verifies declared extensions also receive file and inline storage settings.
    def test_extensions(self):
        schema, _ = self.compile('''import "nanopb.proto";
          option (nanopb_fileopt).max_size=8;
          message M { extensions 100 to 200; }
          extend M { optional string text=100 [(nanopb).max_length=4]; }
        ''')
        self.assertEqual(schema.extensions[0][2].storage['max_bytes'], 4)

    # Verifies inherited map entry bounds and field-local oneof options compile.
    def test_maps_and_oneofs(self):
        schema, _ = self.compile('''syntax="proto3"; import "nanopb.proto";
          option (nanopb_fileopt) = { max_length:5 max_count:2 };
          message M { map<string,string> pairs=1;
            oneof choice { string text=2 [(nanopb).max_length=3]; int32 code=3; } }
        ''')
        self.assertEqual(schema.symbols['M.PairsEntry'].fields[0].storage['max_bytes'],5)
        self.assertEqual(schema.symbols['M'].fields[0].storage['max_count'],2)
        self.assertEqual(schema.symbols['M'].fields[1].storage['max_bytes'],3)

    # Verifies unsupported options and invalid values never silently disappear.
    def test_diagnostics(self):
        cases = [
            ('(nanopb).max_size=-1', 'nonnegative'),
            ('(nanopb).max_size=2147483648', 'nonnegative'),
            ('(nanopb).max_size="8"', 'invalid nanopb'),
            ('(nanopb).max_size=1.5', 'requires an integer'),
            ('(nanopb).max_size=0', 'capacity'),
            ('(nanopb).fixed_length=1', 'requires a boolean'),
            ('(nanopb).int_size=IS_7', 'unsupported nanopb int_size'),
            ('(nanopb).type=FT_POINTER', 'unsupported nanopb type'),
            ('(nanopb).type=FT_DEFAULT', 'unsupported nanopb type'),
            ('(nanopb).type=FT_IGNORE', 'unsupported nanopb type'),
            ('(nanopb).callback_function="custom"', 'unsupported nanopb option'),
            ('(nanopb).descriptorsize=DS_4', 'unsupported nanopb option'),
            ('(nanopb_msgopt).max_size=8', 'scope'),
            ('(nanopb).max_count=2', 'does not apply'),
            ('(nanopb)={max_size:8 max_size:9}', 'duplicate'),
            ('(nanopb)={max_size:8}, (nanopb).max_size=9', 'duplicate'),
            ('(nanopb).max_size=8, (.nanopb).max_size=9', 'duplicate'),
        ]
        for annotation, error in cases:
            with self.subTest(annotation=annotation), self.assertRaisesRegex(SchemaError, error):
                self.compile('import "nanopb.proto"; message M { optional string text=1 ['+annotation+']; }')
        for body, error in [
            ('message M { optional bytes text=1 [(nanopb).max_size=8]; }', 'require import'),
            ('import "nanopb.proto"; option (nanopb_fileopt).packed_struct=true; message M {}', 'unsupported'),
            ('import "nanopb.proto"; enum E { option (nanopb_enumopt).long_names=false; A=0; }', 'scope'),
            ('message M { optional bytes text=1 [(custom)={value:1}]; }', 'aggregate custom'),
            ('import "nanopb.proto"; enum E { A=0 [(nanopb).max_size=8]; }', 'enum values'),
            ('import "nanopb.proto"; message M { optional string text=1; }', 'max_bytes'),
            ('import "nanopb.proto"; message M { optional uint32 x=1 [(nanopb).int_size=IS_64]; }', 'widen'),
        ]:
            with self.subTest(body=body), self.assertRaisesRegex(SchemaError,error):
                self.compile(body)

    # Verifies explicit defaults reset inherited flags and widths without fallback.
    def test_reset_defaults(self):
        schema, _ = self.compile('''syntax="proto3"; import "nanopb.proto";
          option (nanopb_fileopt) = { int_size:IS_8 fixed_count:true max_count:2 };
          message M {
            repeated int32 items=1 [(nanopb)={int_size:IS_DEFAULT fixed_count:false}];
          }''')
        self.assertEqual(schema.symbols['M'].fields[0].storage, {'max_count': 2})

    # Verifies dynamic overrides discard nanopb bounds but preserve TOML validation.
    def test_dynamic_override(self):
        body = '''import "nanopb.proto"; message M {
          optional string text=1 [(nanopb)={max_size:8 type:FT_STATIC}]; }'''
        schema, _ = self.compile(body, '[fields."M.text"]\nstorage="dynamic"')
        self.assertEqual(schema.symbols['M'].fields[0].storage, {'storage': 'dynamic'})
        with self.assertRaisesRegex(SchemaError, 'require bounded'):
            self.compile(body, '[fields."M.text"]\nstorage="dynamic"\nmax_bytes=8')

    # Verifies declaration defaults are validated even when no field uses them.
    def test_unused_defaults_and_empty_aggregates(self):
        for body in [
            'option (nanopb_fileopt).max_size=-1; message M {}',
            'message M { option (nanopb_msgopt).max_size=-1; }',
        ]:
            with self.assertRaisesRegex(SchemaError, 'nonnegative'):
                self.compile('import "nanopb.proto"; ' + body)
        self.compile('import "nanopb.proto"; option (nanopb_fileopt)={}; message M {}')
        with self.assertRaisesRegex(SchemaError, 'require import'):
            self.compile('option (nanopb_fileopt)={}; message M {}')

    # Verifies the built-in annotation import produces no header or depfile input.
    def test_cli(self):
        for direct in ([], ['--direct-only']):
            with tempfile.TemporaryDirectory() as directory:
                root=Path(directory)
                (root/'test.proto').write_text('import "nanopb.proto"; message M { optional string text=1 [(nanopb).max_size=8]; }')
                # A real nanopb.proto is deliberately not parsed or emitted.
                (root/'nanopb.proto').write_text('import "google/protobuf/descriptor.proto";')
                subprocess.run([sys.executable,'-m','roo_pbc','-I',str(root),'--out',str(root/'out'),
                                '--depfile',str(root/'deps.d'),*direct,'test.proto'],check=True,capture_output=True,text=True)
                self.assertEqual([p.name for p in (root/'out').iterdir()],['test.pb.h'])
                self.assertNotIn('nanopb', (root/'deps.d').read_text())
                self.assertIn('BoundedString<7>', (root/'out/test.pb.h').read_text())
