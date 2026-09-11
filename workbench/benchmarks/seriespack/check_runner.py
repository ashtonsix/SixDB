#!/usr/bin/env python3
"""Check SeriesPack profile applicability from small log fixtures; no hardware jobs."""
import unittest

from run import check_applicability


DEFERRED = 'deferred sum: native checks skipped (AVX512 BW/VBMI is not enabled)'
GROUPED = 'SKIP: grouped composition requires AVX-512 BW/VBMI'


def log(profile):
    targets = ['scalar'] + (['neon'] if profile == 'neon' else
                            ['avx2', 'avx512'] if profile == 'avx512' else ['avx2'])
    rows = [f'{family} ({target}): 1 fixture passed' for target in targets
            for family in ['SeriesPack public wire', 'SeriesPack range bounds']]
    executor = {'neon': 'NEON', 'avx512': 'AVX-512'}.get(profile, 'AVX2')
    rows.append(f'{executor} native composition: 1 fixture passed')
    rows += (['PASS: grouped composition 1 fixture', 'PASS: 1 deferred-sum checks; fixture']
             if profile == 'avx512' else [DEFERRED, GROUPED])
    return '\n'.join(rows)


class Applicability(unittest.TestCase):
    def test_optional_avx512_checks_do_not_reject_other_profiles(self):
        for profile in ['avx2', 'avx512bw', 'neon']:
            with self.subTest(profile=profile):
                result = check_applicability(profile, 'ikea_seriespack_check', log(profile))
                self.assertEqual(set(result['observed_unavailable_messages']), {DEFERRED, GROUPED})
                self.assertFalse(result['optional_checks']['deferred_sum']['applicable'])

    def test_full_profile_requires_its_optional_checks_to_execute(self):
        result = check_applicability('avx512', 'ikea_seriespack_check', log('avx512'))
        self.assertEqual(result['observed_unavailable_messages'], [])
        for skipped in [DEFERRED, GROUPED]:
            with self.assertRaisesRegex(RuntimeError, 'unexpected native skip'):
                check_applicability('avx512', 'ikea_seriespack_check', log('avx512') + '\n' + skipped)
        with self.assertRaisesRegex(RuntimeError, 'missing grouped/deferred'):
            check_applicability('avx512', 'ikea_seriespack_check',
                                log('avx512').replace('PASS: 1 deferred-sum checks;', 'absent:'))

    def test_missing_actual_target_or_native_composition_is_not_a_pass(self):
        for removed in ['SeriesPack public wire (neon)', 'SeriesPack range bounds (neon)',
                        'NEON native composition']:
            with self.subTest(removed=removed), self.assertRaisesRegex(RuntimeError, 'missing'):
                check_applicability('neon', 'ikea_seriespack_check',
                                    log('neon').replace(removed, 'absent'))

    def test_unknown_or_explicitly_requested_skips_are_not_suppressed(self):
        for skipped in ['native composition: no enabled native target', 'SKIP: new check',
                        'NEON payload: skipped on non-AArch64 target']:
            with self.assertRaisesRegex(RuntimeError, 'unexpected native skip'):
                check_applicability('neon', 'ikea_seriespack_check', log('neon') + '\n' + skipped)
        with self.assertRaisesRegex(RuntimeError, 'requested check is unavailable'):
            check_applicability('neon', 'ikea_seriespack_deferred_sum_check', DEFERRED)
        check_applicability('neon', 'ikea_seriespack_layout_view_check', 'layout/view fixture passed')


if __name__ == '__main__':
    unittest.main()
