import importlib.util
import json
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('analyze_capture',ROOT/'tools/analyze_capture.py')
assert spec is not None and spec.loader is not None
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
class DecodeTests(unittest.TestCase):
    def test_published_real_bosch_example(self):
        fixture=json.loads((ROOT/'tests/fixtures/upstream_bosch144_cool16.json').read_text())
        result=module.decode_bosch(fixture['timings_us'])
        self.assertEqual(result['bytes'],fixture['expected_bytes'])
        self.assertEqual(result['temperature_c'],16)
        self.assertEqual(result['mode_code'],0)
    def test_corrupt_envelope_not_forced(self):
        for name in ['midea_original_25c.json','midea_original_26c.json']:
            data=json.loads((ROOT/'captures/rn10l2b2hs-bgh'/name).read_text())
            self.assertIsNone(module.decode_bosch(data['timings_us']))
    def test_bad_checksum_rejected(self):
        fixture=json.loads((ROOT/'tests/fixtures/upstream_bosch144_cool16.json').read_text())
        values=fixture['timings_us'][:]
        index=202+2*(17*8-96)+1
        values[index]=610 if values[index]>1000 else 1645
        self.assertIsNone(module.decode_bosch(values))
if __name__=='__main__':unittest.main()
