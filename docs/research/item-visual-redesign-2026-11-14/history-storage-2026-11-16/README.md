# Large acceptance evidence stored in Git LFS

GitHub rejected the original develop push because one retained ZIP exceeded
100 MiB. The two evidence ZIPs above 50 MiB now use Git LFS. Their paths and
hydrated SHA256 hashes are unchanged; source, models, authored inputs and all
other acceptance evidence are unchanged.

Only the 51 commits after the published develop base were migrated. The remote
base remains an ancestor; normal push is sufficient. `commit-map.csv` maps the
original checkpoint SHA values in historical signed reports to their migrated
commits. Do not edit those historical reports to pretend they ran on new SHAs.
Old local history is retained under `refs/backup/pre-item-lfs-2026-11-16/`;
these backup refs must not be pushed as branches or tags.

For a checkout containing the actual archive bytes:

```sh
git lfs install
git lfs pull
git lfs fsck
```

The item-reproducibility and Windows acceptance workflows request LFS hydration.
The other build workflows do not consume these research ZIPs. `migration.json`
records exact before/after hashes and the complete tree-equivalence checks.
The migration changes storage and commit IDs; the Release acceptance results
remain tied to their recorded executable/source fingerprints.
