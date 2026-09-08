# Flight Tracker — Complete New‑Device Setup Guide

A **step‑by‑step walkthrough for someone with a brand‑new board**. It assumes
nothing: no toolchain, no driver, no saved settings. By the end you'll have a
working radar that talks to **your** W‑Fi and the **OpenSky Network** using
**your** own API credentials.

> **What you end up with:** the device boots to a WiFi setup screen, connects
> to *your* network, you add your OpenSky API key once over a web page, and a
> live radar of aircraft near you appears.

---

## How flashing works on this board (read this once)

The flash chip is split into regions. Three are written by the flashing tool
and one is your personal data:

| Offset | Region | What it holds |
|--------|--------|---------------|
| `0x0000`  | **Bootloader** | First code the chip runs. |
| `0x8000`  | **Partition table** | Map of the flash layout. |
| `0x10000` | **App (firmware)** | The radar app itself (~1.5 MB). |
| `0x9000`  | **NVS** (your data) | WiFi name/password, OpenSky API key, saved centre/range. |

Three ESP‑IDF commands matter:

| Command | Writes | Wipes your data? |
|---------|--------|------------------|
| `erase-flash` | Everything (`0xFF`) | **Yes** — factory‑clean chip. |
| `flash` | Bootloader + partition table + app | **No.** NVS at `0x9000` is untouched. |
| `app-flash` | App only | **No.** Fast update when you only changed code. |

**The critical rule for a NEW board:** you must run the **full `flash`** (ideally
`erase-flash` then `flash`) at least once. It writes the bootloader and
partition table. If you only run `app-flash` on an erased chip, the app lands
in flash but the chip has **no bootloader** — it just resets forever and the
screen stays **blank**. (That's the "I flashed it but the screen is blank"
trap.) Once the bootloader exists, later re‑flashes can use fast `app-flash`.

There is an old myth floating around this repo that "`flash` wipes your WiFi
and API settings". It doesn't — only `erase-flash` does. `flash` and
`app-flash` both leave NVS alone.

---

## Quick checklist

- [ ] ESP‑IDF v5.1+ installed (Section 1)
- [ ] CH340 driver installed (Section 2)
- [ ] Board connected, port found (Section 3)
- [ ] Firmware built (Section 4)
- [ ] Full flash — bootloader + partitions + app (Section 5)
- [ ] Board on YOUR WiFi (Section 6)
- [ ] OpenSky API key entered (Section 7)
- [ ] Radar drawing aircraft (Section 8)

---

## 1. Install ESP‑IDF (one time, per computer)

Pick your OS.

**macOS / Linux**

```bash
brew install cmake ninja dfu-util python3      # macOS only; Linux: use your package manager
mkdir -p ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git -b v5.3.3
cd esp-idf
./install.sh esp32s3
```

Then in **every new terminal** you build/flash in, activate the environment:

```bash
. ~/esp/esp-idf/export.sh     # note the leading dot and space
```

**Windows**

Download the official installer from
<https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/windows-setup.html>.
Use the **"ESP-IDF Tools Installer"**, then use the **"ESP-IDF PowerShell"**
shortcut from the Start Menu (it activates the environment for you).

**Verify**

```bash
idf.py --version
```

If that prints nothing, you forgot to run `export.sh` (mac/Linux) or launch the
ESP‑IDF terminal (Windows).

---

## 2. Install the CH340 driver (Windows/macOS only)

The board's USB chip is a WCH **CH340**. macOS and Windows don't ship a driver.

- **macOS:** download and install
  <https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html>.
- **Windows:** download and install
  <https://www.wch-ic.com/downloads/CH341SER_EXE.html>.

Linux needs nothing. If you're on current macOS and the board *does* appear
without the driver, you can skip this step.

---

## 3. Connect the board and find its port

Plug the board into your computer with a **USB‑C cable that carries data**
(charge‑only cables are the #1 cause of "nothing happens"). The screen is a
7" RGB panel that draws a lot of current — if the board keeps resetting
("Brownout detector was triggered"), use a powered hub or a USB‑C phone
charger that also passes data.

Find the port:

```bash
# macOS / Linux
ls /dev/cu.*     # or: ls /dev/ttyUSB* /dev/ttyACM*
```

Look for a `cu.usbserial-*` / `cu.wchusbserial*` or `cu.SLAB_USBtoUART`
entry — that's the CH340. (On this project's board it's been seen as
`/dev/cu.wchusbserial1340`.) **Windows:** Device Manager → *Ports (COM & LPT)*
→ note the `COMxx`.

Use that name everywhere `<PORT>` appears below.

---

## 4. Build the firmware

```bash
cd src                               # repo's src directory
idf.py set-target esp32s3            # only needed on a fresh workspace
idf.py build
```

A good ending looks like:

```
Project build complete. To flash, run:
 idf.py flash
```

