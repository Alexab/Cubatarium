"""Saved Cross coverage must preserve the base plane's light/depth assumptions."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from engine_smoke_fixtures import prepare_cross_plane


class CrossFixtureTest(unittest.TestCase):
    def test_saved_plants_cover_route_without_replacing_opaque_floor(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / "worlds/EngineSmoke_cross"
            (fixture / "chunks").mkdir(parents=True)
            materials = root / "resource_packs/cubatarium_worldgen_cc0/blocks"
            materials.mkdir(parents=True)
            grass = dict(name="tall_grass", id=568, render=dict(style="cross"),
                         physics=dict(movement=dict(occupancy=0)))
            for name, value in (("stone", dict(name="stone", id=560)),
                                ("tall_grass", grass)):
                (materials / f"{name}.json").write_text(json.dumps(value), encoding="utf-8")
            metadata = dict(procedural=dict(min_height=-64, max_height=319),
                            resource_packs=dict(primary=["cubatarium_worldgen_cc0"]))
            (fixture / "world_data.json").write_text(json.dumps(metadata), encoding="utf-8")
            (fixture / "users.json").write_text('{"Username":{}}', encoding="utf-8")
            scene = prepare_cross_plane(fixture, 1)
            self.assertEqual(scene["lighting_fixture"]["sky_level"], 15)
            self.assertEqual(scene["variant"], "cross")
            total = 0
            for cx in range(scene["chunk_x_bounds"][0], scene["chunk_x_bounds"][1] + 1):
                chunk = json.loads((fixture / "chunks" / f"{cx}_-4_3.json").read_text())
                cells = {(v["lx"], v["ly"], v["lz"]): v["type"] for v in chunk["voxels"]}
                self.assertEqual(len(cells), len(chunk["voxels"]), "no duplicate saved cells")
                for x in range(16):
                    for z in range(16):
                        self.assertEqual(cells[(x, 3, z)], "stone", "opaque floor remains complete")
                plants = [cell for cell, kind in cells.items() if kind == "tall_grass"]
                self.assertEqual(len(plants), 16, "every route column contains Cross coverage")
                self.assertTrue(all(y == 4 and 0 <= z <= 6 for x, y, z in plants))
                total += len(plants)
            self.assertEqual(scene["cross_fixture"]["count"], total)
            self.assertEqual(scene["cross_fixture"]["material_sha256"], hashlib.sha256(
                (materials / "tall_grass.json").read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()
