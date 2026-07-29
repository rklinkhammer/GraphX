# Legacy FHSS dashboard operator container

This directory is retained for historical FHSS dashboard qualification
traceability. It does **not** package or launch the current generic
`graphx-dashboard`, and its image is not a supported build or operator path.
Do not use this Compose service to validate the dashboard documented in the
repository root `README.md`.

The former `docker compose build` and `docker compose up` instructions were
removed because they implied a supported generic-dashboard container. The
legacy image builds a different FHSS-embedded application and is not kept
compatible with the current generic dashboard or every supported host
toolchain.

Use the host-native **Generic Graph Dashboard And CLI** instructions in the
repository root `README.md`. A supported generic-dashboard Docker image and
Compose service may be added in a future, explicitly scoped change; none exists
at this revision.

If this legacy service was started previously and still owns host port 8080,
stop and remove it from the repository root:

```bash
docker compose -f containers/dashboard-operator/compose.yaml down
```

This historical container does not qualify the generic dashboard, macOS Metal
behavior, HWIL, conducted RF, OTA, live RF, or production RF.
