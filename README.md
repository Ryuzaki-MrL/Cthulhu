# Cthulhu (CacheTool)

Cthulhu is a homebrew application for managing play time history, step history and cached icon data.

Why "Cthulhu"?
I found the pronunciation similar to "Cache Tool". It's also a cool name.

Current Features:
- Clear play time history: clears your play time history (it can be seen at Activity Log under "Daily Records").
- Clear step history: clears your step history (it can be seen at Activity Log under "Daily Records").
- Clear shared icon cache: clear all shared cached icon data, used by Activity Log, Friends List and Notifications (this will also clear your Activity Log title list).
- Update shared icon cache: iterates through all entries and replaces outdated ones.
- Restore shared icon cache: restores the previous shared icon cache in case something goes wrong while updating it.
- Clear HOME Menu icon cache: clears the icon cache used by HOME Menu, then reboots the console so HOME Menu can create it again.
- Update HOME Menu icon cache: iterates through all entries and replaces outdated ones.
- Restore HOME Menu icon cache: restores the previous HOME Menu icon cache in case something goes wrong while updating it.
- GO BERSERK AND CLEAR EVERYTHING: clears play time history, step history and all cached icon data, then reboots the console.

Future Features:
- Clear Activity Log Title List: this will clear the activity log's title list without wiping out cached icon data.
- Clear Friend List: this will clear your friend list, duh.
- Edit Activity Log: this will allow for editing individual entries on your title list.
- Misc. features seen on dev unit software.

Obs.: Because HOME Menu doesn't allow accessing its icon cache while it's running, Cthulhu runs on extended memory mode.