# Attribution, licensing and asset provenance / 版权及素材来源

> This document records the project's current source tree and planned redistribution precautions. It is not a substitute for a file-by-file license audit or legal advice.

## 1. Original game / 原版

- **Name:** Cataclysm: Dark Days Ahead (CDDA)
- **Project:** https://github.com/CleverRaven/Cataclysm-DDA
- **Starting point:** official `0.I-1` tag, commit `7b2efa5cea38e4d4d97dd0e63b28b9148623da59`
- **Original README:** [UPSTREAM_README.md](UPSTREAM_README.md) (unaltered local reference copy)
- **Original licensing notices:** [../LICENSE.txt](../LICENSE.txt), related license files and any notices within individual upstream files

CDDA's primary code/content license is Creative Commons Attribution-ShareAlike 3.0 Unported (CC BY-SA 3.0). Some components (for example fonts, libraries, third-party code and art) have separate license terms as recorded in the upstream source. Do not replace their licenses with a single blanket statement.

中文：本项目建立于 CDDA 官方 0.I-1 源码之上，保留原版来源及版权声明。原版主体采用 CC BY-SA 3.0，部分文件另有独立许可证，不能简单宣称所有内容都是本项目原创或都适用同一许可。

## 2. Modern Mode modifications / 改造

CDDA Modern Mode introduces experimental first-person 3D rendering, control and UI adaptations, and related tests and documentation. Existing CDDA code/content remains attributable to the original project and contributors. When distributing adaptations of CC BY-SA 3.0 material, retain attribution, identify modifications and comply with the applicable ShareAlike requirements. For files under other licenses, follow those licenses individually.

Do not remove upstream contributor credits or copyright headers. The project is unofficial and has no endorsement from the CDDA maintainers.

## 3. External local assets / 外部本地资源

The Windows development workspace contains **UndeadPeople** material separately, **outside** the Git repository:

- Development source: https://github.com/eremtsov/UndeadPeopleUnpacked
- Development snapshot recorded locally: `a821c120cccd898c14484cdbb5c9d52c1231dd40`
- Local paths relative to the outer workspace: `assets/downloads/`, `assets/undeadpeople-source/`, `assets/tilesets/`
- Internal provenance reference: [FP3D_UNDEADPEOPLE.md](FP3D_UNDEADPEOPLE.md)

**No blanket redistribution permission is asserted for these materials.** Before including them in a Release or committing any copied textures, inspect the actual license notices and provenance of the complete asset set (including contributions with distinct rights). Omit assets if redistribution authorization cannot be established, and offer lawful user-provided installation instead.

中文：不死人图块包目前属于仓库外的本地开发素材。公开游戏包之前应逐项审查其图块及来源许可证；未核清时不要直接把素材一起上传。

## 4. Shaders and other bundles / 光影等资源

A proposed Minecraft Photon shader ZIP was considered during local feasibility analysis, but the repository does not include it as a redistribution target. Do not upload copied shaders, proprietary game files, installer archives, test saves, cached binaries or third-party artwork unless licensed for public redistribution.

## 5. Release checklist / 对外发布要求

- Keep all applicable `LICENSE*` files and upstream attribution when distributing source or binaries.
- Explain that this is an unofficial CDDA 0.I-1-derived modification, and label the version and experimental status clearly.
- Audit every included third-party tileset, shader, font, sound and binary dependency independently.
- Verify that source archives do not contain local saves, credentials, logs, developer tools or unlicensed binary bundles.
- Include appropriate notices with binary releases, not just in the GitHub source repository.
- Record which assets are bundled versus downloaded by the end user.

This file is documentation of outstanding work; it does **not** certify the current tree or any future binary archive as fully license-cleared.
