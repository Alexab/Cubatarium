import json
from pathlib import Path
import struct
import unittest

import test_engine_air_column_evidence as fixture
from engine_disk_read_failure_evidence import evaluate


def frame(coord, stone=False):
    palette = [0, 8] if stone else [0]
    runs = [(273,0),(1,1),(3822,0)] if stone else [(4096,0)]
    return (struct.pack('<4sBiiiH',b'CCHK',4,*coord,len(palette)) +
            struct.pack('<'+'H'*len(palette),*palette) + struct.pack('<I',len(runs)) +
            b''.join(struct.pack('<HH',*r) for r in runs) +
            struct.pack('<I',4096)+bytes(4096)+struct.pack('<I',4096)+bytes(4096)+struct.pack('<HQ',16384,123125))


class DiskReadFailureEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.f = fixture.AirColumnEvidenceTest('test_full_profile_requires_all_twenty_four_decoded_slices')
        self.f.setUp()
        self.addCleanup(self.f.doCleanups)
        self.f.events=[]
        self.root=self.f.base.root
        self.worlds=self.root/'worlds'
        self.world=self.worlds/('EngineSmoke_'+'a'*32)
        (self.world/'chunks').mkdir(parents=True)
        (self.root/'route.json').write_text(json.dumps(dict(fixture_world=self.world.name,world_source='EngineSmoke_'+'b'*32)),encoding='utf-8')
        self.f.add('runtime_owner',1900,-3,0,origin=(1,-3,3))
        for cy in range(-4,20):
            for source in (('runtime_initial','runtime_decoded_initial') if cy==-4 else ('runtime_air_initial','runtime_air_decoded_initial')):
                self.f.add(source,1900,cy,50 if cy==-4 else 100+cy)
        for cy in range(-4,20): self.f.add('remove' if cy==-4 else 'air_remove',3000,cy,50 if cy==-4 else 100+cy)
        for attempt in range(6):
            tick=6000+attempt*100 if attempt<5 else 7403
            demand=10+attempt
            self.f.add('disk_column_queued',tick,0,demand)
            for cy in range(-4,19 if attempt<5 else 20):
                self.f.add('disk_slice_read_valid',tick+1,cy,demand)
                self.f.add('disk_geometry_owner' if cy==-4 else 'disk_air_owner',tick+1,cy,500 if cy==-4 else 600+cy)
                if cy==-4 and attempt: self.f.add('disk_read_edit_current',tick+1,cy,0,origin=(17,-63,81))
            if attempt<5:
                self.f.add('disk_read_frame_rejected',tick+2,19,demand)
                if attempt==0:
                    self.f.add('disk_read_edit_before',tick+2,-4,8,origin=(17,-63,81))
                    self.f.add('disk_read_edit_after',tick+2,-4,0,origin=(17,-63,81))
                    self.f.add('disk_read_edit_owner',tick+2,-4,500,origin=(17,-63,81))
                    self.f.add('disk_read_dirty_before',tick+2,0,3)
                    self.f.add('disk_read_save_rejected',tick+2,0,demand)
                    self.f.add('disk_read_dirty_after',tick+2,0,3)
                    self.f.add('disk_read_files_snapshot',tick+2,0,26)
                self.f.add('disk_column_retry' if attempt<4 else 'disk_read_exhausted',tick+3,0,demand)
            else:
                self.f.add('disk_read_recovered',tick+2,0,demand)
                self.f.add('disk_column_complete',tick+2,0,demand)
        for cy in range(-4,20):
            for source in (('runtime_final','runtime_decoded_final') if cy==-4 else ('runtime_air_final','runtime_air_decoded_final')):
                self.f.add(source,10010,cy,500 if cy==-4 else 600+cy)
        snapshot=self.root/'disk-read-save'
        files=[]
        for cy in range(-4,20):
            name=f'chunks/1_{cy}_5.cchunk'
            payload=frame((1,cy,5),cy==-4)
            for phase in ('before','after'):
                p=snapshot/phase/name
                p.parent.mkdir(parents=True,exist_ok=True)
                p.write_bytes(payload)
            files.append(dict(path=name,bytes=len(payload)))
            (self.world/'chunks'/Path(name).name).write_bytes(frame((1,cy,5)))
        for name in ('world_data.json','users.json'):
            for phase in ('before','after'): (snapshot/phase/name).write_bytes(b'{}')
            files.append(dict(path=name,bytes=2))
        (snapshot/'manifest.json').write_text(json.dumps(dict(schema='disk-read-save-files.v1',column=[1,5],demand_id=10,files=files)),encoding='utf-8')

    def check(self,status,reason=None):
        self.f.write()
        r=evaluate(self.root,self.f.base.report,self.f.base.route,worlds_root=self.worlds)
        self.assertEqual(r['status'],status,r)
        if reason: self.assertEqual(r['reason'],reason,r)
        return r

    def test_actual_fault_retry_edit_save_and_cold_chain(self):
        r=self.check('PASS')
        self.assertEqual(r['failed_demands'],[10,11,12,13,14])
        self.assertEqual(r['recovered_demand_id'],15)
        self.assertEqual(r['retained_slices'],23)

    def test_missing_fault_and_exhaustion_cannot_pass(self):
        self.f.events=[e for e in self.f.events if e['source']!='disk_read_exhausted']
        self.check('UNTESTED','missing_retry_exhaustion_chain')

    def test_changed_retained_owner_fails(self):
        e=next(e for e in self.f.events if e['source']=='disk_geometry_owner')
        e['value']+=1
        self.check('FAIL','retained_read_owner_changed')

    def test_stale_disk_overlay_fails(self):
        next(e for e in self.f.events if e['source']=='disk_read_edit_current')['value']=8
        self.check('FAIL','local_edit_overwritten_during_retry')

    def test_dirty_state_cannot_disappear(self):
        next(e for e in self.f.events if e['source']=='disk_read_dirty_after')['value']=0
        self.check('FAIL','blocked_save_not_rejected_with_dirty_state')

    def test_snapshot_bytes_cannot_change(self):
        (self.root/'disk-read-save/after/users.json').write_bytes(b'[]')
        self.check('FAIL','blocked_save_modified_files')

    def test_final_disk_must_retain_local_deletion(self):
        (self.world/'chunks/1_-4_5.cchunk').write_bytes(frame((1,-4,5),True))
        self.check('FAIL','local_edit_missing_from_final_disk')

    def test_recovery_cooldown_is_actual_time(self):
        for e in self.f.events:
            if 7403<=e['tick']<=7405: e['tick']-=500
        self.check('UNTESTED','missing_bounded_recovery')

    def test_failed_demand_cannot_complete(self):
        self.f.events.insert(-1,dict(self.f.events[-1],source='disk_column_complete',value=10))
        self.check('FAIL','failed_demand_claimed_complete')


if __name__=='__main__': unittest.main()
