# AIPC OS

See [https://aipc-os.catme0w.org/](https://aipc-os.catme0w.org/) for project overview.

## Setup

Python tools are managed as a [uv workspace](https://docs.astral.sh/uv/concepts/workspaces/).

Install everything from the repository root:

```
uv sync
```

This creates a shared virtualenv with all tools installed.  CLI entry
points (`ak7802-usbboot`, `ak7802-nand-dump`) are available immediately.

## License

See [LICENSE](LICENSE) for details.
