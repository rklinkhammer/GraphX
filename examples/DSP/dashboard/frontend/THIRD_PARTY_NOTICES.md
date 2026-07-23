# Dashboard frontend third-party notices

This Phase 1 inventory is generated from the pinned `package-lock.json`. Runtime
libraries are compiled into the self-hosted dashboard; test and build tools are
build-time only.

| Direct package | Version | License | Project |
|---|---:|---|---|
| React | 19.1.1 | MIT | <https://github.com/facebook/react> |
| React DOM | 19.1.1 | MIT | <https://github.com/facebook/react> |
| React Flow (`@xyflow/react`) | 12.8.5 | MIT | <https://github.com/xyflow/xyflow> |
| ELK.js | 0.11.0 | EPL-2.0 | <https://github.com/kieler/elkjs> |
| TypeScript | 5.9.2 | Apache-2.0 | <https://github.com/microsoft/TypeScript> |
| Vite | 7.3.6 | MIT | <https://github.com/vitejs/vite> |
| Vitest | 4.0.16 | MIT | <https://github.com/vitest-dev/vitest> |
| jsdom | 27.3.0 | MIT | <https://github.com/jsdom/jsdom> |
| Testing Library React | 16.3.1 | MIT | <https://github.com/testing-library/react-testing-library> |
| Testing Library user-event | 14.6.1 | MIT | <https://github.com/testing-library/user-event> |

The lock contains 179 resolved package entries, including optional platform
artifacts. Every entry records its package version, license, resolved artifact,
and integrity hash. The complete license inventory is: Apache-2.0 (4),
BlueOak-1.0.0 (1), BSD-2-Clause (2), BSD-3-Clause (3), CC0-1.0 (1),
EPL-2.0 (1), ISC (11), MIT-0 (2), and MIT (154). Package names and exact
associations are authoritative in `package-lock.json`; the Phase 0 policy test
fails if a registry package lacks integrity or license metadata. This notice
does not replace the license texts distributed by each package.
