# Original reference run closure

After the independently completed reduced case was accepted, the sole worker
operator stopped the unchanged original reference. It remained honestly
`incomplete_interrupted`: 30,579,796 distinct states, 5,855,956 queued, depth 50.
It is not one of the accepted completed graphs in the [current selection](../README.md).

`lifecycle.json` records the independent observation that its instance terminated,
its temporary security group was absent and ephemeral keys were removed, with
no relaunch. It also records unchanged source/producer checks and the last saved
checkpoint reference. `original-reference.json` retrieves the separately verified
6,653,880-byte final receipt/log/lineage archive through the retention tools.
The bulk checkpoint remains in S3; it is not downloaded by this compact export.
