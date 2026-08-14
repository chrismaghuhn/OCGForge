from __future__ import annotations

from dataclasses import asdict, dataclass
from pathlib import Path


@dataclass(frozen=True)
class ScriptResolution:
    passcode: int
    path: str | None
    implementation: str | None
    load_result: str

    def as_dict(self) -> dict[str, object]:
        return asdict(self)


def resolve_card_script(passcode: int, root: str | Path) -> ScriptResolution:
    script_root = Path(root)
    candidates = (
        (script_root / "official" / f"c{passcode}.lua", "official"),
        (script_root / "unofficial" / f"c{passcode}.lua", "shared/unofficial"),
        (script_root / f"c{passcode}.lua", "shared/root"),
    )
    for path, implementation in candidates:
        if path.is_file():
            return ScriptResolution(passcode, str(path), implementation, "PASS")
    return ScriptResolution(passcode, None, None, "MISSING")
