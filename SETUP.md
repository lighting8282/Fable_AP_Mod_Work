# Setting up on a new machine

One-time setup to continue the CDEF cataloging work from a fresh Windows machine.

## 1. Install prerequisites

- **Fable: The Lost Chapters** (Steam). Note the install path — referred to as `<FTLC>` below (e.g. `C:\Program Files (x86)\Steam\steamapps\common\Fable The Lost Chapters`).
- **Fable Anniversary** (Steam) — only needed for the FA symlink workflow, not for CDEF testing itself.
- **Visual Studio 2022 17.10+ or newer** (Community is fine) with the **Desktop development with C++** workload. Older versions fail on `dinput8.slnx` with `MSB4068` — our `modified-files/dinput8.sln` is a fallback that works on older MSBuild too.
- **Python 3** (any recent version).
- **GitHub CLI** (`gh`), then `gh auth login` as **lighting8282**.

## 2. Clone the repos

```
git clone https://github.com/lighting8282/Fable_AP_Mod_Work.git
git clone https://github.com/lgbarrere/FableAnniversary-Random.git
```

(Optionally also lgbarrere's FableModdingTools if doing FESN/symlink work — see section 6.)

## 3. Apply our modifications to FableAnniversary-Random

Copy from `Fable_AP_Mod_Work` into `FableAnniversary-Random`:

| From (this repo) | To (FableAnniversary-Random) |
|---|---|
| `modified-files/dllmain.cpp` | `dinput8/dinput8/dllmain.cpp` |
| `modified-files/windowed_hook.cpp` | `dinput8/dinput8/windowed_hook.cpp` |
| `modified-files/dinput8.sln` | `dinput8/dinput8.sln` |
| `modified-files/deploy.bat` | `deploy.bat` |
| `mods/batch_test_mod/` (whole folder) | `mods/batch_test_mod/` |

What these do: borderless window, exclusive-mouse/non-exclusive-keyboard DirectInput (free cursor movement between windows, working Print Screen), classic `.sln`, and the in-game batch tester. The `.sln` already includes the `batch_test_mod` project.

If VS 17.10+ handles the `.slnx` fine you can keep `SOLUTION=.\dinput8\dinput8.slnx` in deploy.bat, but the `.sln` must be used if `batch_test_mod` should build (the shipped `.slnx` doesn't list it — either add it there or use our `.sln`).

## 4. Point deploy.bat at the local install

Edit `deploy.bat` line ~6:

```bat
set "DEFAULT_DIR_FTLC_STEAM=<FTLC>"
```

## 5. First run

1. Run `deploy.bat` from the `FableAnniversary-Random` folder (elevated terminal). It builds all mods, copies them + `dinput8.dll` into `<FTLC>`, and launches the game.
2. Create/copy a throwaway test save (any save works; you'll re-add items to it constantly). Steam Cloud may sync saves automatically.
3. Put the CDEFs to test (one per line) in `<FTLC>\mods\batch_test_mod\batch_cdefs.txt`.
4. Launch the game (plain `Fable.exe` is enough after the first deploy), load the save, and wait — results stream into `<FTLC>\mods\batch_test_mod\batch_results.csv`.
5. If the game crashes: relaunch, reload the save; the tested IDs are skipped and the crasher is marked `CRASHED`.
6. To re-test IDs, archive/delete `batch_results.csv` first.

Screenshot tip: the game minimizes on focus loss, so normal screenshot tools fail. Use a delayed capture from a second terminal, then click into the game within 10 s:

```powershell
Start-Sleep -Seconds 10
Add-Type -AssemblyName System.Drawing; Add-Type -AssemblyName System.Windows.Forms
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
([System.Drawing.Graphics]::FromImage($bmp)).CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
$bmp.Save("$env:USERPROFILE\Desktop\fable_capture.png", [System.Drawing.Imaging.ImageFormat]::Png)
```

## 6. FESN / Fable Explorer (only if extracting more definition data)

1. Clone/download lgbarrere's FableModdingTools (FA-SYMLINKER).
2. Edit `FA-SYMLINKER/tools/FableExplorer/config.xml` → `InstallDirectory` to `<FTLC>\` (with trailing backslash). The tree stays empty otherwise.
3. In Fable Explorer: **File → Open** → `<FTLC>\data\CompiledDefs\game.bin` — the OBJECT/OBJECT_FAMILLY tree only appears after this.
4. The full OBJECT tree is already extracted in `data/cdefs.txt` (2,852 IDs) — FESN is only needed again for other sections or field-level data.

## 7. Git identity

Commits should attribute to lighting8282:

```
git config --global user.name "lighting8282"
git config --global user.email "lighting8282@users.noreply.github.com"
```

(The uconn.edu email maps to a different GitHub account, jon-weber1.)

## Where to resume

Check the README's **Next steps** checklist and `data/batch_results_session*.csv` for what's been tested. The next planned batch was trophies 4511–4527 plus books 4551–4577.
