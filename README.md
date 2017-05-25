# Cthulhu (CacheTool)

Cthulhu is a homebrew application for managing play time history, step history and cached icon data.
The goal of this app is to provide an open-source alternative to some of the [3DS Development Unit Software](https://www.3dbrew.org/wiki/3DS_Development_Unit_Software), alongside with extra features.

Why "Cthulhu"?
I found the pronunciation similar to "Cache Tool". It's also a cool name.

Current Features:
- Clear play time history: clears your play time history (it can be seen at Activity Log under "Daily Records").
- Clear step history: clears your step history (it can be seen at Activity Log under "Daily Records").
- Clear software library: clears your software library (it can be seen at Activity Log under "Software Library").
- Edit software library: allows editing of individual entries on your software library.
- Clear shared icon cache: clear all shared cached icon data, used by Activity Log, Friends List and Notifications (this will also clear your Activity Log title list).
- Update shared icon cache: iterates through all entries and replaces outdated ones.
- Restore shared icon cache: restores the previous shared icon cache in case something goes wrong while updating it.
- Clear HOME Menu icon cache: clears the icon cache used by HOME Menu, then reboots the console so HOME Menu can create it again.
- Update HOME Menu icon cache: iterates through all entries and replaces outdated ones.
- Restore HOME Menu icon cache: restores the previous HOME Menu icon cache in case something goes wrong while updating it.
- Reset demo play count: resets the play count of all installed demo software.
- Reset folder count: resets HOME Menu's folder count so that the next folder created is "1". (Only works if you run Cthulhu from Test Menu).
- Unwrap all HOME Menu software: unpacks all gift-wrapped software on HOME Menu.
- Repack all HOME Menu software: gift-wraps all software on HOME Menu. Mainly intended for testing the above feature.
- Remove software update nag: installed titles will no longer ask for an update when launched (until HOME Menu downloads version data again). This can't be used to bypass the hardcoded version check on "Ironfall: Invasion".
- Clear game notes: clear all of your game notes.
- Replace eShop BGM: replaces the current eShop music with a custom one.
- Restore eShop BGM: restores the default current eShop music.
- Change accepted EULA version: useful for allowing out-of-region online play (when set to FF.FF).
- Toggle HOME/Test menu: changes which menu the 3DS will boot on startup (Test Menu needs UNITINFO patch to work).

Future Features:
- Misc. features seen on dev unit software.

Obs.:
- Because HOME Menu doesn't allow accessing its icon cache while it's running, Cthulhu runs on extended memory mode.
- Clearing or updating HOME Menu icon cache may uninstall sdiconhax (a.k.a menuhax 3.x).