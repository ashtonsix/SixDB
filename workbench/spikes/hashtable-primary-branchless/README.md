# Graduated primary lookup specialization

Ikea maintains predictable/unpredictable getters and caller-owned automatic
selection of complete lookup/consumer loops. **Predictable is the default.**
The hint never supplies membership knowledge. Automatic selection belongs at a
batch/operator boundary and keeps clocks and dispatch out of per-key work.

- [Maintained API and executable example](../../../ikea/docs/hashtable/usage.md#select-lookup-and-consumer-together).
- [Promotion, native actual-control scorecard and verification](GRADUATION.md).
- [Research decision and prior microbatch study](HINTS.md).
- [Rejected uniform replacement](SCALAR.md): why removing the branch everywhere failed.

Rejected generators and duplicate checks are archived; the promotion record
identifies their verified local archive and S3 recovery. Current
`native-graduation.sh full` and `native-graduation-stream.sh` exercise maintained
headers and the public controller, using the actual Boost/Abseil comparison
harness. The remaining report tools regenerate retained evidence without new
timing. There is no open optimization campaign here.
