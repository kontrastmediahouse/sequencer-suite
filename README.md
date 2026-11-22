# Sequencer Suite: all four plug-ins, one installer

| Folder | Plug-in | What it is |
|---|---|---|
| `SnakeSeq/` | **SnakeSeq** | port of MDD Snake v3.2.3 |
| `TGB420/` | **TGB-420 Sequencer** | port of TGB-420 Sequencer v1.0 |
| `Snake420/` | **SNAKE-420** | Snake and TGB-420 as linked modules |
| `Bumblebee/` | **Bumblebee** | Snake and TGB-420 fused into one sequencer |

`build_all_installer.sh` builds all four (VST3 + AU + Standalone, universal arm64/x86_64), runs every project's tests, and makes **one** installer:
`dist/Sequencer-Suite-1.0.0.dmg` → `Install Sequencer Suite.pkg` (click **Customize** to skip any plug-in).

## Get the DMG from GitHub (no Xcode needed)

1. Sign in at github.com, click **+ → New repository**, name it e.g. `sequencer-suite`, choose **Private**, and click **Create repository**.
2. On the new repository page, click **uploading an existing file**. Open the unzipped `PluginSuite` folder, select everything inside it, drag it onto the page, then click **Commit changes**.
3. Finder hides the `.github` folder, so it won't be in that upload. Add it by hand:
   - Click **Add file → Create new file**.
   - Type the name `.github/workflows/build-all.yml`.
   - Paste in the contents of that file from the zip. In Finder, press **Cmd + Shift + .** to show hidden files, or open it in TextEdit.
   - Click **Commit changes**.
4. Open the **Actions** tab, then click **Build Sequencer Suite (macOS)** → **Run workflow** → **Run workflow**.
   - Leave the defaults for the universal build, which takes about 30–45 minutes.
   - Set *Architectures* to `arm64` for an Apple-Silicon-only build in about half the time.
   - Use *only* to build a few plug-ins, e.g. `Bumblebee TGB420`.
5. When the run shows a green tick, open it, scroll to **Artifacts** and download **Sequencer-Suite-macOS**. Unzip it to get the DMG.

If you have GitHub Desktop or git, you can skip step 3: commit the whole folder and the hidden `.github` goes with it.

## Or build it on your Mac
```bash
xcode-select --install        # once
brew install cmake            # once
cd PluginSuite
chmod +x build_all_installer.sh
./build_all_installer.sh                  # all four, universal
ARCHS=arm64 ./build_all_installer.sh      # faster, Apple Silicon only
ONLY="Bumblebee" ./build_all_installer.sh # just one
```

## Installing on Sequoia
1. Open the DMG and double-click **Install Sequencer Suite.pkg**.
2. If macOS says it can't verify the developer, go to **System Settings → Privacy & Security**, scroll down, and click **Open Anyway**. Then run the installer again.
3. In Cubase, if a plug-in doesn't appear, go to **Studio → VST Plug-in Manager** and rescan.
4. Load the plug-in on an **Instrument track**. On your synth's track, set **MIDI input** to the plug-in and turn on **Monitor**.

For a signed, notarised installer that opens without warnings, you need a paid Apple Developer account. Set `DEV_ID_APP`, `DEV_ID_INSTALLER` and `NOTARY_PROFILE` before running the script; its header comments explain each one.

Each sub-folder still has its own `build_mac.sh` / `make_installer.sh` if you want a single plug-in on its own.
