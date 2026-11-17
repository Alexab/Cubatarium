import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from engine_smoke_run import isolated_fixture, world_fingerprint
from engine_smoke_fixtures import prepare_visible_items_plane
from engine_world_item_reference import evaluate


class WorldItemReferenceTest(unittest.TestCase):
    def scene(self):
        return dict(name="open-sky-plane", chunk_x_bounds=[-23, 3],
            plane_bounds=dict(floor=-60.5, z_low=31.5, z_high=79.5),
            visible_world_items=dict(schema="engine-world-item-field-fixture.v1",
                item="raw_meat", instances=[101, 102],
                positions=[[26, -60.4, 51], [-326, -60.4, 51]], minimum_frame_epochs=3))

    def check(self, change="", *, omit_queries=False, omit_draws=False,
              omit_second=False, duplicate=False, marker=True, unrelated=False):
        rows = []
        for frame in range(1, 5):
            identity, x = (101, 26) if frame <= 2 else (102, -326)
            shared = (f"world_epoch=4 frame_epoch={frame} cell=({x},-60,51) "
                      "physical=1 ready=1 state=2 sky=15 block_light=0 "
                      "field_incarnation=12 field_revision=0")
            rows.append("[CameraLightSample] " + shared + "\n")
            if not omit_queries:
                rows.append("[SceneLightSample] consumer=item " + shared + "\n")
            if not omit_draws and not (omit_second and identity == 102):
                row = ("[WorldItemLightDraw] " + shared +
                       f" item=raw_meat instance={999 if unrelated else identity} drawn_primitives=2 " +
                       (change if frame == 1 else "") + "\n")
                rows.append(row)
                if duplicate and frame == 1:
                    rows.append(row)
            if marker and frame == 1:
                # The first draw precedes the same first-paint witness.
                rows.append('[FirstWorldFrameWitness] {"phase":"first_world_paint","world_epoch":4}\n')
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / "native.log"
            log.write_text("".join(rows), encoding="utf-8")
            return evaluate(self.scene(), [log])

    def test_requires_both_declared_instances_and_real_joined_draws(self):
        result = self.check()
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["checked_draws"], 4)
        for options in (dict(omit_queries=True), dict(omit_draws=True),
                        dict(omit_second=True), dict(marker=False), dict(unrelated=True)):
            self.assertEqual(self.check(**options)["status"], "UNTESTED")

    def test_wrong_first_field_identity_pose_or_empty_draw_fails(self):
        for change in ("ready=0", "ready=2", "state=1", "field_incarnation=0",
                       "sky=0", "block_light=1", "drawn_primitives=0", "drawn_primitives=bad",
                       "field_revision=-1", "item=copper_axe", "cell=(80,-60,51)"):
            with self.subTest(change=change):
                self.assertEqual(self.check(change)["status"], "FAIL")
        self.assertEqual(self.check(duplicate=True)["status"], "FAIL")
        self.assertEqual(self.check("physical=0")["status"], "UNTESTED")
        self.assertEqual(self.check("field_incarnation=99")["status"], "UNTESTED")


class VisibleItemsFixtureTest(unittest.TestCase):
    def test_clone_only_bodies_have_unique_inventory_identity_and_real_asset_provenance(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            runtime = root / "bin"
            source = runtime / "worlds/Saved"
            source.mkdir(parents=True)
            (source / "world_data.json").write_text("{}")
            (source / "users.json").write_text(json.dumps({"Username": {"instance_id": 900}}))
            body = dict(id=4, item="raw_meat", kind=4, count=1, instance=800,
                        position=[8, 68, 61], velocity=[0, 0, 0], size=[.24, .1, .16],
                        carrier=0, creature=0, domain="creative")
            (source / "creatures.json").write_text(json.dumps(dict(world_physical_v2=[body])))
            (runtime / "config.json").write_text("{}")
            asset = runtime / "models/items/raw_meat/model.glb"
            asset.parent.mkdir(parents=True)
            asset.write_bytes(b"glTF" + struct.pack("<II", 2, 20) + b"12345678")
            before = world_fingerprint(source)
            scene = WorldItemReferenceTest().scene()
            scene.pop("visible_world_items")
            scene["starting_eye"] = [22, -58.88, 51]
            with isolated_fixture(runtime, "Saved", root / "evidence") as (fixture, _):
                with patch("engine_smoke_fixtures.prepare_open_sky_plane", return_value=scene):
                    prepared = prepare_visible_items_plane(fixture, 19)
                rows = json.loads((fixture / "creatures.json").read_text())["world_physical_v2"]
                self.assertEqual(rows[0], body)
                self.assertEqual(len(rows), 3)
                self.assertEqual([r["instance"] for r in rows[1:]], [901, 902])
                self.assertEqual([r["position"][0] for r in rows[1:]], [26, -326])
                self.assertTrue(all(r["velocity"] == [0, 0, 0] and r["carrier"] == 0 for r in rows[1:]))
                self.assertEqual(prepared["visible_world_items"]["instances"], [901, 902])
                self.assertEqual(len(prepared["visible_world_items"]["available_glb_sha256"]), 64)
            self.assertEqual(world_fingerprint(source), before)
            with self.assertRaises(ValueError):
                prepare_visible_items_plane(source, 19)


if __name__ == "__main__":
    unittest.main()
