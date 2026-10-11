import json
import struct
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_release import write_pack, selftest, validate_pack
from validate_bundle import MANIFEST, digest, validate_manifest


class ReleaseValidation(unittest.TestCase):
    def test_failed_pack_promotion_removes_temp_and_preserves_previous_pack(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'core.pack'
            path.write_bytes(b'previous pack')
            with patch.object(Path,'replace',side_effect=OSError('locked destination')):
                with self.assertRaises(OSError):write_pack(path,[('image',b'new image')])
            self.assertEqual(path.read_bytes(),b'previous pack')
            self.assertFalse(path.with_name(path.name+'.tmp').exists())

    def test_pack_roundtrip_and_determinism(self):
        with tempfile.TemporaryDirectory() as folder:
            first, second = Path(folder)/'first.pack', Path(folder)/'second.pack'
            entries = [('one',b'image'),('two',b'image'),('three',b'another')]
            write_pack(first,entries); write_pack(second,entries)
            selftest(first,entries); validate_pack(first)
            self.assertEqual(first.read_bytes(),second.read_bytes())
    def test_truncated_or_out_of_range_pack_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'bad.pack'
            path.write_bytes(b'TCPK'+struct.pack('<II',1,2))
            with self.assertRaises(SystemExit): validate_pack(path)
            path.write_bytes(b'TCPK'+struct.pack('<III',1,1,1)+b'x'+struct.pack('<QQ',999,5))
            with self.assertRaises(SystemExit): validate_pack(path)
    def test_manifest_rejects_missing_added_modified_wrong_version(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);p=root/'data.json';p.write_text('{}')
            with self.assertRaises(ValueError): validate_manifest(root)
            manifest={'complete':True,'dataVersion':'test','files':{'data.json':digest(p)}}
            (root/MANIFEST).write_text(json.dumps(manifest)); validate_manifest(root,'test')
            with self.assertRaises(ValueError): validate_manifest(root,'other')
            p.write_text('{"changed":true}')
            with self.assertRaises(ValueError): validate_manifest(root)
            p.unlink()
            with self.assertRaises(ValueError): validate_manifest(root)


if __name__=='__main__':unittest.main()