If `idf.py` isn't found, (re)run `export.sh` (mac/Linux) or the ESP‑IDF
terminal (Windows). If the build fails, `git -C $IDF_PATH submodule update
--init --recursive` fixes most "missing submodule" cases.

---

## 5. Flash it (full flash — required on a new board)

For a truly clean, never‑owned‑before board:

```bash
idf.py -p <PORT> flash
```

That writes all three code regions (bootloader, partition table, app) and
leaves the data region (NVS) empty — exactly right for a new user who will
enter their own WiFi and API key. Success looks like:

```
Hash of data verified.
Hard resetting via RTS pin...
Done
```

The board reboots and the **WiFi setup screen** appears ("Select Wi‑Fi
network") — that's the signal it worked.

> **Optional but thorough:** if you want a factory‑fresh NVS too (say the board
> was previously used by someone else and might hold their settings), do it in
> one command instead:
> ```bash
> idf.py -p <PORT> erase-flash flash
> ```
> `erase-flash` wipes *everything* first, `flash` then puts the three code
> regions back. **Do NOT** make a habit of `app-flash` on a freshly erased
> chip — with no bootloader the board won't boot (blank screen).

---

## 6. Put the board on YOUR WiFi (on‑screen setup)

After flashing, the board scans for networks and shows them on the touchscreen
under **"Select Wi‑Fi network"**:

1. Tap **your** WiFi's name from the list.
2. Tap the password box and type your WiFi password on the on‑screen keyboard.
3. Tap **Connect**.

The screen switches to the radar once the board joins your network, and your
WiFi name/password are saved in NVS (shown at the top of the radar screen).
The board will reconnect automatically on every power‑on from now on.

If you ever want to re‑pick a network, use the **"Forget WiFi"** option in the
menu — it clears the saved credentials and returns you to this setup screen.

---

## 7. Add your OpenSky API key (one‑time, over a web page)

The OpenSky API needs free credentials to show live aircraft. You only need
them **once** — after that the board keeps them in NVS.

1. **Get your key:** go to <https://opensky-network.org/>, register a free
   account, verify your email, then in **My OpenSky → Applications** create an
   application. It gives you two strings: a **Client ID** and a **Client
   Secret**.
2. **Find the board's IP:** after WiFi connects, the radar screen shows the
   board's IP address (top of the screen, e.g. `192.168.0.33`). If you missed
   it, check your router's "connected devices" page.
3. **On your phone or laptop** — on the *same WiFi* — open a browser and go to
   **`http://<board-ip>/`** (that IP from step 2, e.g. `http://192.168.0.33/`).
   You'll get the Flight Tracker config page.
4. Paste your **Client ID** and **Client Secret** into the two boxes and click
   **Save & Reboot**.

The board restarts, connects to WiFi, and the radar starts pulling live
aircraft near you every 22 seconds. The config page only works while the board
is missing an API key — once you save one, it stops serving the page.

> The old "softAP / captive portal named FlightRadar-Setup" flow you may read
> about elsewhere **does not exist** in this firmware. The config page is
> served over your home WiFi at the board's own IP.

---

## 8. Verify it's actually working

- The radar dial sweeps and little aircraft glyphs appear (green for most
  types, sized by aircraft category).
- A small **"API: …"** label near the radar shows how old the data is and
  ticks up to 22 s between refreshes.
- Tap an aircraft → its callsign/altitude/speed appear on the right rail.
- If the screen says "Disconnected" at the top, the board lost WiFi. If the
  dial is empty and only the center stays, your OpenSky key may be wrong
  (`Settings → Forget OpenSky API` to redo it).

---

## 9. Re‑flashing later (updating the firmware only)

Once the bootloader exists (i.e. you've already done a full `flash`), you can
update just the app without touching your WiFi/API settings:

```bash
idf.py build
idf.py -p <PORT> app-flash
```

Watch the log with `idf.py -p <PORT> monitor` (exit with `Ctrl-]`).

---

## Common problems

| Symptom | Cause & fix |
|---------|-------------|
| `Failed to connect to ESP32-S3` | Board not entering download mode. Hold **BOOT**, tap **RESET**, release **BOOT**, retry. |
| Screen blank after `app-flash` on a fresh chip | No bootloader was ever written. Run the full `idf.py flash` (Section 5). |
| Random resets / "Brownout detector was triggered" | 7" panel needs more current. Use a powered hub or a data‑passing USB‑C charger. |
| `idf.py: command not found` | Run `. ~/esp/esp-idf/export.sh` (mac/Linux) or the ESP‑IDF terminal (Windows) in this shell. |
| Port disappeared | Try another cable (data!), another port, and close any program holding the port (e.g. a previous `monitor`). |
| WiFi never connects | Wrong password, or the 2.4 GHz band is off. The board only supports 2.4 GHz. |
| Radar empty / "Has creds: 0" | OpenSky key not saved yet — do Section 7. |
| Old settings appear on "new" board | Someone flashed it before. `idf.py -p <PORT> erase-flash flash` for a clean slate. |
