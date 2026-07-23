# GraphX FHSS dashboard operator container

This image provides the canonical Linux environment for first-principles FHSS
dashboard operator qualification. The host prerequisite is Docker with Compose.
If Docker is missing, stop and install it on the host; the operator workflow
does not provision Docker, Node.js, npm, Python packages, or C++ packages into
temporary host directories.

Start from a newly downloaded clone:

```sh
git clone https://github.com/rklinkhammer/GraphX.git graphx-dashboard-operator
cd graphx-dashboard-operator
test -z "$(git status --short)"
export GRAPHX_REVISION="$(git rev-parse HEAD)"
mkdir -p .graphx-operator
docker compose -f containers/dashboard-operator/compose.yaml build
docker compose -f containers/dashboard-operator/compose.yaml up
```

The image build installs its versioned Linux, Node/npm, Python-contract, and
frontend dependencies inside the image and builds GraphX in C++26 mode from the
clean clone. No repository build tree, `node_modules`, CMake cache, or install
prefix is reused.

Open <http://127.0.0.1:8080/> on the Docker host. Docker publishes only on host
loopback. Inside the container the GraphX server still binds to container
loopback; `socat` bridges it to the Docker port without weakening the
application's loopback-only bind policy.

Synthetic IQ, separate truth, and SigMF metadata are written beneath
`.graphx-operator/captures` in the clone. Stop with Ctrl-C:

```sh
docker compose -f containers/dashboard-operator/compose.yaml down
```

This Linux container does not qualify macOS Metal behavior, HWIL, conducted RF,
OTA, live RF, or production RF.

