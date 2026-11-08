# The Shelf for Heltec V4

**[The Shelf](https://github.com/hardwaremack-prog/the-shelf), running on a Heltec WiFi LoRa 32 V4.** The board serves The Shelf's own web app (the same page you get on a PC or a Pi), with your files on a microSD card. Anyone on your home Wi-Fi opens it in a browser, and it shows up in Messages on every other Shelf in the house.

### ➡️ Install from your browser: **https://hardwaremack-prog.github.io/The-Shelf-Heltec-V4900mhzMesh/**

![The Shelf on a Heltec V4](docs/screenshot-pictures.png)

> Installing replaces Meshtastic on that board. You can put Meshtastic back any time from [flasher.meshtastic.org](https://flasher.meshtastic.org).

## What works on the board

| ✅ Included | 💻 Needs The Shelf on a computer |
|---|---|
| Pictures (grid, slideshow, photo editor), Music (shuffle, play counts, artist and cover art read from your MP3s' own tags), Videos, Games (`.html` and zipped HTML games play in the page), Programs, Books (PDF reader and cover thumbnails) | The AI assistant and Create with AI |
| Home: bulletin board with notes, pasted pictures and videos, quick links, weather | Remote desktop (VNC) |
| Notes, Docs, Sheets, Slides, Draw | Online album-art lookup, tag editing, Standardize names, Remove duplicates, Export |
| Tidy Suite | Big Transfer, backup/restore zips, spreadsheet import |
| **Messages**: finds and chats with your other Shelves (PCs, Pis, other boards), pictures included | Accounts and passwords |
| **900MHz Coms**: LoRa text mesh between boards, every board a relay, 12-hour catch-up | |

The buttons for those computer-only features are hidden on the board, and the AI and Big Transfer tabs are left out.

## Hardware

| Part | Notes |
|---|---|
| Heltec WiFi LoRa 32 **V4** | The standard V4 (ESP32-S3R2, 2 MB PSRAM), or the V4-R8 (8 MB PSRAM). Each has its own install button |
| 3.3 V SPI microSD module | Same module and wiring as [Heltec Shelf Lite](https://github.com/hardwaremack-prog/Heltec-Shelf-Lite--Wifi-Thumbdrive) |
| microSD card | FAT32. A 32 GB card works out of the box |

![microSD wiring for The Shelf on Heltec V4](docs/sd-wiring-shelf-v4.png)

| microSD module | Heltec V4 |
|---|---|
| VCC | **3V3** |
| GND | GND |
| CS | GPIO 4 |
| MISO | GPIO 6 |
| CLK (SCK) | GPIO 47 |
| MOSI | GPIO 48 |

With no card, The Shelf keeps files in about 10 MB of the board's own flash. The screen shows **SD** or **FLASH** in the corner.

### On the card

Your shelves are normal folders at the top of the card: `Pictures`, `Music`, `Videos`, `Games`, `Programs`, `Books`. Pop the card into a computer and copy music or photos straight in. Put files directly in those folders, because subfolders inside a shelf aren't shown (the same as The Shelf). Notes, docs, drawings, play counts, thumbnails and messages live in the hidden `.shelf` folder.

## First-time setup

1. Install with the [web installer](https://hardwaremack-prog.github.io/The-Shelf-Heltec-V4/) (Chrome or Edge).
2. Power the board. The screen shows **Join The-Shelf-XXXX**. Join it from your phone with the password **`theshelf`**.
3. The setup page opens (or go to `http://192.168.4.1`). Tap **Find networks**, pick your Wi-Fi and enter its password.
4. Back on your home Wi-Fi, open the address on the screen, or **`http://shelf-v4-xxxx.local`**.

### 900MHz Coms (LoRa mesh between boards)

A **900MHz Coms** tab in The Shelf's Tools lets Shelf boards text each other over the V4's LoRa radio, with no Wi-Fi or internet between them.

![900MHz Coms tab: on, with the message log, who's in range, and settings](docs/screenshots/coms-desktop.png)

<p align="center"><img src="docs/screenshots/coms-phone.png" width="300" alt="900MHz Coms on an iPhone"></p>

- **On/off switch.** When it's on, the tab shows the message log and a box to send to everyone on the mesh (180 characters per message).
- **Every board is a relay.** Anything new a board hears, it re-sends once after a short random wait, so messages hop board to board as far as the mesh reaches (no hop limit). Message IDs are remembered so nothing loops, and boards listen before talking.
- **"Received" receipts.** A **✓ Sending "received"** button in the tab, on by default. When it's on, the board answers every message it hears with a short receipt that hops back to the sender, so your own messages show **✓ Received by Garage V4, Truck**. Click it to stop this board sending receipts. You'll still see receipts from boards that have theirs on.
- **Catch-up.** When a board turns Coms on, it asks its neighbors for the last 12 hours. One of them replays what it's missing (up to 60 messages), marked "caught up", with their original times.
- **Display.** The tab shows who's in range with signal strength, plus counts of heard, relayed and caught-up messages. The OLED shows an antenna icon while Coms is on, and new messages for 15 seconds.
- **Settings.** You can set your name on the mesh and the transmit power: Low ~11 dBm, Normal ~20 dBm (default), or High ~27 dBm, through the V4's front-end amplifier.
- **Air settings.** 915.0 MHz, 125 kHz, SF9, CR 4/5, private sync word 0x12. This is its own channel, separate from Meshtastic, which it doesn't talk to.

**Fit the LoRa antenna before switching Coms on.** Transmitting without an antenna can damage the radio. The log is kept on the card in `.shelf/data/coms-log.jsonl`.

#### How the mesh works

1. You type a message. Your board sends it once over the radio, after checking that nobody else is transmitting.
2. Every board that hears it adds it to its log, then re-sends it once after a random 0.4–2 second wait. That way boards don't all talk at the same moment.
3. Boards farther away hear those re-sends and do the same, so the message spreads board to board as far as the mesh reaches.
4. Each board remembers the last 1,024 message IDs. When a copy it already has comes back around, it ignores it, so nothing loops and each board sends each message only once.
5. In the log, a message shows **direct** if it came straight from the sender, or **N hops** if it was relayed, along with the signal strength it arrived at.
6. With receipts on, each board that gets a message waits 2–5 seconds (so the relays go first), then sends a tiny "received" packet. It hops back across the mesh the same way, and only the original sender keeps it. Each receipt is a few bytes, but every board sends one for every message, so on a big, busy mesh you can turn receipts off to save airtime.

#### Catching up a new board

When a board turns Coms on (or powers up with it on), it broadcasts a short "catch me up" request. The request lists the newest messages it already has.

1. Each board in range picks a random wait of 0.5–3 seconds. The first one to finish waiting answers.
2. The others hear it answering and stay quiet, so there's only one answer.
3. It replays up to 60 messages from the last 12 hours that the new board is missing, oldest first, about one every 1.2 seconds.
4. Each replay carries the message's age rather than a clock time, so it shows at the right time even if the boards' clocks disagree. Replays are marked **caught up** and aren't relayed further.

The request goes out again after 2 minutes in case nobody heard the first one.

#### Turned off

![900MHz Coms switched off](docs/screenshots/coms-off.png)

### Its own Wi-Fi (car, camping, anywhere)

On the settings page, choose **Use its own Wi-Fi**, then pick a network name and password. The board then runs its own network instead of joining yours, and The Shelf works fully on it. Join that network on your phone or laptop and open `http://192.168.4.1` (any address you type gets there). There's no internet on that network, so phones are told it's fine and stay connected without a sign-in pop-up. The board borrows the time from the first browser that opens it, so notes and files get the right dates. Messages with other Shelves needs your home Wi-Fi.

If the board is set to join your home Wi-Fi but can't find it at power-up, it starts its own Wi-Fi anyway and keeps looking for home.

<p align="center"><img src="docs/screenshots/settings-own-wifi.png" width="360" alt="Board settings: Use its own Wi-Fi, with network name and password"></p>

### The screen

![The board's OLED: on your Wi-Fi, on its own Wi-Fi, and at first power-up](docs/screenshots/oled-all.png)

*Left to right: on your home Wi-Fi with a 900MHz Coms message showing, on its own Wi-Fi, and first power-up setup. These were drawn by the board's own screen code with the same U8g2 library and fonts, so they show what the OLED really draws, pixel for pixel.*

- **Top bar:** THE SHELF V4, SD or FLASH, an antenna icon while 900MHz Coms is on, and the **battery meter** with percent (`USB` when no battery is plugged in)
- **Line 1:** the board's name, or its own Wi-Fi network's name
- **Line 2:** nearby Shelves or Wi-Fi strength (on its own Wi-Fi: the password). New Coms messages show here for 15 seconds, and long ones scroll.
- **Storage bar** with free space
- **Bottom line:** the **IP address** to open

The screen sleeps after 5 minutes; a short press of **PRG** wakes it.

The **Heltec V4** card at the bottom of The Shelf's sidebar opens the board's settings page (`/board/`). There you can change Wi-Fi, rename the board, see storage, and restart it. Hold **PRG** for 6 seconds to forget the Wi-Fi.

## More screenshots

| | |
|:---:|:---:|
| ![Home](docs/screenshots/home.png) | ![Music](docs/screenshots/music.png) |
| **Home**: bulletin board, links, weather | **Music**: artists and art from your MP3s' tags |
| ![Messages](docs/screenshots/messages.png) | ![Tidy Suite](docs/screenshots/tidy-suite.png) |
| **Messages** with your other Shelves | **Tidy Suite** |

*Screenshots use sample files and messages.*

## Messages between Shelves

The board talks to other Shelves the same way they talk to each other: `shelf-presence` broadcasts on UDP port 41234, a network scan that asks every address on port 8080 "are you a Shelf?", and direct HTTP for messages. That's why the board answers on **port 8080** as well as port 80. Click the board in another Shelf's Messages list to chat, or choose **Connect Shelf** to open it.

## Good to know

- **There's no login.** Anyone on your home Wi-Fi can view and change things, the same as Heltec Shelf Lite. Don't put it on a guest network you share.
- Expect roughly 0.5–1 MB/s through the SPI card. Songs, photos, documents and games are quick; big videos are slow to upload.
- Weather needs the browser to share its location. Browsers only allow that on `https` or `localhost` pages, so on the board it usually stays hidden, the same as The Shelf does on a LAN.

## Building

`build.sh` packs The Shelf's current `public/` folder (cloned from [the-shelf](https://github.com/hardwaremack-prog/the-shelf), or set `SHELF=/path/to/the-shelf`) into a LittleFS image, compiles the firmware for both V4 versions, and writes full images to `docs/firmware/`.

Needs:
- arduino-cli with **esp32 core 2.0.17**
- **ESPAsyncWebServer 3.3.23** + **AsyncTCP 3.2.14** (mathieucarbou), **ArduinoJson 7**, **U8g2**
- **mklittlefs 3.x** built with `LFS_NAME_MAX=64` to match the board. The copy bundled with the 2.0.x core breaks on paths over 32 characters (`make CPPFLAGS="-D LFS_NAME_MAX=64"`, or edit its Makefile)

`tools/pack_www.py` makes a few changes to The Shelf's page for the board:
- removes the AI and Big Transfer tabs
- adds `board-ui/board.css`, which hides computer-only buttons
- adds `board-ui/board.js`, which shows the board card and its address

Rebuild after updating The Shelf to pick up its newest page.

### Layout

```
ShelfV4/
  main.cpp         Wi-Fi + setup mode, storage, web servers (80 and 8080), screen, button
  api_files.cpp    the six shelves: list, upload, rename, move, delete, play counts,
                   thumbnails, photo editor, zipped games, Range-capable file serving
  api_tools.cpp    Home board, Notes/Docs/Slides/Sheets, Draw, Visitors, weather, settings
  messaging.cpp    Shelf-to-Shelf discovery and chat
  music_meta.cpp   artist, title and cover art from ID3 tags
  util.cpp         JSON bodies (spooled to the card when big), streaming responses
board-ui/          the board's settings page and the small additions to The Shelf's page
tools/pack_www.py  builds the web app image
docs/              web installer (GitHub Pages) and firmware
```

Flash layout (16 MB): 3 MB app, 2 MB web app (LittleFS), about 10.9 MB storage for when no card is fitted.
