# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)
#
# The private initializer must be reviewed whenever its pinned source changes.

import hashlib
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / "external/miniaudio/miniaudio.h").read_text(encoding="utf-8")
signature = "MA_API ma_result ma_context_init(const ma_backend backends[], ma_uint32 backendCount, const ma_context_config* pConfig, ma_context* pContext)\n{"
start = source.index(signature)
end = source.index("\nMA_API ma_result ma_context_uninit(", start)
initializer = source[start:end].rstrip()
expected = "07161a4908d6e2bc7333a591810c1583422c28f2c54f0b1970a547b130128bd5"
if hashlib.sha256(initializer.encode()).hexdigest() != expected:
    raise SystemExit("Pinned miniaudio context initializer changed: review the private acquisition/unwind implementation")
private = (root / "audio/runtime/miniaudio_context.inl").read_text(encoding="utf-8")
# Preserve the actual upstream selection/dispatch sequence, not a separately
# maintained list of preferred backends.
begin = "    for (iBackend = 0; iBackend <= ma_backend_null; ++iBackend)"
finish = "        /* If this iteration was successful, return. */"
if initializer[initializer.index(begin):initializer.index(finish)] != private[private.index(begin):private.index(finish)]:
    raise SystemExit("Private Audio backend preference/dispatch differs from pinned miniaudio")
print("Pinned miniaudio initializer and private backend dispatch match")
