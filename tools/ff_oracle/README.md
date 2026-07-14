# BMK4 Windows fastfile oracle

`bmk4-ff-oracle` is an opt-in, non-shipping Windows tool. It parses the real
IW3 unsigned container prefix and zlib payload without starting the game. The
Slice-1 fixture is generated from zero-valued `XFile` and `XAssetList` records;
no binary fixture and no game-derived bytes are checked in.

The v1 report deliberately distinguishes container fields from runtime
observations. `*.observed=1` is earned for the empty fixture because it has no
assets, script strings, blocks, delayed reads, or external data. For non-empty
zones, the container totals remain useful but those observation flags stay
zero until Slice 2 adds Windows-loader event capture. A zero observed-count
must never be presented as a real-zone runtime count when its flag is zero.

CI invokes the tool with `--fixture-allowlist-root <repo-root>`. In that mode,
both the canonical input and output must remain beneath the canonical root;
refusal exits with code 3 before the input is read or output is created. Local
real-zone work omits the fixture-only flag and keeps all resulting dumps out of
git, CI logs, and artifacts.
