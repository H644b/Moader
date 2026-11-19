# Contribute to Moader

Check [open issues](https://github.com/H644b/Moader/issues) for related work. Use [Discussions](https://github.com/H644b/Moader/discussions) for questions and [private reporting](SECURITY.md) for security issues.

## Make a change

Fork the repository and create a branch. Keep the change focused and update the guide that describes the behavior. See [Building](docs/BUILDING.md) for the toolchain and [Maintaining](docs/MAINTAINING.md) for changes that affect the files in a release.

In your pull request, describe what changed and which checks you ran. For a console check, include the release, firmware, game and mod layout when relevant. Record what Moader displayed and what changed in the game, including any content reload, game relaunch or reboot.

## Run the relevant checks

The [CI workflow](.github/workflows/ci.yml) lists the full suite. For documentation and release packaging, start with:

```sh
python3 tools/check-public-tree.py .
python3 .github/tests/test_public_tree.py
```

Run these in a clean public tree before downloading local dependencies. For installer wording or local preparation, run the host checks in [Building](docs/BUILDING.md#host-tests-and-runtime-assets). Use synthetic data in tests and sanitized examples in reports. [Reporting a problem](docs/TROUBLESHOOTING.md#reporting-a-problem)

By submitting a contribution, you agree that it may be distributed under the repository's applicable license, including GPL-2.0-only for Moader source. Third-party components retain their own notices and exceptions. No copyright assignment is required.
