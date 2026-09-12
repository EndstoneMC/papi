# PAPI migration lifecycle probe

Test-only plugins for a fresh Windows or Linux BDS generation running the pinned
Endstone snapshot and the candidate PAPI wheel. No player is required. The command
disables the fixture owner and PAPI; run other PAPI checks, including native Spark
placeholder resolution, first. Restart the server before another run.

Build from the repository root:

```shell
python -m build --wheel --outdir build/migration-final-windows/fixtures tests/fixtures/migration_probe/owner
python -m build --wheel --outdir build/migration-final-windows/fixtures tests/fixtures/migration_probe/observer
```

The platform-independent wheels are `endstone_papi_migration_owner-1.0.0-py3-none-any.whl`
and `endstone_papi_migration_observer-1.0.0-py3-none-any.whl`. Install both into the
server launcher's Python environment with normal dependency resolution, supplying
the final PAPI wheel and the official Endstone wheelhouse:

```shell
python -m pip install --find-links <official-wheelhouse> <candidate-papi-wheel> <owner-wheel> <observer-wheel>
python -m pip check
```

Use an isolated environment for dependency acceptance if unrelated installed
plugins have inconsistent requirements. Record the server environment's separate
`pip check` result. Do not install duplicate fixture wheels into the plugins
directory when using installed entry-point discovery.

Control BDS only through the `endstone-bds-testing` skill's `scripts/bds.ps1`:

```powershell
& $bds windows status
& $bds windows start
& $bds windows wait-ready
& $bds windows tail 150
# Run native Spark resolution checks here if deployed.
& $bds windows send 'migrationprobe run'
& $bds windows tail 200
& $bds windows stop
& $bds windows status
```

For Linux, replace `windows` with `linux`. `$bds` is the installed skill script's
absolute path. Require both `PAPI_MIGRATION_OWNER_READY` and
`PAPI_MIGRATION_PROBE_READY` in the current generation before sending the command.

Acceptance requires `PAPI_MIGRATION_PROBE_PASS`, individual
`PAPI_MIGRATION_CHECK_PASS` assertions, no `PAPI_MIGRATION_PROBE_FAIL` or
`PAPI_MIGRATION_OWNER_FAIL`, and graceful controller shutdown (`forced=false`).
Record generation, artifact hashes, platform, command output, and any traceback.
Readiness alone is not acceptance. A failed assertion logs the named failure and
traceback and rethrows to Endstone's command boundary.

The owner registers a deterministic Python expansion at enable and deliberately
does not unregister in `on_disable`. The observer verifies public service discovery,
Python resolution, automatic owner cleanup, exactly one `OWNER_DISABLED` callback
and event, and no later owner requests. It then registers its own expansion and
disables PAPI. It verifies exactly one `PAPI_SHUTDOWN` callback, suppressed shutdown
unregister events, removal from both service lookup paths, and repeated inert
queries and rejected mutations through its retained service. The observer has no
hard or soft Endstone plugin dependency on PAPI or the owner.
Both events must arrive as their concrete Python types through automatic listener
registration. The observer retains each event's copied metadata and checks it after
PAPI teardown; it never retains the callback-scoped event wrapper.

This probe does not test non-null players, relational calls, internal shutdown
idempotence, or native consumer dispatch. Native tests and separate Spark resolution
checks cover their respective paths. No internal bootstrap or test service is used.

After a graceful stop, uninstall only these two test distributions using the same
server Python environment. Preserve the candidate PAPI deployment and all unrelated
plugins and worlds. The fixture source and wheel hashes identify the probe version;
runtime results must be reported separately for Windows and Linux.
