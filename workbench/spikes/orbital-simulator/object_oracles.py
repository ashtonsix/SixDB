"""Independent safety observers for the authored object/extension fixtures.

These checks do not establish completion: absent reads or publications can
vacuously satisfy safety. A runner must separately account for every offered
obligation and require its scenario's expected completion, refusal, abort or
explicitly unfinished outcome. In particular, an extension missing a required
checker must remain unpublished; passing this safety check does not make that
transaction complete. The unit tests retain separate completion assertions.
"""

def events(world, kind, op=None):
    return [entry for entry in world.trace if entry["kind"] == kind
            and (op is None or entry.get("op") == op)]


def check_observations(world):
    """Fixture oracle, independent of the store's reconstruction loop/cache."""
    initial = (10, 20, 30, 40)
    for observation in events(world, "view_observed"):
        expected = (99 if observation["page"] == 1 and observation["cut"] >= 20
                    else initial[observation["page"]])
        if observation["value"] != expected:
            raise AssertionError((observation, expected))


def check_verification(world):
    plans = events(world, "verification_plan", "T")
    assert len(plans) == 1
    # Required membership comes from the authored input, not the coordinator's
    # claim about whichever reports it happened to accept.
    required = {"check-a", "check-b"}
    assert set(plans[0]["required"]) == required
    assert plans[0]["cut"] == 10
    reports = {e["checker"]: e for e in events(world, "extension_transcript", "T")}
    for published in events(world, "published", "T"):
        assert set(reports) == required
        records = [reports[name] for name in required]
        assert all(r["time"] <= published["time"] for r in records)
        first = records[0]["transcript"]
        # Structural comparison is independent of the actor's encoded equality.
        assert all(r["transcript"] == first for r in records)
        assert first["completion"] == "return"
        assert published["value"] == first["response"]["value"] + 1
