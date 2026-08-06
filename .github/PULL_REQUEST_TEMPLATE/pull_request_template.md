---
name: Pull Request
about: Default template for code changes, bug fixes, and feature additions
---

# Pull Request

## Description

<!--
Provide a clear, concise summary of the changes and their purpose.
Focus on the *what* and the *why* (not just the how).
Example: "Implements SMPTE 170M-2004 Section 11.3 sync timing for NTSC."
-->

## Related Issues / Documents

<!--
Link to any relevant issues, discussions, or specification sections.
- Fixes #XXXX
- Implements docs-tech/design/XXXX.md §Y.Y
- Aligns with docs-tech/analogue-video-specifications/XXXX
-->

## Testing

<!--
- [ ] All new code has corresponding unit tests (classify as unit/functional in CMakeLists.txt)
- [ ] Existing tests pass (CI green)
- [ ] Manual verification steps performed (describe if applicable)
-->

- **Unit tests added/modified:**
- **Functional tests added/modified:**
- **Test coverage:**

## Checklist

- [ ] Code follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [ ] All C++ files include SPDX headers (see copilot-instructions.md §4.2)
- [ ] `clang-format --style=Google` passes
- [ ] New public APIs are documented
- [ ] Specification references added where applicable (see §4.3.6)
- [ ] HLD and sub-specifications updated if behaviour changed (see §7.2)
- [ ] No secrets or credentials committed

## Performance Impact (if applicable)

<!--
Note any expected performance changes (>5 % time, >10 % memory).
-->

## Breaking Changes

<!--
List any backwards-incompatible changes or migration steps required.
-->

## Additional Context

<!--
Screenshots, logs, or references to external specifications.
-->
