import unittest
from engine_air_field_evidence import join_jobs


class AirJobEvidenceTest(unittest.TestCase):
    def rows(self, job):
        return [
            f'[RelightAudit] job_submit job={job} source=light_sample_consumer sky=1 block=1 air_requested=1 samples_requested=1',
            f'[RelightAudit] worker_done job={job} inputs_complete=1 unknown_inputs=0 frontier_unfinished=0 chunks=1 read_set=27',
            f'[RelightAudit] calculation_certificate job={job} sections=1 air_outputs=1 air_complete=1 include_sky=1 include_block=1 inputs_complete=1']

    def test_complete_cohorts_and_negative_controls(self):
        a, b = self.rows(1), self.rows(2)
        self.assertEqual(join_jobs(a+b)['status'], 'PASS')
        for invalid in [a, a+b[:-1], a+[x.replace('job=2','job=3') if 'worker_done' in x else x for x in b],
                        [x.replace('air_complete=1','air_complete=0') for x in a+b],
                        [x.replace('unknown_inputs=0','unknown_inputs=1') for x in a+b],
                        [x.replace('source=light_sample_consumer','source=streaming_fifo') for x in a+b],
                        [x.replace('inputs_complete=1','inputs_complete=0') for x in a+b]]:
            self.assertNotEqual(join_jobs(invalid)['status'], 'PASS')


if __name__ == '__main__':
    unittest.main()
