#!/usr/bin/env python3
"""Check independent-core agreement and the local pipeline shortcut contract."""

from copy import deepcopy
import unittest
from unittest.mock import patch

from envelope_locality import EnvelopeSimulation, workload
from pipeline_contrasts import blind_overlap
from pipeline_simulation import PipelineSimulation, PipelineStore
import position_pipeline


class CoreAdapter(PipelineStore):
    @property
    def pending_by_key(self):
        return {key: list(claims.values()) for key, claims in self.pending_versions.items()}

    def admission_releasable(self, attempt):
        return attempt.c is not None and self.published[attempt.owner] == self.coverage[attempt.owner]


class PipelineSimulationChecks(unittest.TestCase):
    def test_independently_authored_histories_agree_between_cores(self):
        expected = position_pipeline.probes()
        with patch.object(position_pipeline, 'PipelineStore', CoreAdapter):
            actual = position_pipeline.probes()
        for left, right in zip(expected, actual, strict=True):
            self.assertEqual(left['name'], right['name'])
            self.assertEqual(left.get('final'), right.get('final'))
            self.assertEqual(left.get('unsafe_variant_rejected'), right.get('unsafe_variant_rejected'))

    def test_held_control_preserves_backend_outcomes(self):
        case = workload('target', 4)
        reference = EnvelopeSimulation(deepcopy(case)).run()
        control = PipelineSimulation(deepcopy(case), False).run()
        self.assertEqual(reference['cohorts'], control['cohorts'])
        self.assertEqual(reference['logical_state_sha256'], control['logical_state_sha256'])

    def test_local_blind_puts_wait_for_allocation_not_private_wan_computation(self):
        rows = [PipelineSimulation(blind_overlap(delay), release).run()
                for release, delay in ((True,150),(True,600),(False,150))]
        self.assertEqual(rows[0]['cohorts']['blind']['p99_ticks'], rows[1]['cohorts']['blind']['p99_ticks'])
        self.assertLess(rows[0]['cohorts']['blind']['p99_ticks'], rows[2]['cohorts']['blind']['p99_ticks'])
        self.assertTrue(all(c['complete'] == c['offered'] for row in rows for c in row['cohorts'].values()))


if __name__ == '__main__':
    unittest.main()
