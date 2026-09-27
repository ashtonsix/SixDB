"""Reduce an authored incident list while preserving one named failure predicate.

This is deletion minimization, not a minimum counterexample or a schedule proof.
The predicate reconstructs a fresh world; it must not reuse mutated actor state.
"""


def minimize(items, fails):
    current = list(items)
    if not fails(current):
        raise ValueError("initial history does not exhibit the requested failure")
    width = max(1, len(current) // 2)
    while current:
        removed = False
        for start in range(0, len(current), width):
            candidate = current[:start] + current[start + width:]
            if fails(candidate):
                current = candidate
                removed = True
                break
        if removed:
            width = min(width, max(1, len(current)))
        elif width > 1:
            width = max(1, width // 2)
        else:
            break
    return current
