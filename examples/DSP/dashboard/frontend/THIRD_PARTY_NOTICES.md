# Dashboard frontend third-party notices

This Phase 0 inventory is generated from the pinned `package-lock.json`. The
packages are build-time inputs for later phases; Phase 0 contains no React
application or compiled frontend bundle.

| Direct package | Version | License | Project |
|---|---:|---|---|
| React | 19.1.1 | MIT | <https://github.com/facebook/react> |
| React DOM | 19.1.1 | MIT | <https://github.com/facebook/react> |
| React Flow (`@xyflow/react`) | 12.8.5 | MIT | <https://github.com/xyflow/xyflow> |
| ELK.js | 0.11.0 | EPL-2.0 | <https://github.com/kieler/elkjs> |
| TypeScript | 5.9.2 | Apache-2.0 | <https://github.com/microsoft/TypeScript> |
| Vite | 7.3.6 | MIT | <https://github.com/vitejs/vite> |

The lock contains 91 resolved package entries, including optional platform
artifacts. Every entry records its package version, license, resolved artifact,
and integrity hash. The complete license inventory is: Apache-2.0 (1),
BSD-3-Clause (2), EPL-2.0 (1), ISC (9), and MIT (78). Package names and exact
associations are authoritative in `package-lock.json`; the Phase 0 policy test
fails if a registry package lacks integrity or license metadata. This notice
does not replace the license texts distributed by each package.
