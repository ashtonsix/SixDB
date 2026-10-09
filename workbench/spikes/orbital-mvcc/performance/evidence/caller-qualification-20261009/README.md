# Complete-caller correctness captures

These successful checks support the actual catalog/Allocation caller, not a
performance claim or qualification of later composed source treatments.
Each passed 26,430 independent comparisons, 120 CLI cases through 4096 Objects,
and four explicit metadata-capacity refusals. Checks include sparse duplicate
writes, an abandoned group after earlier successful clones, physical-capacity
refusal, successful retry, late old discovery and shutdown at zero Arena charge.

The native closure uses the later candidate header; the sanitizer capture uses
the preceding header. These are distinct source cuts. Exact project input maps,
compiler flags, binaries and full logs are recovered with the adjacent artifact
references; the full receipt is `qualification.json`.

```json
{
  "native": {
    "compiled_source_digest": "6128cfc140bd23cd11b15789ff8319a89ac99769d15dd6182475f72c5e8a71f9",
    "independent_comparisons": 26430,
    "cli_cases": 120,
    "capacity_refusals": 4,
    "source_input_count": 136,
    "sanitizer_flags": []
  },
  "sanitizer": {
    "compiled_source_digest": "9aa95db998478e530c5a34af3d6dda0574814f37d139b56e31e8a3b2662bf8db",
    "independent_comparisons": 26430,
    "cli_cases": 120,
    "capacity_refusals": 4,
    "source_input_count": 133,
    "sanitizer_flags": [
      "-fsanitize=address,undefined",
      "-fsanitize-address-use-after-return=always",
      "-fno-omit-frame-pointer"
    ]
  }
}
```

Recover into ignored output using `workbench/tools/artifacts.py fetch` and the
chosen artifact reference. Routine logs and raw per-case outputs stay out of Git.
