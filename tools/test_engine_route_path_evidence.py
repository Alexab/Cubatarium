import copy
import json
from pathlib import Path
import tempfile
import unittest

from engine_route_path_evidence import evaluate


class RoutePathEvidenceTest(unittest.TestCase):
    def test_order_targets_containment_and_terminal_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / "trace.jsonl"
            route = dict(schema="diagnostic-flight-route.v1", relative=True,
                         waypoints=[dict(position=[0, 6, 0]), dict(position=[176, 6, 0])])
            report = dict(teleport_cruise=False, diagnostic_relocations_after_start=0,
                          diagnostic_bot=dict(enabled=True, state="complete", waypoints_visited=2))
            scene = dict(cave_bounds=dict(floor=-1, ceiling=10, z_low=-1, z_high=1),
                         chunk_x_bounds=[-1, 12], starting_eye=[0, 0, 0], expected_eye_in_fluid=False)
            baseline = [dict(schema="diagnostic-bot-trace.v1", time=i, position=p,
                             state=state, waypoints_visited=visited, eye_in_fluid=False)
                        for i, (p, state, visited) in enumerate([
                            ([0, 0, 0], "travel", 0), ([0, 6, 0], "inspect", 0),
                            ([176, 6, 0], "inspect", 1), ([176, 6, 0], "complete", 2)])]
            def check(rows):
                trace.write_text("\n".join(json.dumps(r) for r in rows))
                return evaluate(trace, report, route, minimum_chunks=8, scene=scene)
            self.assertEqual(check(baseline)["status"], "PASS")
            for index, key, value in [(1, "position", [0, 8, 0]), (1, "waypoints_visited", 1),
                                       (0, "position", [0, 0, 2]), (1, "eye_in_fluid", True),
                                       (2, "time", 0), (3, "state", "blocked")]:
                with self.subTest(index=index, key=key):
                    rows = copy.deepcopy(baseline)
                    rows[index][key] = value
                    self.assertEqual(check(rows)["status"], "FAIL")
            rows = copy.deepcopy(baseline)
            rows.insert(2, dict(rows[1], time=1.5, position=[20, 6, 2], state="travel"))
            self.assertEqual(check(rows)["reason"], "route_left_authored_cave")
            report["diagnostic_relocations_after_start"] = 1
            self.assertEqual(check(baseline)["status"], "FAIL")


if __name__ == "__main__":
    unittest.main()
