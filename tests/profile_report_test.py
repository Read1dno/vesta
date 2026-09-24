import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / 'tools' / 'profile_report.py'
spec = importlib.util.spec_from_file_location('profile_report', path)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class ProfileReportTest(unittest.TestCase):
    def test_active_scene_filter(self):
        records = [
            {
                'type': 'process', 't_ms': tick, 'foreground_pid': foreground,
                'wall_ns': 1_000_000_000, 'cpu_ns': 80_000_000,
                'logical_cpus': 8, 'presented': 190,
            }
            for tick, foreground in [(1, 50), (2, 50), (3, 99)]
        ]
        records += [
            {
                'type': 'zone', 'name': name, 't_ms': tick, 'calls': 10,
                'self_cycles': 12, 'total_ns': 100, 'max_ns': 20,
            }
            for name, tick in [
                ('world_update', 1), ('chams_world_depth', 1),
                ('world_update', 3), ('chams_world_depth', 3),
            ]
        ]
        chosen = report.select_records(
            records, foreground_pid=50,
            require_zones=['world_update', 'chams_world_depth'],
        )
        self.assertEqual({record['t_ms'] for record in chosen}, {1})
        result = report.summarize(chosen)
        self.assertEqual(result['cpu_percent'], 1.0)
        self.assertEqual(result['presented_fps'], 190)
        self.assertTrue(result['active_world'])
        self.assertTrue(result['depth_rendered'])
        self.assertNotIn('NOT A FULL-FEATURE', report.render(result))
        self.assertEqual(report.select_records(
            records, after_ms=2, foreground_pid=50,
            require_zones=['world_update'],
        ), [])

    def test_empty_scene_is_labeled(self):
        self.assertIn('NOT A FULL-FEATURE', report.render(report.summarize([])))


if __name__ == '__main__':
    unittest.main()
