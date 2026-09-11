# Executable placement and short-call measurements

How much can unchanged instructions move in performance when a program is
relinked? The [checked-get placement study](evidence/checked-point-layout-20260911/summary.md)
and [call-boundary findings](../ikea-composition/call-boundaries/checked-get.md)
separate instruction changes from linked placement, without claiming the latter
explains every residual loss.

[replay-k10.json](replay-k10.json) compares retained old/new binaries through the
[shared replay helper](../../tools/replay.py). [relink.py](relink.py) varies the
placement of exact retained K10 inputs without recompiling them. These are
specific discriminators, not a general benchmark correction.

The [relink recovery note](layout-recovery.md) explains why source and an archive
alone may not recover the measured executable. Old checkpoint-specific drivers
and inputs are [recoverable](../ikea-composition/archive/validation-20260911.md).
