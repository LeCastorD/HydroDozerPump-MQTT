from datetime import datetime
from pathlib import Path

Import("env")

project_dir = Path(env["PROJECT_DIR"])
counter_file = project_dir / ".build_number"
header_file = project_dir / "include" / "fw_version_auto.h"
base_version = env.GetProjectOption("custom_fw_version_base", "1.0").strip()

build_number = 0
if counter_file.exists():
    try:
        build_number = int(counter_file.read_text(encoding="utf-8").strip())
    except ValueError:
        build_number = 0

build_number += 1
counter_file.write_text(f"{build_number}\n", encoding="utf-8")

version = f"{base_version}.{build_number}"
build_stamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

header_file.parent.mkdir(parents=True, exist_ok=True)
header_file.write_text(
    "\n".join(
        [
            "#ifndef FW_VERSION_AUTO_H",
            "#define FW_VERSION_AUTO_H",
            "",
            f'#define FW_VERSION_BASE "{base_version}"',
            f"#define FW_BUILD_NUMBER {build_number}",
            f'#define FW_VERSION "{version}"',
            f'#define FW_BUILD_STAMP "{build_stamp}"',
            "",
            "#endif",
            "",
        ]
    ),
    encoding="utf-8",
)

print(f"[version] FW_VERSION={version} ({build_stamp})")
