from pathlib import Path
import subprocess

source = subprocess.check_output([
    "git",
    "show",
    "dd69f8586d6fbbee77ca3fcf5e3e58a073341c23:apps/vice_lab/app.cpp",
], text=True)
old = "static_assert(sizeof(LabVertex) == 24U);"
if source.count(old) != 1:
    raise RuntimeError(f"expected one LabVertex size guard, found {source.count(old)}")
source = source.replace(old, "static_assert(sizeof(LabVertex) == 44U);", 1)
Path("apps/vice_lab/app.cpp").write_text(source)
