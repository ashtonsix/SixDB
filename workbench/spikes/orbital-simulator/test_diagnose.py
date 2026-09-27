import unittest

from campaigns import Observation, cohort
from diagnose import symptoms


class DiagnosisTests(unittest.TestCase):
    def test_slow_cohort_is_not_hidden_by_fast_successes(self):
        o = Observation(dict(fast=cohort({"a":0}, {"a":99}, {}, offered_until=100, until=100),
                             slow=cohort({"b":0}, {}, {}, offered_until=100, until=100)), {})
        result = symptoms(o, 50)["cohorts"]
        self.assertEqual(result["fast"]["status"], "complete")
        self.assertEqual(result["slow"]["status"], "quiet_with_unfinished_work")
        self.assertEqual(result["slow"]["aged_obligations"], {"b":100})

    def test_refusal_is_not_reported_as_a_stall_or_success(self):
        o = Observation(dict(work=cohort({"x":1}, {}, {"x":2}, offered_until=10, until=100)), {})
        self.assertEqual(symptoms(o, 50)["cohorts"]["work"]["status"], "refused")

    def test_ongoing_service_does_not_hide_an_old_operation(self):
        o = Observation(dict(work=cohort({"old":0,"recent":90}, {"recent":99}, {}, offered_until=100, until=100)), {})
        c = symptoms(o, 50)["cohorts"]["work"]
        self.assertEqual(c["status"], "unfinished")
        self.assertEqual(c["aged_obligations"], {"old":100})


if __name__ == "__main__":
    unittest.main()
