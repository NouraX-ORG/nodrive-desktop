# tools/rebrand.d: lane plugins for tools/rebrand.py

`tools/rebrand.py` applies the shared NoDrive branding and then runs every
`*.py` file in this folder, in file-name order. Each platform lane owns its
own plugin and edits only its own files (plan: `plans/desktop-plan.md`,
section 11.3).

| File | Lane | Edits |
|---|---|---|
| `linux.py` | Linux | `admin/linux/**` (AppImage script) |
| `windows.py` | Windows | Windows packaging, installer, shell extensions |
| `macos.py` | macOS | macOS packaging, Finder / File Provider files |
| `problemreports.py` | shared | The six marked hooks of the problem reports feature (`src/gui/CMakeLists.txt`, `application.cpp`, `advancedsettings.cpp`, `test/CMakeLists.txt`). The logic is in `src/gui/nourax/`; see [../../docs/problem-reports.md](../../docs/problem-reports.md) |

A plugin is a plain Python file:

```python
LANE = "windows"

def apply(ctx):
    ctx.replace("admin/win/msi/Nextcloud.wxs", "old text", "new text")
```

`ctx` is a `rebrandlib.Context`. Use only its helpers (`replace`,
`regex_sub`, `write`, `copy`, `require`, `read`), never `open()` for
writing, so that `python tools/rebrand.py --check` works and a second run
changes nothing. Read ids from `ctx.ids` (`tools/branding/ids.json`); do not
hard-code them. The full contract is at the top of `tools/rebrandlib.py`.

After changing a plugin, run it twice and make sure the second run reports
`0 change(s)`:

```sh
python tools/rebrand.py
python tools/rebrand.py --check   # exit 0 = nothing left to change
```
