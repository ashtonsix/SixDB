# Independent wire reference

These two frozen files retain the previously independently checked Ikea scalar
physical implementation. They share no implementation with Ikea2. Tests compare
final wire bytes and use the reference to construct reader input.

`provenance.json` records original paths and SHA-256 hashes. Only the include path
and namespace changed. Do not update this oracle alongside a candidate kernel to
make a failing comparison pass. A deliberate wire-version change needs an
independent reference and explicit compatibility tests. Replacing the old Ikea
module does not affect this fixture.
