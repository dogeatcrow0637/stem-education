# Repository Guidelines

## Project Structure & Module Organization

This repository is a collection of self-contained STEM projects. Put each project under `projects/<project-slug>/`; register it in `projects/README.md`. A project may contain `docs/`, `hardware/`, `enclosure/`, `firmware/`, `software/`, `code/`, and `resources/`. Cross-project conventions live in `docs/`, while `templates/project-template/` is the authoritative skeleton for new projects. Automation belongs in `scripts/`; teaching-material generators are under `.trae/skills/stem-project-onboarding/`.

## Build, Test, and Development Commands

There is no repository-wide build system. Follow the README inside the project or component you change.

- `./scripts/new-project.sh line-follower-car "循迹小车"` creates a project from the template and assigns its next ID.
- `bash -n scripts/new-project.sh` checks the project-creation script for shell syntax errors.
- `python3 .trae/skills/stem-project-onboarding/scripts/build_slides.py projects/<slug>/docs/儿童课程/*/课件.md --strict` rebuilds lesson HTML and rejects invalid slide sources.

After scaffolding, complete `project.yaml`, the project README, the central project index, and the relevant component READMEs.

## Coding Style & Naming Conventions

Documentation is primarily Chinese, concise, and instructional. Use `NN-中文标题.md` for ordered documents, lowercase kebab-case for project directories, and language-native English identifiers for code (for example, `grasp_node.py`). Name printable parts like `base_v1.1_x1.stl` and BOMs like `bom_b601-dm_v1.1.csv`. Shell scripts use two-space indentation, quoted variables, and `set -euo pipefail`. No global formatter or linter is configured; preserve the surrounding style.

## Testing Guidelines

Run the narrowest relevant check and document manual hardware validation. Firmware and software additions must include build, run, and verification commands in their local README. Mark unmeasured claims `待验证`. Highlight hazardous steps with `⚠️`; never let testing guidance omit calibration or safe power-up procedures. This repository has no global coverage target.

## Commit & Pull Request Guidelines

Repository Git history is not available in this checkout; follow the documented convention: one concern per commit, Chinese subject, and a scoped Conventional Commit, such as `docs(rebot-devarm): 新增标定说明`. Pull requests should describe the affected project, list verification performed, link the issue when applicable, and include screenshots for slide or visual changes. Call out BOM, enclosure, compatibility, or reprint impacts explicitly.

## Security, Assets, and Licensing

Do not commit credentials, private IPs, student data, or local configuration. Keep files over 50 MB out of normal Git; use Git LFS or record an external link and checksum. Record upstream sources and licenses in `docs/99-参考资源.md` and `project.yaml`.
