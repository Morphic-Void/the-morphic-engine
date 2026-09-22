# Development filesystem roots

`development/logical-roots` is the first directory-backed implementation of the
runtime logical-root model.  It deliberately makes every currently supported
development root visible in one place while the engine loads ordinary Windows
or Linux files.

The root names are logical-root identifiers, not a proposal for the shipping
filesystem layout.  The later platform resolver will bind the same identifiers
to installed content, per-user data, Android assets or other appropriate
backends.

## Roots

| Directory | Logical root | Initial purpose | Discovery | Git policy |
| --- | --- | --- | --- | --- |
| `package/` | `package:` | Files that model immutable core packaged content | startup/on demand | tracked |
| `dev-source/` | `dev-source:` | Development source material eligible for the ordinary development content pipeline | manual/on demand | tracked |
| `dev-workspace/` | `dev-workspace:` | Editable source assets and projects not yet promoted to packaged content | manual/on demand | tracked |
| `config/` | `config:` | Local development settings | direct lookup | ignored |
| `saves/` | `save:` | Save slots and their metadata | on demand | ignored |
| `state/` | `state:` | Crash reports and machine-specific runtime state | no content scan | ignored |
| `logs/` | `logs:` | Runtime and development-tool diagnostics | no content scan | ignored |
| `test-logs/` | `test-logs:` | Test-infrastructure diagnostic output | no content scan | ignored |
| `test-output/` | `test-output:` | Generated test files, manifests and other non-log test artefacts | no content scan | ignored |
| `cache/` | `cache:` | Rebuildable local data | no content scan | ignored |
| `ugc-work/` | `ugc-work:` | Editable local UGC projects | manual/on demand | ignored |
| `ugc-inbox/` | `ugc-inbox:` | Imported/downloaded UGC awaiting validation | manual/on demand | ignored |
| `ugc-installed/` | `ugc-installed:` | Validated local UGC available for selection on a later launch | startup/on demand | ignored |

`ugc-provider:` is intentionally absent from this local shape.  It will be a
read-only provider adapter rather than a directory owned by this repository.
Android does not bind any `ugc-*` root until Android UGC support is explicitly
introduced.

## First-pass catalogue

The first filesystem image can scan these ordinary directories on demand and
publish one data-model document.  Every catalogue record must retain the root
identifier and root-relative path; a native absolute path is not a durable
resource identity.  The scan records root availability and failures separately
from an empty directory, and only enumerates roots whose discovery policy
permits it.

Test logs belong beneath `test-logs/`; all other test artefacts belong beneath
`test-output/`.  The test runner uses process-ID and optional tag-qualified
filenames in these roots. Its existing `--output-directory` override puts
non-log files directly in that directory and logs in its `logs` child.
The Executive acceptance exercise and DLL lifecycle fixtures currently use
fixed non-log output filenames and must run serially.

The shared input fixture lives at `dev-source/test_input.tga`. Host diagnostics
use `logs/`, policy-validator reports use `logs/policy_validator/`, and the DLL
lifecycle harness redirects Host diagnostics into `test-logs/`. These consumers
currently use direct filesystem paths; runtime resolution from the root
manifest and on-demand catalogue generation remain future work.

`root-manifest.json` is the checked-in first-pass root configuration.  Generated
catalogue documents belong in `development/manifests/` and are ignored.  The
catalogue's baked-document schema will be specified with the filesystem-image
request; it should be generated from this configuration rather than become a
second root configuration.
